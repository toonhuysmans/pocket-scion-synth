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

Each bank also deliberately separates the three timbral roles:

| Bank | Low: bass/percussion | Middle: pad | High: lead |
|---|---|---|---|
| Legacy | Synth bass, pulse, sub thump, or noisy accent | Balanced playable pad | Clear lead/pluck |
| Foundation | Round sub, filter pluck, pitch pulse, or growl | Reference harmonic pad | Bright reference lead |
| Organic | Mallet, skin tom, wooden knock, or noise shaker | Warm, slowly breathing pad | Expressive reed lead |
| Percussive | Per-hit kick, tom, noise-snare, or noise-hat | Compact chord stabs | Bright pluck |
| Bass & Lead | Sustained sub, resonant bass, pitch accent, or dirty bass | Dark analog chord bed | Portamento acid/solo lead |
| Atmosphere | Sub drone, heartbeat, swell, or filtered wind | Longest evolving pad | Slow high-pass air lead |
| Spectral | Resonant body, metal strike, noise click, or hollow ring | Glass/high-pass pad | Glass/noise lead |
| Extreme | Per-hit randomized sub/noise impacts | Animated gated cloud | Unstable sample-and-hold lead |

### Percussive bank profiles

The Percussive bank is deliberately varied rather than uniformly short and
dense. Patch 8 retains the balanced reference groove. Patches 4, 10, and 16
are the three intentional click/micro/glitch extremes; the remaining patches
use lower lane densities, fewer ratchets, and longer synth and articulation
decays.

| Patches | Direction |
|---|---|
| 1–3 | Grounded groove, tom circle, and spaced backbeat |
| 4 | Dense click extreme |
| 5–7 | Heavy impacts, tribal movement, and broken beat |
| 8 | Original balanced reference |
| 9 | Sparse dub-like impacts with the longest tails |
| 10 | Micro-ratchet extreme |
| 11–15 | Syncopated, loose, metallic, rolling, and driven kits |
| 16 | Maximum glitch extreme |

These directions are stored as ordinary patch gates, lane densities, lane
ratchet responses, amplifier envelopes, and articulation decay/ratchet values.
They can be edited or replaced without any runtime patch-number branch.

The low lane is configurable independently of bank identity. Every patch can
inherit its bank default or force **Bass**, **Percussion**, or **Hybrid**.
Hybrid alternates bass and percussion events because both use the same low
synth engine; it does not attempt two simultaneous low voices. Bank defaults
are Bass for Legacy, Bass & Lead, and Atmosphere; Percussion for Percussive;
and Hybrid for Foundation, Organic, Spectral, and Extreme. A bank-level
percussion balance sets the inherited Hybrid tendency, while a patch override
has its own balance.

Every patch contains six configurable articulation slots. Each slot chooses
kick, tom, snare, closed hat, open hat, clap, rim/wood, or shaker/metal and
stores weight, rhythmic role, level, tuning, tone, body/noise balance, decay,
transient, and ratchet response. Rhythmic roles are anchor, backbeat, offbeat,
fill, and free. The selector favours slots whose role matches the current
sixteenth-note position; zero weight disables a slot.

Pressure/expression, proximity, and signal spread are applied after the stored
slot values. The patch's Sensor influence controls their depth and Variation
controls random parameter motion and Hybrid switching. This gives every bank
the same exposed percussion vocabulary while its authored defaults retain a
distinct direction.

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
| Shared chorus | mix, rate, depth | Add stereo width and slow modulation to the complete three-part mix. Stored in the pad snapshot. |
| Shared delay | feedback, time, mode | Add stereo or ping-pong repeats to the complete three-part mix. Stored in the pad snapshot. |

The bass and lead PRA32-U instances bypass effects and feed the pad instance's
chorus/delay stage. Schema 4 reuses their twelve formerly inactive FX bytes for
the exposed patch-motion controls below. The record remains 236 bytes, while
the pad snapshot continues to own the six audible shared-effect values.

### Patch motion parameters

