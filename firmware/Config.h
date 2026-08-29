// Config.h — every tuning knob in one place.
//
// Values marked [RT] are starting points and can be changed at runtime over
// the serial console (see README), without reflashing. Those that are stored
// in flash override the value here on the next boot.
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------- Control loop
// The hub's drive frame expires after 10 s without a repeat. 20 Hz is fast
// enough to feel direct and slow enough for two BLE links to share one radio.
static const uint32_t TICK_INTERVAL_MS   = 50;   // 20 Hz drive frame
static const uint32_t STATUS_INTERVAL_MS = 500;  // 2 Hz status line

// ---------------------------------------------------------------- Failsafe
//
// IMPORTANT — this holds a finding measured on the actual hardware:
//
// The Xbox controller only reports ON CHANGE, not continuously. A resting,
// connected controller produced 0 notifications per second. A timeout on
// getReceiveNotificationAt() would therefore be DANGEROUSLY WRONG: full
// throttle against the mechanical stop is a constant value and generates no
// notifications, so the vehicle would cut out while the trigger is held.
//
// The only trustworthy signal is the BLE link itself (xbox.isConnected()).
// How quickly that notices a powered-off controller is set by the link's
// supervision timeout — measured with the sketch in smoketest/.
//
// The bottom layer stays the hub: its drive frame expires after 10 s without
// a repeat, should the bridge fail completely.

// ---------------------------------------------------------------- Driving
// The effective cap is DEFAULT_MAX_SPEED * speed step. With 100 and the
// starting step of 50 % that is 50 — half power after switching on, full
// after pressing RB twice. For something tamer, put 30 back here.
static const int8_t DEFAULT_MAX_SPEED = 100;  // [RT] full protocol range
static const int8_t DEFAULT_MAX_STEER = 100;  // [RT] protocol maximum
// There is deliberately NO acceleration ramp here.
//
// One used to sit on the setpoint. It was removed on 2026-08-29 after a test
// drive and must not come back: any smoothing of the setpoint puts a delay
// between the trigger and the car, and that delay is worse to drive than a
// hard launch. Smoothing the drive is the VM's job — it runs its own ramp.

static const int8_t PROTOCOL_LIMIT = 100;  // hard limit of the LWP3 frame

// Speed steps, cycled with LB / RB.
static const uint8_t SPEED_STEP_COUNT = 4;
static const uint8_t SPEED_STEPS_PERCENT[SPEED_STEP_COUNT] = {25, 50, 75, 100};
static const uint8_t DEFAULT_SPEED_STEP = 1;  // = 50 %

// ---------------------------------------------------------------- Input
static const float STICK_DEADZONE   = 0.08f;  // the 1914 does not rest exactly centred
static const float TRIGGER_DEADZONE = 0.03f;
static const float STEER_EXPO       = 0.30f;  // 0 = linear, 1 = softest around centre

// ---------------------------------------------------------------- Vehicle

// ---------------------------------------------------------------- Lights
// The six LEDs sit in a ring around the vehicle, 60 degrees apart:
// LED 1 left, 2 and 3 front, 4 right, 5 and 6 rear.
//
// The numbering runs "left before right" per pair — front 2/3, rear 5/6 — so
// it does NOT run around the circle in order. Measured on the hardware.
//
// The 42176's own light program only uses 2, 3, 5 and 6. LED 1 and 4 are free
// and are used here as turn indicators.
static const uint32_t BLINKER_PERIOD_MS = 550;  // 550 ms on, 550 ms off

// How long the indicator's wave takes to run from the tail to the nose. For
// the rest of the on-phase all three lamps stand still at full brightness.
//
// The hold is the point. Spread over the whole phase, the front lamp reaches
// full brightness in the very moment everything drops again — on the car it
// reads as if the front barely lights at all.
static const uint32_t BLINKER_SWEEP_MS = 250;

// Headlight flash on the right stick click: half-period of the blink.
//
// This goes over the LED array, not the drive frame's light bit, because that
// bit switches all six lamps together (see MoveHub.h) — and a headlight flash
// that blinks the tail lights too is not a headlight flash.
//
// The floor is the tick, not LED_REFRESH_MS: emitLeds() serves a lamp whose
// value CHANGED before one that only wants its periodic repeat, so a toggle
// costs one tick. At 100 ms that is two ticks per state, which is short enough
// to read as a stab rather than a blink.
static const uint32_t HEADLIGHT_FLASH_MS = 100;

// Self-cancelling indicators, as in a real car: the wheel must first be
// turned into the indicated direction (CANCEL_ANGLE), then returning to
// centre (CENTER_ANGLE) switches the indicator off.
static const int8_t BLINKER_CANCEL_ANGLE = 40;
static const int8_t BLINKER_CENTER_ANGLE = 10;

// Stick deflection at which the light ring engages.
static const float LIGHT_RING_DEADZONE = 0.15f;

// Brightness in steps, so neighbouring LEDs often share a value and one frame
// covers several of them. Finer steps mean more radio traffic.
static const uint8_t LIGHT_RING_STEP = 20;

// How often the LEDs the hub's own light program owns (2, 3, 5, 6) are
// reasserted. They have to be repeated or the light program paints over them
// within a tick or two; LEDs 1 and 4 keep their value indefinitely and are
// therefore only written when they actually change.
//
// This is the main lever on radio load: at 150 ms a static light costs about
// seven frames a second instead of twenty per lamp.
static const uint32_t LED_REFRESH_MS = 150;

