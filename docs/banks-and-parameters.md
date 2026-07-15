# Banks, scenes, and parameters

The sound library contains 128 patches: eight banks with sixteen scene slots
per bank. A scene supplies the musical identity (scale, motif, base tempo, and
Euclidean lane lengths); a bank gives that identity a deliberate synthesis and
performance direction.

Patch numbers below are written as 1–16 for players. In MIDI and source code
they are zero-based program numbers 0–15. The combined patch ID sent on MIDI
CC 23 is `bank × 16 + program`, ranging from 0 to 127.

## Bank overview

| Bank | Name | Tempo | Character and parameter direction |
|---:|---|---:|---|
| 0 | Legacy | 100% | The first ten patches from the earlier synth firmware, followed by Foundation scenes 11–16. Direct, varied, and relatively lightly sensor-routed. This is not Instruō's factory sample bank. |
| 1 | Foundation | 100% | The authored reference versions of all sixteen scenes. It exposes the broadest balanced set of PRA32-U capabilities and full breath/modulation routing. |
| 2 | Organic | 90% | Softer edges, slower triangle or sample-and-hold movement, longer attacks/releases, stronger breath response, and richer chorus. Lower rhythm density and fewer ratchets. |
| 3 | Percussive | 125% | Zero attack, short variable envelopes, pitch transients, brighter filter envelopes, shorter gates, reduced ambience, higher density, and more ratchets. |
| 4 | Bass & Lead | 108% | More sub oscillator, lower tuning in the first half, darker resonant filtering, and longer gates. Programs 9–16 add mono/legato behavior, portamento, and wider bend range. |
| 5 | Atmosphere | 70% | Sparse patterns, slow attacks, high sustain, long releases, slow LFOs, deep chorus, and ping-pong delay. Longest gates and lowest ratchet response. |
| 6 | Spectral | 112% | Alternating filter modes, wider oscillator and pitch geometry, stronger resonance, envelope/LFO pitch routing, faster timbral motion, and upward register expansion. |
| 7 | Extreme | 135% | Deliberately bounded edge cases: hard waveform contrasts, high resonance, fast LFOs, maximum routing, strong effects, short gates, dense rhythms, wide bends, and aggressive ratchets. |

The **pitch-bend layer is separate from the eight banks**. Holding Instrument
toggles sensor pitch bend for whichever bank and scene are active; it does not
replace the selected patch.

## Scene overview

Every bank retains the scene's scale, motif, base tempo, and three Euclidean
lane lengths. It then transforms the synthesis and interaction as described
above.

| Patch | Foundation identity | Base BPM | Euclidean steps: bass / melody / upper | Scale offsets in semitones |
|---:|---|---:|---|---|
| 1 | Rooted Poly — stable reference voice | 92 | 16 / 12 / 7 | 0, 2, 3, 7, 10, 12, 14 |
| 2 | Bright Bloom — animated major shimmer | 106 | 16 / 15 / 9 | 0, 2, 4, 7, 9, 12, 16 |
| 3 | Subterranean Pulse — weighted short envelopes | 84 | 16 / 14 / 11 | 0, 1, 5, 7, 8, 12, 13 |
| 4 | Organic S&H — irregular filter and shape motion | 118 | 16 / 11 / 13 | 0, 3, 5, 6, 10, 12, 15 |
| 5 | Glass Pluck — bright pitch transient and long sparkle | 72 | 16 / 13 / 8 | 0, 2, 4, 7, 9, 11, 14 |
| 6 | Whole-tone Drift — detuned, slow, and wide | 124 | 16 / 9 / 7 | 0, 2, 4, 6, 8, 10, 12 |
| 7 | Shadow Pad — dark swell | 78 | 16 / 15 / 11 | 0, 2, 3, 5, 7, 8, 11 |
| 8 | Deep Space — slow paraphonic spectral cloud | 68 | 16 / 13 / 15 | 0, 2, 4, 6, 7, 9, 11 |
| 9 | Acid Teeth — resonant low-pass bite | 132 | 16 / 7 / 5 | 0, 1, 2, 3, 6, 7, 10 |
| 10 | Chaos Garden — fast random routing | 148 | 16 / 11 / 9 | 0, 1, 3, 4, 6, 7, 9 |
| 11 | Mono Glide Lead — portamento and delayed vibrato | 104 | 16 / 13 / 10 | 0, 2, 4, 5, 7, 9, 11 |
| 12 | Paraphonic Organ — stable harmonics and shared contour | 96 | 16 / 12 / 16 | 0, 3, 5, 7, 10, 12, 15 |
| 13 | High-pass Air — thin, floating, breath-controlled tone | 88 | 16 / 15 / 8 | 0, 2, 5, 7, 9, 12, 14 |
| 14 | Pitch-envelope Percussion — tuned impacts and ratchets | 120 | 16 / 8 / 6 | 0, 1, 3, 6, 7, 10, 12 |
| 15 | Breath Reed — sensor-led expression | 82 | 16 / 14 / 9 | 0, 2, 4, 7, 9, 11, 12 |
| 16 | Maximum Mutation — extreme full routing | 156 | 16 / 7 / 13 | 0, 1, 2, 5, 6, 9, 11 |

