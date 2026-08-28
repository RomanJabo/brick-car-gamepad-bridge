#include "MoveHub.h"

#include "Config.h"

MoveHub hub;

namespace {

const NimBLEUUID SVC_UUID("00001623-1212-efde-1623-785feabcd123");
const NimBLEUUID CHR_UUID("00001624-1212-efde-1623-785feabcd123");

const char*    HUB_NAME_PREFIX       = "Technic Move";
const uint16_t LEGO_COMPANY_ID       = 0x0397;  // 919
const uint8_t  HUB_TYPE_TECHNIC_MOVE = 0x84;

// The drive frame. Only the three marked bytes change during operation.
//   0D 00 81 36 11 51 00 03 00 <speed> <angle> <lights> 00
const uint8_t DRIVE_FRAME_TEMPLATE[13] = {0x0D, 0x00, 0x81, 0x36, 0x11, 0x51, 0x00,
                                          0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
const size_t IDX_SPEED  = 9;
const size_t IDX_ANGLE  = 10;
const size_t IDX_LIGHTS = 11;

// Calibration: the same frame with speed=0, angle=0 and flag bit 0x10 resp.
// 0x08 in the lights byte. Those bits apply here only, never while driving.
const uint8_t CALIB_FRAME_A[13] = {0x0D, 0x00, 0x81, 0x36, 0x11, 0x51, 0x00,
                                   0x03, 0x00, 0x00, 0x00, 0x10, 0x00};
const uint8_t CALIB_FRAME_B[13] = {0x0D, 0x00, 0x81, 0x36, 0x11, 0x51, 0x00,
                                   0x03, 0x00, 0x00, 0x00, 0x08, 0x00};

// Hub property "Battery Voltage" (0x06), operation "Enable Updates" (0x02).
const uint8_t BATTERY_SUBSCRIBE[5] = {0x05, 0x00, 0x01, 0x06, 0x02};

class HubScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override { hub.onDeviceFound(dev); }
};

class HubClientCallbacks : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* /*c*/, int reason) override {
    hub.noteDisconnectReason(reason);
    // NimBLE adds 0x200 to HCI error codes, so 531 is HCI 0x13 and so on.
    // Decoding the common ones turns a mystery number into an instruction.
    const char* why;
    switch (reason) {
      case 0x213:  // HCI 0x13
        why = "the hub hung up on us";
        break;
      case 0x208:  // HCI 0x08
        why = "supervision timeout - out of range or powered off";
        break;
      case 0x216:  // HCI 0x16
        why = "closed from this side";
        break;
      case 0x23E:  // HCI 0x3E
        why = "connection could not be established";
        break;
      default:
        why = "unknown reason";
        break;
    }
    Serial.printf("[hub] disconnected: %s (%d)\n", why, reason);

    if (reason == 0x213) {
      // Worth spelling out: the hub accepts exactly one connection. Opening
      // the Control+ app on a phone takes it over and throws the bridge out,
      // after which the hub stops advertising and every reconnect fails. That
      // looks like a broken bridge and is not one.
      Serial.println("      The hub accepts only ONE connection. Did the Control+ app,");
      Serial.println("      or another device, just connect to it? Close that, then the");
      Serial.println("      bridge will find the hub again.");
    }
    hub.dumpRecentWrites();
    hub.onDisconnected();
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    Serial.printf("[hub] pairing: encrypted=%d bonded=%d\n", info.isEncrypted() ? 1 : 0,
                  info.isBonded() ? 1 : 0);
  }
};

HubScanCallbacks   scanCallbacks;
HubClientCallbacks clientCallbacks;

}  // namespace

void MoveHub::begin() {
  state_ = State::Idle;
}

const char* MoveHub::stateName() const {
  switch (state_) {
    case State::Idle:       return "idle";
    case State::Scanning:   return "scanning";
    case State::Found:      return "found";
    case State::Connecting: return "connecting";
    case State::Ready:      return "ready";
    case State::Failed:     return "failed";
  }
  return "?";
}

