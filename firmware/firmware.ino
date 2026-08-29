// firmware.ino — BLE bridge from an Xbox controller to a LEGO Technic Move Hub 88019.
//
// The ESP32-C3 holds two BLE links as a client at the same time: one to the
// Xbox Series X|S controller and one to the hub inside the Porsche GT4
// e-Performance (42176).
//
// Safety principle: after power-up the bridge is DISARMED. Only START releases
// the drive; B or the Xbox button lock it again immediately.

#include <Preferences.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>
#include <esp_system.h>

#include "Config.h"
#include "Mapping.h"
#include "MoveHub.h"

XboxSeriesXControllerESP32_asukiaaa::Core xbox;

enum class AppState : uint8_t {
  XboxConnect,  // waiting for the controller
  HubScan,      // looking for the hub (only while the controller is connected)
  HubConnect,   // connecting and pairing
  Calibrate,    // hub sweeps the steering against both end stops
  Disarmed,     // connected, drive locked, steering follows the stick
  Armed         // full control
};

// Button state, and at the same time the carrier for the edges.
//
// Must appear BEFORE the first function: the Arduino preprocessor inserts its
// generated function prototypes above the first function. If the type came
// after that, the prototypes would not know it.
struct ButtonEdges {
  bool start = false, b = false, xbox = false, a = false, x = false, y = false;
  bool lb = false, rb = false;
  bool dirLeft = false, dirRight = false;
  bool select  = false;  // the View key, two rectangles, left of the Xbox button
};

// Indicators latch like in a car: press once to switch on, press again to
// switch off. They also cancel themselves once you have steered into their
// direction and returned to centre.
enum class Blinker : uint8_t { None, Left, Right, Hazard };

static AppState    appState = AppState::XboxConnect;
static ButtonEdges prevBtn;

static uint32_t lastTickAt   = 0;
static uint32_t lastStatusAt = 0;
static uint32_t calibrationStartedAt = 0;

static int8_t  currentSpeed = 0;  // slew-limited actual value
static int8_t  maxSpeed     = DEFAULT_MAX_SPEED;
static int8_t  maxSteer     = DEFAULT_MAX_STEER;
static uint8_t speedStep    = DEFAULT_SPEED_STEP;

static uint8_t lightMode    = HubLights::LIGHTS_ON;

// Crossed light guides — also a property of the build, see Config.h.
static bool swapFrontLeds = SWAP_FRONT_LEDS;
static bool swapRearLeds  = SWAP_REAR_LEDS;

// Direct LED control via port 0x35, for the 'E' command.
//
// Has to be repeated: the VM on port 0x36 paints the LED array according to
// its own light program, so a one-shot command is overwritten straight away.
// Repeating it every LED_REFRESH_MS is enough — LEDs 1 and 4 are not part of
// that light program and hold their value on their own.
static bool    ledOverride   = false;
static uint8_t ledMask       = 0;
static uint8_t ledBrightness = 100;

// Master switch for everything that writes to the LED array. Lets the LED
// path be ruled in or out as a suspect without reflashing.
static bool ledsEnabled = true;

// Loop timing watchdog. A tick that takes far longer than its 50 ms budget
// means something blocked - and delayed drive frames look exactly like a
// vehicle that stops responding.
static uint32_t slowTicks   = 0;
static uint32_t worstTickMs = 0;

// Until when the charge gauge is on screen. Set by the View key.
static uint32_t gaugeUntil = 0;

// Charging is inferred, not reported: the hub has no "charging" flag in the
// protocol, but a level that GOES UP can only mean one thing. It creeps up a
// percent at a time, so the state is held for a while after each rise; a fall
// ends it immediately, which also shrugs off a reading that wobbles.
static int      lastBatt      = -1;
static uint32_t chargingUntil = 0;

// Kept in flash, because the detection is slow to start: it needs the reading
// to climb a whole percent, which on a nearly full pack takes minutes. Without
// this every restart — a firmware update above all — would blank the display
// for that long while the car sits on the cable, looking broken. A falling
// reading clears it immediately, so a stale flag cannot survive a drive.
static bool chargeSticky = false;

// Set by VIEW to dismiss the permanent charging display. Cleared by VIEW and
// whenever charging ends, so it never outlives the thing it silences.
static bool gaugeMuted = false;

// Set by VIEW while the car is parked: the display then stays until it is
// switched off, instead of timing out.
static bool gaugeLatched = false;

static Blinker blinker = Blinker::None;
// Self-cancelling only arms once the wheel has actually been turned —
// otherwise the indicator would switch itself off the moment it comes on.
static bool blinkerCancelArmed = false;

static String serialLine;

// Why did we start, and how often?
//
// A vehicle that suddenly stops dead looks identical from the outside whether
// the hub went away or the bridge itself rebooted — and only one of those is
// a firmware bug. The boot counter is persisted, so a restart cannot hide:
// if it goes up while nobody flashed anything, the board restarted on its own.
static uint32_t bootCount = 0;

// Last hub disconnect, kept across restarts for the same reason: the moment
// it happens is never the moment somebody is watching the console.
static int      lastDiscReason = 0;
static uint32_t lastDiscBoot   = 0;

// How long the hub link had been standing when it broke.
static bool     hubWasConnected = false;
static uint32_t hubUpSince      = 0;

// Settings in the ESP32's flash (NVS).
//
// They survive a restart AND a firmware update — so when moving to another
// model you change the motor direction once and never recompile again. The
// values in Config.h are only the factory defaults.
static Preferences prefs;
static const char* PREFS_NAMESPACE = "porsche";

static void loadSettings() {
  prefs.begin(PREFS_NAMESPACE, true);
  maxSpeed       = prefs.getChar("maxSpeed", DEFAULT_MAX_SPEED);
  maxSteer       = prefs.getChar("maxSteer", DEFAULT_MAX_STEER);
  swapFrontLeds  = prefs.getBool("swapF", SWAP_FRONT_LEDS);
  swapRearLeds   = prefs.getBool("swapR", SWAP_REAR_LEDS);
  bootCount      = prefs.getUInt("boots", 0);
  lastDiscReason = prefs.getInt("discWhy", 0);
  lastDiscBoot   = prefs.getUInt("discBoot", 0);
  chargeSticky   = prefs.getBool("charging", false);
  prefs.end();

  // Trust the stored flag for one hold period. If the car really is still on
  // the cable the next rise renews it; if it is not, the first falling reading
  // clears it within seconds.
  if (chargeSticky) chargingUntil = millis() + CHARGE_HOLD_MS;
}

static void saveSettings() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putChar("maxSpeed", maxSpeed);
  prefs.putChar("maxSteer", maxSteer);
  prefs.putBool("swapF", swapFrontLeds);
  prefs.putBool("swapR", swapRearLeds);
  prefs.putUInt("boots", bootCount);
  prefs.putInt("discWhy", lastDiscReason);
  prefs.putUInt("discBoot", lastDiscBoot);
  prefs.end();
}

