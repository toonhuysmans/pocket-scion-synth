# Pico 2 / RP2350 target

The `pico2-sam` branch builds the complete three-lane synth, SAM voice, USB/DIN
MIDI, editor protocol, simulated sensor input, I2S audio, and control-display
integration for the ARM cores of the RP2350 used by Raspberry Pi Pico 2.

This is a firmware port, not a UF2 upgrade for an unmodified Pocket SCION. The
factory Pocket SCION contains an RP2040 and rejects RP2350 UF2 files. Running
this target requires a Pico 2 or RP2350A board connected to the Pocket SCION
peripherals with the same GPIO assignments documented in
[`platform.md`](platform.md). Pico 2 is pin-compatible at the GPIO-number level,
but power, grounding, analogue audio, and mechanical integration remain a
hardware project.

This profile is for a Pico Omnibus fitted with a Pico 2, a Pimoroni Pico Audio
Pack, and a Pimoroni Pico Display Pack. It uses Audio Pack I2S pins GP9 (data),
GP10 (BCLK), and GP11 (LRCLK), and Display Pack A/B/X/Y buttons on GP12–15.
Those four buttons provide sensitivity down/up, mode, and volume down; holding
mode provides their alternate functions. The fifth Pocket SCION button input is
left on unused GP28 and is not expected to respond.

There is no plant sensor in this setup. The RP2350 build synthesizes a
continuously active, gently sweeping ten-interval sensor window every 20 ms.
This keeps the generative sequence, pressure-dependent timbre, MIDI CC output,
SAM phrase triggering, and editor sensor monitor alive until a real sensor is
added. The RP2040 target retains the physical GPIO0 sensor path.

## Build

Use Pico SDK 2.2.0 and select the custom board (it is the branch default):

```sh
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake -S . -B build-pico2 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-pico2
```

The output is `build-pico2/pocket_scion_synth_pico2.uf2`. The board definition
inherits the official `pico2` configuration: RP2350A ARM secure cores and 4 MiB
flash. The flash journal detects the installed SPI flash at startup and keeps
its final 576 KiB for patch, bank, global, and speech overrides.

To make an RP2040 comparison build from this branch, configure a separate build
directory with `-DPICO_BOARD=pocket_scion`.

## Porting notes

- RP2350 runs at its rated 150 MHz. PIO's fractional divider produces the same
  exact 3.072 MHz I2S bit clock and 48 kHz stereo sample rate.
- GPIO numbers, PIO state machines, chained DMA, UART0 MIDI, TinyUSB MIDI, and
  the two-core scheduling model are unchanged.
- RP2350 has 512 KiB of main SRAM and 4 KiB in each scratch bank. SAM's scratch
  arrays retain their explicit placement; the linker map must keep each bank
  below 4 KiB including its core stack.
- The target uses an RP2350 UF2 family identifier and cannot be flashed onto
  RP2040 hardware.

The target has been compile-, link-, metadata-, sanitizer-, and host-tested.
It has not yet been tested on a wired Pico 2/Pocket SCION hardware conversion;
audio timing, pin electrical compatibility, and flash persistence therefore
still require the physical checklist in [`hardware-testing.md`](hardware-testing.md).
