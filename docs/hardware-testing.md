# Hardware testing

Use this checklist after firmware changes that can affect sound, timing,
sensor interaction, controls, MIDI, LEDs, raw mode, or boot behavior. Record
the commit and test conditions so results can be reproduced.

## Safety and preparation

- Keep a copy or link to Instruō's
  [official Pocket SCÍON firmware](https://www.instruomodular.com/firmware/)
  available for recovery.
- Start with moderate headphone, interface, or monitor gain. This firmware can
  produce high output levels and bright transients.
- Disconnect batteries when repeatedly entering the USB bootloader unless the
  hardware setup specifically requires them.
- Confirm that the mounted boot volume is the intended Pocket SCION before
  copying a UF2.
- Note the commit, UF2 SHA-256, power source, audio path, MIDI connection, and
  sensor source.

Suggested record:

```text
Commit:
UF2 SHA-256:
Power:
Audio connection:
USB host/OS:
DIN MIDI receiver:
Sensor source:
Tester/date:
```

## 1. Boot and recovery

- Enter the RP2040 bootloader and confirm the `RPI-RP2` volume appears.
- Copy the candidate UF2 and confirm the volume unmounts and the unit reboots.
- Confirm the A/E/A startup chord is clean and lasts approximately two
  seconds.
- Power-cycle once over USB and once on batteries, when batteries are
  available.
- Re-enter the bootloader after testing to confirm recovery remains possible.

Fail if copying consistently stalls, the board boot-loops, the startup chord
clicks, or recovery requires undocumented steps.

## 2. Baseline audio

- Listen at the default volume with no sensor contact for at least one minute.
- Check both stereo channels.
- Listen for DC thumps, periodic ticks, rattling, buffer underruns, excessive
  hiss, stuck notes, or a changing noise floor.
- Step through several quiet, percussive, atmospheric, and effect-heavy
  patches.
- Raise and lower volume through all twelve steps, including mute and maximum.

Maximum volume may clip downstream equipment; raise it gradually. Fail for
firmware-generated periodic clicking, channel loss, runaway feedback, or
persistent notes after changing patches.

## 3. Sensor response

- Test no contact, light contact, and gradually increasing pressure/contact.
- Confirm activity increases musically rather than turning only into pulse
  density or glitching.
- Confirm pitch/register, expression, cutoff, resonance, morph, LFO rate,
  density, velocity, and ratchet response vary across several banks.
- Hold stable contact and check that the sound settles instead of chattering.
- Make fast changes and check for clicks or audio starvation.
- Exercise all eight sensitivity levels.

Pay special attention to high pressure with all three simultaneous notes,
where earlier builds exposed clicking.

## 4. Voice separation and envelopes

- Reach one, two, and three simultaneous notes and observe the growing LED
  radius.
- Confirm bass/percussion, pad, and lead remain separately monophonic.
- Confirm repeated pitches tie and extend rather than repeatedly restarting at
  a quiet attack.
- Test short Percussive envelopes and long Atmosphere envelopes.
- In every bank, audition Bass, Percussion, Hybrid, and Inherit low-lane modes.
- Set each articulation slot to kick, tom, snare, closed/open hat, clap,
  rim/wood, and shaker/metal in turn. Verify zero weight disables it and role,
  tune, tone, noise, decay, transient, level, and ratchet edits are audible.
- Confirm Hybrid keeps strong anchors bass-led and becomes more percussive as
  balance and sensor influence rise, without clicks or stuck notes.
- Check Foundation/Legacy Glass Pluck, Acid Teeth, and Pitch-envelope
  Percussion under low and high sensor activity.
- Confirm patch changes and MIDI-mode changes send note-offs and leave no stuck
  voices.
- Send USB MIDI chords containing one through seven distinct pitch classes.
  Confirm every generated lane stays on a captured pitch class, the previous
  chord remains active between changes, and a new chord replaces it without a
  patch-scale note leaking through. Confirm CC 120 restores the patch scale.

## 5. Rhythm and ratchets

- Let each tested patch run for at least four bars.
- Confirm the three Euclidean lanes form a changing but stable groove.
- Compare low and high sensor activity for density and rotation changes.
- Confirm ratchets vary between two and four subdivisions and occur more often
  in Percussive/Extreme than Atmosphere.
- Confirm white LED flashes occur only when a ratchet actually fires.
- Listen for clicks at every ratchet intensity and with all three notes active.

## 6. Controls

Verify every gesture without assuming front-panel labels match GPIO order:

| Gesture | Expected result |
|---|---|
| Sensitivity − / + | Eight sensitivity steps; green radial feedback |
| Volume − / + | Twelve volume steps; yellow radial feedback |
| Instrument single press | Next patch |
| Instrument double press | Next bank |
| Instrument triple press | Toggle single/multichannel MIDI |
| Hold Instrument | Toggle sensor pitch bend |
| Instrument + Sensitivity − / + | Duration down/up; cyan feedback |
| Instrument + Volume − / + | Root semitone down/up; purple feedback |
| Both Sensitivity buttons for 3 s | Toggle Raw Output Mode |
| Both Volume buttons for 3 s | Toggle MIDI channel mode |

Check short presses, long holds, repeat behavior, and quick transitions. Fail
if Volume +/− are reversed, Sensitivity + invokes Instrument, or shifted
gestures also trigger their unshifted actions.

## 7. RGB display

- Confirm all nine pixels can illuminate and no pixel remains permanently red.
- Confirm the normal animation keeps one saturated main colour with brightness
  variation rather than rapid multicolour flicker.
- Confirm the illuminated radius follows simultaneous active voices.
- Confirm envelope motion is immediate enough to avoid a delayed afterglow.
- Confirm LFO motion is restrained and the ground tone changes slowly.
- Confirm white accents are ratchet-only.
- Change sensitivity, volume, duration, root, bank, patch, pitch bend, MIDI
  mode, and raw mode and verify their overlays are distinct and temporary.

## 8. MIDI

Test USB MIDI and DIN MIDI independently, then together.

- In single-channel mode, confirm notes use MIDI channel 1.
- In multichannel mode, confirm bass, melody, and upper lanes use channels
  1, 2, and 3.
- Confirm notes, velocities, ratchets, matching note-offs, and pitch bend.
- Monitor CC 0/32, Program Change, and CC 23 on bank/patch changes.
- Monitor CC 20, 7, 21, and 22 for sensitivity, volume, duration, and root.
- Monitor live CC 74, 71, 1, and 2 for cutoff, resonance, modulation, and
  breath.
- Switch programs, MIDI modes, and raw mode repeatedly and check for stuck
  notes.
- Connect the web editor in desktop Chrome/Edge and confirm discovery reports
  the expected firmware and all 128 patch slots.
- Move representative oscillator, filter, envelope, effect, sequence, sensor,
  LED, and global controls while all three lanes and ratchets are active;
  listen for clicks, starvation, or stuck notes.
- Save a patch, its bank settings, and globals, then power-cycle and confirm
  exact recall. Revert unsaved changes and restore the compiled patch default.
- Export/import patch and bank JSON, interrupt one save by removing power, and
  verify the previous valid record or compiled default still boots.
- Confirm editor SysEx never appears at DIN MIDI and ordinary incoming MIDI
  notes/controllers remain ignored.

## 9. Raw Output Mode

- Enter raw mode and confirm all synth notes and pending MIDI notes stop.
- Listen for a continuous pulse train whose audible pitch rises as contact or
  pressure increases.
- Confirm it does not behave only as constant-pitch clicks with increasing
  density.
- Check latency; response should arrive within approximately one 256-frame
  window plus I/O buffering.
- Remove the sensor and confirm stale pulses time out to silence.
- Exit raw mode and confirm synthesis, controls, LEDs, and MIDI recover.

## 10. Stress test

Run for at least fifteen minutes with:

- high sensor activity;
- all three notes active;
- pitch bend enabled;
- an effect-heavy or Extreme patch;
- DIN and USB MIDI connected;
- active LEDs and frequent ratchets;
- repeated bank, patch, duration, and volume changes.

Pass only if audio remains continuous, controls remain responsive, MIDI has no
stuck notes, the LEDs remain synchronized, and the device can still enter its
bootloader afterward.

## Reporting a result

Report each section as pass, fail, or not tested. Include exact reproduction
steps for failures and, when useful, a short audio recording or MIDI log as a
pull-request attachment rather than committing large media files.