static bool isCharging() { return millis() < chargingUntil; }

static void saveCharging() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putBool("charging", chargeSticky);
  prefs.end();
}

static void trackCharging() {
  const int b = hub.batteryPercent();
  if (b < 0) return;
  if (lastBatt >= 0) {
    if (b > lastBatt) {
      chargingUntil = millis() + CHARGE_HOLD_MS;
      if (!chargeSticky) {
        chargeSticky = true;
        saveCharging();
      }
    } else if (b < lastBatt) {
      chargingUntil = 0;
      gaugeMuted    = false;
      if (chargeSticky) {
        chargeSticky = false;
        saveCharging();
      }
    }
  }
  lastBatt = b;
}

static const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external reset";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "PANIC - firmware crashed";
    case ESP_RST_INT_WDT:   return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT:  return "TASK WATCHDOG";
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_BROWNOUT:  return "BROWNOUT - supply voltage dipped";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
    default:                return "unknown";
  }
}

static void printSettings() {
  Serial.printf("[cfg] m%d  s%d\n", (int)maxSpeed, (int)maxSteer);
  Serial.printf("[cfg] light guides: front %s, rear %s\n",
                swapFrontLeds ? "crossed" : "straight",
                swapRearLeds ? "crossed" : "straight");
  Serial.printf("[cfg] boot #%lu | last hub disconnect: reason %d (boot #%lu)\n",
                (unsigned long)bootCount, lastDiscReason, (unsigned long)lastDiscBoot);
}

static void resetSettings() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  maxSpeed       = DEFAULT_MAX_SPEED;
  maxSteer       = DEFAULT_MAX_STEER;
  swapFrontLeds  = SWAP_FRONT_LEDS;
  swapRearLeds   = SWAP_REAR_LEDS;
  Serial.println("[cfg] reset to factory defaults");
  printSettings();
}

// Measurement: how quickly does the BLE link notice a powered-off controller?
//
// Since the failsafe rests on xbox.isConnected() alone, this is THE safety
// figure — it is how long the vehicle keeps driving in the worst case. The
// value stays in the status line once measured.
static bool          xboxWasConnected  = false;
static unsigned long lastNotifSeen     = 0;
static uint32_t      lastDropLatencyMs = 0;
static bool          haveDropLatency   = false;

// Finds the controller's link among the open BLE connections. The Xbox
// library does not hand out its client, but it does expose its address.
static NimBLEClient* findXboxClient() {
  const String want = xbox.buildDeviceAddressStr();
  for (auto* c : NimBLEDevice::getConnectedClients()) {
    if (String(c->getPeerAddress().toString().c_str()).equalsIgnoreCase(want)) return c;
  }
  return nullptr;
}

// Request a shorter supervision timeout once per connection, so a powered-off
// controller is noticed sooner. See Config.h.
static bool xboxParamsTuned = false;

static void tuneXboxLink() {
  if (xboxParamsTuned || !xbox.isConnected()) return;
  NimBLEClient* c = findXboxClient();
  if (c == nullptr) return;

  const bool ok = c->updateConnParams(XBOX_CONN_MIN_INTERVAL, XBOX_CONN_MAX_INTERVAL,
                                      XBOX_CONN_LATENCY, XBOX_CONN_TIMEOUT);
  Serial.printf("[xbox] shorter supervision timeout requested: %s\n",
                ok ? "accepted" : "refused");
  xboxParamsTuned = true;
}

static void trackLinkLoss() {
  // Keep our own copy: on a dropout the library resets its own timestamp to
  // 0, which would take the reference with it.
  const unsigned long notifAt = xbox.getReceiveNotificationAt();
  if (notifAt != 0) lastNotifSeen = notifAt;

  const bool up = xbox.isConnected();
  if (xboxWasConnected && !up && lastNotifSeen != 0) {
    lastDropLatencyMs = (uint32_t)(millis() - lastNotifSeen);
    haveDropLatency   = true;
    Serial.printf(">>> dropout detected %lu ms after the last notification\n",
                  (unsigned long)lastDropLatencyMs);
    lastNotifSeen = 0;
  }
  if (!up) xboxParamsTuned = false;  // request again on the next connection
  xboxWasConnected = up;

  // Keep the hub's last disconnect reason in flash. The moment it happens is
  // never the moment somebody is watching the console.
  const int r = hub.lastDisconnectReason();
  if (r != 0 && (r != lastDiscReason || bootCount != lastDiscBoot)) {
    lastDiscReason = r;
    lastDiscBoot   = bootCount;
    saveSettings();

    // Park the frame log too — printing it only at the moment of the drop is
    // useless, because that is never when anybody is reading the console.
    static uint8_t buf[600];
    const size_t   n = hub.exportWriteLog(buf, sizeof(buf));
    prefs.begin(PREFS_NAMESPACE, false);
    if (n > 0) prefs.putBytes("wlog", buf, n);
    // The hub's own battery level and how long the link had held. A hub that
    // browns out under load looks exactly like one that hangs up on purpose:
    // link gone, lights out, steering stuck. These two tell them apart.
    prefs.putInt("discBatt", hub.batteryPercent());
    prefs.putUInt("discUp", hubUpSince ? (millis() - hubUpSince) : 0);
    prefs.end();
  }

  // Note when the hub link came up, so its length can be recorded above.
  const bool hubUp = hub.isConnected();
  if (hubUp && !hubWasConnected) hubUpSince = millis();
  hubWasConnected = hubUp;
}

// Only two real light states. The values 0x01 and 0x05 carried earlier have
// bit 0 set and therefore held the brake permanently — in those "modes" the
// vehicle would not move at all.
static const uint8_t LIGHT_MODES[2] = {HubLights::LIGHTS_ON, HubLights::LIGHTS_OFF};
static uint8_t       lightModeIndex = 0;

static const char* appStateName() {
  switch (appState) {
    case AppState::XboxConnect: return "waiting for controller";
    case AppState::HubScan:     return "searching hub";
    case AppState::HubConnect:  return "connecting hub";
    case AppState::Calibrate:   return "calibrating";
    case AppState::Disarmed:    return "DISARMED";
    case AppState::Armed:       return "ARMED";
  }
  return "?";
}

