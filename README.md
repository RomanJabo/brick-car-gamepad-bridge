# brick-car-gamepad-bridge

Drive a LEGO® Technic™ Move Hub 88019 with an Xbox Series X|S controller,
using an ESP32-C3 as a Bluetooth bridge.

*[Deutsche Version](README.de.md)*

---

The Move Hub that ships in set 42176 (Porsche GT4 e-Performance) can only be
driven from the LEGO® Control+ phone app. That app is fine for a demo and
poor for driving: no feel for the throttle, no proportional steering, touch
buttons instead of triggers.

This firmware turns an ESP32-C3 into a **BLE bridge**. It holds two Bluetooth
LE links as a client at the same time — one to the Xbox controller, one to the
hub — and translates gamepad input into the LEGO Wireless Protocol.

**Nothing on the vehicle is modified.** Battery, motors and hub stay untouched,
and the Control+ app still works afterwards. The ESP32-C3 does not have to go
into the car either: both legs are radio, so the bridge is best placed
somewhere between driver and vehicle.

### Why this is here

I built this for my own car, and my kids drive it with it. It works on my
setup, every day.

I am publishing it because getting there took a fair amount of poking at an
undocumented protocol, and nobody should have to repeat that. If you own a
model with this hub and would rather drive it with a real controller than with
a touchscreen, everything you need is in this repository — the firmware, and
the protocol notes that took the longest to work out.

That is the point of the whole thing. Take what is useful, ignore the rest.

## What you need

