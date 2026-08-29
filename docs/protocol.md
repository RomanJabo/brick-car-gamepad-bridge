# Protocol reference

*[Deutsche Fassung](protokoll.de.md)*

Everything the firmware needs to know about the two radio links. Sources at
the end. Statements marked *measured* were verified on real hardware.

---

## LEGO Technic Move Hub 88019

Built into set 42176 (Porsche GT4 e-Performance), and also 42214 and 42239.
Drive motors and steering servo are integral to the hub.

| | |
|---|---|
| Advertising name | `Technic Move` (prefix, usually followed by a space) |
| Company ID | 919 = `0x0397` (LEGO) |
| Manufacturer data | `97 03 <btn> 84 02 FF 01 00` → **byte 3 = hub type `0x84`** |
| Service UUID | `00001623-1212-EFDE-1623-785FEABCD123` |
| Characteristic | `00001624-1212-EFDE-1623-785FEABCD123` (write + notify) |
| Security | **Pairing mandatory**, security mode 1 level 2 |

### Port inventory of this hub

Reported by the hub itself (Hub Attached I/O) plus a Port Information Request
sweep — serial command `o`:

| Port | Device type | What |
|---|---|---|
| `0x14` | — | answers queries (5 modes, in/out) but reports **nothing attached** |
| `0x32`, `0x33` | 86 | drive motors |
| `0x34` | 87 | steering motor |
| `0x35` | 88 | LED array, 6 elements |
| `0x36` | 89 | VM — the combined drive frame |
| `0x37`–`0x3B` | 60, 57, 58, 59, 93 | temperature, accelerometer, gyro, tilt |
| `0x3C` | 20 | voltage |
| `0x3D`–`0x40` | 92, 94, 23, 95 | among them the RGB status light (23) |

`0x14` is the only port with a motor profile that has nothing attached — a
possible external connector. To check, plug something in and run `o` again: if
the port then shows up as attached, it is usable.

> The sweep blocks the control loop for about 1.6 s (64 ports with a wait in
> between). It is a workbench tool, not something to run while driving — in one
> test the hub link dropped during it and re-established itself.

> **Keep away from `GOPOS` on port `0x34`.** The documented LWP3 way of driving
> the steering motor to an absolute position **crashes the hub**. Steering
> therefore goes exclusively through the combined frame on port `0x36`, where
> the hub runs the servo loop itself. The Control+ app does the same.

### Drive frame (13 bytes)

```
0D 00 81 36 11 51 00 03 00 <speed> <angle> <lights> 00
 │  │  │  │  │  │  │  └──┴─ sub-command "drive"
 │  │  │  │  │  │  └─────── mode 0
 │  │  │  │  │  └────────── WriteDirectModeData (0x51)
 │  │  │  │  └───────────── startup / completion flags
 │  │  │  └──────────────── port 0x36
 │  │  └─────────────────── Port Output Command (0x81)
 │  └────────────────────── hub ID
 └───────────────────────── length
```

- `speed`, `angle`: **int8, −100 … +100 each**, written as `value & 0xFF`
- **The frame expires after 10 s** without a repeat. The firmware therefore
  sends at 20 Hz — which doubles as the lowest safety layer: if the bridge
  crashes, the car stops within ten seconds.

### The last byte — brake and lights, not just lights

> **Correction to the reverse-engineering source.** There this byte is
> described as a pure lights byte with four modes: `0x00` front+rear on ·
> `0x01` front on, rear on braking · `0x04` all off · `0x05` front off, rear on
> braking.
>
> *Measured:* **bit 0 is the brake.** The brake light coming on is the visible
> consequence — and that is what the source actually described.

| Bit | Meaning |
|---|---|
| `0x01` | **brake** |
| `0x04` | lights off (`0` = lights on) |

Two consequences, both observed:

1. Setting bit 0 permanently as a supposed "light mode" means **the vehicle
   will not move** — the brake is engaged the whole time. That is exactly what
   happened in the "modes" `0x01` and `0x05`.