static void printHelp() {
  Serial.println();
  Serial.println("Serial commands:");
  Serial.println("  c       recalibrate the steering");
  Serial.println("  d       disarm immediately");
  Serial.println("  m<n>    workshop limit (1..100). The effective cap is");
  Serial.println("          m-value x speed step (LB/RB) = vmax");
  Serial.println("  s<n>    maximum steering angle (1..100)");
  Serial.println("  v       show stored settings");
  Serial.println("  r       reset settings to factory defaults");
  Serial.println("  o       query the hub's ports (what is built in?)");
  Serial.println("  E<hex>[,bright]  individual LEDs: E3F,100 all, E09,100 LED 1+4,");
  Serial.println("                   E01 only LED 1, E all off, Ex = back to the VM");
  Serial.println("  n[hex]  query a port's mode names, e.g. n35");
  Serial.println("  l       LED control on/off (to rule the LED path in or out)");
  Serial.println("  w       the frames sent just before the last hub drop");
  Serial.println("  b       list stored BLE bonds");
  Serial.println();
  Serial.println("Vehicle configuration (kept in flash):");
  Serial.println("  f       front light guides crossed / straight");
  Serial.println("  h       rear light guides crossed / straight");
  Serial.println("  x       delete all BLE bonds and restart");
  Serial.println("  ?       this help");
  Serial.println();
  Serial.println("Controller: RT throttle | LT brake/reverse | left stick steering");
  Serial.println("            START arm | B or XBOX emergency stop (parking brake)");
  Serial.println("            A lights | X hazard lights | LB/RB speed step");
  Serial.println("            Y held = boost (full workshop limit)");
  Serial.println("            right stick move = light ring, click = headlight flash");
  Serial.println("            D-pad left/right = indicators");
  Serial.println("            VIEW (two rectangles) = hub charge on the ring, on/off");
  Serial.println();
}

// Lists the stored BLE bonds. Non-destructive, purely diagnostic: a bond with
// an unusable address explains a connection that fails without saying much.
static void printBonds() {
  const int n = NimBLEDevice::getNumBonds();
  Serial.printf("[bonds] %d stored\n", n);
  for (int i = 0; i < n; i++) {
    const NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    Serial.printf("        %d: %s  type=%u\n", i, a.toString().c_str(),
                  (unsigned)a.getType());
  }
}

// Known hub addresses taken from the stored bonds.
//
// The scan reports the hub with an all-zero address and is therefore only
// good for the very first pairing. Once the hub has been paired, its valid
// address sits in the bond — and that is tried first.
static const uint8_t MAX_BOND_CANDIDATES = 4;
static NimBLEAddress bondCandidates[MAX_BOND_CANDIDATES];
static uint8_t       bondCandidateCount  = 0;
static uint8_t       bondCandidateNext   = 0;
static bool          bondCandidatesReady = false;

static void loadBondCandidates() {
  bondCandidateCount = 0;
  bondCandidateNext  = 0;

  // The controller is connected at this point, so its address is known.
  // Everything else in the bond store is a candidate for the hub.
  const String xboxAddr = xbox.buildDeviceAddressStr();

  const int n = NimBLEDevice::getNumBonds();
  for (int i = 0; i < n && bondCandidateCount < MAX_BOND_CANDIDATES; i++) {
    const NimBLEAddress a = NimBLEDevice::getBondedAddress(i);
    const String        s(a.toString().c_str());
    if (s.equalsIgnoreCase(xboxAddr)) continue;
    if (s == "00:00:00:00:00:00") continue;
    bondCandidates[bondCandidateCount++] = a;
  }

  bondCandidatesReady = true;
  Serial.printf("[hub] %u known address(es) from the bond store\n",
                (unsigned)bondCandidateCount);
}

static void disarm(const char* reason) {
  if (appState == AppState::Armed) {
    Serial.printf("[!] disarmed: %s\n", reason);
  }
  currentSpeed = 0;
  if (appState == AppState::Armed) appState = AppState::Disarmed;
}

static void handleSerial() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (serialLine.length() < 32) serialLine += c;
      continue;
    }

    serialLine.trim();
    if (serialLine.length() == 0) {
      serialLine = "";
      continue;
    }

    const char cmd = serialLine.charAt(0);
    const int  arg = (serialLine.length() > 1) ? serialLine.substring(1).toInt() : -1;

    switch (cmd) {
      case 'c':
        if (hub.isConnected()) {
          disarm("calibration requested");
          appState             = AppState::Calibrate;
          calibrationStartedAt = 0;
        } else {
          Serial.println("[!] hub not connected");
        }
        break;

      case 'd':
        disarm("serial command");
        appState = (hub.isConnected()) ? AppState::Disarmed : appState;
        break;

      case 'm':
        if (arg >= 1 && arg <= PROTOCOL_LIMIT) {
          maxSpeed = (int8_t)arg;
          saveSettings();
          Serial.printf("[cfg] workshop limit = %d (stored)\n", maxSpeed);
        } else {
          Serial.println("[!] range 1..100");
        }
        break;

      case 's':
        if (arg >= 1 && arg <= PROTOCOL_LIMIT) {
          maxSteer = (int8_t)arg;
          saveSettings();
          Serial.printf("[cfg] maximum steering angle = %d (stored)\n", maxSteer);
        } else {
          Serial.println("[!] range 1..100");
        }
        break;


      case 'x':
        Serial.println("[cfg] deleting all BLE bonds and restarting ...");
        NimBLEDevice::deleteAllBonds();
        delay(200);
        ESP.restart();
        break;

      case 'b':
        printBonds();
        break;

      case 'f':
        swapFrontLeds = !swapFrontLeds;
        saveSettings();
        Serial.printf("[cfg] front light guides %s (stored)\n",
                      swapFrontLeds ? "crossed" : "straight");
        break;

      case 'h':
        swapRearLeds = !swapRearLeds;
        saveSettings();
        Serial.printf("[cfg] rear light guides %s (stored)\n",
                      swapRearLeds ? "crossed" : "straight");
        break;

      case 'l':
        ledsEnabled = !ledsEnabled;
        Serial.printf("[cfg] LED control %s\n", ledsEnabled ? "on" : "OFF");
        break;

      case 'w': {
        // The frames that went out just before the last drop. Reading them
        // only at boot is not enough: the board usually keeps running while
        // the hub is gone, so the boot line never comes and the evidence sits
        // in flash unreachable.
        static uint8_t buf[600];
        prefs.begin(PREFS_NAMESPACE, true);
        const size_t   n    = prefs.getBytes("wlog", buf, sizeof(buf));
        const int      batt = prefs.getInt("discBatt", -1);
        const uint32_t up   = prefs.getUInt("discUp", 0);
        prefs.end();
        Serial.printf("[hub] last drop: reason %d, link had held %lus, hub battery %d%%\n",
                      lastDiscReason, (unsigned long)(up / 1000), batt);
        if (n >= hub.writeLogSize()) {
          hub.printExportedWriteLog(buf, n);
        } else {
          Serial.println("[hub] no frame log stored yet.");
        }
        Serial.println("[hub] frames written since then:");
        hub.dumpRecentWrites();
        break;
      }

      case 'r':
        resetSettings();
        break;

      case 'v':
        printSettings();
        break;

      case 'o':
        hub.requestPortInventory();
        break;

      case 'n':
        // n35 queries port 0x35 (hex, without 0x). Without an argument: the
        // LED array and the VM — the two that concern the lights.
        if (serialLine.length() > 1) {
          hub.requestModeInfo((uint8_t)strtol(serialLine.substring(1).c_str(), nullptr, 16));
        } else {
          hub.requestModeInfo(0x35);
          hub.requestModeInfo(0x36);
        }
        break;

      case 'E': {
        // E<mask hex>,<brightness>  e.g. E3F,100 = all six at full
        //                                E09,100 = LED 1 and 4 only
        // Without a brightness: 100. Ex ends the override.
        const int    comma = serialLine.indexOf(',');
        const String maskStr =
            (comma > 0) ? serialLine.substring(1, comma) : serialLine.substring(1);
        const uint8_t mask =
            maskStr.length() ? (uint8_t)strtol(maskStr.c_str(), nullptr, 16) : 0;
        const uint8_t bright =
            (comma > 0) ? (uint8_t)serialLine.substring(comma + 1).toInt() : 100;

        if (maskStr.equalsIgnoreCase("x")) {
          // Clear explicitly: otherwise the last state stays, because the hub
          // never resets the LEDs on its own.
          hub.setLeds(0x3F, 0);
          ledOverride = false;
          Serial.println("[led] direct control off, all cleared.");
          Serial.println("      Press A on the controller to set the lights again.");
          break;
        }

        ledOverride   = true;
        ledMask       = mask;
        ledBrightness = bright;
        Serial.printf("[led] mask 0x%02X, brightness %u (sent continuously)\n", mask & 0x3F,
                      bright);
        if (!hub.setLeds(mask, bright)) Serial.println("[!] hub not connected");
        break;
      }

      case '?':
        printHelp();
        break;

      default:
        Serial.printf("[!] unknown command: %s\n", serialLine.c_str());
        break;
    }
    serialLine = "";
  }
}

