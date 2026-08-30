// MoveHub.h — LWP3 client for the LEGO Technic Move Hub 88019 (set 42176).
//
// Deliberately not using the Legoino library: it depends on NimBLE 1.x, which
// would break the Xbox side (NimBLE 2.x). The protocol we need fits in here.
//
// Byte-level details are in docs/protocol.md.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

// The last byte of the drive frame.
//
// CAREFUL — the reverse-engineering source presents this byte as four
// selectable "light modes" (0x00/0x01/0x04/0x05), which reads like a menu you
// can pick from. Bit 0 is the BRAKE; the tail light coming on is the visible
// consequence, not the function. Their own code uses it that way too, setting
// it only on the brake key — the trap is in the table, not in their code.
//
// Treating bit 0 as a permanent "light mode" stops the vehicle from moving at
// all, because the brake is then always engaged. It also makes negative speed
// values useless, since the brake wins — exactly why reverse appeared broken.
namespace HubLights {
static const uint8_t LIGHTS_ON  = 0x00;  // bit 2 = 0
static const uint8_t LIGHTS_OFF = 0x04;  // bit 2 = 1
static const uint8_t BRAKE      = 0x01;  // bit 0 — brake, not a light
}  // namespace HubLights

// The hub's internal ports.
namespace HubPort {
static const uint8_t STEER   = 0x34;  // NEVER drive via GOPOS, it crashes the hub
static const uint8_t LEDS    = 0x35;  // LED array, 6 elements per the hub itself
static const uint8_t RGB_LED = 0x3F;
}  // namespace HubPort

class MoveHub {
 public:
  enum class State : uint8_t {
    Idle,        // nothing to do
    Scanning,    // scan running
    Found,       // hub found, address remembered
    Connecting,  // connecting and pairing
    Ready,       // connected, characteristic available
    Failed       // last attempt failed
  };

  void begin();

  // Start a scan. May ONLY be called while the Xbox library is not scanning —
  // NimBLE has a single global scan object.
  bool startScan(uint32_t durationMs);
  void stopScan();

  // Blocking, may take several seconds. Only call in State::Found.
  bool connectFound();

  // Connect directly to a known address, without scanning.
  //
  // Going through the scan is unreliable: the hub advertises itself with the
  // address 00:00:00:00:00:00 once it has been paired. The address stored in
  // a bond is valid — and connecting that way is faster too.
  bool connectToAddress(const NimBLEAddress& addr);

  bool isConnected() const;
  void disconnect();

  // The 13-byte drive frame. speed and angle are -100..100 each.
  bool sendDrive(int8_t speed, int8_t angle, uint8_t lightsByte);

  // Asks the hub which ports it has and what is attached. Answers whether a
  // given output exists at all, instead of guessing.
  void requestPortInventory();

  // Asks for a port's mode names and data formats. The hub names its own
  // functions — more reliable than any third-party source.
  void requestModeInfo(uint8_t port);

  // Drive the six LEDs individually.
  //
  // Mode 0 of port 0x35 is called "6LEDS" and expects EXACTLY TWO datasets:
  // a bitmask (bit 0 = LED 1 ... bit 5 = LED 6) and a brightness of 0..100.
  // A frame with any other payload length is rejected with error 0x06.
  bool setLeds(uint8_t mask, uint8_t brightness);

  // Two frames 100 ms apart; the hub then sweeps the steering against both
  // end stops and sets the centre. Blocks for roughly 150 ms.
  bool calibrateSteering();

  // Write statistics. A BLE link can only carry so many packets per
  // connection interval; if more are pushed at it the queue backs up and
  // writes start failing. Without counting them that stays invisible.
  uint32_t writeOk() const { return writeOk_; }
  uint32_t writeFail() const { return writeFail_; }

  State       state() const { return state_; }
  const char* stateName() const;
  int         batteryPercent() const { return batteryPercent_; }

  // Used by the NimBLE callbacks.
  void onDeviceFound(const NimBLEAdvertisedDevice* dev);
  void onDisconnected();

  // Reason code of the last disconnect, so it can be recorded and read back
  // later. 0 means there has not been one.
  int  lastDisconnectReason() const { return lastDiscReason_; }
  void noteDisconnectReason(int r) { lastDiscReason_ = r; }

  // Dumps the last few frames written before now. Called when the link drops,
  // so the question "what was the hub chewing on when it hung up" gets an
  // answer instead of a guess.
  void dumpRecentWrites();

  // The write log has to survive a restart, otherwise it is only readable in
  // the very moment of the failure — which is never the moment somebody is
  // watching. These two let firmware.ino park it in flash and print it back.
  size_t exportWriteLog(void* dst, size_t maxBytes) const;
  void   printExportedWriteLog(const void* src, size_t bytes) const;
  size_t writeLogSize() const { return sizeof(writeLog_) + 2; }

 private:
  bool connectTo(const NimBLEAddress& addr);
  bool writeFrame(const uint8_t* data, size_t len);
  void handleNotification(const uint8_t* data, size_t len);
  bool requestBatteryUpdates();

  NimBLEClient*               client_         = nullptr;
  NimBLERemoteCharacteristic* characteristic_ = nullptr;
  NimBLEAddress               address_{};
  bool                        haveAddress_    = false;
  State                       state_          = State::Idle;
  int                         batteryPercent_ = -1;
  bool                        warnedZeroAddr_ = false;
  bool                        inventoryMode_  = false;  // suppresses error spam
  uint32_t                    writeOk_        = 0;
  uint32_t                    writeFail_      = 0;
  int                         lastDiscReason_ = 0;

  // Small ring of recent writes, kept purely for post-mortem.
  struct WriteRec {
    uint32_t t;
    uint8_t  port;
    uint8_t  len;
    uint8_t  tail[4];  // LAST four bytes: for a drive frame that is speed,
                       // angle, lights, 00 - the values that actually vary.
                       // The header bytes are identical in every frame and
                       // told us nothing when we first looked at a drop.
  };
  static const uint8_t WRITE_LOG = 16;
  WriteRec             writeLog_[WRITE_LOG]{};
  uint8_t              writeLogNext_ = 0;
  uint8_t              writeLogCount_ = 0;
};

extern MoveHub hub;
