# Repository guidance for coding agents

## Mission

Maintain and extend the clean-room Pocket SCION real-time synthesis firmware.
Preserve musical behavior, real-time audio reliability, recoverability, and the
project's separation from Instruō's proprietary firmware and sample content.

This file applies to the entire repository. A future, more specific
`AGENTS.md` in a subdirectory may add or override guidance for that subtree.

## Start here

Read the smallest relevant set before changing code:

- `README.md` for product scope, installation, and recovery.
- `docs/platform.md` for confirmed peripherals and GPIO assignments.
- `docs/architecture.md` for real-time data flow and voice allocation.
- `docs/banks-and-parameters.md` for patches and sensor routing.
- `docs/controls.md` for gestures, LEDs, and MIDI behavior.
- `docs/reverse-engineering.md` for provenance and confidence levels.
- `docs/hardware-testing.md` for physical-device validation.

Treat the source, especially `src/synth.cpp` and `src/board_pins.h`, as
authoritative when prose and implementation disagree. Correct stale prose in
the same change.

## Repository map

```text
boards/       RP2040 board definition
pio/          I2S and WS2812 PIO programs
src/          firmware, synthesis, sequencing, controls, MIDI, and display
tests/        host-side deterministic tests
vendor/       pinned third-party PRA32-U DSP source and provenance
docs/         architecture, platform, interaction, and testing notes
editor/       TypeScript Web MIDI patch editor and tests
releases/     curated release UF2 and checksum manifest
```

## Build and test

The supported SDK is Raspberry Pi Pico SDK 2.2.0. Prefer an out-of-tree build
and do not commit generated files.

```sh
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the host sensor test for every change that touches sensor acquisition,
statistics, or sequencing inputs:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -Isrc src/sensor_math.c tests/test_sensor_math.c \
  -lm -o /tmp/test_sensor_math
/tmp/test_sensor_math
```

For editor or USB protocol changes, also run:

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc \
  src/editor_protocol.c tests/test_editor_protocol.c \
  -o /tmp/test_editor_protocol
/tmp/test_editor_protocol
cd editor && npm test && npm run build
```

Before handing off a firmware change:

1. Run `git diff --check`.
2. Run relevant host tests.
3. Build `pocket_scion_synth.uf2` with Pico SDK 2.2.0.
4. Inspect metadata with `picotool info -a build/pocket_scion_synth.uf2` when
   `picotool` is available.
5. State clearly whether the change was hardware-tested.

Do not copy a UF2 to a mounted device or otherwise flash hardware unless the
user explicitly requests it. A successful compile is not a substitute for the
physical checks in `docs/hardware-testing.md`.

## Hardware and real-time invariants

- Target: RP2040 at 153.6 MHz, 48 kHz signed 16-bit stereo output.
- Audio: PIO0 and chained DMA, I2S data/BCLK/LRCLK on GPIO12/13/14.
- Sensor: GPIO0 both-edge pulse input; ten accepted intervals per window.
- RGB: nine WS2812-compatible pixels on GPIO1 through PIO1.
- MIDI: UART0 TX on GPIO16 at 31,250 baud plus USB MIDI.
- Buttons are active-low on GPIO17–21 with the assignments in
  `src/board_pins.h`.
- GPIO27 and GPIO29 have unresolved auxiliary roles; initialize them safely
  and do not assign behavior without new evidence and hardware validation.

Audio buffers have first priority. Keep blocking I/O, allocation, logging,
USB service, sensor analysis, and UI work out of the per-sample render path.
Preserve the dual-core PRA32-U arrangement and SRAM placement of hot DSP code
and lookup tables unless timing is measured again on hardware.

There are three independent monophonic timbres: bass/percussion, pad, and lead.
New notes replace only their own role, while repeated pitches tie and extend.

Changes to clocks, DMA buffer size, PIO timing, gain, effects, raw capture,
part count, or cross-core memory placement are high-risk and require the full
hardware checklist.

## Behavioral expectations

- Preserve all eight banks and sixteen scene positions unless a change is
  intentionally versioned and documented.
- Pitch bend remains a separate toggle layer, not an additional bank.
- DIN and USB MIDI should describe the same musical events.
- Single-channel MIDI uses channel 1; multichannel maps the three lanes to
  channels 1–3.
- White LED flashes are reserved for actual ratchet firings in normal mode.
- Raw mode must silence synth voices and MIDI notes before presenting the
  captured pulse train.
- Program and mode transitions must avoid stuck notes.

When altering controls, test single, double, triple, held, shifted, and paired
button gestures for conflicts. When altering patches, compare quiet and high
sensor pressure, all three active notes, ratchets, and effect-heavy scenes.

## Clean-room and licensing boundary

Never commit or redistribute:

- Instruō factory firmware images or decoded binaries;
- extracted factory samples or other factory content;
- the Instruō manual or copied proprietary source/material;
- local disassembly databases, dumps, or analysis artifacts.

It is acceptable to link to Instruō's official factory-firmware download.
Document behavioral observations and independently implemented interfaces,
not copied implementation. Keep third-party code under `vendor/` pinned and
retain its license and provenance. Do not casually reformat vendored files.

## Change discipline

- Keep changes focused and preserve unrelated work in a dirty tree.
- Prefer existing abstractions and PIO programs over parallel replacements.
- Use fixed-width types in firmware-facing code and make signed/unsigned
  conversions explicit.
- Avoid dynamic allocation and unbounded foreground work.
- Add deterministic host tests when logic can be separated from Pico hardware.
- Update user documentation when controls, banks, MIDI, pins, build steps, or
  audible behavior change.
- Do not change version metadata or curated release files for an ordinary
  development commit.

## Release checklist

Only prepare a release when explicitly requested:

1. Choose one version and update CMake metadata, filenames, release notes, and
   documentation consistently.
2. Run host tests, a clean Release build, and the complete hardware checklist.
3. Confirm UF2 board/family/version metadata with `picotool`.
4. Place only the verified UF2 in `releases/` and update `SHA256SUMS`.
5. Confirm the factory-recovery link still works.
6. Commit source and release artifacts, push, verify CI, and attach the exact
   checked UF2 to the matching GitHub Release.

In the final handoff, report tests, build result, hardware-test status, UF2
path, checksum, and any remaining risk.