#if BUTTON_DEBUG
// Shows which buttons are pressed on every change. That reveals which field
// of the library belongs to which physical button.
static void debugButtons() {
  auto&  n = xbox.xboxNotif;
  String s;
  if (n.btnA) s += "A ";
  if (n.btnB) s += "B ";
  if (n.btnX) s += "X ";
  if (n.btnY) s += "Y ";
  if (n.btnLB) s += "LB ";
  if (n.btnRB) s += "RB ";
  if (n.btnLS) s += "LS ";
  if (n.btnRS) s += "RS ";
  if (n.btnStart) s += "Start ";
  if (n.btnSelect) s += "Select ";
  if (n.btnShare) s += "Share ";
  if (n.btnXbox) s += "Xbox ";
  if (n.btnDirUp) s += "Up ";
  if (n.btnDirDown) s += "Down ";
  if (n.btnDirLeft) s += "Left ";
  if (n.btnDirRight) s += "Right ";

  static String last = "@";
  if (s != last) {
    last = s;
    Serial.printf("[button] %s\n", s.length() ? s.c_str() : "(none)");
  }
}
#endif

// Read the edges and carry the previous state forward.
//
// The previous state is ALWAYS updated, even when no edges are emitted.
// Otherwise a button held across a state change would produce a phantom edge
// right afterwards.
static ButtonEdges readButtonEdges(bool emitEdges) {
  auto&       n = xbox.xboxNotif;
  ButtonEdges e;

  if (emitEdges) {
    e.start    = n.btnStart && !prevBtn.start;
    e.b        = n.btnB && !prevBtn.b;
    e.xbox     = n.btnXbox && !prevBtn.xbox;
    e.a        = n.btnA && !prevBtn.a;
    e.x        = n.btnX && !prevBtn.x;
    e.y        = n.btnY && !prevBtn.y;
    e.lb       = n.btnLB && !prevBtn.lb;
    e.rb       = n.btnRB && !prevBtn.rb;
    e.dirLeft  = n.btnDirLeft && !prevBtn.dirLeft;
    e.dirRight = n.btnDirRight && !prevBtn.dirRight;
    e.select   = n.btnSelect && !prevBtn.select;
  }

  prevBtn.start    = n.btnStart;
  prevBtn.b        = n.btnB;
  prevBtn.xbox     = n.btnXbox;
  prevBtn.a        = n.btnA;
  prevBtn.x        = n.btnX;
  prevBtn.y        = n.btnY;
  prevBtn.lb       = n.btnLB;
  prevBtn.rb       = n.btnRB;
  prevBtn.dirLeft  = n.btnDirLeft;
  prevBtn.dirRight = n.btnDirRight;
  prevBtn.select   = n.btnSelect;

  return e;
}

// Apply the buttons.
//
// Y is deliberately absent: the boost acts while the button is held, so it is
// read directly in the drive path rather than through an edge.
static void applyButtons(const ButtonEdges& edges) {
  auto& n = xbox.xboxNotif;

  const bool startEdge = edges.start;
  const bool bEdge     = edges.b;
  const bool xboxEdge  = edges.xbox;
  const bool aEdge     = edges.a;
  const bool lbEdge    = edges.lb;
  const bool rbEdge    = edges.rb;

  if (bEdge || xboxEdge) disarm("emergency stop");

  if (startEdge && appState == AppState::Disarmed) {
    // Do not arm while the throttle is applied, or the car would leap away.
    const bool throttleNeutral =
        normalizeTrigger(n.trigRT) < 0.1f && normalizeTrigger(n.trigLT) < 0.1f;
    if (throttleNeutral) {
      currentSpeed = 0;
      appState     = AppState::Armed;
      gaugeLatched = false;  // the parked display makes way for driving
      Serial.println("[+] ARMED - the vehicle now responds to input");
    } else {
      Serial.println("[!] release the triggers first, then press START");
    }
  }

  if (aEdge) {
    lightModeIndex = (uint8_t)((lightModeIndex + 1) % 2);
    lightMode      = LIGHT_MODES[lightModeIndex];
    Serial.printf("[cfg] lights %s\n", lightMode == HubLights::LIGHTS_ON ? "on" : "off");
  }

  // X is the hazard lights: both free LEDs at once. Unlike the indicators
  // they do not cancel themselves when steering.
  if (edges.x) {
    blinker            = (blinker == Blinker::Hazard) ? Blinker::None : Blinker::Hazard;
    blinkerCancelArmed = false;
    Serial.printf("[light] hazard lights %s\n",
                  blinker == Blinker::Hazard ? "on" : "off");
  }

  // View shows the hub's charge as a level around the light ring. A glance at
  // the fuel gauge, so it times out on its own rather than needing a second
  // press while driving.
  if (edges.select) {
    // Same key on and off.
    //
    // How long it stays depends on what the car is doing. Parked — which is
    // what it is while charging — there is no reason for it to vanish, so it
    // stays until it is switched off again. Armed, it is a glance at the gauge
    // and times out on its own, because a light show is not what you want
    // while driving.
    if (!gaugeMuted && (isCharging() || gaugeLatched || millis() < gaugeUntil)) {
      gaugeMuted   = true;
      gaugeLatched = false;
      gaugeUntil   = 0;
      Serial.println("[light] charge display off");
    } else {
      gaugeMuted   = false;
      gaugeLatched = (appState == AppState::Disarmed);
      gaugeUntil   = gaugeLatched ? 0 : millis() + GAUGE_SHOW_MS;

      const int pct = hub.batteryPercent();
      if (pct < 0) {
        Serial.println("[light] charge unknown - the hub has not reported yet");
      } else {
        Serial.printf("[light] hub charge %d %%%s\n", pct,
                      isCharging() ? " (charging)" : "");
      }
    }
  }

  // Indicators latch: pressing the same direction again switches them off,
  // the other direction switches straight over.
  if (edges.dirLeft) {
    blinker            = (blinker == Blinker::Left) ? Blinker::None : Blinker::Left;
    blinkerCancelArmed = false;
    Serial.printf("[light] indicator %s\n", blinker == Blinker::Left ? "left" : "off");
  }
  if (edges.dirRight) {
    blinker            = (blinker == Blinker::Right) ? Blinker::None : Blinker::Right;
    blinkerCancelArmed = false;
    Serial.printf("[light] indicator %s\n", blinker == Blinker::Right ? "right" : "off");
  }

  if (lbEdge && speedStep > 0) {
    speedStep--;
    Serial.printf("[cfg] speed step %u %%\n", SPEED_STEPS_PERCENT[speedStep]);
  }
  if (rbEdge && speedStep < SPEED_STEP_COUNT - 1) {
    speedStep++;
    Serial.printf("[cfg] speed step %u %%\n", SPEED_STEPS_PERCENT[speedStep]);
  }
}

