# Firmware architecture

## Data flow

```text
GPIO0 edges
    → interval queue
    → ten-interval statistics window
    → sensor expression and trigger features
    → three Euclidean rhythm lanes
    → scale / motif / ratchet decisions
    → three independent monophonic PRA32-U parts
    → shared chorus and stereo delay
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

The upper part renders on RP2040 core 1 while core 0 renders bass, then core 0
adds the middle part and shared effects. Monophonic parts skip low-rate work for
their three inactive PRA32-U voices. The full middle engine and all lookup
tables used by oscillator, filter, envelope, LFO, and effects paths reside in
SRAM. Both dry parts share one non-inlined SRAM processing entry point so code
is not duplicated and no per-sample DSP executes from XIP flash. The tighter
remaining SRAM margin makes physical timing and stress validation mandatory.

## Generative sequencing

Three lanes use Euclidean patterns with independently derived step counts,
pulse counts, and rotations. Sensor statistics influence density, tempo,
melodic spread, velocity, patch modulation, and ratchet probability. Banks
provide different musical directions rather than merely changing oscillator
waveforms.

Each lane owns one monophonic part. A new pitch replaces only the previous note
in that lane, while a repeated pitch becomes a tie and extends duration without
restarting its envelope. Thus bass activity cannot steal the melody or upper
part, and attack churn remains isolated to the lane that actually changed.

## Synthesis

The vendored PRA32-U core provides two oscillators, sub/noise mixing, resonant
multimode filters, per-voice envelopes, multiple LFO waveforms, portamento,
chorus, stereo delay, and modulation routing. Each part uses the synthesis and
modulation sections. The scene builder in
[`src/synth.cpp`](../src/synth.cpp) defines 128 patches across eight banks and
maps live sensor statistics into patch-appropriate parameter ranges. Bass,
melody, and upper variants then deliberately offset oscillator balance, tuning,
filter range, envelopes, LFO behavior, and gain. Only the middle part owns the
chorus and delay; the other two are mixed into that shared stage.

## MIDI

DIN and USB outputs carry note on/off, velocity, ratchets, pitch bend, and
expressive controller changes. Single-channel mode uses channel 1. Multichannel
mode maps the three sequencer lanes to MIDI channels 1–3.

## Raw output

Raw mode is intentionally separate from synthesis. It suppresses MIDI notes,
stops active synth voices, and streams the digitized GPIO0 pulse train. A
double-buffered one-window DMA capture keeps its latency to approximately one
256-frame window.