2. **Reverse fails** while bit 0 is set: the brake beats the negative speed
   value. The motor only twitches briefly when the trigger is released.

The firmware therefore knows only two light states (`0x00` and `0x04`) and
sets bit 0 exclusively when braking is actually intended.

**Braking and reversing are mutually exclusive.** The firmware handles that
like an RC speed controller: while the setpoint is positive, LT brakes (bit 0,
speed 0); once it has reached 0, LT becomes reverse (negative speed, bit 0
cleared). See `mapTriggers()` in `Mapping.cpp`.

Bit 1 (`0x02`) has no visible effect in the drive frame — all eight values
`0x00`–`0x07` were swept and observed.

### Steering calibration

Once after connecting, 100 ms apart:

```
0D 00 81 36 11 51 00 03 00 00 00 10 00
0D 00 81 36 11 51 00 03 00 00 00 08 00
```

The same frame with `speed=0`, `angle=0` and flag bits `0x10` resp. `0x08` in
the lights byte. The hub then sweeps the steering against both end stops and
sets the centre, which takes about two seconds.

> **Send no frames during the sweep.** A drive frame arriving in between
> aborts the calibration. The firmware therefore pauses completely for
> `CALIBRATION_SETTLE_MS`; the hub's 10 s timeout is far away in that window.

### The six LEDs are individually addressable

**The six LED outlets are fibre-optic sockets** — two on each side at the
front, two at the rear. In the 42176 light guides run from there to the
headlights and tail lights; two sockets are left unused by that model.

> The LEDs behind the two unused sockets do light up — but with no guide
> plugged in, nothing of it is visible on the model. Using them means running
> your own light guides, i.e. a small build modification.

Mode 0 of port `0x35` is called `6LEDS` by the hub itself and expects
**exactly two datasets**:

```
09 00 81 35 11 51 00 <mask> <brightness>
                      │      └─ 0..100
                      └──────── bit 0 = LED 1 ... bit 5 = LED 6
```

| Wanted | Mask |
|---|---|
| LED 1 only | `0x01` |
| LED 4 only | `0x08` |
| LED 1 + 4 | `0x09` |
| all six | `0x3F` |

Serial command: `E3F,100` for all, `E09,100` for LED 1 and 4, `Ex` ends the
override.

**Measured on the 42176:**

- The bit number matches the labelling on the hub **directly**: bit 0 = LED 1
  … bit 5 = LED 6. All six can be driven individually.
- The **Porsche's own light program uses only 2, 3, 5 and 6.** **LED 1 and 4
  stay free** — you can plug your own light guides in there and switch them
  however you like without disturbing the driving lights.
- The numbering runs "left before right" per pair: front 2 left and 3 right,
  rear 5 left and 6 right. It does **not** run around the circle in order.

> **The mask switches nothing off — it only selects what gets set.** LEDs that
> are not addressed keep their state. `E3F,100` followed by `E01,100`
> therefore leaves all six on rather than reducing to LED 1.
>
> An exclusive display needs **two frames**: first `(~mask & 0x3F)` at
> brightness 0, then the mask at the wanted brightness. That is what
> `firmware.ino` does while the override is active.
>
> The hub likewise **never resets the LEDs on its own**. After the override
> ends the last state stays until the VM repaints its light program.

**The override has to be repeated continuously.** The drive frame goes to the
VM at 20 Hz and the VM paints the LED array according to its own program, so a
one-shot command is overwritten. The firmware therefore sends the LED frames
in step with it (every other tick, to leave radio headroom).

> **If a write is refused with `Generic Error 0x06`, check the payload length
> first.** `0x06` means "invalid use, wrong parameters" and is what the hub
> answers when the number of payload bytes does not match the mode's dataset
> count. Sending six brightness values here — one per LED — looks reasonable
> and fails for exactly that reason. `n35` prints the expected count.