bool MoveHub::startScan(uint32_t durationMs) {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) return false;

  haveAddress_    = false;
  warnedZeroAddr_ = false;
  state_          = State::Scanning;

  // The Xbox library installs its own callbacks whenever it scans, so ours
  // have to be set again before every scan of our own.
  scan->setScanCallbacks(&scanCallbacks, false);
  scan->setActiveScan(true);  // names often only appear in the scan response
  scan->setInterval(100);
  scan->setWindow(50);
  // No duplicate filter: once the hub has been filtered out it never shows up
  // again within the same scan. This matches the smoke test's settings.
  scan->setDuplicateFilter(false);
  Serial.println("[hub] scanning ...");
  return scan->start(durationMs, false, true);
}

void MoveHub::stopScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
}

void MoveHub::onDeviceFound(const NimBLEAdvertisedDevice* dev) {
  if (state_ != State::Scanning || haveAddress_) return;

  const std::string name = dev->getName();

#if HUB_SCAN_VERBOSE
  // Answers whether the hub is advertising at all — and whether it advertises
  // under a different name than expected.
  if (!name.empty()) {
    // Address type: 0=public, 1=random, 2=public identity, 3=random identity.
    // 2 or 3 mean NimBLE resolved a private address via a stored bond.
    Serial.printf("[scan] %-24s %s  type=%u  rssi=%d\n", name.c_str(),
                  dev->getAddress().toString().c_str(),
                  (unsigned)dev->getAddress().getType(), dev->getRSSI());
  }
#endif

  if (name.rfind(HUB_NAME_PREFIX, 0) != 0) return;

  // Cross-check against the manufacturer data: 97 03 <btn> 84 ...
  // The first two bytes are the company ID (little endian), byte 3 the hub type.
  if (dev->haveManufacturerData()) {
    const std::string md = dev->getManufacturerData();
    if (md.size() >= 4) {
      const uint16_t company = (uint8_t)md[0] | ((uint16_t)(uint8_t)md[1] << 8);
      const uint8_t  hubType = (uint8_t)md[3];
      if (company == LEGO_COMPANY_ID && hubType != HUB_TYPE_TECHNIC_MOVE) {
        Serial.printf("[hub] ignoring %s: hub type 0x%02X, expected 0x%02X\n",
                      name.c_str(), hubType, HUB_TYPE_TECHNIC_MOVE);
        return;
      }
    }
  }

  const NimBLEAddress found = dev->getAddress();

  // Once paired, the hub regularly advertises with an all-zero address. No
  // connection can be made to that, so such hits are rejected outright —
  // otherwise the state machine loops between finding and failing. Connecting
  // via the stored bond is what actually works.
  if (found.toString() == "00:00:00:00:00:00") {
    if (!warnedZeroAddr_) {  // otherwise this floods the monitor
      warnedZeroAddr_ = true;
      Serial.println("[hub] visible in scan but address is 00:00:00:00:00:00 - unusable.");
      Serial.println("      Happens after the first pairing. Connecting via the bond instead.");
    }
    return;
  }

  address_     = found;
  haveAddress_ = true;
  state_       = State::Found;
  Serial.printf("[hub] found: %s @ %s\n", name.c_str(), address_.toString().c_str());
}

bool MoveHub::connectFound() {
  if (!haveAddress_) return false;
  return connectTo(address_);
}

bool MoveHub::connectToAddress(const NimBLEAddress& addr) {
  address_     = addr;
  haveAddress_ = true;
  return connectTo(addr);
}

bool MoveHub::connectTo(const NimBLEAddress& addr) {
  state_ = State::Connecting;

  stopScan();

  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&clientCallbacks, false);
    // Kept short: a failed attempt blocks the control loop for exactly this
    // long, and the hub is either advertising or it is not.
    client_->setConnectTimeout(2500);
  }
  client_->setPeerAddress(addr);

#if HUB_FORCE_SECURE_CONNECTIONS
  // Only for pairing with the hub; the Xbox side stays on legacy pairing.
  NimBLEDevice::setSecurityAuth(true, false, true);
#endif

  bool ok = client_->connect(true, false, true);

#if HUB_FORCE_SECURE_CONNECTIONS
  NimBLEDevice::setSecurityAuth(true, false, false);