// Light ring on the right stick, indicators on the D-pad.
//
// The six LEDs sit in a ring around the vehicle, 60 degrees apart.
// 0 degrees is front, positive is clockwise:
//
//        2 (-30)   3 (+30)
//   1 (-90)   [car]   4 (+90)
//        5 (+210)  6 (+150)
//
// The numbering runs "left before right" per pair: front 2 left and 3 right,
// rear 5 left and 6 right. It therefore does NOT run around the circle in
// order — measured on the hardware.
//
// The stick's direction picks the point on the ring, its deflection the
// brightness. The cosine of the angular distance cross-fades neighbours, so
// the light travels instead of jumping from LED to LED.
static const float LED_ANGLE_DEG[6] = {-90.0f, -30.0f, 30.0f, 90.0f, 210.0f, 150.0f};

// Where LED i actually sits on the car.
//
// The angles above describe the sockets on the hub. Which lamp a socket ends
// up lighting depends on how the light guides were routed during the build —
// on the 42176 the front pair is crossed, so the front-left socket feeds the
// front-right headlight. Swapping the pair here keeps the ring matching what
// is visible, without touching anything else.
static float ledAngleDeg(int i) {
  int idx = i;
  if (swapFrontLeds) {
    if (i == 1) idx = 2;
    else if (i == 2) idx = 1;
  }
  if (swapRearLeds) {
    if (i == 4) idx = 5;
    else if (i == 5) idx = 4;
  }
  return LED_ANGLE_DEG[idx];
}

// Physical position on the car -> LED index, honouring crossed light guides.
// Indices are zero-based, so LED 1 is 0.
static int ledFrontLeft() { return swapFrontLeds ? 2 : 1; }
static int ledFrontRight() { return swapFrontLeds ? 1 : 2; }
static int ledRearLeft() { return swapRearLeds ? 5 : 4; }
static int ledRearRight() { return swapRearLeds ? 4 : 5; }
static const int LED_MID_LEFT  = 0;  // LED 1, the free socket on the left
static const int LED_MID_RIGHT = 3;  // LED 4, the free socket on the right

// Sends a set of six brightnesses to the hub, one frame per tick at most.
//
// The straightforward version wrote one frame per distinct brightness level,
// so a smooth running light cost six frames per update on top of the 20 Hz
// drive frame. That was suspected of killing the link and it turned out not
// to be the cause — but 120 frames a second for six lamps is wasteful either
// way, and the animation looks the same at a fifth of that.
//
// Two things keep the rate down:
//
//   * Only send what changed. The hub never resets an LED by itself, so an
//     unchanged lamp needs no traffic at all. The exception is LEDs 2, 3, 5
//     and 6: the hub's own light program repaints those, so they have to be
//     refreshed periodically to stay overridden. LEDs 1 and 4 are untouched
//     by the light program and therefore hold their value indefinitely.
//
//   * At most one frame per tick. What does not fit waits for the next one;
//     with 20 ticks a second an animation still looks continuous.
static uint8_t  ledSent[6]    = {255, 255, 255, 255, 255, 255};  // 255 = unknown
static uint32_t ledFramesSent = 0;  // shown in the status line, so the radio
                                    // load is a measured number, not a guess

// Call after writing the LEDs by any other route, so the next emitLeds() does
// not skip a lamp it believes is already correct.
static void forgetLedState() {
  for (int i = 0; i < 6; i++) ledSent[i] = 255;
}

static void emitLeds(const uint8_t want[6]) {
  static uint32_t refreshedAt = 0;
  uint8_t*        sent        = ledSent;

  if (!ledsEnabled) return;

  // Is it time to reassert the lamps the hub's light program overwrites?
  const bool refresh = (millis() - refreshedAt) >= LED_REFRESH_MS;
  if (refresh) refreshedAt = millis();

  bool changed[6] = {false, false, false, false, false, false};
  bool needs[6]   = {false, false, false, false, false, false};
  for (int i = 0; i < 6; i++) {
    const bool ownedByVm = (i != LED_MID_LEFT && i != LED_MID_RIGHT);
    changed[i]           = (want[i] != sent[i]);
    needs[i]             = changed[i] || (refresh && ownedByVm);
  }

  // Pick the next pending lamp ROUND ROBIN, not the biggest group.
  //
  // Picking the biggest group looks cheaper and is wrong: in a running light
  // the largest group is always the dark one, so the dark LEDs would be
  // served on every tick and the bright head of the wave never at all. The
  // cursor makes sure every lamp gets its turn.
  //
  // But a lamp whose value actually CHANGED goes before one that only wants
  // its periodic re-assertion: a repeat can wait a tick, a change cannot.
  // Without that the headlight flash comes out uneven — the refresh of the
  // four driving lamps keeps taking the very tick in which the flash wanted
  // to switch, which costs 50 ms at a time and is plainly visible.
  static uint8_t cursor = 0;

  int pick = -1;
  for (int pass = 0; pass < 2 && pick < 0; pass++) {
    for (int k = 0; k < 6; k++) {
      const int i = (cursor + k) % 6;
      if (pass == 0 ? changed[i] : needs[i]) {
        pick = i;
        break;
      }
    }
  }
  if (pick < 0) return;
  cursor = (uint8_t)((pick + 1) % 6);

  // One frame carries any number of LEDs as long as they share a brightness,
  // so take everything else that is waiting for the same value along.
  uint8_t mask = 0;
  for (int j = 0; j < 6; j++) {
    if (needs[j] && want[j] == want[pick]) mask |= (uint8_t)(1 << j);
  }

  if (!hub.setLeds(mask, want[pick])) return;
  ledFramesSent++;
  for (int i = 0; i < 6; i++) {
    if (mask & (1 << i)) sent[i] = want[i];
  }
}

