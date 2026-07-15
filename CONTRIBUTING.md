# Contributing

Contributions are welcome, especially measured improvements to musicality,
real-time reliability, platform documentation, tests, and new clean-room patch
directions.

## Before starting

Read:

- [Firmware architecture](docs/architecture.md)
- [Platform and peripherals](docs/platform.md)
- [Banks, scenes, and parameters](docs/banks-and-parameters.md)
- [Clean-room reverse engineering](docs/reverse-engineering.md)
- [Hardware testing](docs/hardware-testing.md)

For work performed with Codex or another coding agent, also read
[`AGENTS.md`](AGENTS.md).

For a substantial behavioral change, open a GitHub issue first and describe
the musical goal, affected controls or peripherals, and how you can test it on
real hardware. Focused bug fixes and documentation corrections can go directly
to a pull request.

## Development setup

Install Raspberry Pi Pico SDK 2.2.0, CMake, Ninja or Make, and the complete Arm
GNU embedded toolchain including Newlib. Follow
[the build guide](docs/building.md) for the exact commands.

Run the host test with:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -Isrc src/sensor_math.c tests/test_sensor_math.c \
  -lm -o /tmp/test_sensor_math
/tmp/test_sensor_math
```

Then make a clean Release build:

```sh
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Pull requests

Keep each pull request focused. Include:

- what changed and why;
- the musical or technical result;
- host tests and build commands run;
- whether it was tested on a physical Pocket SCION;
- the hardware-test cases completed;
- audible limitations or regressions still known;
- documentation updates for changed behavior.

For audio changes, short recordings are useful, but do not commit large media
files to the repository. Attach them to the issue or pull request instead.

## Firmware standards

- Audio continuity takes priority over UI and background work.
- Avoid dynamic allocation, blocking work, and logging in real-time paths.
- Preserve note ties and the no-voice-stealing behavior unless the change is
  explicitly justified and click-tested.
- Treat changes to clocks, DMA, PIO, gain, effects, cross-core processing, and
  raw capture as high-risk.
- Keep DIN MIDI and USB MIDI behavior aligned.
- Add a host test when hardware-independent logic is introduced or changed.
- Run `git diff --check` before submitting.

## Hardware reports

Use [the hardware checklist](docs/hardware-testing.md). Include the firmware
commit, board behavior, power source, audio connection, MIDI connection, and
which sensor source was used. A report of “works” without these details is
difficult to reproduce.

Never flash a contributor's device remotely or assume a mounted `RPI-RP2`
volume is the intended target. The person operating the hardware should
explicitly approve the flash.

## Licensing and clean-room rules

New project code and documentation are contributed under the MIT license.
Do not submit Instruō firmware, decoded binaries, extracted samples, manual
content, or proprietary source/material. Behavioral descriptions obtained by
observation are welcome when their method and confidence are documented.

Third-party code must have a compatible license, clear provenance, and a
specific reason to be vendored. Discuss large dependencies before adding them.