Source for the mask format:
[toorisrael/LEGO-Porsche-Controller](https://github.com/toorisrael/LEGO-Porsche-Controller)
(command `getledmask`), confirmed by our own mode query.

**A value written through the LED array wins against the light program**, for
as long as it keeps being repeated. Confirmed on 2026-08-29 by the headlight
flash: it blinks LEDs 2 and 3 at 200 ms per half-period over this path while
the VM repaints the same lamps at 20 Hz, and the result is clean, with no
flicker. 200 ms sits comfortably above `LED_REFRESH_MS` (150 ms) — whether a
faster blink still holds was not tested.

### Two ways to drive — and why both are needed

The combined drive frame on port `0x36` sets a **speed**. The hub regulates
towards it and caps the power in the process, which is why full torque never
reaches the wheel.

Next to it there is the direct command to a single motor:

```
08 00 81 <port> 00 51 00 <power>
                    │  └─ mode 0 = POWER (1 = SPEED, 2 = POS)
                    └──── WriteDirectModeData
```

| Value | Meaning |
|---|---|
| `-100 … 100` | power, sign = direction |
| `126` | hold position (`END_STATE_HOLD`) |
| `127` | brake actively (`END_STATE_BRAKE`) |

> **Careful:** `127` is not full throttle but the brake. Anyone stretching the
> power range to 127 by mistake will brake at full deflection.

**This firmware no longer uses that path, and the reason is worth recording.**
It was in as a "power mode" on the drive motors `0x32`/`0x33`, with the drive
frame reduced to steering, lights and brake (speed 0). It does spin the motors
faster than the regulated frame, and it does correspond to the power button in
the Control+ app — but the VM still owns those ports and keeps regulating them
towards the speed the drive frame asks for. The two then fight at the frame
rate:

- the motors **stutter audibly**, with brief dropouts;
- on the 25 % speed step the vehicle is **slower** than without it, because the
  direct command is weak enough that the VM wins;
- at 100 % the difference is barely noticeable.

Handing the VM the *real* speed instead of 0 stops the stutter and creates a
worse problem: the VM then regulates properly, with its own gentle ramp, and
the wheels follow that ramp rather than the direct command — the vehicle
responds about a second late.

**If you do use it, you have to supply the rotation direction yourself.** The
drive frame sorts that out internally, the direct command does not. The two
motors face each other and need opposite signs, so exactly *one* must be
inverted — inverting both cancels out. On the 42176 it is A.

Steering must stay in the drive frame in any case: driving the steering motor
directly crashes the hub.

The two motors face each other and therefore need **opposite signs** — so
exactly *one* of them must be inverted. Two inversions cancel out and give the
same result as none. *Which* one you invert then sets the direction of travel.

Measured on the 42176:

| Setting | Result |
|---|---|
| neither inverted | counter-rotating, the vehicle only grinds |
| both inverted | counter-rotating — identical to "neither" |
| only B inverted | co-rotating, but drives the wrong way round |
| **only A inverted** | **correct** |

On another model (42214, 42239) this may differ. None of it matters when
driving through the drive frame, which is why this firmware no longer carries
the setting at all.

The first switch into direct mode once produced a
`Generic Error 0x06 (invalid use)` on command `0x81`, and never again —
probably a hand-over between the VM and direct access. The motors ran normally
afterwards.

### What the hub says about itself

Queried with the serial command `n` (Port Mode Information):

| Port | Mode 0 is called | Datasets |
|---|---|---|
| `0x35` | `6LEDS` | 2 |
| `0x36` | `PLAYVM` | 8 |
| `0x38` | `GRV` | 3 (int16) |
| `0x39` | `ROT` | 3 (int16) |
| `0x3A` | `POS` | 3 (int16) |

### The launch jerk cannot be reached from the setpoint

Measured on 2026-08-29. On a low speed step the car pulls away with a jerk,
and the setpoint turns out not to be the lever:

- **Smoothing the setpoint does not remove the jerk, it trades it for dead
  time.** A ramp on the setpoint (tried at 500 and 2000 ms) does soften the
  launch — but it puts a delay between the trigger and the car. Press, and for
  a moment nothing happens; and while the car is not yet rolling, steering
  changes nothing either, so the whole vehicle feels sluggish. Unusable on the
  road, and worse than the jerk it cures. The ramp was removed for this reason
  and should not be brought back.
- **The jerk does not scale with the size of the setpoint.** With no ramp at
  all, the smallest trigger movement that clears the deadzone already produces
  it. The VM's speed controller answers any deviation from standstill with
  power, however small the request was.

Together these two rule out the whole setpoint path — an expo curve on the
throttle included, because that only changes which setpoint a given trigger
position asks for.

**Port `0x36` has exactly one mode.** `requestModeInfo` asks for modes 0 to 7;
only mode 0 (`PLAYVM`) answers. There is no alternative mode to switch to, so
the two unused payload bytes below are the only remaining lead on this port.

### Open thread: PLAYVM declares eight datasets

`PLAYVM` declares **8 datasets**, but our drive frame only fills six:

```
0D 00 81 36 11 51 00 | 03 00 <speed> <angle> <lights> 00
                       └─ 6 payload bytes, 8 are declared
```

The reference code in the reverse-engineering repository does not use the two
missing parameters either — their meaning is unknown.

**How to probe it:** send a 15-byte frame (`0x0F`) with eight payload bytes and
try the two extra ones. Put the vehicle on blocks first. Careful: in the lights
byte, bits `0x08` and `0x10` trigger the steering calibration, so similar side
effects could hide in the unknown bytes.

That much was done, with the result below. **What is still missing is a run
under load** — on blocks the wheels spin up so quickly that a change in the
acceleration would be hard to see at all.

#### Tried on 2026-08-29 — inconclusive, no warranty

> **These are impressions, not measurements.** The only hard numbers below are
> the frame counters. Read the whole block as *"no result"*, not as *"ruled
> out"* — and see the unexplained observation at the end, which weakens every
> subjective judgement in it.

**The hub accepts the full-length frame.** A 15-byte frame (`0x0F`) with both
extra bytes filled produced no `Generic Error 0x06` and not a single write
failure, over several thousand frames. Sending eight datasets is evidently no
format violation — the hub simply never complained about receiving six either.

**No side effect at standstill.** Byte 7 was swept through all eight single
bits (`01` … `80`) and `FF`, byte 8 through `01`, `08`, `10` and `FF`, and both
together at `FF FF`. Vehicle on blocks and disarmed throughout: no error, no
dropped link, no steering calibration, nothing visible on the LEDs.

**No clear effect on the spin-up, wheels free.** Off the ground at full
throttle, `FF` in the first byte felt at most marginally different from the
unmodified frame. If one of the bytes set an acceleration time, unloaded wheels
ought to be the easiest place to see it — which argues against that reading
without settling it.

**Not tested under load.** The question these two bytes were opened for —
whether they can soften the launch jerk — needs the car on the ground, and that
test was not run.

**One observation from the same session is unexplained.** After the sweeps the
vehicle was reported to respond sluggishly *while the probe was switched off*,
that is on the plain 13-byte frame. In the same window the hub's battery had
fallen from 54 % to 22 %. Whether that was the cause was never established.
Anyone repeating this should start from a full pack and watch the level.

### Accelerometer — works, and kills the hub

> **Do not subscribe to this port on a Technic Move Hub.** Everything below is
> correct and reproducible, which is exactly the problem: the subscription is
> accepted, the values are good, impacts measure cleanly — and after a few
> minutes of driving the hub stops dead. Status LED out, vehicle LEDs still
> lit, link gone with HCI `0x13`, and only a power cycle brings it back.
>
> Established by a controlled test, not by reasoning: the same drive runs
> through with the subscription off and fails with it on, repeatedly. It is
> **not** a matter of load — 299 notifications in 625 seconds, well under one
> per second. Same class of trap as `GOPOS` on the steering motor.
>
> The measurements are kept here because they are hard-won and because anyone
> attacking this again should start from them, not from scratch. Impact
> detection built on this path has been removed from the firmware.

Port `0x38`, mode `GRV`, three int16 values in milli-g. Subscribe with a Port
Input Format Setup:

```
0A 00 41 38 00 <delta, 4 bytes little endian> 01
```

Axes, measured on the 42176 by pushing the car in each direction:

| Push | dx | dy | Conclusion |
|---|---|---|---|
| forward | **+675** | +121 | **x = longitudinal, positive = forward** |
| backward | **−377** | +44 | confirms it |
| to the left | −2 | **−465** | **y = lateral, negative = left** |
| to the right | −108 | **+823** | confirms it |

The other axis stays near zero in each case. `z` carries gravity, about −1020
at rest, so 1 g ≈ 1000 counts.

The detection tracked a slow-moving resting attitude and treated the deviation
from it as the impact magnitude. Measured over a real driving session:

| | milli-g |
|---|---|
| at rest, noise | below 70 |
| normal driving | 255 – 530 |
| outliers while driving | 713, 999 |
| **genuine impacts** | **2361, 2655** |

A threshold belongs in the gap between the two groups; 1200 worked well.

Two traps found along the way, in case anyone rebuilds this on a hub that
tolerates the subscription:

> **Do not freeze the attitude tracking above half the threshold** — the
> obvious way to stop an impact being "learned away". It creates a latch: if
> the vehicle comes to rest differently than before, the deviation stays large
> forever and the next arming immediately fires another crash. Track slowly but
> *always* (time constant ~1.5 s); an impact lasts about 100 ms and barely
> shifts it.

> **Read every sample, not just the newest one.** The hub reports on change,
> not on a clock, so an impact arrives as a burst of several notifications
> inside one 20 Hz tick. Code that only looks at the latest value sees whichever
> one happened to be last — usually the decaying flank. A real impact was
> logged at 827 milli-g that way and stayed under the threshold.

### Other commands used

| Purpose | Bytes |
|---|---|
| set the RGB status light | `08 00 81 3F 11 51 00 <colorId>` |
| subscribe to battery level | `05 00 01 06 02` (hub property 0x06, enable updates) |
| → reply | `06 00 01 06 06 <percent>` |
| hub error message | `05 00 05 <triggering command> <error code>` |

Generic error codes: `01` ACK · `02` MACK · `03` buffer overflow · `04`
timeout · `05` command not recognized · `06` invalid use · `07` overcurrent ·
`08` internal error.

---

## Xbox Series X\|S controller (model 1914)

The ESP32-C3 is **not a compromise here but a requirement**: the controller
needs BLE 5.0. On a classic ESP32 (BLE 4.2) the link aborts with "HCI packet
count mismatch"; on the C3 it does not.

Older models: the Xbox One S (1708) usually works with current firmware; the
Xbox One (1537/1697) has no Bluetooth at all and cannot be used.

Values from `xbox.xboxNotif` (`XboxControllerNotificationParser`):

| Field | Type | Range |
|---|---|---|
| `joyLHori`, `joyLVert`, `joyRHori`, `joyRVert` | `uint16_t` | `0 … 0xFFFF`, centre ≈ 32768 |
| `trigLT`, `trigRT` | `uint16_t` | `0 … 0x3FF` — **10 bits, not 16** |
| `btnA`, `btnB`, `btnX`, `btnY`, `btnLB`, `btnRB`, `btnLS`, `btnRS` | `bool` | |
| `btnStart`, `btnSelect`, `btnShare`, `btnXbox` | `bool` | |
| `btnDirUp`, `btnDirDown`, `btnDirLeft`, `btnDirRight` | `bool` | D-pad |

Constants: `maxJoy = 0xFFFF`, `maxTrig = 0x3FF`, `expectedDataLen = 16`.

### The controller only reports on change

*Measured:* **0 notifications per second** from a resting, connected
controller. It sends no periodic HID reports, only changes.

That makes a timeout on `getReceiveNotificationAt()` **dangerously wrong** as
a failsafe: full throttle against the mechanical stop is a constant value and
generates no notifications — the vehicle would cut out with the trigger held
down.

**Solution:** the failsafe rests on `xbox.isConnected()` alone. The bottom
layer stays the hub's 10 s frame timeout.

### The supervision timeout decides the safety margin

Because the failsafe hangs on `isConnected()`, the time until a powered-off
controller is noticed is **the** safety figure: that is how long the vehicle
keeps driving unattended.

Measured by wiggling the stick and switching the controller off mid-motion, so
the last notification marks the moment of switch-off:

| | Dropout detected after |
|---|---|
| Controller defaults | **2610 ms** and **3180 ms** |
| After `updateConnParams(12, 24, 0, 100)` | **963 ms** |

The firmware requests the shorter timeout once after every connect
(`tuneXboxLink()`). The controller is free to refuse — what actually came out
is shown as `drop:` in the status line.

Going shorter is possible (specification lower bound: 60 ms at a 30 ms
interval) but raises the risk of false trips, since two BLE links share one
antenna here. 1 s is the compromise.

---

## The hub advertises with `00:00:00:00:00:00`

Reproducible on the ESP32-C3 with NimBLE 2.5.1: after the **first successful
pairing** the hub shows up in the scan with the correct name and a plausible
RSSI (−57 dBm), but with an all-zero address and `type=0` (public):

```
[scan] Technic Move   00:00:00:00:00:00  type=0  rssi=-57
```

Other devices in the same scan report valid addresses at the same time, so it
is not a general scan fault. Connecting to it inevitably fails. Before the
first pairing the scan does return a usable address — otherwise the initial
pairing could never have happened.

**Solution:** the address stored in the bond is valid and is used in
preference. `firmware.ino` reads the bonds when connecting, filters out the
controller by its known address and connects directly to the rest — without
scanning at all. The scan remains only the path for the very first pairing.

Side effect: connecting is noticeably faster.

An `x` (delete all bonds) restores the as-delivered state; the scan path then
applies again and pairs afresh.

---

## Two traps when running both links together

### 1. NimBLE has a single global scan object

Two concurrent scans block each other. The decisive find is in the Xbox
library's source: **`onLoop()` only scans while the controller is not
connected** — its entire body sits inside `if (!isConnected())`.

From that follows the firmware's hard rule:

> The hub scan runs only while `xbox.isConnected()` is true.
> If the controller drops out, the hub scan is stopped immediately.

The Xbox library also installs its own callbacks on every scan of its own, so
`MoveHub::startScan()` sets ours again each time.

*Scanning while connected* is unproblematic — that is exactly what happens in
the failsafe, when the controller drops out and the hub link is still up.

### 2. The security parameters are global

`xbox.begin()` sets `NimBLEDevice::setSecurityAuth(true, false, false)` —
bonding, no MITM, **legacy pairing** (`sc = false`). That then applies to the
hub as well. Level 2 is reachable with legacy pairing, so it should fit.

Should hub pairing fail anyway, set `HUB_FORCE_SECURE_CONNECTIONS` in
[Config.h](../firmware/Config.h) to `1`. Secure connections are then enabled
just for the hub handshake and reset afterwards, so the Xbox side stays on
legacy.

Connection intervals: the hub is deliberately slowed to 15–30 ms
(`updateConnParams`) so it does not get in the way of the faster Xbox link on
the single antenna.

---

## Sources

- [LEGO Wireless Protocol 3.0.00](https://lego.github.io/lego-ble-wireless-protocol-docs/) — the official specification
- [DanieleBenedettelli/TechnicMoveHub](https://github.com/DanieleBenedettelli/TechnicMoveHub) — reverse engineering of the 88019, including ESP32 and Xbox examples
- [pybricks discussion #1733](https://github.com/orgs/pybricks/discussions/1733) — hub type `0x84`, port map, the `GOPOS` crash
- [toorisrael/LEGO-Porsche-Controller](https://github.com/toorisrael/LEGO-Porsche-Controller) — the LED mask format
- [Technic Move Hub Controller](https://move-hub.site/guide) — drive modes, calibration behaviour, the 10 s timeout
- [asukiaaa/arduino-XboxSeriesXControllerESP32](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32) — the Xbox BLE client
