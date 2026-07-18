# Firmware architecture

## Data flow

```text
GPIO0 edges
    → interval queue
    → ten-interval statistics window
    → sensor expression and trigger features
    → three Euclidean rhythm lanes
    → scale / motif / ratchet decisions
    → mono bass/percussion + mono pad + mono lead
    → shared chorus and stereo delay
    → gain and limiter
    → PIO0 + chained DMA
    → I2S DAC
```

The same musical events are mirrored to UART0 DIN MIDI and class-compliant USB
MIDI. Versioned USB-only editor SysEx controls live parameter snapshots and
flash overrides. UI state and live engine modulation drive the PIO1 RGB display.

## Real-time organization

The foreground loop gives audio buffers first priority. When a free buffer is
available, it renders and submits 256 stereo frames immediately. Between audio
buffers it services USB/DIN MIDI, sensor windows, controls, note-off traffic,
and RGB updates.

Core 1 renders the complete lead engine while core 0 renders bass/percussion followed
by the pad and shared effects. This is the hardware-proven division of work.
The cross-core wait is bounded: if the lead render does not complete, it is
muted for that boot instead of freezing audio, controls, and LEDs. All
per-sample code and lookup tables reside in SRAM; no audio DSP executes from
XIP flash. PRA32-U's internal secondary-core split is disabled so polyphonic
and paraphonic engine modes cannot wait on a nested core handshake. The 153.6
MHz clock is exactly 3,200 times the 48 kHz sample rate.

## Generative sequencing

Three lanes use Euclidean patterns with independently derived step counts,
pulse counts, and rotations. Sensor statistics influence density, tempo,
melodic spread, velocity, patch modulation, and ratchet probability. Banks
provide different musical directions rather than merely changing oscillator
waveforms.

Held USB MIDI notes build a transient pitch-class constraint after scale,
motif, octave, and low-lane articulation. Up to seven distinct pitch classes
are captured over 50 ms and then atomically replace the previous latched chord.
Each generated note is moved to the nearest allowed pitch class before note
scheduling and MIDI output. Releasing keys keeps the chord active; CC 120 or a
USB disconnect clears it and restores the stored patch scale.

Ten-edge windows update an adaptive absolute-pressure range, adaptive signal
variation, and a fast transient. Musical timing runs from a separate 50 Hz hold
tick using the most recent valid state, so a quiet plant or very light touch
does not stop the sequencer while the next window accumulates. Individual edge
activity distinguishes a slow source from an open input. After 1.5 seconds
without an accepted edge, new sequencing stops until activity returns.

The low, middle, and high lanes each own one monophonic slot for
bass/percussion, pad, and lead. Repeated pitches tie within their lane; a new
pitch replaces only the previous note in that same role.

## Synthesis

The vendored PRA32-U core provides two oscillators, sub/noise mixing, resonant
multimode filters, per-voice envelopes, multiple LFO waveforms, portamento,
chorus, stereo delay, and modulation routing. Each part uses the synthesis and
modulation sections. The scene builder in
[`src/synth.cpp`](../src/synth.cpp) defines 128 patches across eight banks and
maps live sensor statistics into patch-appropriate parameter ranges. The low
part ranges from sub bass and tuned percussion to genuine filtered noise for
snares and hats. The pad supplies harmonic body and owns the patch-shared
chorus/delay: dry bass and lead signals feed that single effects stage. The
lead uses a brighter, faster, more expressive transform. The low lane can
inherit a bank default or run Bass, Percussion, or alternating Hybrid events;
six stored articulation slots provide the per-hit percussion vocabulary.

## MIDI

DIN and USB outputs carry note on/off, velocity, ratchets, pitch bend, and
expressive controller changes. Single-channel mode uses channel 1. Multichannel
mode maps the three sequencer lanes to MIDI channels 1–3.

Correctly signed USB SysEx is parsed in the foreground and can read, audition,
save, revert, or restore parameters. The active patch and bank stay in RAM;
other overrides are read directly from a dual-half append-only flash journal.
The journal is placed from the flash chip's startup-detected JEDEC capacity,
not the board header's maximum address space. Flash saving uses a bounded SDK
safe-execute operation and never runs from an audio ISR or per-sample path. See the
[editor protocol](editor-protocol.md).

## Raw output

Raw mode is intentionally separate from synthesis. It suppresses MIDI notes,
stops active synth voices, and streams the digitized GPIO0 pulse train. A
double-buffered one-window DMA capture keeps its latency to approximately one
256-frame window.
