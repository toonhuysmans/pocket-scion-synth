# Firmware architecture

## Data flow

```text
GPIO0 edges
    → interval queue
    → ten-interval statistics window
    → sensor expression and trigger features
    → three Euclidean rhythm lanes
    → scale / motif / ratchet decisions
    → four-voice PRA32-U synthesizer
    → gain and limiter
    → PIO0 + chained DMA
    → I2S DAC
```

The same musical events are mirrored to UART0 DIN MIDI and class-compliant USB
MIDI. UI state and live engine modulation drive the PIO1 RGB display.

## Real-time organization

The foreground loop gives audio buffers first priority. When a free buffer is
available, it renders and submits 256 stereo frames immediately. Between audio
buffers it services USB/DIN MIDI, sensor windows, controls, note-off traffic,
and RGB updates.

PRA32-U divides its four-voice DSP load across both RP2040 cores. Lookup tables
used in hot oscillator, filter, envelope, LFO, and chorus paths reside in SRAM;
this avoids cross-core XIP contention that otherwise produces audible clicks.

## Generative sequencing

Three lanes use Euclidean patterns with independently derived step counts,
pulse counts, and rotations. Sensor statistics influence density, tempo,
melodic spread, velocity, patch modulation, and ratchet probability. Banks
provide different musical directions rather than merely changing oscillator
waveforms.

The allocator does not steal an active voice. If all four slots are occupied,
a new base trigger is dropped so existing envelopes are not repeatedly reset.
Repeated pitches become ties and extend duration without restarting their
envelope. This avoids the low-volume failure mode caused by continuously
restarting attacks.

## Synthesis

The vendored PRA32-U engine provides two oscillators, sub/noise mixing,
resonant multimode filters, per-voice envelopes, multiple LFO waveforms,
portamento, chorus, stereo delay, and modulation routing. The scene builder in
[`src/synth.cpp`](../src/synth.cpp) defines 128 patches across eight banks and
maps the live sensor statistics into patch-appropriate parameter ranges.

## MIDI

DIN and USB outputs carry note on/off, velocity, ratchets, pitch bend, and
expressive controller changes. Single-channel mode uses channel 1. Multichannel
mode maps the three sequencer lanes to MIDI channels 1–3.

## Raw output

Raw mode is intentionally separate from synthesis. It suppresses MIDI notes,
stops active synth voices, and streams the digitized GPIO0 pulse train. A
double-buffered one-window DMA capture keeps its latency to approximately one
256-frame window.
