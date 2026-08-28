# Third-party notices

**This repository contains no third-party source code.** The libraries listed
below are fetched by `arduino-cli` at build time and compiled in; nothing is
copied into this tree, and no compiled binaries are distributed here.

The notices are reproduced anyway, because part of the protocol knowledge in
[docs/protocol.md](docs/protocol.md) and some of the frame constants in
[firmware/MoveHub.cpp](firmware/MoveHub.cpp) were derived from the projects in
the first table.

## Protocol sources

| Project | License | What was taken |
|---|---|---|
| [DanieleBenedettelli/TechnicMoveHub](https://github.com/DanieleBenedettelli/TechnicMoveHub) | MIT — Copyright (c) 2024 Danny's LAB | the drive frame and the steering calibration sequence, both re-verified on hardware and, for the lights byte, corrected |
| [LEGO Wireless Protocol 3.0.00](https://lego.github.io/lego-ble-wireless-protocol-docs/) | MIT — Copyright (c) 2018 LEGO System A/S | the message format the hub speaks |
| [toorisrael/LEGO-Porsche-Controller](https://github.com/toorisrael/LEGO-Porsche-Controller) | none stated | the LED bitmask format — a protocol fact. No code and no text was taken from this project. |
| [move-hub.site](https://move-hub.site/guide) | none stated | drive modes and the 10 s frame timeout — facts, independently confirmed |
| [pybricks discussion #1733](https://github.com/orgs/pybricks/discussions/1733) | — | hub type `0x84`, the port map, the `GOPOS` crash — facts, independently confirmed |

## Libraries this firmware is built against

None of these are contained in this repository.

| Library | License |
|---|---|
| [asukiaaa/arduino-XboxSeriesXControllerESP32](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32) | MIT — Copyright (c) 2022 Asuki Kono |
| [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | Apache-2.0 — Copyright 2020-2025 Ryan Powell and contributors |
| [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32) (board core) | LGPL-2.1 |

> **On the LGPL-2.1 core:** this repository ships source only — `.bin`, `.elf`
> and `.hex` are in `.gitignore` and there are no release binaries. Anyone who
> distributes a *compiled* image built from this source links the LGPL core
> statically and takes on the relinking obligations of LGPL-2.1 section 6.
> Passing on the source instead avoids the question entirely.

## MIT License

Applies to the three MIT-licensed entries above, each with its own copyright
line as given in the tables:

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Apache License 2.0

Full text: <https://www.apache.org/licenses/LICENSE-2.0>

## This project

Everything else in this repository is MIT, see [LICENSE](LICENSE).
