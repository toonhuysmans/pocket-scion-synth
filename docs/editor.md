# USB patch editor

The Pocket SCION Editor is a small installable web application in [`editor/`](../editor).
It controls this alternative firmware over class-compliant USB MIDI SysEx. It
does not work with Instruō's factory firmware.

## Browser and connection

Use current desktop Chrome or Edge. Web MIDI SysEx requires an HTTPS page (or
`localhost`) and explicit browser permission. Safari and iOS do not currently
provide the required Web MIDI interface.

1. Connect Pocket SCION normally over USB; do not enter the UF2 bootloader.
2. Open the hosted editor, or run `npm install && npm run dev` in `editor/`.
3. Select **Connect USB MIDI** and approve MIDI/SysEx access.
4. Choose a bank and patch. The device selects it and the editor reads its
   current saved or compiled values.

Patch and bank changes made with the Instrument button are sent over USB MIDI
CC 23. The editor follows them automatically, updates both selectors, and
loads the newly active patch. Rapid changes cancel stale parameter reads so
the final device selection remains authoritative.

Rotary knobs audition changes immediately in RAM. Drag vertically or
horizontally, use the mouse wheel for single steps, or use the arrow keys.
Changes are not persistent until
**Save patch**, **Save bank settings**, or **Save globals** is pressed. Revert
reloads the last saved state. Restore removes the selected flash override and
returns to the compiled alternative-firmware default.

Voice Mode is a discrete selector rather than a continuous MIDI value. The
editor exposes the three modes that fit this firmware's real-time budget:
Monophonic, Legato + glide, and Legato. It applies a 400 ms settling delay so
dragging produces one final voice reset instead of a burst of `all notes off`
transitions. PRA32-U polyphonic/paraphonic modes are intentionally unavailable:
the sequencer currently has one note slot per lane, so they add DSP load but no
additional simultaneous notes.

## Parameter ownership

- A patch owns explicit bass, pad, and lead snapshots, plus scale, motif,
  tempo, swing, Euclidean step counts, lane gates, density, ratchets, low-role
  selection, and six configurable percussion articulation slots.
- A bank owns tempo/gate multipliers, sensor routing, density/ratchet response,
  the sensor-motion depth of each lane, its main LED colour, and the inherited
  low-role mode and percussion balance.
- Globals own root, sensitivity, volume, duration, pitch-bend preference, MIDI
  channel mode, and LED master brightness.

The pad tab owns the one effective **Shared effects** section. Bass and lead
are dry inputs to that chorus/delay stage, so their redundant packed FX values
are hidden. Runtime sensor modulation and low-part per-note articulation remain additive.
See [Bass, percussion, and hybrid low lane](low-articulation.md) for the full
description of modes, slot parameters, rhythmic roles, and practical recipes.
Raw mode is deliberately not restored at boot, so a saved preference cannot
make the synthesizer appear silent after power-up.

The Sequence tab offers known seven-degree scale presets: the seven diatonic
modes, harmonic and melodic minor, major/minor pentatonic, blues, and whole
tone. Applying a preset changes only the seven scale offsets; the global root,
motif, rhythm, and synthesis parameters remain unchanged. The degree knobs can
still be edited afterward to create a custom scale.

## JSON files

`pocket-scion-patch` files contain one patch and can be auditioned before an
explicit save. `pocket-scion-bank` files contain sixteen patches plus the
bank's shared interaction settings. Importing a bank asks for confirmation and
saves all sixteen slots because only one unsaved patch can live in device RAM.

New exports are UTF-8 JSON with `version: 2`. The editor also imports version 1
files and preserves compiled defaults for fields that did not yet exist. Values use the same integer
ranges shown by the editor. Scale offsets are encoded as `semitones + 24`, and
density offsets as `offset + 16`. Unknown schemas or invalid array sizes are
rejected before transmission.

## Persistent storage and recovery

At startup the firmware reads the fitted flash chip's JEDEC capacity instead
of assuming the RP2040's maximum address space. It reserves a compact 256 KiB
journal at the end of that detected device. Records are 256-byte append-only
pages with a magic value, key, format version, sequence number, payload size,
and CRC-32. Two 128 KiB halves allow the newest value of every key to be copied
before an old half is erased during the infrequent compaction cycle.

A corrupt or interrupted newest record is ignored in favour of the preceding
valid record. If neither copy is valid, the compiled defaults load. Flash
operations use the Pico SDK's timeout-bounded multicore-safe executor; a failed
lockout aborts the save instead of hanging the instrument. No flash work occurs
in the per-sample renderer.

Restoring the official Instruō UF2 remains possible as described in the main
[README](../README.md#restore-the-factory-firmware).