Effective BPM is `base BPM × bank tempo percentage`. The clock uses sixteenth
notes with 56/44 swing; each long/short pair still occupies exactly two
straight sixteenth notes.

## Patch parameters

Patch values use PRA32-U's MIDI-style 0–127 range unless the parameter is a
discrete mode. The scene builder sets the following 47 fields.

| Section | Parameters | Function |
|---|---|---|
| Oscillator 1 | wave, shape, morph, sub mix | Select and reshape the primary waveform and add sub/noise content. |
| Oscillator 2 | wave, coarse tuning, fine pitch, oscillator mix | Define the second oscillator, its interval/detune, and the balance between oscillators. |
| Filter | cutoff, resonance, envelope amount, key tracking, mode | Set the resonant low/high-pass tone and its keyboard/envelope response. |
| Modulation envelope | attack, decay, sustain, release, oscillator amount, oscillator destination | Shape filter/pitch motion independently from the amplifier contour. |
| Voice behavior | voice mode, assignment mode, portamento, pitch-bend range | Choose poly/paraphonic/mono behavior, allocation, glide, and bend span. |
| LFO | waveform, rate, depth, fade, oscillator amount/destination, filter amount | Provide periodic or sample-and-hold motion for pitch, waveform, and filter. |
| Amplifier | gain, attack, decay, sustain, release, EG amp modulation, release-equals-decay | Set level and the audible note contour. |
| Expression | breath-to-filter, breath-to-amplitude, EG velocity, amp velocity | Define how sensor breath and note velocity affect timbre and loudness. |
| Chorus | mix, rate, depth | Add stereo width and slow modulation. |
| Delay | feedback, time, mode | Add stereo or ping-pong repeats. |

The authoritative numeric patch construction is in
[`make_foundation_scene()` and `make_scene()`](../src/synth.cpp). Bank variants
are generated deterministically from the sixteen authored Foundation scenes,
so there is no hidden preset file.

## Sensor-derived parameters

Each accepted window contains ten GPIO edge intervals. The analysis exposes
minimum, maximum, range (`delta`), mean, variance, standard deviation, a range
fault, and a trigger flag. The synth derives:

- **Expression:** `clamp(standard deviation / mean × 8)`, then 0.16 smoothing.
- **Proximity:** `1 − clamp(mean / 100000 µs)`, then 0.12 smoothing.
- **Bend motion:** relative change in consecutive means, scaled by four,
  clamped to ±1, then 0.18 smoothing.
- **Spread:** `clamp(delta / 60000 µs)`, used in ratchet drive and selected
  envelopes.
- **Breath:** 65% proximity plus 35% expression, limited by the bank route.
- **Modulation:** expression, limited by the bank route.

### Bank sensor routes

The `+` columns are the maximum amount added to the patch's base 0–127 value.
Density is an offset to the Euclidean pulse calculation; bend and ratchet are
multipliers.