| | |
|---|---|
| An ESP32-**C3** | Not an arbitrary choice: the Xbox controller needs BLE 5.0. On a classic ESP32 the link drops with HCI packet-count errors. |
| Xbox Series X\|S controller | Model 1914, with share button and USB-C. An Xbox One S (1708) usually works too; the older Xbox One (1537/1697) has no Bluetooth at all. |
| LEGO set 42176 | Or any other model using the Technic Move Hub 88019 (42214, 42239) — see [Adapting to another model](#adapting-to-another-model). |
| A USB cable | For flashing, and as a power supply. |

Toolchain, once:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "XboxSeriesXControllerESP32_asukiaaa@1.1.2"
```

> **Versions have to line up:** library ≥ 1.1.0 requires NimBLE ≥ 2.1.0.
> After installing, check with `arduino-cli lib list` that NimBLE was not
> pulled back to 1.4.x. For the same reason the popular `Legoino` library is
> **not** used here — it pins NimBLE 1.x. The hub client in this repository is
> written directly against NimBLE 2.x instead.

## Getting started

```bash
# The board options matter. Without CDCOnBoot=cdc the C3 stays completely
# silent on the serial port — it works, you just cannot see anything.
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app"
PORT=COM6           # /dev/ttyACM0 or similar on Linux and macOS

# 1. Smoke test: can the C3 hold both links at once? Nothing moves.
arduino-cli compile --fqbn "$FQBN" smoketest
arduino-cli upload -p $PORT --fqbn "$FQBN" smoketest

# 2. The actual firmware
arduino-cli compile --fqbn "$FQBN" firmware
arduino-cli upload -p $PORT --fqbn "$FQBN" firmware

# Serial monitor, 115200 baud
arduino-cli monitor -p $PORT -c baudrate=115200
```

If your board is not an "ESP32C3 Dev Module" the FQBN differs;
`arduino-cli board listall` shows the alternatives.

### Staged bring-up

Each stage has to hold before the next one starts. **Keep the vehicle up on
blocks, wheels free, until stage 5 is done.**

1. **Smoke test.** Flash `smoketest`, switch the controller on (hold the pair
   button for 3 s until the Xbox logo blinks fast), switch the car on. Expect
   `xbox:OK hub:OK`, `failed=0`, and — the important one — `drop detected
   after` well under a second once you test it. Nothing moves during this.
2. **Flash the firmware.** The hub calibrates its steering on connect: it
   sweeps visibly against both end stops and centres.
3. **Steering, still disarmed.** The steering already follows the stick while
   the drive stays locked. If it hits the mechanical stop, limit it with `s80`.
4. **Drive, on blocks.** `m20`, then **START**. The bridge only arms with both
   triggers released. Check throttle, brake and reverse.
5. **Failsafe, dry.** With the wheels turning, press **B** — they must stop at
   once. Then switch the **controller off** while moving the stick: the
   `drop:` field in the status line shows how long detection took.
6. **First drive.** Put the car down, `m30`, outdoors. Raise the limit step by
   step.

## Controls

| Input | Effect |
|---|---|
| **RT** | Throttle |
| **LT** | Brake, then reverse — like an RC speed controller |
| **Left stick** | Steering, proportional with an expo curve |
| **START** (☰) | Arm (only with the triggers released) |
| **B** / **Xbox** | Emergency stop — engages the **parking brake** |
| **A** | Lights on / off |
| **X** | Hazard lights |
| **Y** (hold) | Boost — full workshop limit instead of the speed step |
| **LB / RB** | Speed step 25 / 50 / 75 / 100 % |
| **Right stick move** | Light ring — the light travels around the car |
| **Right stick click** | Headlight flash |
| **D-pad left/right** | Indicators, self-cancelling |
| **VIEW** (⧉) | Hub charge as a level around the light ring |

After power-up the bridge is **always disarmed**. The vehicle never drives off
by itself.

Serial commands for tuning without reflashing are listed with `?` in the
monitor; `m` and `s` are stored in flash and survive a firmware update.

## Features worth knowing about

**Parking brake.** Disarmed means *held*, not coasting — for the emergency
stop on B and for losing the controller alike. That makes a radio dropout much
gentler: the car brakes instead of rolling on.

**Let the hub do the driving.** The combined drive frame asks the hub for a
*speed* and its VM regulates towards it, ramping up gently on the way. Four
features that duplicated that work have been built and removed again:

- **Direct motor commands** ("power mode", what the Control+ app's power
  button does). The VM owns the same ports and keeps regulating them, so the
  two fight: the motors stutter audibly, and on the 25 % step the car is
  actually *slower* than without. At 100 % the difference is barely there.
- **Motor direction inversion.** Only the direct commands needed it — the
  drive frame sorts the rotation out internally.
- **An acceleration ramp of our own**, on top of the VM's. Two ramps in series
  gave a visibly two-stage launch and lag on release.
- **A ramp on the *setpoint*** — the last one to go, and the most tempting to
  rebuild. It kept the VM's speed controller from seeing a large error at the
  launch, and the low steps did pull away more gently for it. But it also put
  a delay between the trigger and the car, and on the road that delay is
  worse to drive than a hard launch.

Nothing smooths the setpoint any more. The trigger value goes into the drive
frame as it stands, and the only ramp left in the system is the hub's own.

**Charge gauge (VIEW).** The hub's battery level as a dial running once around
the light ring, starting at the left flank and growing anti-clockwise — so
half charge lights the half of the ring from the left flank round the tail to
the right, and a quarter lights the left quarter of the car. While charging it
stays up and a light travels through the dark part of the ring.

Charging is *inferred*, not reported: LWP3 has no charging flag, but a level
that goes up can only mean one thing. That needs a whole percentage point, so
the state is kept in flash — otherwise every restart would blank the display
for minutes while the car sits on the cable.

**Light ring.** The hub has six fibre-optic light outlets arranged in a ring.
The right stick picks a direction on that ring and the light travels with it,
cross-faded so it glides rather than jumping. The 42176's own light program
uses only four of the six, which leaves two free for the indicators.

> **The indicators need a bit of building.** Outlets 1 and 4 do light up, but
> the 42176 routes no light guides to them — so nothing is visible on the
> model yet. To actually see the indicators you have to run flexible light
> guides from those two outlets to wherever you want the lamps, which means a
> small model modification. The firmware side is done; the brick side is not.

**Crash detection was built and then removed — do not rebuild it.** The hub
has an accelerometer on port `0x38`, subscribing to it works, and impacts
measure cleanly (normal driving peaks at 255–530 milli-g, real impacts at 2361
and 2655). It also kills the hub. See finding 5 below.

## What this project found out about the protocol

LEGO publishes the [LEGO Wireless Protocol](https://lego.github.io/lego-ble-wireless-protocol-docs/)
itself, and the community has reverse-engineered the Move Hub on top of it.
Building this bridge turned up **five things the existing documentation gets
wrong or does not cover**. All of them were measured on real hardware:

**1. Bit 0 of the drive frame's last byte is the BRAKE, not a light mode.**
The widely cited table describes four "light modes" (`0x00/0x01/0x04/0x05`).
In reality bit 0 engages the brake; the brake light coming on is the visible
consequence. Treating it as a light mode means the vehicle will not move at
all, and reverse silently fails because the brake beats a negative speed.

**2. After pairing, the hub advertises with the address `00:00:00:00:00:00`.**
Name and RSSI are correct, other devices in the same scan report valid
addresses — but connecting is impossible. The address stored in the BLE bond
is valid, so that is what this firmware connects to. The scan is only used for
the very first pairing.

**3. The Xbox controller only reports on change.** A resting, connected
controller produced **0 notifications per second**. Any failsafe built on a
notification timeout is therefore dangerously wrong: full throttle held
against the stop is a constant value and generates nothing, so the car would
cut out mid-drive. The BLE link itself is the only trustworthy signal.

**4. The six LEDs *are* individually addressable** — via port `0x35`, mode
`6LEDS`, with exactly two payload bytes: a bitmask and a brightness. Two
traps: the mask only selects *what is set* and switches nothing off, and the
hub never resets the LEDs on its own.

**5. Subscribing to the accelerometer kills the hub.** Port `0x38`, mode
`GRV`, is documented, the subscription is accepted and the values are good.
But after a few minutes of driving the hub stops dead: status LED out, vehicle
LEDs still lit, BLE link gone with HCI `0x13` ("remote user terminated"), and
only a power cycle brings it back. From the outside it looks exactly like a
radio problem, which is what makes it expensive to chase.

Established by a controlled test, not by reasoning — the same drive runs
through with the subscription off and fails with it on. It is **not** a matter
of load: the sensor produced 299 notifications in 625 seconds, well under one
per second. This is the same class of trap as `GOPOS` on the steering motor,
which is already known to crash the hub: a documented LWP3 path the Technic
Move Hub does not survive while its VM is running.

The full byte-level reference, including the port inventory and the
measurements, is in **[docs/protocol.md](docs/protocol.md)**.

### One number worth quoting

Because the failsafe rests on the BLE link, the supervision timeout decides
how long the vehicle keeps driving unattended after the controller dies:

| | Dropout detected after |
|---|---|
| Controller defaults | **2610 ms** and **3180 ms** |
| After `updateConnParams(12, 24, 0, 100)` | **963 ms** |

## Adapting to another model

The Move Hub also ships in 42214 and 42239. Driving needs nothing model-
specific: the combined drive frame handles the motors, including which way
round they are mounted, so there is nothing to configure.

What may need adjusting is the **light guides**. Which outlet feeds which lamp
depends on how the model was built — on the 42176 the front pair is crossed.
`f` and `h` in the serial monitor swap the front and rear pairs, and the
setting lives in flash, no recompile needed.

The workshop limits `m` (speed) and `s` (steering angle) are worth a look too:
±100 is the *protocol* range, not necessarily what a given model likes
mechanically.

## If something goes wrong

**The monitor stays silent.** Either the FQBN does not match your board or
`CDCOnBoot=cdc` is missing from it — without that option the C3 says nothing
at all. `arduino-cli board listall` lists the alternatives.

**The car drives for a while, then goes dead and cannot be found again.**
The monitor names it: `disconnected: the hub hung up on us (531)` — HCI
`0x13`, the hub terminating the link itself. Two causes, and they look
identical from the driver's seat:

*Something else grabbed the hub.* It accepts **exactly one connection** —
opening the Control+ app on a phone takes it over and throws the bridge out.
Close the app and the bridge picks the hub up by itself.

*The hub crashed.* Status LED dark while the vehicle LEDs stay lit, and it
stays unreachable until you power-cycle it. That is a hub that has stopped,
not a hub that is busy. The known trigger is subscribing to a sensor port —
see finding 5 — so if you added one, that is where to look first.

Chasing this cost an evening and four wrong theories: radio saturation, a flat
battery, the LED traffic and the steering angle were all argued convincingly
and all ruled out by the counters. What finally settled it was one controlled
test — the same drive with the sensor subscription off and on. The lesson is
in the firmware as a persistent frame log (`w` in the monitor): it records the
last frames before a drop into flash, because the moment of failure is never
the moment anybody is watching the console.

**The hub is not found.** Usually it is simply asleep — without a connection
it powers down after a few minutes, which happens after every flash. Press the
button on the hub. Also close the Control+ app, for the reason above. For
diagnosis set `HUB_SCAN_VERBOSE` to 1 in
[firmware/Config.h](firmware/Config.h): it then lists every BLE device it can
see, which answers whether the hub is advertising at all.

**`address 00:00:00:00:00:00 - unusable`.** Not an error but a quirk of the
hub, see finding 2 above. The firmware connects via the stored bond instead;
`b` lists what is stored, `x` clears it and starts a fresh pairing.

**Pairing with the hub fails.** Try `x` first — a stale bond with the wrong key
makes pairing fail without saying much. If that does not help, set
`HUB_FORCE_SECURE_CONNECTIONS` to 1 in Config.h and reflash.

**The controller will not connect.** It has to be in pairing mode (Xbox logo
blinking *fast*). Controllers previously paired to a phone or PC tend to
reconnect there instead — remove the pairing on that device.

## Safety

Three properties are deliberate and should be preserved in any fork:

- **Boot state is disarmed.** The vehicle never drives off on power-up.
- **Arming is refused while the triggers are pressed.**
- **The failsafe drops the setpoint to zero at once** and engages the parking
  brake. Losing the controller must not ease off over half a second — which is
  one of the reasons nothing smooths the setpoint any more.

Protection is layered, and the layers are very different in speed:

| What fails | Reaction | After |
|---|---|---|
| Controller switched off or out of range | parking brake | **~1 s**, measured 963 ms |
| Hub link lost | vehicle is no longer commanded | immediately |
| The bridge itself dies (crash, power loss) | hub stops on its own | 10 s |

The first row is the normal case and the one that matters. The last row is a
backstop that only applies when the ESP32 stops sending altogether: the hub's
drive frame expires after ten seconds without a repeat, so even a dead bridge
cannot leave the car running indefinitely.

## Built with an AI assistant — please read this

I am a mechanical engineer, not a firmware developer. **This code was written
with the help of an AI assistant (Claude, by Anthropic), and it may well
contain mistakes I did not catch.** Treat it as a working starting point from
one hobbyist, not as production software.

What I can vouch for:

- The firmware **runs successfully on my own setup**, every day. My kids drive
  the car with it.
- The design decisions and **all testing on real hardware are mine.**
- **Every protocol statement in this repository was measured on the actual
  car**, not taken on faith. Where something could not be measured, it says so
  instead of guessing.

What I cannot vouch for: that it behaves the same on your hub, your
controller, or your board. Work through the staged bring-up above and keep the
vehicle on blocks until the failsafe has proven itself. If you find something
wrong, an issue is welcome — that is rather the point of putting it here.

## Credits

This stands on other people's work:

- [LEGO Wireless Protocol 3.0.00](https://lego.github.io/lego-ble-wireless-protocol-docs/) — the official specification, published by LEGO
- [DanieleBenedettelli/TechnicMoveHub](https://github.com/DanieleBenedettelli/TechnicMoveHub) — reverse engineering of hub 88019; the drive frame and the calibration sequence come from there
- [pybricks discussion #1733](https://github.com/orgs/pybricks/discussions/1733) — hub type `0x84`, port map, the `GOPOS` crash
- [toorisrael/LEGO-Porsche-Controller](https://github.com/toorisrael/LEGO-Porsche-Controller) — the LED mask format
- [move-hub.site](https://move-hub.site/guide) — drive modes and the 10 s frame timeout
- [asukiaaa/arduino-XboxSeriesXControllerESP32](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32) — the Xbox BLE client
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — the BLE stack

## Trademarks

LEGO®, Technic™ and Control+ are trademarks of the LEGO Group, which does not
sponsor, authorize or endorse this project. Xbox® is a trademark of Microsoft
Corporation, and Porsche® of Dr. Ing. h.c. F. Porsche AG — neither sponsors,
authorizes or endorses it either. "Porsche GT4 e-Performance" is part of the
official name of LEGO set 42176. All these names are used here only to
identify the products this bridge works with.

## License

[MIT](LICENSE). The sources and libraries this firmware builds on are listed
with their notices in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
