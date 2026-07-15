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
