// smoketest.ino — step 1: can the ESP32-C3 hold two BLE links at once?
//
// Deliberately standalone and minimal: no control logic, no calibration, no
// movement. Only neutral frames (throttle 0, steering 0) are sent at 20 Hz,
// so the write path is exercised under load as well.
//
// Expected result: 10 minutes of continuous operation without a dropout and
// without gaps in the status line. Only once that holds is the actual
// firmware worth building.

#include <NimBLEDevice.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>

XboxSeriesXControllerESP32_asukiaaa::Core xbox;

static const NimBLEUUID SVC_UUID("00001623-1212-efde-1623-785feabcd123");
static const NimBLEUUID CHR_UUID("00001624-1212-efde-1623-785feabcd123");
static const char*      HUB_NAME_PREFIX = "Technic Move";

// Neutral: throttle 0, steering 0 — and bit 0 of the last byte set, which is
// the BRAKE (not the lights, see firmware/MoveHub.h). That keeps the vehicle
// safely stationary during the smoke test even if something goes wrong.
static const uint8_t NEUTRAL_FRAME[13] = {0x0D, 0x00, 0x81, 0x36, 0x11, 0x51, 0x00,
                                          0x03, 0x00, 0x00, 0x00, 0x01, 0x00};

static NimBLEClient*               hubClient = nullptr;
static NimBLERemoteCharacteristic* hubChar   = nullptr;
static NimBLEAddress               hubAddress{};
static bool                        hubFound  = false;

static uint32_t framesSent   = 0;
static uint32_t framesFailed = 0;
static uint32_t lastFrameAt  = 0;
static uint32_t lastStatusAt = 0;

// Measurement 1: the controller's notification rate.
//
// Result on the hardware: 0 notifications per second from a resting
// controller. It reports ON CHANGE ONLY. A timeout on those notifications is
// therefore NOT usable as a failsafe — full throttle held steady produces no
// notifications at all.
static unsigned long lastNotifSeen = 0;
static uint32_t      notifCount    = 0;
static uint32_t      notifPerSec   = 0;
static uint32_t      maxGapMs      = 0;

// Measurement 2: how quickly does the link notice a powered-off controller?
//
// After measurement 1 this is the decisive safety figure: it is how long the
// vehicle keeps driving unattended in the worst case. It is set by the BLE
// link's supervision timeout.
//
// Procedure: keep moving the stick (so notifications flow), then switch the
// controller off. The last notification then marks the moment of switch-off.
// The result stays in the status line so it can still be read later.
static bool     xboxWasConnected  = false;
static uint32_t lastDropLatencyMs = 0;
static bool     haveDropLatency   = false;

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (hubFound) return;
    const std::string name = dev->getName();
    if (name.rfind(HUB_NAME_PREFIX, 0) != 0) return;
    hubAddress = dev->getAddress();
    hubFound   = true;
    Serial.printf("[hub] found: %s @ %s\n", name.c_str(), hubAddress.toString().c_str());
  }
};

class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int reason) override {
    Serial.printf("[hub] disconnected (reason %d)\n", reason);
    hubChar  = nullptr;
    hubFound = false;
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    Serial.printf("[hub] pairing: encrypted=%d bonded=%d\n", info.isEncrypted() ? 1 : 0,
                  info.isBonded() ? 1 : 0);
  }
};

static ScanCB   scanCB;
static ClientCB clientCB;

