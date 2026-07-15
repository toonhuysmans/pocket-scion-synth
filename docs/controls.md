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
| Hold Instrument ~0.9 s | Toggle pitch-bend bank | Pitch-bend colour accent |

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
voices selects how far it reaches through the five rings. Its brightness is
read directly from the loudest live PRA32-U amplifier envelope, with subtle
modulation from the patch's real LFO. White appears only when a ratchet actually
fires. Raw mode uses a separate white pulse display.

## MIDI output

Both physical DIN MIDI on GPIO16 and USB MIDI are sent simultaneously.

- Single-channel mode: MIDI channel 1
- Multichannel mode: rhythm lanes on MIDI channels 1, 2, and 3
- Notes and ratchets include velocity and matching note-off messages
- Pitch-bend mode sends 14-bit pitch bend
- Live timbre motion is mirrored with useful MIDI CC messages
- Mode/program transitions send All Notes Off where necessary