#endif

  if (!ok) {
    Serial.println("[hub] connection failed");
    state_ = State::Failed;
    return false;
  }

  // Without pairing the characteristic only returns "insufficient
  // authentication", so this must happen before discovering the service.
  if (!client_->secureConnection()) {
    Serial.printf("[hub] pairing failed (error %d)\n", client_->getLastError());
    Serial.println("      Hint: 'x' deletes all bonds. Otherwise set");
    Serial.println("      HUB_FORCE_SECURE_CONNECTIONS to 1 in Config.h.");
    client_->disconnect();
    state_ = State::Failed;
    return false;
  }

  client_->updateConnParams(HUB_CONN_MIN_INTERVAL, HUB_CONN_MAX_INTERVAL,
                            HUB_CONN_LATENCY, HUB_CONN_TIMEOUT);

  NimBLERemoteService* svc = client_->getService(SVC_UUID);
  if (svc == nullptr) {
    Serial.println("[hub] LWP3 service not found");
    client_->disconnect();
    state_ = State::Failed;
    return false;
  }

  characteristic_ = svc->getCharacteristic(CHR_UUID);
  if (characteristic_ == nullptr) {
    Serial.println("[hub] LWP3 characteristic not found");
    client_->disconnect();
    state_ = State::Failed;
    return false;
  }

  if (characteristic_->canNotify()) {
    characteristic_->subscribe(
        true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
          handleNotification(data, len);
        });
  }

  state_ = State::Ready;
  Serial.println("[hub] ready");
  requestBatteryUpdates();
  // The accelerometer is deliberately NOT subscribed here.
  //
  // Subscribing makes the hub push a value on every change, which while
  // driving is a continuous stream of notifications on the same link that
  // carries the drive frames. firmware.ino turns it on only when crash
  // detection is actually switched on, so a vehicle running without crash
  // detection carries no sensor traffic at all.
  return true;
}

bool MoveHub::isConnected() const {
  return state_ == State::Ready && client_ != nullptr && client_->isConnected();
}

void MoveHub::disconnect() {
  if (client_ != nullptr && client_->isConnected()) client_->disconnect();
  onDisconnected();
}

void MoveHub::onDisconnected() {
  characteristic_ = nullptr;
  batteryPercent_ = -1;
  if (state_ == State::Ready || state_ == State::Connecting) state_ = State::Idle;
}

bool MoveHub::writeFrame(const uint8_t* data, size_t len) {
  if (characteristic_ == nullptr || client_ == nullptr || !client_->isConnected()) return false;
  // Without response: at 20 Hz a lost frame does not matter, the next tick
  // corrects it 50 ms later anyway.
  // Record it before sending: if the hub hangs up we want to know what the
  // last thing on the wire was.
  if (len >= 4) {
    WriteRec& r = writeLog_[writeLogNext_];
    r.t         = millis();
    r.port      = data[3];
    r.len       = (uint8_t)len;
    for (int i = 0; i < 4; i++) r.tail[i] = (len >= 4) ? data[len - 4 + i] : 0;
    writeLogNext_ = (uint8_t)((writeLogNext_ + 1) % WRITE_LOG);
    if (writeLogCount_ < WRITE_LOG) writeLogCount_++;
  }

  const bool ok = characteristic_->writeValue(data, len, false);
  if (ok) {
    writeOk_++;
  } else {
    writeFail_++;
  }
  return ok;
}

bool MoveHub::sendDrive(int8_t speed, int8_t angle, uint8_t lightsByte) {
  uint8_t frame[sizeof(DRIVE_FRAME_TEMPLATE)];
  memcpy(frame, DRIVE_FRAME_TEMPLATE, sizeof(frame));
  frame[IDX_SPEED]  = (uint8_t)speed;
  frame[IDX_ANGLE]  = (uint8_t)angle;
  frame[IDX_LIGHTS] = lightsByte;
  return writeFrame(frame, sizeof(frame));
}

bool MoveHub::calibrateSteering() {
  if (!isConnected()) return false;
  Serial.println("[hub] calibrating steering ...");
  if (!writeFrame(CALIB_FRAME_A, sizeof(CALIB_FRAME_A))) return false;
  delay(100);
  if (!writeFrame(CALIB_FRAME_B, sizeof(CALIB_FRAME_B))) return false;
  delay(50);
  return true;
}

