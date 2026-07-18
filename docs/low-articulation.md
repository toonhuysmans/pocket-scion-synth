# Bass, percussion, and hybrid low lane

The low sequencer lane uses one PRA32-U synth engine, but its role is no longer
fixed by the selected bank. It can behave as a continuous bass voice, select a
configurable percussion articulation for every note, or alternate between both
behaviours.

All settings below are available in the browser editor. Changes are auditioned
immediately and become persistent only after **Save patch** or **Save bank
settings**.

## Low-lane modes

| Mode | Behaviour |
|---|---|
| Inherit bank | Uses the bank's Default low-lane mode and Percussion balance. |
| Bass | Plays the stored Bass synth patch without per-note drum conversion. |
| Percussion | Converts every low-lane event into one of the six articulation slots. |
| Hybrid | Keeps strong anchors bass-led and probabilistically converts other events to percussion. |

Hybrid alternates events because bass and percussion share one engine. It does
not layer a kick and bass note simultaneously. **Percussion balance** controls
the basic conversion probability. Sensor activity can move that balance when
Sensor influence and Variation are enabled.

The compiled bank defaults are:

| Bank | Default mode | Percussion balance |
|---|---|---:|
| Legacy | Bass | 9% |
| Foundation | Hybrid | 27% |
| Organic | Hybrid | 57% |
| Percussive | Percussion | 100% |
| Bass & Lead | Bass | 6% |
| Atmosphere | Bass | 14% |
| Spectral | Hybrid | 61% |
| Extreme | Hybrid | 82% |

Selecting an explicit patch mode overrides the mode in this table. An explicit
Hybrid patch uses its patch-level Percussion balance; Inherit uses the bank
balance.

## Six articulation slots

Every patch owns six slots. A slot is a complete description of a possible
low-lane percussion hit. Slots may duplicate an algorithm, so a patch can have
several differently tuned toms or kicks. Setting Weight to zero disables that
slot.

| Parameter | Musical effect |
|---|---|
| Sound | Kick, tom, snare, closed hat, open hat, clap, rim/wood, or shaker/metal synthesis topology. |
| Rhythmic role | Favours the slot on anchors, backbeats, offbeats, fills, or anywhere. |
| Weight | Relative probability after rhythmic-role weighting. Zero disables the slot. |
| Level | Per-hit low-engine amplifier level. |
| Tune | Pitch offset from the generated low note, from −24 to +24 semitones. |
| Tone | Base filter position; low values are darker. |
| Body / noise | Moves from pitched/sub body toward filtered noise. |
| Decay | Length of the percussion body and release. |
| Transient | Pitch-envelope and resonant attack strength. |
| Ratchet response | Multiplies the existing bass-lane ratchet probability. |

The algorithms deliberately interpret these common controls differently. For
example, hats force a bright high-pass/noise topology, while kick and tom keep
more sub body and use the transient as a downward pitch contour.

## Rhythmic selection

The selector does not choose all slots with uniform randomness. It multiplies
the stored Weight when the current sixteenth-note position matches the slot's
role:

- **Anchor:** phrase starts and strong eight-step anchors.
- **Backbeat:** secondary accents at positions suited to snare-like hits.
- **Offbeat:** alternating spaces suited to hats and shakers.
- **Fill:** the final four positions of a sixteen-step phrase.
- **Free:** equal role strength at every position.

Non-matching slots remain possible at lower probability, preventing the groove
from becoming a fixed drum-machine pattern. The low lane's Euclidean rhythm
still decides whether an event exists; articulation decides what that event
becomes.

## Sensor modulation

The stored articulation values are the stable starting point. Runtime sensor
statistics are additive:

- Pressure/proximity opens Tone and influences Hybrid conversion.
- Variation/expression adds brightness, noise, resonance, and transient energy.
- Signal spread lengthens selected decays and contributes to fills and Hybrid
  movement.
- Variation adds bounded random motion to tone, noise, and decay.

**Sensor influence** controls the modulation depth. At zero the articulation is
repeatable and Hybrid selection follows only its static balance and rhythmic
anchors. **Variation** controls random motion and how far sensor activity can
push Hybrid away from its stored balance.

## Shared effects

Bass, percussion, and lead are dry PRA32-U engines feeding the pad engine's
chorus and stereo delay. These are therefore one **patch-shared effects bus**,
not three independent effects. The editor exposes the effective controls under
**Pad + effects → Shared effects**.

The six shared controls are Chorus mix/rate/depth and Delay
feedback/time/mode. They are saved with the patch, so “shared” means shared by
the three parts of one patch—not global across the complete device or bank.

## Starting recipes

For a stable bass with occasional percussion:

1. Select Hybrid and set Percussion balance around 20–35%.
2. Keep kick and snare weights moderate and set hats lower.
3. Use low Sensor influence and Variation.

For a coherent generative kit:

1. Select Percussion.
2. Assign kick to Anchor, snare to Backbeat, hats to Offbeat, and tom/clap to
   Fill.
3. Adjust weights before increasing Variation.
4. Use per-slot Ratchet response mainly for hats, clap, and shaker.

For plant input, begin with lower Sensor influence than for direct touch. Plant
signals usually change more slowly, so large modulation depths can otherwise
hold one timbral extreme for a long time.

## Storage compatibility

The expanded patch record remains inside one 256-byte flash journal page.
Firmware loads older prefix-compatible records over new compiled defaults, so
previously saved synthesis and sequence parameters are retained while new
articulation fields receive safe defaults. Editor JSON version 4 contains the
current controls; version 1, 2, and 3 patch and bank files remain importable.