static bool connectHub() {
  if (hubClient == nullptr) {
    hubClient = NimBLEDevice::createClient();
    hubClient->setClientCallbacks(&clientCB, false);
    hubClient->setConnectTimeout(8000);
  }
  hubClient->setPeerAddress(hubAddress);

  if (!hubClient->connect(true, false, true)) {
    Serial.println("[hub] connection failed");
    return false;
  }
  if (!hubClient->secureConnection()) {
    Serial.printf("[hub] pairing failed (error %d)\n", hubClient->getLastError());
    hubClient->disconnect();
    return false;
  }
  hubClient->updateConnParams(12, 24, 0, 400);

  NimBLERemoteService* svc = hubClient->getService(SVC_UUID);
  if (svc == nullptr) {
    Serial.println("[hub] service missing");
    hubClient->disconnect();
    return false;
  }
  hubChar = svc->getCharacteristic(CHR_UUID);
  if (hubChar == nullptr) {
    Serial.println("[hub] characteristic missing");
    hubClient->disconnect();
    return false;
  }
  Serial.println("[hub] ready - sending neutral frames from now on");
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Smoke test: two simultaneous BLE links ===");
  Serial.println("Nothing moves. Throttle and steering stay at zero.");
  Serial.println();

  xbox.begin();  // initialises NimBLE and sets the security parameters
  lastStatusAt = millis();
}

void loop() {
  xbox.onLoop();  // only scans while the controller is not connected

  const uint32_t now = millis();

  // Record the notification rate. Leave the controller untouched and let it
  // run: that shows whether it reports while idle as well.
  const unsigned long notifAt = xbox.getReceiveNotificationAt();
  if (notifAt != 0 && notifAt != lastNotifSeen) {
    if (lastNotifSeen != 0) {
      const uint32_t gap = (uint32_t)(notifAt - lastNotifSeen);
      if (gap > maxGapMs) maxGapMs = gap;
    }
    lastNotifSeen = notifAt;
    notifCount++;
  }

  // Measure how a dropout is detected.
  const bool xboxUp = xbox.isConnected();
  if (xboxWasConnected && !xboxUp) {
    if (lastNotifSeen != 0) {
      lastDropLatencyMs = (uint32_t)(now - lastNotifSeen);
      haveDropLatency   = true;
      Serial.printf(
          ">>> MEASUREMENT: dropout detected %lu ms after the last notification.\n"
          ">>> That is how long the vehicle keeps driving in the worst case.\n",
          (unsigned long)lastDropLatencyMs);
    } else {
      Serial.println(">>> MEASUREMENT: dropout detected (no reference notification).");
    }
    lastNotifSeen = 0;
    maxGapMs      = 0;
  }
  if (!xboxWasConnected && xboxUp) Serial.println(">>> Xbox connected.");
  xboxWasConnected = xboxUp;

  // The hub is only searched for once the controller is up. NimBLE has a
  // single global scan object - two concurrent scans would block each other.
  if (xbox.isConnected() && hubChar == nullptr) {
    if (hubFound) {
      NimBLEDevice::getScan()->stop();
      if (!connectHub()) hubFound = false;
    } else if (!NimBLEDevice::getScan()->isScanning()) {
      NimBLEScan* scan = NimBLEDevice::getScan();
      scan->setScanCallbacks(&scanCB, false);
      scan->setActiveScan(true);
      scan->setInterval(100);
      scan->setWindow(50);
      scan->start(5000, false, true);
    }
  }

  if (hubChar != nullptr && now - lastFrameAt >= 50) {
    lastFrameAt = now;
    if (hubChar->writeValue(NEUTRAL_FRAME, sizeof(NEUTRAL_FRAME), false)) {
      framesSent++;
    } else {
      framesFailed++;
    }
  }

  if (now - lastStatusAt >= 1000) {
    lastStatusAt = now;
    notifPerSec  = notifCount;
    notifCount   = 0;

    char drop[12];
    if (haveDropLatency) {
      snprintf(drop, sizeof(drop), "%lums", (unsigned long)lastDropLatencyMs);
    } else {
      snprintf(drop, sizeof(drop), "--");
    }

    Serial.printf(
        "t=%6lus  xbox:%s hub:%s  frames ok=%lu failed=%lu  notif=%3lu/s "
        "drop detected after=%-7s RT=%4u LX=%5u\n",
        (unsigned long)(now / 1000), xbox.isConnected() ? "OK" : "--",
        (hubChar != nullptr) ? "OK" : "--", (unsigned long)framesSent,
        (unsigned long)framesFailed, (unsigned long)notifPerSec, drop,
        xbox.xboxNotif.trigRT, xbox.xboxNotif.joyLHori);
  }
}
