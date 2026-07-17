# Experimental multitimbral design

The `multi-timbral` branch maps the generative sequencer's bass, melody, and
upper lanes to three independent monophonic PRA32-U parts. This is a structural
experiment, not the hardware-validated v2.3.0 release.

## Signal path

```text
bass lane   → dry PRA32-U ─┐
                           ├→ mono mix → middle chorus/delay → stereo output
upper lane  → dry PRA32-U ─┘
middle lane → full PRA32-U ────────────────────────────────┘
```

Each part owns its oscillator, sub/noise mix, filter, modulation and amplifier
envelopes, LFO, portamento state, note state, and sensor modulation. The middle
part alone owns chorus and stereo/ping-pong delay because another delay line
would consume roughly 64 KiB of SRAM.

## Lane directions

| Lane | Internal part | Deliberate transform from the selected scene |
|---|---|---|
| Bass | Dry | More sub, oscillator 2 lower, darker filter, slower LFO, longer release, higher gain |
| Melody | Full effects | Authored scene values, shared chorus/delay owner |
| Upper | Dry | Less sub, oscillator 2 higher, brighter filter/key tracking, faster LFO, shorter release |

The same bank and scene still establish the common musical identity. Sensor
cutoff, resonance, morph, and LFO motion are scaled to 70% on bass, 100% on
melody, and 112% on upper. Breath, modulation, pitch bend, special live
envelope changes, and all-notes-off reach every part.

## Notes, MIDI, ratchets, and LEDs

Each lane has exactly one active note slot. Repeating its pitch ties and extends
the note; a new pitch ends and replaces that lane only. Ratchet events retain
their lane identity. MIDI behavior is unchanged: channel 1 in single-channel
mode, or bass/melody/upper on channels 1/2/3. The RGB radius maps one, two, and
three active parts to the centre, middle, and outer ring respectively.

## RP2040 resource strategy

The full effects engine remains in the SRAM hot path. Both compact dry engine
states fit in RAM, but inlining their second template specialization into the
hot renderer would overflow RP2040 SRAM. They therefore share one non-inlined,
flash-resident DSP entry point. The Release build leaves approximately 20 KiB
of main SRAM before the separately allocated core stacks.

This tradeoff must be tested on a physical Pocket SCION for XIP-cache timing,
audio underruns, clicking under high pressure, effect-heavy scenes, ratchets,
and all three parts sounding together. Do not promote this branch to a release
until it passes [the full hardware checklist](hardware-testing.md).
