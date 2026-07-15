# Clean-room reverse engineering

## Purpose and boundaries

The platform layer was reconstructed by observing the behavior and interfaces
of the original Pocket SCION v1.0.1 firmware. The new firmware is independently
written and uses a separate CC0 synthesis engine. It contains no original
firmware code, embedded samples, extracted audio, or manual content.

The original binary is deliberately not included in this repository.

## Method

The original UF2 was decoded into a contiguous RP2040 flash image and imported
as ARM Cortex-M0+ Thumb code at XIP base `0x10000000`. Analysis focused on
hardware register access, Pico SDK call patterns, PIO instruction words,
constant tables, and repeated data flow.

Cross-checks included:

- RP2040 UF2 family ID and vector table;
- GPIO initialization and alternate-function selection;
- peripheral base addresses and DMA DREQ routing;
- PIO instruction decoding;
- WAV container metadata in the original resource area;
- user-visible control behavior and hardware testing;
- logic implied by repeated sensor-statistics and voice-allocation paths.

## Principal findings

- RP2040 application with approximately 67 KiB of executable/constant code
- 216 embedded mono PCM16 samples at 20 kHz in the original image
- GPIO0 both-edge sensor timestamping
- ten accepted intervals per analysis window
- five-voice sample allocator without active-voice stealing
- PIO0/DMA I2S on GPIO12–14
- 31,250-baud UART0 MIDI on GPIO16
- nine WS2812-compatible pixels on GPIO1 / PIO1
- five-ring chain map `4,3,2,1,0,1,2,3,4`
- exact button GPIO mapping confirmed on hardware

## Confidence convention

- **Confirmed:** directly encoded in initialization, register access,
  descriptors, constants, or repeated instruction flow.
- **Hardware confirmed:** observed in the replacement firmware on the device.
- **High confidence:** supported by several independent static clues.
- **Unresolved:** the binary confirms a signal exists but not its physical
  meaning, as with GPIO27 and GPIO29.

## Intentional differences

This project is not a byte-for-byte emulator. It preserves useful platform
interfaces while replacing sample playback with synthesis. It also improves
edge buffering, uses 48 kHz audio, provides true polyphony, adds USB MIDI,
generates richer rhythms, and links the LED artwork to live synthesis state.

The reverse engineering describes compatibility facts; the musical behavior
is new work.
