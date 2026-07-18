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
On this multitimbral branch, every engine renders all of its own oscillator and
filter voices locally. The firmware separately assigns the complete lead engine
to RP2040 core 1, so enabling PRA32-U's internal secondary-core request/wait
handshake would nest two schedulers and deadlock bass or pad in polyphonic and
paraphonic modes. `PRA32_U_EMULATION` selects upstream's full voice-mode paths
without enabling that internal split; it does not add an emulator runtime.

All per-sample PRA32-U code remains in SRAM. In particular, the shared dry-part
specialization must not be moved to XIP flash: hardware testing showed audible
cache-miss clicking even with lookup tables retained in SRAM.

The monophonic low-rate scheduler skips oscillator, filter, amplifier, and
envelope updates for inactive voices 1–3. Core 1 renders the independent lead;
core 0 renders bass/percussion followed by pad and effects.