void MoveHub::requestPortInventory() {
  if (!isConnected()) {
    Serial.println("[hub] not connected");
    return;
  }
  Serial.println("[ports] querying hub (unused ports simply do not answer) ...");

  // Port Information Request, info type 0x01 (mode info) for every port.
  // Ports that do not exist reply with a generic error, which we deliberately
  // swallow for the duration of the sweep.
  inventoryMode_ = true;
  for (uint16_t port = 0; port <= 0x3F; port++) {
    const uint8_t frame[5] = {0x05, 0x00, 0x21, (uint8_t)port, 0x01};
    writeFrame(frame, sizeof(frame));
    delay(20);  // give the hub time to answer
  }
  delay(300);
  inventoryMode_ = false;
  Serial.println("[ports] query finished");
}

void MoveHub::requestModeInfo(uint8_t port) {
  if (!isConnected()) {
    Serial.println("[hub] not connected");
    return;
  }
  Serial.printf("[modes] querying port 0x%02X ...\n", port);

  inventoryMode_ = true;
  for (uint8_t mode = 0; mode < 8; mode++) {
    // Port Mode Information Request: 06 00 22 <port> <mode> <infoType>
    const uint8_t name[6] = {0x06, 0x00, 0x22, port, mode, 0x00};  // name
    writeFrame(name, sizeof(name));
    delay(30);
    const uint8_t fmt[6] = {0x06, 0x00, 0x22, port, mode, 0x80};  // value format
    writeFrame(fmt, sizeof(fmt));
    delay(30);
  }
  delay(300);
  inventoryMode_ = false;
  Serial.println("[modes] query finished");
}

bool MoveHub::setLeds(uint8_t mask, uint8_t brightness) {
  // 09 00 81 35 11 51 00 <mask> <brightness>
  //                       └─ exactly two payload bytes, see MoveHub.h
  if (brightness > 100) brightness = 100;
  const uint8_t frame[9] = {0x09, 0x00, 0x81, HubPort::LEDS, 0x11,
                            0x51, 0x00, (uint8_t)(mask & 0x3F), brightness};
  return writeFrame(frame, sizeof(frame));
}

namespace {
const char* portName(uint8_t port) {
  switch (port) {
    case 0x36: return "drive frame";
    case 0x35: return "LED array  ";
    case 0x32: return "motor A    ";
    case 0x33: return "motor B    ";
    case 0x38: return "accel setup";
    default:   return "other      ";
  }
}
}  // namespace

size_t MoveHub::exportWriteLog(void* dst, size_t maxBytes) const {
  const size_t need = writeLogSize();
  if (maxBytes < need) return 0;
  uint8_t* p = (uint8_t*)dst;
  p[0]       = writeLogNext_;
  p[1]       = writeLogCount_;
  memcpy(p + 2, writeLog_, sizeof(writeLog_));
  return need;
}

void MoveHub::printExportedWriteLog(const void* src, size_t bytes) const {
  if (bytes < writeLogSize()) return;
  const uint8_t*  p     = (const uint8_t*)src;
  const uint8_t   next  = p[0];
  const uint8_t   count = (p[1] > WRITE_LOG) ? WRITE_LOG : p[1];
  const WriteRec* log   = (const WriteRec*)(p + 2);
  if (count == 0) return;

  Serial.printf("[hub] the last %u frames before the previous drop:\n", (unsigned)count);
  const uint8_t start = (count < WRITE_LOG) ? 0 : next;
  // Relative to the newest entry, so the numbers read as "this long before
  // the end" regardless of when the board was restarted.
  const uint32_t newest = log[(start + count - 1) % WRITE_LOG].t;
  for (uint8_t i = 0; i < count; i++) {
    const WriteRec& r = log[(start + i) % WRITE_LOG];
    Serial.printf("   -%5lums  port 0x%02X %s  len %2u  %02X %02X %02X %02X\n",
                  (unsigned long)(newest - r.t), r.port, portName(r.port), r.len,
                  r.tail[0], r.tail[1], r.tail[2], r.tail[3]);
  }
}

void MoveHub::dumpRecentWrites() {
  Serial.printf("[hub] last %u frames before the drop (oldest first):\n",
                (unsigned)writeLogCount_);
  const uint8_t  start = (writeLogCount_ < WRITE_LOG) ? 0 : writeLogNext_;
  const uint32_t now   = millis();
  for (uint8_t i = 0; i < writeLogCount_; i++) {
    const WriteRec& r = writeLog_[(start + i) % WRITE_LOG];
    Serial.printf("   -%5lums  port 0x%02X %s  len %2u  %02X %02X %02X %02X\n",
                  (unsigned long)(now - r.t), r.port, portName(r.port), r.len, r.tail[0],
                  r.tail[1], r.tail[2], r.tail[3]);
  }
}

