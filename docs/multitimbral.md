# Multi-timbral design

The `multi-timbral` branch maps the three sequencer ranges to independent
PRA32-U timbres: bass/percussion, pad, and lead. This hardware-tested
three-engine topology is packaged as release v2.4.0.

## Signal path

```text
low lane    → dry bass/percussion ─┐
high lane   → dry lead ────────────┼→ pad chorus/delay → stereo output
middle lane → pad PRA32-U ─────────┘
```

All three parts own oscillators, sub/noise mix, filter, envelopes, LFO,
portamento, note state, and sensor modulation. The pad owns chorus and
stereo/ping-pong delay because another delay line consumes roughly 64 KiB.

## Part directions

| Part | Architecture | Direction from the selected scene |
|---|---|---|
| Bass/percussion | Voice-mode selectable, dry | Sub bass, tuned impacts, or filtered noise snares/hats; darker filtering and stronger pitch envelopes |
| Pad | Voice-mode selectable, full effects | Longer envelope, harmonic body, chorus, and delay |
| Lead | Voice-mode selectable, dry | Brighter register, faster contour, stronger motion and expressive routing |

The bank and scene establish a common musical identity. Sensor cutoff,
resonance, morph, and LFO motion are scaled to 70% on low, 88% on pad, and 115%
on lead. Breath, modulation, pitch bend, live envelopes, and all-notes-off
reach all three.

## Notes, MIDI, ratchets, and LEDs

Every role has one note slot. Repeating a pitch ties within its source lane; a
new pitch replaces only the note in that role. Ratchets retain lane identity.
The current sequencer supplies at most one simultaneous note per lane.
Polyphonic and paraphonic PRA32-U paths therefore add no notes here while
exceeding the RP2040 real-time budget when combined with three engines and pad
effects. Firmware v2.4.0 exposes only monophonic, legato-with-glide, and legato
until lane note slots and DSP resource allocation are redesigned together.
MIDI remains channel 1, or low/pad/lead on channels 1/2/3. RGB radius follows
one through three simultaneous notes.

## RP2040 resource strategy

The full effects engine and both dry engines remain in SRAM. At 153.6 MHz,
core 1 renders lead while core 0 renders bass/percussion and then pad/effects.
A bounded handshake mutes lead if core 1 fails to respond, preventing a device
freeze. Each PRA32-U instance renders all of its own oscillators on its assigned
core. Its upstream internal two-core handshake remains disabled because nesting
it inside this three-engine scheduler deadlocks bass and pad. The full voice
paths remain compiled for future scheduling work but are rejected at the live
parameter boundary in v2.4.0.

Future DSP or scheduling changes still require the complete
[hardware checklist](hardware-testing.md), especially audio-underrun tests at
high pressure with effect-heavy scenes, ratchets, controls, and all three notes
sounding together.