// Charge gauge on the View key: the hub's battery level as a ring that fills
// from the tail forwards.
//
// How long it stays up after one press, and how soft its edge is. The
// softness is one lamp spacing (the ring runs tail -> side -> nose in steps of
// 1/3), which puts a lamp sitting exactly at the current level at half
// brightness — so "half charged" shows the tail lit, the sides at half and the
// headlights dark.
static const uint32_t GAUGE_SHOW_MS   = 3000;
static const float    GAUGE_SOFTNESS  = 0.33f;

// While charging, the gauge stays up and a light travels through the dark part
// of the ring. CHARGE_SPIN_MS is one pass; CHARGE_HOLD_MS is how long a rise
// in the reading counts as "still charging" — the level climbs a percent at a
// time, so this has to outlast the gap between two rises.
static const uint32_t CHARGE_SPIN_MS = 1800;
static const uint32_t CHARGE_HOLD_MS = 600000;  // 10 minutes

// Focus of the light ring's cross-fade. 1 = plain cosine (the neighbour 60
// degrees away is still at half brightness), higher = tighter. At 4 the
// 60-degree neighbour keeps 6 percent, so full deflection lights one LED only.
static const float LIGHT_RING_SHARPNESS = 4.0f;

// If the right stick's up and down are swapped, set this to 1.
#define INVERT_STICK_Y 0

// ---------------------------------------------------------------- Diagnostics
// Lists every named BLE device seen while scanning, with address, address
// type and RSSI. Set to 1 when the hub is not found: the list answers whether
// it is advertising at all.
#define HUB_SCAN_VERBOSE 0

// Prints which buttons are pressed on every change, to work out the mapping
// of your own controller. Confirmed for this one: START = the three-bar key.
#define BUTTON_DEBUG 0

// ---------------------------------------------------------------- Hub pairing
// The Xbox library globally sets setSecurityAuth(bond=true, mitm=false,
// sc=false), i.e. legacy pairing. The Move Hub asks for security mode 1
// level 2, which legacy pairing normally satisfies. Should pairing with the
// hub fail anyway, set this to 1: secure connections are then enabled just
// for the hub handshake and reset afterwards.
#define HUB_FORCE_SECURE_CONNECTIONS 0

// ---------------------------------------------------------------- Xbox link
//
// Measured with the controller's default parameters: the link only notices a
// powered-off controller after 2610 resp. 3180 ms. Since the failsafe hangs
// on exactly that, the vehicle keeps driving unattended for that long.
//
// A shorter supervision timeout is therefore requested after connecting. The
// controller is free to refuse — what actually came out is shown as "drop:"
// in the status line.
//
// Specification lower bound: timeout > (1 + latency) * maxInterval * 2.
// At 30 ms and latency 0 that is 60 ms, so 1000 ms is comfortably safe.
static const uint16_t XBOX_CONN_MIN_INTERVAL = 12;   // 15 ms
static const uint16_t XBOX_CONN_MAX_INTERVAL = 24;   // 30 ms
static const uint16_t XBOX_CONN_LATENCY      = 0;
static const uint16_t XBOX_CONN_TIMEOUT      = 100;  // 1.0 s

// ---------------------------------------------------------------- Hub link
// Units: interval 1.25 ms | timeout 10 ms.
// Deliberately slower than the Xbox link so the two do not fight over the
// single radio.
static const uint16_t HUB_CONN_MIN_INTERVAL = 12;   // 15 ms
static const uint16_t HUB_CONN_MAX_INTERVAL = 24;   // 30 ms
static const uint16_t HUB_CONN_LATENCY      = 0;
static const uint16_t HUB_CONN_TIMEOUT      = 400;  // 4 s

// Settling time after calibration, while the hub sweeps the steering against
// both end stops. No frame is sent during that window, on purpose.
static const uint32_t CALIBRATION_SETTLE_MS = 2500;

// ------------------------------------------------- Accelerometer: do not use
//
// The hub HAS an accelerometer on port 0x38, and subscribing to it works. It
// also kills the hub: after a few minutes of driving the hub stops dead, its
// status LED goes out while the vehicle LEDs stay lit, the BLE link is gone
// and only a power cycle brings it back.
//
// Established by a controlled test, not by reasoning: the same drive runs
// through with the subscription off and fails with it on. It is not a matter
// of load — the sensor produced 299 notifications in 625 seconds, well under
// one per second.
//
// This is the same class of trap as GOPOS on the steering motor (see
// MoveHub.h): a documented LWP3 path that the Technic Move Hub does not
// survive while its VM is running. Crash detection was built on it and has
// been removed again.

// Crossed light guides.
//
// Which LED on the hub ends up lighting which lamp on the model depends on
// how the fibre-optic guides were routed during the build. On the 42176 the
// front pair is crossed: the hub's front-left LED feeds the car's front-right
// headlight. That only matters for the light ring, where the position in the
// ring has to match what is actually visible.
//
// Both are switchable at runtime ('f' and 'h') and stored in flash, so a
// rebuild never needs a recompile.
#define SWAP_FRONT_LEDS 1
#define SWAP_REAR_LEDS  0
