# Pocket SCION USB editor manual

The Pocket SCION Editor is an installable browser application for configuring
the alternative synthesis firmware over class-compliant USB MIDI SysEx. It can
audition every parameter live, save overrides into the device, monitor the
sensor, and exchange complete patches or banks as readable JSON.

The editor does not work with Instruō's factory firmware. It contains no
factory firmware, samples, or proprietary patch data.

## Open and connect

Use current desktop Chrome or Edge. Web MIDI SysEx requires an HTTPS page or
`localhost` and explicit browser permission. Safari and iOS do not currently
provide the required Web MIDI interface.

1. Connect Pocket SCION normally over USB. Do not enter the UF2 bootloader.
2. Open the [hosted editor](https://toonhuysmans.github.io/pocket-scion-synth/),
   or run `npm install && npm run dev` from [`editor/`](../editor).
3. Select **Connect USB MIDI** and approve MIDI/SysEx access.
4. Choose a bank and patch. The device selects it and the editor reads its
   current saved override or compiled default.

The status line reports the firmware version and detected flash capacity.
Patch changes made with the hardware Instrument button are sent over USB as CC
23. The editor follows them, updates both selectors, and loads the newly active
patch. Rapid hardware changes cancel stale reads so the last device selection
wins.

## The complete signal flow

### Control and storage flow

```text
compiled 128-patch library
          │
          ├── overridden by newest valid flash-journal record
          │
          ▼
active patch + active bank + globals in RP2040 RAM
          ▲                         │
          │ USB editor SysEx        ├── immediate synth/sequence audition
browser knobs and selectors         └── Save → CRC-protected flash record
```

Editor changes first alter the active RAM snapshot and are heard immediately.
They are not persistent until the matching Save button is used. Runtime sensor
modulation and per-hit articulation are applied after the stored values and do
not rewrite the patch.

The effective parameter order is:

```text
compiled default → saved override → unsaved editor value
                 → bank/sensor modulation → per-hit articulation
```

### Sensor and note-generation flow

```text
GPIO0 sensor edges
    → ten-interval statistics window
    → pressure/proximity + variation/expression + transient + pitch motion
    → bank sensor routing and lane-motion depths
    → tempo/density/rotation/velocity/timbre/ratchet decisions
    → three Euclidean event lanes
    → motif degree → seven-degree scale + global root → sensor octave
    → low-lane Bass/Percussion/Hybrid articulation
    → optional held USB-MIDI chord constraint
    → bass/percussion, pad, and lead note events
    → matching USB and DIN MIDI output
```

The Scale and Motif controls determine pitch material; Euclidean rhythm decides
when each lane can play. Sensor data changes pulse density and rotation rather
than replacing the stored Euclidean step lengths. A held USB MIDI chord is the
last pitch stage: generated notes move to the nearest captured pitch class
without changing the rhythm or approximate register.

### Synthesis and audio flow

```text
low note events → Bass/Percussion PRA32-U ───────┐
                                                  │ dry input
high note events → Lead PRA32-U on RP2040 core 1 ├──┐
                                                  │  │
middle events → Pad PRA32-U on core 0 ───────────┘  │
                    │                                │
                    └── internal pad + dry low/lead ─┘
                         → shared chorus
                         → shared stereo delay
                         → master gain and saturation
                         → 48 kHz stereo PIO/DMA I2S
                         → onboard DAC/audio output
```

There are three distinct monophonic musical parts. A new pitch replaces only
the previous note in its own lane; repeated pitches tie and extend. Bass and
lead bypass their own effects and feed the pad engine's effect stage. Therefore
the chorus and delay are **shared per patch**, not separate per lane and not
global across the whole bank/device. The editor exposes the one effective set
under **Pad + effects → Shared effects**.

### MIDI and display flow

```text
generated note/velocity/duration/ratchet
        ├── internal three-part synth
        ├── USB MIDI output
        └── TRS DIN MIDI output

live cutoff/resonance/modulation/breath/bend
        ├── internal synth control
        └── musician-facing MIDI CC / pitch bend

live amp envelopes + LFO + active lanes + ratchet events
        └── nine-pixel RGB animation
```

Editor SysEx is USB-only and is never forwarded to DIN MIDI. Ordinary incoming
MIDI notes constrain the generated scale; they do not directly play the
internal synth.

## Editing model and top controls

Rotary controls can be dragged vertically or horizontally. The mouse wheel and
arrow keys move one step at a time. Discrete parameters show named choices
instead of raw values.

| Control | Effect |
|---|---|
| Bank / Patch | Selects one of eight banks and sixteen patches per bank. The device follows the editor. |
| Reload | Reads the current device values again. |
| Save patch | Writes the active synth, sequence, and articulation patch to flash. |
| Save bank settings | Writes the active bank's shared interaction settings. |
| Save globals | Writes device-wide performance, MIDI, and LED settings. |
| Unsaved indicator | Shows which ownership scopes differ from their saved state. |

Only one unsaved patch can live in device RAM. Select or export deliberately
before moving away from extensive edits.

## Parameter ownership

| Owner | Stored values | Save action |
|---|---|---|
| Patch | Three synth snapshots, sequence, scale, motif, rhythm, low mode, and six articulation slots | Save patch |
| Bank | Tempo/gate multipliers, sensor routing, density/ratchet response, lane motion, LED colour, inherited low role | Save bank settings |
| Globals | Root, sensitivity, volume, duration, pitch-bend preference, MIDI channel mode, LED brightness | Save globals |
| Live sensor | Four read-only normalized measurements | Never saved |

## Bass tab

The Bass tab edits the stored low-engine timbre used by Bass events and as the
starting point for Hybrid operation.

| Section | Purpose |
|---|---|
| Oscillator 1 | Primary waveform, shape, morph, and sub/noise contribution. |
| Oscillator 2 | Secondary waveform, coarse/fine tuning, and oscillator balance. |
| Filter | Cutoff, resonance, envelope amount, key tracking, and low/high-pass mode. |
| Modulation envelope | Independent attack/decay/sustain/release and oscillator/pitch routing. |
| Voice | Monophonic/legato behaviour, portamento, pitch-bend range, and assignment. |
| LFO | Wave, rate, depth, fade, and oscillator/filter destinations. |
| Amplifier | Gain and audible envelope contour. |
| Expression | Sensor breath and velocity response for filter, envelope, and level. |

FX controls are intentionally absent because this engine is dry. Percussion
events temporarily articulate a bounded subset of these controls per hit; the
stored Bass snapshot remains unchanged.

## Pad + effects tab

The Pad tab edits the middle harmonic voice using the same oscillator, filter,
envelope, voice, LFO, amplifier, and expression sections as Bass. It also owns
the one effective **Shared effects** section:

| Parameter | Purpose |
|---|---|
| Chorus mix | Wet/dry width contribution. |
| Chorus rate | Modulation speed. |
| Chorus depth | Modulation range and stereo movement. |
| Delay feedback | Number and persistence of repeats. |
| Delay time | Repeat spacing. |
| Delay mode | Delay routing, including stereo/ping-pong behaviour. |

These effects process the complete bass/percussion + pad + lead mix. Excessive
feedback or dense, long-release patches can increase masking even when they do
not overload the audio renderer.

## Lead tab

The Lead tab edits the high engine. It shares the same synthesis parameter
families but is a separate snapshot and engine. It commonly uses brighter
filtering, quicker envelopes, more LFO motion, and stronger sensor response.
Like Bass, it feeds the shared effects stage dry and therefore has no effective
per-lane chorus or delay controls.

### Voice modes

The safe editor choices are:

- **Monophonic:** every new pitch retriggers/replaces the lane voice.
- **Legato + glide:** connected notes can slide using Portamento.
- **Legato:** connected notes avoid unnecessary envelope retriggers.

Voice Mode uses a 400 ms settling delay so a quick knob movement results in one
voice reset. PRA32-U polyphonic/paraphonic modes are hidden because this
sequencer owns one note slot per lane; those modes add DSP load without adding
simultaneous sequencer notes.

## Sequence tab

### Scale

Seven scale degrees are stored as semitone offsets from the global Root MIDI
note. The editor displays both the offset and resulting note name. A degree may
extend beyond one octave; the seven values do not have to form a conventional
ascending scale.

The Known scale selector provides Ionian/major, Aeolian/natural minor, Dorian,
Phrygian, Lydian, Mixolydian, Locrian, harmonic/melodic minor, major/minor
pentatonic, blues, and whole-tone starting points. Applying one changes only
the seven offsets.

### Motif

The sixteen motif steps select scale degrees 1–7. They do not store MIDI note
numbers. The editor groups them into four groups of four and displays the
resulting base Pad and Lead note names before sensor octave movement or chord
constraint.

### Clock and Euclidean rhythm

| Parameter | Meaning |
|---|---|
| Tempo | Patch BPM before the bank Tempo multiplier. |
| Swing | Long/short sixteenth-note ratio; 50/50 is straight. |
| Bass/Pad/Lead steps | Euclidean cycle length for each lane, 1–16. |
| Bass/Pad/Lead density | Independently adds or removes requested pulses after sensor/bank density. |
| Bass/Pad/Lead gate | Base duration in sequencer steps. |
| Bass/Pad/Lead ratchets | Lane response to the shared live ratchet drive. |

Pulse counts and rotations are derived at bar boundaries from sensitivity,
bank routing, and current sensor statistics. A ratchet subdivides a generated
event into two to four notes and produces a white LED flash for each actual
repeat.

## Low articulation tab

The low lane can **Inherit bank**, force **Bass**, force **Percussion**, or use
**Hybrid**. Hybrid keeps strong anchors bass-led and probabilistically converts
other low events into percussion. Since both behaviours use one engine, it
alternates events rather than layering bass and kick simultaneously.

Each patch contains six slots. Every slot exposes:

- Sound: kick, tom, snare, closed/open hat, clap, rim/wood, shaker/metal.
- Rhythmic role: anchor, backbeat, offbeat, fill, or free.
- Weight, level, tune, tone, body/noise, decay, transient, and ratchet response.

Weight zero disables a slot. Sensor influence controls how strongly live
proximity/expression changes the articulation. Variation controls bounded
random tone/noise/decay motion and sensor-driven Hybrid movement. See the
[dedicated articulation guide](low-articulation.md) for the selection algorithm,
bank defaults, and starting recipes.

## Bank interaction tab

Bank parameters reshape all sixteen patches in the selected bank without
duplicating their patch records.

| Section | Parameters and effect |
|---|---|
| Clock | Tempo multiplier applied after patch BPM. |
| Sensor routing | Maximum breath, modulation, cutoff, resonance, morph, LFO-rate, and bend movement. |
| Rhythm | Density offset, ratchet response, and gate multiplier. |
| Lane response | Separate Bass, Pad, and Lead motion depth. |
| Display colour | Base red, green, and blue levels for the normal LED animation. |
| Low role | Default Bass/Percussion/Hybrid mode and inherited percussion balance. |

A patch set to Inherit bank follows the bank's low role. Explicit patch modes
ignore the bank mode; an explicit Hybrid patch uses its patch-level balance.

## Globals tab

| Parameter | Effect |
|---|---|
| Root MIDI note | Transposes the stored scale and displayed resulting notes. |
| Sensitivity | Eight sensor-analysis response levels; also contributes to rhythm density and ratchets. |
| Master volume | Twelve post-synth gain steps. |
| Note duration | Global multiplier applied after lane gate and bank gate. |
| Sensor pitch bend | Enables/disables live sensor-derived bend without changing patch or bank. |
| MIDI channels | Channel 1 for all lanes, or channels 1–3 for Bass/Pad/Lead. |
| LED master brightness | Scales the complete nine-pixel display. |

Equivalent hardware gestures update the device immediately. Root and patch
changes are reflected back into an attached editor.

## Sensor monitor tab

The monitor performs read-only polling and never changes the sound by itself.

| Meter | Firmware signal |
|---|---|
| Pressure / proximity | Smoothed absolute position in the learned interval range. |
| Variation / expression | Smoothed coefficient-of-variation movement. |
| Transient | Fast gesture onset with slower decay. |
| Pitch motion | Bipolar relative interval movement, displayed around its normalized centre. |

These are normalized performance features, not raw electrical voltages. A
plant often changes more slowly than direct pad touch; use lower modulation
depths first and increase them after observing the monitor.

## USB MIDI chord constraint

Up to seven distinct held USB MIDI pitch classes can temporarily constrain all
generated pitches. A 50 ms capture window atomically replaces the previous
latched chord. Releasing the keys leaves the constraint active, preventing the
stored scale from leaking between chords. CC 120 or USB disconnect clears it;
CC 123 releases held-key state but keeps the latch.

Chord input is not an editor parameter and is not saved in a patch. The editor
continues to display the stored scale because that remains the source material
used when the chord constraint is cleared.

## Share, backup, and import

**Export patch** writes one `pocket-scion-patch` JSON file. **Export bank**
reads sixteen patches plus their bank settings and writes one
`pocket-scion-bank` file. These files contain parameters only, never firmware
or samples.

Patch imports are loaded into RAM for audition and require **Save patch** to
become persistent. Bank import asks for confirmation, writes all sixteen target
slots, and commits the bank settings because only one unsaved patch can occupy
device RAM.

New exports use JSON version 3. Version 1 and 2 files remain importable; new
fields retain compiled or migrated defaults. Scale offsets use `semitones + 24`, density uses
`offset + 16`, and all other values follow the ranges visible in the editor.
Unknown schemas, invalid values, and incorrect array sizes are rejected before
transmission.

## Revert, restore, and flash safety

| Action | Result |
|---|---|
| Reload | Reads current RAM/saved values without intentionally discarding a device-side state change. |
| Revert unsaved edits | Reloads the newest saved override for the active patch. |
| Restore patch default | Removes the saved override and rebuilds the compiled alternative-firmware patch. |
| Restore factory firmware | Requires flashing Instruō's official UF2; it is outside the editor. |

The device stores settings in a 256 KiB append-only journal. Each 256-byte page
contains a magic value, key, format version, sequence number, payload size, and
CRC-32. Two journal halves allow compaction without erasing the only current
copy. A corrupt or interrupted newest record is ignored in favour of an older
valid record or the compiled default.

Flash operations run outside the per-sample audio path using the Pico SDK's
bounded multicore-safe executor. A failed save aborts instead of waiting
forever. Raw audio mode is deliberately not restored at boot, so a saved
preference cannot make synthesis appear permanently silent.

## A practical patch-design workflow

1. Select a nearby patch and immediately export it as a backup.
2. Set the global Root and patch Scale before detailed oscillator tuning.
3. Shape Bass, Pad, and Lead with low sensor routing so their static identities
   are clear.
4. Program Motif, Euclidean lengths, gates, density, and ratchets.
5. Choose Bass/Percussion/Hybrid and balance the articulation slot weights and
   rhythmic roles.
6. Increase bank sensor routing and watch the Sensor monitor while playing the
   intended pad or plant.
7. Add the shared chorus/delay last so masking does not hide envelope and
   articulation problems.
8. Test low and high pressure, several patch transitions, held MIDI chords,
   and dense ratchets before saving.
9. Save the patch, save bank settings only when intentionally changed, and
   export the finished JSON.

## Further reference

- [Bass, percussion, and hybrid low lane](low-articulation.md)
- [Banks, scenes, and numeric parameter ranges](banks-and-parameters.md)
- [Controls, LEDs, and musician-facing MIDI](controls.md)
- [Firmware architecture](architecture.md)
- [Open USB editor SysEx protocol](editor-protocol.md)
- [Hardware testing checklist](hardware-testing.md)
- [Factory firmware recovery](../README.md#restore-the-factory-firmware)
