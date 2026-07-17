# Controls, modes, and MIDI

All buttons are active-low and debounced in firmware. Paired controls begin
repeating after a 400 ms hold and repeat every 120 ms.

## Direct controls

| Gesture | Action | Display |
|---|---|---|
| Sensitivity − / + | Set sensor sensitivity, 8 steps | Green radial level |
| Volume − / + | Set output level, 12 steps | Yellow radial level |
| Instrument press | Next patch | Patch colour |
| Instrument double press | Next bank | Bank/patch colour |
| Instrument triple press | Toggle MIDI channel mode | Green single / red multi |
| Hold Instrument ~0.9 s | Toggle pitch-bend layer | Pitch-bend colour accent |

Single and double presses resolve after a 320 ms multi-click window.

## Shift controls

Hold Instrument while pressing:

| Chord | Action | Display |
|---|---|---|
| Instrument + Sensitivity − / + | Note duration | Cyan radial level |
| Instrument + Volume − / + | Root note by semitone | Purple radial level |

## Long chords

| Gesture | Action |
|---|---|
| Hold both Sensitivity buttons for 3 s | Toggle Raw Output Mode |
| Hold both Volume buttons for 3 s | Toggle single/multichannel MIDI |

## Live RGB behavior

The normal animation uses one saturated patch colour. The number of active
parts selects how far it reaches through the five rings: one reaches the
centre, two the middle radius, and all three the outside. Its brightness is
read directly from the loudest live PRA32-U amplifier envelope, with subtle
modulation from the patch's real LFO. White appears only when a ratchet actually
fires. Raw mode uses a separate white pulse display.

## MIDI output

The same MIDI is sent over USB and the Type-A TRS MIDI output. The firmware is
currently MIDI output only: incoming notes and controls do not play or change
the internal synth.

### Channel modes

- **Single-channel:** every part is sent on MIDI channel 1.
- **Multichannel:** bass, melody, and upper parts use channels 1, 2, and 3.

Toggle the mode with an Instrument triple-press or by holding both Volume
buttons for three seconds.

### Musical output

MIDI includes generated notes and velocities, note durations, ratchets as
repeated notes, bank and patch changes, sensor-controlled pitch bend, and live
filter/expression movement. Up to four notes can sound simultaneously.
Repeated pitches may be tied into longer notes rather than retriggered.

| MIDI message | Musical function |
|---|---|
| CC 1 | Sensor modulation |
| CC 2 | Breath/expression |
| CC 7 | Volume |
| CC 20 | Sensor sensitivity |
| CC 21 | Note duration |
| CC 22 | Root note |
| CC 71 | Filter resonance |
| CC 74 | Filter cutoff |
| Pitch Bend | Sensor-derived pitch movement when enabled |

These messages allow an external synth, DAW, or visual system to follow the
same plant-driven movement as the internal synth. Standard Bank Select and
Program Change messages identify the eight banks and sixteen patches per bank.

Raw Output Mode produces the sensor pulse train as audio; it does not send raw
sensor pulses over MIDI, and generated MIDI notes stop while raw mode is active.

The firmware does not currently send MIDI Clock, transport messages,
aftertouch, or SysEx. External instruments can follow its notes and expression,
but cannot synchronize to its internal rhythm through MIDI Clock.

See [Banks, scenes, and parameters](banks-and-parameters.md#midi-parameter-output)
for the complete implementation-level message map.
