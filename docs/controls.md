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
notes selects how far it reaches through the five rings: one reaches the
centre and all four reach the outside. Its brightness is
read directly from the loudest live PRA32-U amplifier envelope, with subtle
modulation from the patch's real LFO. White appears only when a ratchet actually
fires. Raw mode uses a separate white pulse display.

## MIDI output

The same musical MIDI is sent over USB and the Type-A TRS MIDI output. Incoming
notes and ordinary controllers do not play or change the internal synth. USB
does accept the checksummed SysEx used by the open
[patch editor](editor.md); editor traffic is never forwarded to DIN MIDI.

### Channel modes

- **Single-channel:** every part is sent on MIDI channel 1.
- **Multichannel:** bass, melody, and upper parts use channels 1, 2, and 3.

Toggle the mode with an Instrument triple-press or by holding both Volume
buttons for three seconds.

### Musical output

MIDI includes generated notes and velocities, note durations, ratchets as
repeated notes, bank and patch changes, sensor-controlled pitch bend, and live
filter/expression movement. Up to three notes can sound simultaneously.
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
Program Change messages identify sixteen banks with sixteen patches per bank.
Bank Select CC0 plus Program Change is authoritative across the complete
library; legacy combined-patch CC23 remains limited to banks 0–7.

Raw Output Mode produces the sensor pulse train as audio; it does not send raw
sensor pulses over MIDI, and generated MIDI notes stop while raw mode is active.

## USB MIDI chord constraint

Playing up to seven distinct pitch classes over USB MIDI temporarily replaces
the patch scale as the sequencer's allowed pitch set. Each generated bass, pad,
or lead pitch moves to the nearest captured chord tone while retaining its
rhythmic lane and approximate register. Octave duplicates count as the same
tone; additional distinct tones beyond seven are ignored for that capture.

The chord is latched after its keys are released, preventing patch-scale notes
from leaking through between chord changes. A 50 ms capture window collects a
new chord while the previous chord remains authoritative, then replaces it
atomically. MIDI All Notes Off (CC 123) releases held-key state but keeps the
latch. MIDI All Sound Off (CC 120) or disconnecting USB clears the latch and
restores the stored patch scale.

Chord input is USB-only. The mapped TRS MIDI connection is an output and
continues to carry the constrained generated notes.

The firmware does not send MIDI Clock, transport messages, aftertouch, or
unsolicited SysEx. It sends SysEx only in response to editor requests. External
instruments can follow its notes and expression, but cannot synchronize to its
internal rhythm through MIDI Clock.

See [Banks, scenes, and parameters](banks-and-parameters.md#midi-parameter-output)
for the complete implementation-level message map.