// Fills the six brightnesses as a charge gauge running once around the ring.
//
// It is a dial, not a battery symbol: the lit arc starts at the left flank and
// grows anti-clockwise — left, tail, right, nose — until the circle closes at
// 100 %. So the half-way mark lies on the car's LONG axis, and a quarter charge
// lights the left quarter of the car.
//
// While charging, a light additionally travels through the DARK part of the
// ring, in the same anti-clockwise direction the arc grows — the gap filling
// itself, over and over.
//
// The edge is soft over one lamp spacing: a lamp sitting exactly at the end of
// the arc shows half brightness. That is what makes three quarters read as
// "left half lit, one headlight fading, the other dark".
static void ringAsGauge(float level, bool charging, uint8_t want[6]) {
  // Where the travelling light currently sits: from the end of the arc round
  // to the seam, then back to the start of the gap.
  float head = 0.0f;
  if (charging && level < 1.0f) {
    const float phase = (float)(millis() % CHARGE_SPIN_MS) / (float)CHARGE_SPIN_MS;
    head              = level + phase * (1.0f - level);
  }

  for (int i = 0; i < 6; i++) {
    // Position along the arc, 0 at the left flank, growing anti-clockwise.
    // ledAngleDeg is 0 at the nose and positive clockwise, so the left flank
    // (-90 degrees) is the origin and the direction has to be reversed.
    float pos = (-90.0f - ledAngleDeg(i)) / 360.0f;
    pos -= floorf(pos);  // wrap into 0..1

    float f = 0.5f + (level - pos) / GAUGE_SOFTNESS;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;

    // At a full charge the arc closes on itself; without this the lamp at the
    // seam would sit half lit next to its own starting point.
    if (level >= 1.0f) f = 1.0f;

    if (charging && pos > level) {
      float d = fabsf(pos - head);
      float g = 1.0f - d / GAUGE_SOFTNESS;
      if (g > f) f = (g > 1.0f) ? 1.0f : g;
    }

    int v   = (int)lroundf(f * 100.0f);
    v       = (v / LIGHT_RING_STEP) * LIGHT_RING_STEP;
    want[i] = (uint8_t)constrain(v, 0, 100);
  }
}

// Fills the six brightnesses for one direction on the ring.
// ang: 0 = front, positive clockwise. r: 0..1 as overall brightness.
static void ringFromAngle(float ang, float r, uint8_t want[6]) {
  for (int i = 0; i < 6; i++) {
    float d = ang - ledAngleDeg(i) * 0.01745329f;
    while (d > 3.14159265f) d -= 6.28318531f;
    while (d < -3.14159265f) d += 6.28318531f;

    float c = cosf(d);
    if (c < 0.0f) c = 0.0f;

    // Sharpen: with a plain cosine the neighbour 60 degrees away still glows
    // at half brightness, so full deflection to the right also lit the front
    // and rear a little. The exponent suppresses that (6 % remain at 60
    // degrees, which the quantisation rounds to 0) while the cross-fade
    // between two LEDs stays smooth.
    c = powf(c, LIGHT_RING_SHARPNESS);

    int v   = (int)lroundf((r > 1.0f ? 1.0f : r) * c * 100.0f);
    v       = (v / LIGHT_RING_STEP) * LIGHT_RING_STEP;  // quantise
    want[i] = (uint8_t)constrain(v, 0, 100);
  }
}

// The driving lights as the hub's own light program would paint them. Needed
// as the backdrop for the headlight flash, which takes over the front pair
// only and has to leave a sensible picture on the other four lamps.
static void fillDrivingLights(uint8_t want[6], bool braking) {
  const bool on = (lightMode == HubLights::LIGHTS_ON);

  want[ledFrontLeft()]  = (uint8_t)(on ? 100 : 0);
  want[ledFrontRight()] = (uint8_t)(on ? 100 : 0);
  want[ledRearLeft()]   = (uint8_t)(on ? (braking ? 100 : 30) : 0);
  want[ledRearRight()]  = (uint8_t)(on ? (braking ? 100 : 30) : 0);
}

static void updateRingAndBlinkers(XboxControllerNotificationParser& n, int8_t steering,
                                  bool braking) {
  static bool    wasActive = false;

  // Self-cancelling as in a car: the cancel only arms once the wheel has
  // actually been turned into the indicated direction; returning to centre
  // then switches the indicator off.
  if (blinker == Blinker::Left && steering <= -BLINKER_CANCEL_ANGLE) blinkerCancelArmed = true;
  if (blinker == Blinker::Right && steering >= BLINKER_CANCEL_ANGLE) blinkerCancelArmed = true;
  if (blinkerCancelArmed && abs((int)steering) <= BLINKER_CENTER_ANGLE) {
    blinker            = Blinker::None;
    blinkerCancelArmed = false;
    Serial.println("[light] indicator cancelled by steering");
  }

  uint8_t want[6] = {0, 0, 0, 0, 0, 0};
  bool    active  = false;

  // While charging the gauge stays up permanently — the car is parked on the
  // cable and the ring is the only thing anyone wants to see from it.
  // Otherwise it is a three-second glance on the View key, and it takes
  // precedence for that time: a gauge an indicator can interrupt is no gauge.
  if (!gaugeMuted && (isCharging() || gaugeLatched || millis() < gaugeUntil)) {
    active = true;
    const int pct = hub.batteryPercent();
    ringAsGauge((pct < 0) ? 0.0f : (float)pct / 100.0f, isCharging(), want);
  } else if (blinker == Blinker::Hazard) {
    // Hazard is not an indicator. It says "I am standing here", not "I am
    // about to turn" — so it deliberately does NOT sweep: a wave carries a
    // direction, and direction is the one thing the hazard lights must not
    // suggest. All six lamps go on and off together.
    active = true;

    if (millis() % (BLINKER_PERIOD_MS * 2) < BLINKER_PERIOD_MS) {
      for (int i = 0; i < 6; i++) want[i] = 100;
    }
  } else if (blinker != Blinker::None) {
    // Sweeping indicator, the way a modern car does it: the lamps along that
    // side light up one after another from the rear towards the front, then
    // all go dark together. The direction of travel is the direction you are
    // about to turn, which is what makes it readable at a glance.
    //
    // Which LED sits where depends on how the light guides were routed, so
    // the sequence is built from physical positions, not hub numbering.
    active = true;

    const uint32_t cycle = millis() % (BLINKER_PERIOD_MS * 2);
    if (cycle < BLINKER_PERIOD_MS) {
      const bool doLeft  = (blinker == Blinker::Left);
      const bool doRight = (blinker == Blinker::Right);

      // The wave runs inside BLINKER_SWEEP_MS, one lamp per third of it, and
      // afterwards all three hold at full brightness until the phase ends.
      // That hold is what makes the front lamp readable — see Config.h.
      const float sweep   = (float)min(BLINKER_SWEEP_MS, BLINKER_PERIOD_MS);
      const float segment = sweep / 3.0f;
      for (int k = 0; k < 3; k++) {
        float f = ((float)cycle - (float)k * segment) / segment;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;

        int v = (int)lroundf(f * 100.0f);
        v     = (v / LIGHT_RING_STEP) * LIGHT_RING_STEP;
        const uint8_t bright = (uint8_t)constrain(v, 0, 100);

        if (doLeft) {
          const int seq[3] = {ledRearLeft(), LED_MID_LEFT, ledFrontLeft()};
          want[seq[k]]     = bright;
        }
        if (doRight) {
          const int seq[3] = {ledRearRight(), LED_MID_RIGHT, ledFrontRight()};
          want[seq[k]]     = bright;
        }
      }
    }
  } else {
    const float rx = applyDeadzone(normalizeStick(n.joyRHori), STICK_DEADZONE);
    float       ry = applyDeadzone(normalizeStick(n.joyRVert), STICK_DEADZONE);
#if INVERT_STICK_Y
    ry = -ry;
#endif
    const float up = -ry;  // stick up should mean "front"
    const float r  = sqrtf(rx * rx + up * up);

    if (r >= LIGHT_RING_DEADZONE) {
      active = true;
      ringFromAngle(atan2f(rx, up), r, want);  // 0 = front, +90 degrees = right
    }
  }

  // Headlight flash, laid over whatever is on the ring: it takes the two front
  // lamps and leaves the rest of the picture standing underneath. Where there
  // was no picture, the normal driving lights become the backdrop.
  if (n.btnRS) {
    if (!active) fillDrivingLights(want, braking);
    active = true;

    const bool lit        = ((millis() / HEADLIGHT_FLASH_MS) % 2) != 0;
    want[ledFrontLeft()]  = (uint8_t)(lit ? 100 : 0);
    want[ledFrontRight()] = (uint8_t)(lit ? 100 : 0);
  }

  if (!active) {
    // Restore the previous state.
    //
    // Nobody else touches LED 1 and 4 (the indicators), so we have to clear
    // them ourselves. The driving lights on 2, 3, 5 and 6 are usually
    // repainted by the hub's own light program — but that cannot be relied
    // upon, so we set them explicitly. If the VM writes anyway, our values
    // are overwritten a tenth of a second later and do no harm.
    if (wasActive) {
      hub.setLeds(0x09, 0);  // indicators off

      if (lightMode == HubLights::LIGHTS_ON) {
        hub.setLeds(0x06, 100);                 // 2 + 3 front, full brightness
        hub.setLeds(0x30, braking ? 100 : 30);  // 5 + 6 rear, bright when braking
      } else {
        hub.setLeds(0x36, 0);                   // driving lights off
      }
      forgetLedState();
      wasActive = false;
    }
    return;
  }
  wasActive = true;
  emitLeds(want);
}

