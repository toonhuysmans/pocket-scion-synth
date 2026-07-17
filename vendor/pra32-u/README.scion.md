# Vendored PRA32-U DSP core

This directory contains the DSP headers required from Digital Synth PRA32-U
v3.3.2, pinned to upstream commit:

`05922ba6c1905b98a880a9b7af9e31bb9995254d`

Upstream: <https://github.com/risgk/digital-synth-pra32-u>

The upstream Arduino application, board drivers, control panel, MIDI stack,
EEPROM support, scripts, and binary artifacts are intentionally excluded.
Pocket SCION uses the upstream synthesis engine behind its own Pico SDK sensor,
control, MIDI, and PIO/DMA I2S layers. The unmodified upstream license is in
`LICENSE`.

The first SCION adapter marked generated lookup tables as read-only, keeping
approximately 120 KiB in XIP flash. Hardware testing exposed severe clicking:
PRA32-U's two signal-processing cores contended for XIP during per-sample
oscillator and filter lookups. The hot oscillator, filter, envelope, LFO, and
chorus tables now retain upstream's writable declarations so startup copies
them into SRAM. Their values are unchanged.

The SCION adapter also parameterizes the synth with an effects-bypass template
for compact dry parts and exposes read-only envelope/LFO visualization taps.
On this multitimbral branch, the monophonic process path omits upstream's
secondary-core request/wait handshake because that branch performs no
secondary-core DSP work. Polyphonic and paraphonic paths retain the upstream
two-core behavior.

All per-sample PRA32-U code remains in SRAM. In particular, the shared dry-part
specialization must not be moved to XIP flash: hardware testing showed audible
cache-miss clicking even with lookup tables retained in SRAM.

The monophonic low-rate scheduler skips oscillator, filter, amplifier, and
envelope updates for inactive voices 1–3. Pocket SCION renders its independent
upper part on core 1 instead of asking PRA32-U to calculate unused voices.
