# USB MIDI editor protocol

This document is sufficient to implement another Pocket SCION editor without
using this web application. The protocol is USB-only; editor SysEx is never
forwarded to DIN MIDI.

## Frame

Every message is a MIDI SysEx frame:

```text
F0 7D 50 53 VV CC RR [payload ...] ZZ F7
```

- `7D` is MIDI's development/non-commercial manufacturer ID.
- `50 53` is the Pocket SCION product signature (`P`, `S`).
- `VV` is protocol version `01`.
- `CC` is a command or response.
- `RR` is the caller's 0–127 request ID and is echoed by the response.
- All payload values are 7-bit clean. A 14-bit value is low seven bits first.
- `ZZ` makes the sum from `7D` through `ZZ`, modulo 128, equal zero.

The host sends one request at a time. A normal request receives `ACK`, `VALUE`,
or `CAPABILITIES`; errors receive `NACK`. Hosts should use a 1.2-second normal
timeout and allow up to 6 seconds for flash commands.

## Commands

| Code | Command | Payload | Successful response |
|---:|---|---|---|
| `01` | Hello | none | `41` Capabilities |
| `02` | Select patch | target LSB [, target MSB] | `40` ACK |
| `03` | Get parameter | scope, target LSB [, target MSB], lane, parameter | `42` Value |
| `04` | Set live parameter | scope, target LSB [, target MSB], lane, parameter, value LSB, value MSB | `40` ACK |
| `05` | Commit | scope, target LSB [, target MSB] | `40` ACK |
| `06` | Revert | scope, target LSB [, target MSB] | `40` ACK |
| `07` | Restore compiled default | scope, target LSB [, target MSB] | `40` ACK |
| `08` | Sensor snapshot | none | `43` four-value snapshot |

`ACK`/`NACK` payloads contain the original command and status/error code.
`VALUE` echoes scope, target, lane, and parameter followed by the 14-bit value.
`SENSOR SNAPSHOT` contains pressure, expression, transient, and bipolar pitch
motion as four consecutive 14-bit values, low seven bits first. One coherent
packet replaces four separately queued `GET` requests.

Firmware reporting at most 128 patches uses the original one-byte target.
Firmware reporting more than 128 patches uses two 7-bit bytes, least
significant first, for every target-bearing command. Firmware v2.5 accepts
both layouts; this keeps the hosted editor compatible with v2.4 devices.

Capabilities payload is firmware major, minor, patch; patch count LSB/MSB;
bank count; scene-parameter count; shared-patch count; bank-parameter count;
and global-parameter count.
The final capability byte reports the startup-detected flash capacity in MiB.

## Address spaces

Scopes are patch `0`, bank `1`, global `2`, read-only live sensor `3`,
read-only compiled factory patch `4`, and read-only compiled factory bank `5`.
Factory scopes return the firmware defaults without applying any overrides stored in
flash. They are intended for archival/export tools and reject set, commit, revert,
and restore commands.

- Patch targets are 0–255. Lanes 0, 1, and 2 address the 47 packed scene
  parameters in the order documented in
  [Banks and parameters](banks-and-parameters.md#patch-parameters). Lane 3
  addresses 113 shared fields: scale 0–6, motif 7–22, BPM 23, Euclidean lengths
  24–26, swing 27, gates 28–30, Bass density bias 31, ratchet depths 32–34,
  low-role mode/balance/sensor/variation 35–38, and six ten-parameter
  articulation slots at 39–98. Each slot exposes algorithm, role, weight,
  level, tune, tone, noise, decay, transient, and ratchet response. Pad and
  Lead density biases are 99 and 100. Patch-local motion parameters are
  breath override 101, bend response 102, ratchet response 103, amp envelope
  motion 104–106, pressure octave span 107, expression octave threshold 108,
  and cutoff/resonance/morph/LFO-rate motion 109–112. Bass and Lead lane
  parameters 41–46 are unavailable because their packed storage belongs to
  these lane-3 parameters; only Pad lane 1 exposes the effective shared FX.
- Bank targets are 0–15. Parameters are tempo, breath maximum, modulation
  maximum, cutoff range, resonance range, morph range, LFO-rate range, bend
  percent, biased density offset, ratchet percent, gate percent, and motion
  percent for bass, pad, and lead, red/green/blue LED levels, default low-role
  mode, and inherited percussion balance.
- Global target is ignored. Parameters are root, sensitivity index, volume
  index, duration index, pitch-bend enable, multichannel enable, and LED
  brightness.
- Sensor parameters 0–3 are pressure, expression, transient, and bipolar pitch
  motion mapped to 0–1000.

Editors should prefer command `08` while the sensor monitor is visible. A
bounded request/response loop around 20 Hz is fast enough for fluid meters and
keeps sensor UI traffic far below audio and MIDI event bandwidth. For older
firmware that rejects command `08`, fall back to the four individual sensor
`GET` requests at a slower rate.

Ordinary incoming Note, CC, Program Change, and pitch-bend messages remain
ignored. This prevents a DAW or controller from accidentally altering the
generative instrument; only correctly signed and checksummed editor SysEx is
accepted.