// Is the failsafe engaged? Then nothing drives, whatever else is pending.
//
// Deliberately NO timeout on getReceiveNotificationAt(): the controller only
// reports on change (measured on the hardware, see Config.h). Full throttle
// held steady produces no notifications and would be mistaken for a dropout.
// The BLE link alone carries this.
static bool failsafeActive() {
  if (!xbox.isConnected()) return true;
  if (xbox.getReceiveNotificationAt() == 0) return true;  // no data ever received
  return false;
}

static void tick() {
  const bool xboxUp = xbox.isConnected();

  // Carry the edges forward on every tick where the controller is present.
  // They are only emitted in the driving states and only on a live link.
  ButtonEdges edges;
  if (xboxUp) {
#if BUTTON_DEBUG
    debugButtons();
#endif
    const bool emit = !failsafeActive() && (appState == AppState::Disarmed ||
                                            appState == AppState::Armed);
    edges = readButtonEdges(emit);
  }

  // The controller is the precondition for everything. If it goes away, the
  // hub scan is stopped at once: the Xbox library then needs the global scan
  // object for itself.
  if (!xboxUp && appState != AppState::XboxConnect) {
    if (appState == AppState::HubScan) hub.stopScan();
    if (appState == AppState::Armed) disarm("controller lost");

    // During the calibration sweep NO frame may interfere, otherwise the hub
    // aborts the calibration. It simply keeps running here.
    if (hub.isConnected() && appState != AppState::Calibrate) {
      // Controller gone means hold, not coast.
      currentSpeed = 0;
      hub.sendDrive(0, 0, lightMode | HubLights::BRAKE);
    }
    if (!hub.isConnected()) appState = AppState::XboxConnect;
    return;
  }

  switch (appState) {
    case AppState::XboxConnect:
      if (xbox.isConnected()) {
        Serial.println("[+] controller connected");
        appState = hub.isConnected() ? AppState::Disarmed : AppState::HubScan;
      }
      break;

    case AppState::HubScan: {
      if (!bondCandidatesReady) loadBondCandidates();

      if (hub.state() == MoveHub::State::Found) {
        appState = AppState::HubConnect;
        break;
      }

      // Known addresses first, one per tick.
      if (bondCandidateNext < bondCandidateCount) {
        const NimBLEAddress a = bondCandidates[bondCandidateNext++];
        hub.stopScan();
        Serial.printf("[hub] trying known address %s\n", a.toString().c_str());
        if (hub.connectToAddress(a)) {
          appState             = AppState::Calibrate;
          calibrationStartedAt = 0;
        }
        break;
      }

      // Then scan — and after every unsuccessful scan run through the known
      // addresses again, in case the hub woke up in the meantime.
      if (!NimBLEDevice::getScan()->isScanning()) {
        if (hub.state() == MoveHub::State::Scanning) bondCandidateNext = 0;
        hub.startScan(5000);
      }
      break;
    }

    case AppState::HubConnect:
      if (hub.connectFound()) {
        appState             = AppState::Calibrate;
        calibrationStartedAt = 0;
      } else {
        appState = AppState::HubScan;
      }
      break;

    case AppState::Calibrate:
      if (calibrationStartedAt == 0) {
        if (!hub.calibrateSteering()) {
          appState = AppState::HubScan;
          break;
        }
        calibrationStartedAt = millis();
      } else if (millis() - calibrationStartedAt >= CALIBRATION_SETTLE_MS) {
        // Deliberately send NO frame during the end-stop sweep, otherwise the
        // hub aborts the calibration. Its 10 s timeout is far off here.
        Serial.println("[+] steering calibrated");

        appState = AppState::Disarmed;
      }
      break;

    case AppState::Disarmed:
    case AppState::Armed: {
      if (!hub.isConnected()) {
        Serial.println("[!] lost the hub connection");
        currentSpeed = 0;
        appState     = AppState::HubScan;
        break;
      }

      applyButtons(edges);

      auto& n = xbox.xboxNotif;

      const int8_t steering = mapStickToSteering(n.joyLHori, maxSteer);

      // Boost on Y: while the button is held the full workshop limit applies
      // instead of the selected speed step. A held state, not an edge —
      // releasing takes the reserve away immediately.
      const int8_t limited =
          n.btnY ? maxSpeed
                 : (int8_t)((int)maxSpeed * SPEED_STEPS_PERCENT[speedStep] / 100);

      DriveCommand cmd{0, false};
      if (appState == AppState::Armed && !failsafeActive()) {
        cmd = mapTriggers(n.trigRT, n.trigLT, limited);
      }

      // The trigger goes straight through to the hub, unfiltered.
      //
      // A ramp on the setpoint used to sit here. Removed on 2026-08-29 after a
      // test drive, and it must NOT come back: smoothing the setpoint puts a
      // delay between the trigger and the car, and that delay is worse to
      // drive than a hard launch. Smoothing is the VM's job — it has its own.
      currentSpeed = cmd.targetSpeed;

      // Parking brake: while disarmed the vehicle is held rather than left to
      // coast — that covers the emergency stop on B as well as losing the
      // controller. Arming releases it.
      const bool holdBrake = (appState == AppState::Disarmed) || failsafeActive();
      const bool brakeNow  = holdBrake || cmd.brake;

      // Bit 0 is the brake, not a light — see MoveHub.h.
      uint8_t lights = lightMode;
      if (brakeNow) lights |= HubLights::BRAKE;

      // The headlight flash is NOT handled here any more. Through the drive
      // frame it could only blink all six lamps at once — see the light bit in
      // MoveHub.h. It now runs over the LED array in updateRingAndBlinkers().

      hub.sendDrive(brakeNow ? 0 : currentSpeed, steering, lights);

      // After the drive frame, so our own values override the VM's.
      //
      // TWO frames, and that is the crux: the hub leaves LEDs it was not
      // addressed with untouched. The mask only selects WHAT is set — it
      // switches nothing off. For an exclusive display the rest has to be set
      // to brightness 0 explicitly.
      //
      // The LED array shares the radio with the drive frame, and the hub hangs
      // up if it is fed too fast — see emitLeds().
      if (!ledsEnabled) {
        // Master switch off: not one byte goes to the LED array.
      } else if (ledOverride) {
        static uint32_t lastOverrideAt = 0;
        if (millis() - lastOverrideAt >= LED_REFRESH_MS) {
          lastOverrideAt = millis();
          hub.setLeds((uint8_t)(~ledMask & 0x3F), 0);
          hub.setLeds(ledMask, ledBrightness);
          forgetLedState();
        }
      } else {
        updateRingAndBlinkers(n, steering, brakeNow);
      }
      break;
    }
  }
}