| Bank | Breath max | Mod max | Cutoff + | Resonance + | Morph span | LFO rate + | Bend × | Density | Ratchet × |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 Legacy | 0 | 0 | 34 | 14 | 70 | 48 | 0.55 | 0 | 0.85 |
| 1 Foundation | 127 | 127 | 34 | 14 | 42 | 24 | 1.00 | 0 | 1.00 |
| 2 Organic | 127 | 76 | 24 | 10 | 58 | 18 | 0.65 | −1 | 0.72 |
| 3 Percussive | 54 | 62 | 42 | 20 | 28 | 32 | 0.35 | +2 | 1.45 |
| 4 Bass & Lead | 86 | 70 | 38 | 24 | 34 | 22 | 0.85 | +1 | 1.12 |
| 5 Atmosphere | 108 | 54 | 18 | 8 | 46 | 12 | 0.45 | −2 | 0.45 |
| 6 Spectral | 92 | 112 | 46 | 22 | 72 | 40 | 1.10 | 0 | 1.08 |
| 7 Extreme | 127 | 127 | 56 | 30 | 92 | 56 | 1.40 | +2 | 1.75 |

Scene 15 always permits full breath. Scene 11 increases bend response by 25%,
and scene 14 increases ratchet response by 20%. Foundation/Legacy scenes 5, 9,
and 14 also live-modulate amplifier decay, sustain, and release to prevent
their transient contours from remaining uniformly short.

## Sequencer and performance parameters

| Parameter | Range/default | Use |
|---|---|---|
| Bank | 0–7, default 0 | Selects the synthesis and interaction direction. |
| Program | 0–15, default 0 | Selects scene identity, motif, scale, tempo, and lane lengths. |
| Root note | MIDI 24–72, default 45 (A2) | Transposes all generated notes in semitone steps. |
| Sensitivity | 8 steps: 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.5, 8.0; default 4.0 | Changes trigger analysis and contributes to pulse density and ratchet drive. |
| Duration | ×0.25, ×0.40, ×0.65, ×1, ×1.5, ×2, ×3, ×4; default ×1 | Scales per-lane gate lengths, clamped to 0.25–4 steps. |
| Volume | 12 steps, mute through 4× digital gain; default step 8 | Sets the post-synth master gain with saturation protection. |
| Pitch bend | off/on, default off | Enables sensor bend without changing bank or scene. |
| MIDI mode | channel 1 or channels 1–3 | Maps all lanes together or bass/melody/upper separately. |
| Raw mode | off/on | Replaces synthesis/MIDI notes with the captured GPIO pulse train. |

Base lane gates are 2.20 steps for bass, 1.45 for melody, and 0.90 for the
upper voice before duration and bank scaling. Percussive uses ×0.62, Bass &
Lead ×1.15, Atmosphere ×1.85, Extreme ×0.78, and other banks ×1. Notes are
four-voice polyphonic; repeated pitches tie instead of restarting their
envelopes, and new triggers are dropped rather than stealing an active voice.

Ratchet probability combines expression (48%), proximity (20%), spread (22%),
and sensitivity, followed by the bank multiplier. A successful ratchet divides
the step into two to four repeats. Bass responds at ×0.52, melody ×0.92, and
upper voice ×1.00.

## MIDI parameter output

DIN and USB MIDI carry the same data. Values are emitted on channel 1 in single
mode or channels 1–3 in multichannel mode.

| Message | Meaning |
|---|---|
| Bank Select CC 0 / CC 32 | Bank 0–7 / zero |
| Program Change | Scene 0–15 |
| CC 23 | Combined patch ID 0–127 |
| CC 24 | MIDI mode: 0 single, 127 multichannel |
| CC 20 | Sensitivity, normalized to 0–127 |
| CC 7 | Volume, normalized to 0–127 |
| CC 21 | Duration, normalized to 0–127 |
| CC 22 | Root MIDI note 24–72 |
| CC 74 / CC 71 | Live filter cutoff / resonance |
| CC 1 / CC 2 | Live modulation / breath |
| Pitch Bend | Live 14-bit bend when enabled; center otherwise |
| CC 123 | All Notes Off during mode/program transitions |
