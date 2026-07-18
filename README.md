# Pocket SCION Synth

An alternative, clean-room firmware for the Instruō Pocket SCION that turns
the RP2040 hardware into a real-time, sensor-driven polyphonic synthesizer.
Instead of selecting prerecorded samples, it generates every sound live with
oscillators, filters, envelopes, LFOs, chorus, delay, and four-voice polyphony.

[![Watch the video](https://img.youtube.com/vi/5qdIYkmIK2Y/maxresdefault.jpg)](https://youtu.be/5qdIYkmIK2Y)

This is an independent community project. It is not affiliated with or
endorsed by Instruō. Pocket SCION and Instruō are trademarks of their
respective owner.

## Highlights

- 128 deliberately designed patches across eight banks
- Three sensor-modulated Euclidean rhythm lanes
- Four-voice PRA32-U synthesis at 48 kHz stereo
- Sensor control of notes, expression, timbre, rhythm, ratchets, and pitch bend
- DIN MIDI and class-compliant USB MIDI output
- Single-channel and three-channel MIDI modes
- Raw sensor pulse output with pressure-to-pitch response
- Nine-pixel RGB display linked to live amp envelopes, LFO, polyphony, and
  ratchets
- No original Pocket SCION samples or firmware code in this repository

## Install the compiled firmware

The ready-to-flash build is
[`releases/pocket_scion_synth_v2.3.0.uf2`](releases/pocket_scion_synth_v2.3.0.uf2).

1. Disconnect the Pocket SCION from USB.
2. Hold its RP2040 boot-selection control while reconnecting USB, or otherwise
   enter the RP2040 USB bootloader.
3. A drive named `RPI-RP2` appears.
4. Copy the UF2 onto that drive.
5. Wait for the drive to disappear and the instrument to reboot.

The SHA-256 digest is:

```text
0b649b349ff7d80dbb2adc0df043c73c1219b6a2c9f8c301b31d10ee8e3f1840
```

Use moderate monitoring volume for the first boot. A short A/E/A startup chord
confirms that the audio path is working before sensor input begins.

## Restore the factory firmware

You can return to Instruō's original sample-based firmware at any time. Download
the official [Pocket SCÍON firmware 1.0.1 UF2](https://www.instruomodular.com/wp-content/uploads/firmware/pocket_scion_v1.0.1.uf2)
from Instruō, enter the RP2040 USB bootloader as described above, and copy that
UF2 to the `RPI-RP2` volume.

If the direct download changes, use Instruō's
[official firmware support page](https://www.instruomodular.com/firmware/) and
select the latest firmware listed under **Pocket SCÍON**. The factory firmware
is linked rather than redistributed by this project.

## How it works

GPIO0 supplies edge timestamps from the biofeedback oscillator. Ten accepted
intervals form one analysis window. Range, variance, standard deviation,
proximity, and trigger statistics continuously reshape a generative sequencer.
Three Euclidean lanes choose notes from a scale and feed a four-voice
[PRA32-U](https://github.com/risgk/digital-synth-pra32-u) engine. The output is
sent to the onboard DAC through an exact-rate PIO/DMA I2S pipeline.

The firmware retains the useful physical interface—buttons, MIDI, raw mode,
and the five-ring RGB artwork—but gives it a new synthesis and sequencing
engine.

## Controls

| Control | Action |
|---|---|
| Sensitivity − / + | Sensor sensitivity |
| Volume − / + | Output volume |
| Instrument, single press | Next patch |
| Instrument, double press | Next bank |
| Instrument, triple press | Toggle single/multichannel MIDI |
| Hold Instrument | Toggle pitch bend bank |
| Hold Instrument + Sensitivity − / + | Note duration |
| Hold Instrument + Volume − / + | Root note by semitone |
| Hold both Sensitivity buttons for 3 s | Toggle Raw Output Mode |
| Hold both Volume buttons for 3 s | Toggle MIDI channel mode |

See [docs/controls.md](docs/controls.md) for display feedback and MIDI details.

## Multi-timbral firmware editor

The [v2.4.0 multi-timbral release](https://github.com/toonhuysmans/pocket-scion-synth/releases/tag/multi-tibral)
includes a browser editor for live USB control of its synthesis, sequencing,
sensor-routing, rhythm, articulation, MIDI, and global parameters. Patches can
be saved to the instrument or exchanged as JSON. [Open the Alternative Synth
Firmware Editor](https://toonhuysmans.github.io/pocket-scion-synth/).

[![Alternative Synth Firmware Editor showing scale and motif controls](docs/images/alternative-synth-firmware-editor.png)](https://toonhuysmans.github.io/pocket-scion-synth/)

## Documentation

- [Platform and peripherals](docs/platform.md)
- [Firmware architecture](docs/architecture.md)
- [Banks, scenes, and parameters](docs/banks-and-parameters.md)
- [Controls, modes, and MIDI](docs/controls.md)
- [Clean-room reverse engineering](docs/reverse-engineering.md)
- [Building from source](docs/building.md)
- [Hardware testing checklist](docs/hardware-testing.md)

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the build,
test, hardware-validation, and clean-room requirements. Coding agents should
also follow the repository instructions in [AGENTS.md](AGENTS.md).

## Project layout

```text
boards/       RP2040 board definition
pio/          I2S and WS2812 PIO programs
src/          platform, sequencer, synthesis adapter, MIDI, and UI
tests/        host-side sensor mathematics tests
vendor/       pinned CC0 PRA32-U DSP headers and provenance
docs/         hardware and implementation documentation
releases/     verified compiled UF2
```

## License

The new platform firmware and documentation are MIT licensed. The vendored
PRA32-U DSP is CC0 1.0 Universal and retains its upstream license and provenance
inside [`vendor/pra32-u`](vendor/pra32-u). See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

No original Pocket SCION firmware image, manual, extracted audio, or sample
content is distributed here.