static void printStatus() {

  // LED frames since the last status line, scaled to per second. This is the
  // number that matters for link stability — see emitLeds().
  static uint32_t lastLedCount = 0;
  static uint32_t lastLedAt    = 0;
  const uint32_t  now          = millis();
  const uint32_t  span         = (now > lastLedAt) ? (now - lastLedAt) : 1;
  const uint32_t  ledRate      = (ledFramesSent - lastLedCount) * 1000UL / span;
  lastLedCount                 = ledFramesSent;
  lastLedAt                    = now;

  char      battHub[8];
  const int battRaw = hub.batteryPercent();
  if (battRaw >= 0) {
    const unsigned pct = (unsigned)((battRaw > 100) ? 100 : battRaw);
    snprintf(battHub, sizeof(battHub), "%u%%", pct);
  } else {
    snprintf(battHub, sizeof(battHub), "--");
  }

  char drop[12];
  if (haveDropLatency) {
    snprintf(drop, sizeof(drop), "%lums", (unsigned long)lastDropLatencyMs);
  } else {
    snprintf(drop, sizeof(drop), "--");
  }

  // vmax is the cap that actually applies: workshop limit times speed step.
  // Without it "step=100%" reads like full throttle even though m=30 only
  // permits 30.
  const int vmax = (int)maxSpeed * SPEED_STEPS_PERCENT[speedStep] / 100;

  Serial.printf(
      "[%-22s] hub:%-11s v=%4d  vmax=%3d  steer=%4d  w:%lu/%lu  led:%lu/s  slow:%lu(%lums)  "
      "hubBatt:%-5s padBatt:%3u%%  drop:%-7s\n",
      appStateName(), hub.stateName(), currentSpeed, vmax,
      mapStickToSteering(xbox.xboxNotif.joyLHori, maxSteer),
      (unsigned long)hub.writeOk(), (unsigned long)hub.writeFail(), (unsigned long)ledRate,
      (unsigned long)slowTicks, (unsigned long)worstTickMs, battHub, xbox.battery, drop);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=====================================================");
  Serial.println(" brick-car-gamepad-bridge");
  Serial.println(" Xbox Series X|S  ->  LEGO Technic Move Hub 88019");
  Serial.println("=====================================================");
  printHelp();

  loadSettings();

  bootCount++;
  saveSettings();
  Serial.printf("[boot] #%lu, reason: %s\n", (unsigned long)bootCount, resetReasonName());
  if (lastDiscReason != 0) {
    Serial.printf("[boot] last hub disconnect was reason %d, during boot #%lu\n",
                  lastDiscReason, (unsigned long)lastDiscBoot);

    static uint8_t buf[600];
    prefs.begin(PREFS_NAMESPACE, true);
    const size_t   n    = prefs.getBytes("wlog", buf, sizeof(buf));
    const int      batt = prefs.getInt("discBatt", -1);
    const uint32_t up   = prefs.getUInt("discUp", 0);
    prefs.end();
    Serial.printf("[boot] the link had held %lus, hub battery was %d%%\n",
                  (unsigned long)(up / 1000), batt);
    if (n >= hub.writeLogSize()) hub.printExportedWriteLog(buf, n);
  }

  printSettings();

  // Must run first: it initialises NimBLE and sets the global security
  // parameters (bonding, no MITM, legacy pairing).
  xbox.begin();
  hub.begin();

  printBonds();

  Serial.println("[*] Switch the controller on. If it does not report in:");
  Serial.println("    hold the pair button for 3 s until the Xbox logo blinks fast.");

  lastTickAt   = millis();
  lastStatusAt = millis();
}

void loop() {
  // Only scans and connects while the controller is NOT connected. That is
  // exactly why the hub scan can never collide with the Xbox scan.
  xbox.onLoop();

  trackLinkLoss();
  trackCharging();
  tuneXboxLink();
  handleSerial();

  const uint32_t now = millis();

  if (now - lastTickAt >= TICK_INTERVAL_MS) {
    lastTickAt = now;
    tick();

    // A tick is budgeted 50 ms. Anything much longer delays the drive frame,
    // which looks from the outside like a vehicle that stops responding.
    const uint32_t took = millis() - now;
    if (took > TICK_INTERVAL_MS) {
      slowTicks++;
      if (took > worstTickMs) worstTickMs = took;
    }
  }

  if (now - lastStatusAt >= STATUS_INTERVAL_MS) {
    lastStatusAt = now;
    printStatus();
  }
}