| Section | Parameters | Function |
|---|---|---|
| Sensor routing | Breath override; pitch-bend and ratchet response; cutoff, resonance, morph, and LFO-rate motion | Refines the bank interaction route for this patch. Zero breath override inherits the bank maximum; motion responses are percentages. |
| Envelope motion | Amplifier decay, sustain, and release motion | Adds sensor-driven movement to each lane's own stored amplifier envelope. Zero keeps that stage static. |
| Register | Pressure octave span; expression octave threshold | Controls sensor transposition. A zero expression threshold disables its extra octave. |

These values replace all former scene-number and bank-number exceptions in the
performance path. Bank and patch IDs now only select records. Factory builders
still use IDs to author initial values, exactly as a preset file would, but the
resulting values can all be edited and saved.

### Low-articulation parameters

| Owner | Parameters |
|---|---|
| Patch | mode override, percussion balance, sensor influence, variation |
| Six slots per patch | sound algorithm, rhythmic role, weight, level, tune, tone, body/noise, decay, transient, ratchet response |
| Bank | default low-lane mode and inherited percussion balance |

The authoritative numeric patch construction is in
[`make_foundation_scene()` and `make_scene()`](../src/synth.cpp). Bank variants
are generated deterministically from the sixteen authored Foundation scenes,
so there is no hidden preset file.

## Sensor-derived parameters

Each accepted window contains ten GPIO edge intervals. The analysis exposes
minimum, maximum, range (`delta`), mean, variance, standard deviation, a range
fault, and a trigger flag. It learns slowly contracting low/high envelopes for
both mean interval and coefficient of variation. Fast envelope expansion
captures a new gesture while slow contraction follows drift. The synth derives:

- **Absolute pressure/proximity:** 78% adaptive position within the learned
  mean-interval range plus 22% fixed `1 − mean / 100000 µs`, then 0.18
  smoothing. A stable source rests near the adaptive midpoint rather than off.
- **Variation/expression:** 82% adaptive coefficient-of-variation position plus
  18% fixed `standard deviation / mean × 8`, then 0.22 smoothing.
- **Transient:** the fast change in adaptive pressure and mean interval, with
  fast attack and slower decay.
- **Bend motion:** relative change in consecutive means, scaled by four,
  clamped to ±1, then 0.18 smoothing.
- **Spread:** `clamp(delta / 60000 µs)`, used in ratchet drive and selected
  envelopes.
- **Breath:** 68% pressure, 24% variation, and 8% transient, limited by the bank
  route.
- **Modulation:** 76% variation plus 24% transient, limited by the bank route.

Fresh ten-edge windows update these measurements, but they no longer clock the
sequencer. A 50 Hz hold tick continues from the last valid state while sparse
plant edges accumulate. Individual edge activity distinguishes a slow source
from an open circuit: 1.5 seconds without any accepted edge marks the input
inactive and stops new notes. Thus slow window completion is allowed without
letting a disconnected pad sequence forever.

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

The compiled defaults express the former special cases as ordinary patch
values: scene 15 starts with a full-breath override, scene 11 starts at 125%
bend response, and scene 14 starts at 120% ratchet response. The relevant
Foundation/Legacy transient scenes start with non-zero decay, sustain, and
release motion. None of these behaviours depends on scene number after load.

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
played by three independent monophonic parts: bass/percussion, pad, and lead.
Repeated pitches tie within their lane; a new pitch replaces only the note in
that same role.

Each patch has independent Bass, Pad, and Lead density biases from −8 to +8.
They offset the sensor- and bank-derived pulse request before it is clamped to
the lane's Euclidean length. Step length therefore sets the cycle geometry,
while the three density controls establish the relative activity of the lanes.
Exact hit counts and rotation remain generative.

Every base scene receives deliberate role transforms. Low emphasizes sub,
darker filtering, percussion and noise; pad emphasizes harmonic sustain and
owns chorus/delay; lead emphasizes brightness, speed, and expressive motion.
Sensor modulation depth is 70% for low, 88% for pad, and 115% for lead.

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
