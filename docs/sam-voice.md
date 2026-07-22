# SAM voice branch

The `sam-voice` branch adds a fourth sonic role to the three PRA32-U lanes:
the classic Software Automatic Mouth speech synthesizer. It speaks editable
sentences as events inside the plant-driven generative sequence rather than as
pre-recorded audio.

## Signal and scheduling flow

1. Each patch contains ten phrases and ten voice parameters.
2. At the beginning of a bar, phrase density is combined with pressure,
   expression, and transient activity according to **Sensor influence**.
3. A successful trigger chooses one of the ten non-empty phrases while avoiding an
   immediate repeat.
4. Core 1 renders SAM while core 0 continues bass/percussion, pad, effects,
   sensor analysis, MIDI, and the audio DMA clock. The lead is omitted only
   during SAM generation because it normally uses core 1.
5. SAM streams unsigned 8-bit audio at approximately 22.05 kHz through a
   512-byte ring buffer. Core 0 linearly resamples it to 48 kHz and feeds it
   into the shared chorus/delay signal path. Lead synthesis resumes as soon as
   sentence generation finishes, while the buffered voice continues playing.

The streaming design is important on RP2040: the three synth engines already
occupy most of its 264 KB SRAM, and a conventional complete sentence buffer
does not fit. SAM working memory and its stream ring occupy the two RP2040
scratch banks below their respective core stacks.

The English reciter uses a dedicated 256-byte in-place expansion buffer even
though stored sentences are limited to 47 characters. This prevents spelled
letters and punctuation-heavy text from overwriting the voice state when they
expand into longer phoneme strings. A progress watchdog cancels a renderer when
neither generated samples nor playback position advances for five seconds; a
separate 60-second ceiling protects against corrupt active states. Both unwind
the streaming writer so later phrases can still trigger.

SAM's original renderer uses three fixed 60-entry phoneme staging arrays. The
firmware renders a full staging block at 59 entries and then resumes with the
same phoneme, preventing long or highly expanded phrases from writing into
adjacent scratch memory. End-of-phrase and beginning-of-phrase checks also
guard the translated length-adjustment rules from indexing flag tables with a
sentinel. These boundaries are covered by a host AddressSanitizer stress test
using all bundled phrases and extreme voice settings.

## Per-patch voice parameters

| Parameter | Range | Function |
| --- | ---: | --- |
| Enabled | Off / On | Includes or excludes spoken events |
| Voice level | 0–127 | Speech level before shared effects |
| Speed | 1–255 | Native SAM speed; larger values speak more slowly |
| Pitch | 0–255 | Native SAM fundamental pitch control |
| Mouth | 0–255 | Native vocal-tract mouth/formant character |
| Throat | 0–255 | Native vocal-tract throat/formant character |
| Phrase density | 0–127 | Base probability of a phrase at a bar boundary |
| Sensor influence | 0–127 | Amount by which plant/touch dynamics increase that probability |
| Motion chance | 0–127 | Probability that a spoken phrase receives temporary character variation |
| Motion amount | 0–127 | Maximum bounded movement around stored Speed, Pitch, Mouth, and Throat values |

The editor also provides the classic SAM, Elf, Little Robot, Stuffy, Little
Old Lady, and Extra-Terrestrial character settings as starting points. These
presets only set Speed, Pitch, Mouth, and Throat; every value remains editable.
The compiled Voice level default is 25/127, approximately 20%, because SAM's
8-bit output is naturally much louder than the surrounding synth voices.

Each phrase is at most 47 ASCII characters. Text is uppercased before the SAM
reciter converts it to phonemes. Unusual spelling and punctuation can be used
deliberately to alter rhythm and pronunciation.

## Editor protocol extension

Firmware 2.7 reports speech parameter count, phrase count, and phrase length in
the capabilities response. Numeric voice settings use patch lane `4`. SysEx
commands `0x09` and `0x0a` read and write phrase strings in chunks. Patch and
bank JSON schema 5 includes `speech` and `phrases`, while the editor continues
to accept older schema 1–4 files without inventing voice data.

Phrase target `0x3fff` is a read-only alias for the active patch. The editor
uses this alias while refreshing its performance view, so a delayed phrase
read cannot switch the instrument back to a previously selected patch.

Voice data is stored separately from the existing 236-byte patch record. The
original four-phrase record remains prefix-compatible, while phrases 5–10 use
two extension records. The enlarged flash journal still scans the former store
region, so existing patch, bank, and four-phrase voice overrides migrate
without being erased.

## SAM provenance and licensing note

The adapted source in `vendor/sam/` comes from
[`s-macke/SAM`](https://github.com/s-macke/SAM), pinned to commit
`a7b36efac730957b59471a42a45fd779f94d77dd`. That upstream project describes
SAM as reverse-engineered abandonware and does not provide an explicit
open-source license. This experimental branch preserves the source provenance,
but downstream redistributors should make their own licensing assessment.