bool MoveHub::requestBatteryUpdates() {
  return writeFrame(BATTERY_SUBSCRIBE, sizeof(BATTERY_SUBSCRIBE));
}

void MoveHub::handleNotification(const uint8_t* data, size_t len) {
  if (len < 3) return;
  const uint8_t msgType = data[2];

  // Hub Property Update: 06 00 01 <property> 06 <value>
  if (msgType == 0x01 && len >= 6 && data[3] == 0x06 && data[4] == 0x06) {
    const int prev  = batteryPercent_;
    batteryPercent_ = data[5];

    // Plain information, mentioned once while crossing the threshold. Note
    // that a low battery has NOT been observed to drop the link: the hub kept
    // driving happily for twenty minutes at this level.
    if (batteryPercent_ <= 40 && (prev < 0 || prev > 40)) {
      Serial.printf("[hub] battery down to %d%%.\n", batteryPercent_);
    }
    return;
  }

  // Hub Attached I/O: 0F 00 04 <port> <event> <typeLo> <typeHi> ...
  // The hub sends this on its own for every populated port.
  if (msgType == 0x04 && len >= 5) {
    const uint8_t port  = data[3];
    const uint8_t event = data[4];
    if (event == 0x00) {
      Serial.printf("[ports] 0x%02X detached\n", port);
    } else if (len >= 7) {
      const uint16_t type = (uint16_t)data[5] | ((uint16_t)data[6] << 8);
      Serial.printf("[ports] 0x%02X attached%s, device type %u\n", port,
                    (event == 0x02) ? " (virtual)" : "", type);
    }
    return;
  }

  // Port Information: 0B 00 43 <port> 01 <capabilities> <modeCount> ...
  if (msgType == 0x43 && len >= 7 && data[4] == 0x01) {
    const uint8_t port  = data[3];
    const uint8_t caps  = data[5];
    const uint8_t modes = data[6];
    Serial.printf("[ports] 0x%02X present: %u modes%s%s\n", port, modes,
                  (caps & 0x02) ? ", input" : "", (caps & 0x01) ? ", output" : "");
    return;
  }

  // Port Mode Information: <len> 00 44 <port> <mode> <infoType> <payload...>
  if (msgType == 0x44 && len >= 6) {
    const uint8_t port     = data[3];
    const uint8_t mode     = data[4];
    const uint8_t infoType = data[5];

    if (infoType == 0x00) {  // name, zero-padded
      char         name[12] = {0};
      const size_t n        = (len - 6 < 11) ? (len - 6) : 11;
      memcpy(name, data + 6, n);
      for (size_t i = 0; i < sizeof(name); i++) {
        if (name[i] != '\0' && (name[i] < 32 || name[i] > 126)) name[i] = '\0';
      }
      Serial.printf("[modes] 0x%02X mode %u is called \"%s\"\n", port, mode, name);
    } else if (infoType == 0x80 && len >= 10) {  // value format
      Serial.printf("[modes] 0x%02X mode %u: %u datasets, type %u\n", port, mode, data[6],
                    data[7]);
    }
    return;
  }

  // Generic Error: 05 00 05 <triggering command> <error code>
  if (msgType == 0x05 && len >= 5) {
    if (inventoryMode_) return;  // unused ports answer like this, that is normal
    const char* reason;
    switch (data[4]) {
      case 0x01: reason = "ACK"; break;
      case 0x02: reason = "MACK"; break;
      case 0x03: reason = "buffer overflow"; break;
      case 0x04: reason = "timeout"; break;
      case 0x05: reason = "command not recognized"; break;
      case 0x06: reason = "invalid use (port busy or wrong parameters)"; break;
      case 0x07: reason = "overcurrent"; break;
      case 0x08: reason = "internal error"; break;
      default:   reason = "unknown code"; break;
    }
    Serial.printf("[hub] error on command 0x%02X: %s (0x%02X)\n", data[3], reason,
                  data[4]);
  }
}
