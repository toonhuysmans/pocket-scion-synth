export interface Parameter {
  id: number;
  name: string;
  section: string;
  min: number;
  max: number;
  display?: (value: number) => string;
  settleMs?: number;
  values?: readonly { value: number; label: string }[];
}

const noteNames = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];

export function midiNoteName(note: number): string {
  const value = Math.max(0, Math.min(127, Math.round(note)));
  return `${noteNames[value % 12]}${Math.floor(value / 12) - 1}`;
}

export const scalePresets = [
  { name: "Major / Ionian", offsets: [0, 2, 4, 5, 7, 9, 11] },
  { name: "Natural minor / Aeolian", offsets: [0, 2, 3, 5, 7, 8, 10] },
  { name: "Dorian", offsets: [0, 2, 3, 5, 7, 9, 10] },
  { name: "Phrygian", offsets: [0, 1, 3, 5, 7, 8, 10] },
  { name: "Lydian", offsets: [0, 2, 4, 6, 7, 9, 11] },
  { name: "Mixolydian", offsets: [0, 2, 4, 5, 7, 9, 10] },
  { name: "Locrian", offsets: [0, 1, 3, 5, 6, 8, 10] },
  { name: "Harmonic minor", offsets: [0, 2, 3, 5, 7, 8, 11] },
  { name: "Melodic minor", offsets: [0, 2, 3, 5, 7, 9, 11] },
  { name: "Major pentatonic", offsets: [0, 2, 4, 7, 9, 12, 14] },
  { name: "Minor pentatonic", offsets: [0, 3, 5, 7, 10, 12, 15] },
  { name: "Blues", offsets: [0, 3, 5, 6, 7, 10, 12] },
  { name: "Whole tone", offsets: [0, 2, 4, 6, 8, 10, 12] },
] as const;

const sceneNames = [
  ["Oscillator 1", "Wave"], ["Oscillator 1", "Shape"], ["Oscillator 1", "Morph"], ["Oscillator 1", "Sub / noise mix"],
  ["Oscillator 2", "Wave"], ["Oscillator 2", "Coarse tuning"], ["Oscillator 2", "Fine pitch"], ["Oscillator 2", "Oscillator mix"],
  ["Filter", "Cutoff"], ["Filter", "Resonance"], ["Filter", "Envelope amount"], ["Filter", "Key tracking"],
  ["Modulation envelope", "Attack"], ["Modulation envelope", "Decay"], ["Modulation envelope", "Sustain"], ["Modulation envelope", "Release"],
  ["Modulation envelope", "Oscillator amount"], ["Modulation envelope", "Oscillator destination"],
  ["Voice", "Voice mode"], ["Voice", "Portamento"],
  ["LFO", "Wave"], ["LFO", "Rate"], ["LFO", "Depth"], ["LFO", "Fade"], ["LFO", "Oscillator amount"], ["LFO", "Oscillator destination"], ["LFO", "Filter amount"],
  ["Amplifier", "Gain"], ["Amplifier", "Attack"], ["Amplifier", "Decay"], ["Amplifier", "Sustain"], ["Amplifier", "Release"],
  ["Filter", "Mode"], ["Amplifier", "Envelope modulation"], ["Amplifier", "Release equals decay"], ["Voice", "Pitch-bend range"],
  ["Expression", "Breath to filter"], ["Expression", "Breath to amplifier"], ["Expression", "Envelope velocity"], ["Expression", "Amplifier velocity"], ["Voice", "Assignment mode"],
  ["Chorus", "Mix"], ["Chorus", "Rate"], ["Chorus", "Depth"], ["Delay", "Feedback"], ["Delay", "Time"], ["Delay", "Mode"],
] as const;

const safeVoiceModes = [
  { value: 2, label: "Monophonic" },
  { value: 4, label: "Legato + glide" },
  { value: 5, label: "Legato" },
] as const;

export const sceneParameters: Parameter[] = sceneNames.map(([section, name], id) => id === 18
  ? { id, name, section, min: 2, max: 5, values: safeVoiceModes, settleMs: 400 }
  : { id, name, section, min: 0, max: 127 });

export const patchSharedParameters: Parameter[] = [
  ...Array.from({ length: 7 }, (_, id) => ({ id, name: `Scale degree ${id + 1}`, section: "Scale", min: 0, max: 48, display: (v: number) => `${v - 24} st` })),
  ...Array.from({ length: 16 }, (_, index) => ({ id: index + 7, name: `Motif step ${index + 1}`, section: "Motif", min: 0, max: 6 })),
  { id: 23, name: "Tempo", section: "Clock", min: 40, max: 240, display: (v: number) => `${v} BPM` },
  ...["Bass steps", "Pad steps", "Lead steps"].map((name, index) => ({ id: index + 24, name, section: "Euclidean rhythm", min: 1, max: 16 })),
  { id: 27, name: "Swing", section: "Clock", min: 50, max: 75, display: (v: number) => `${v}/${100 - v}` },
  ...["Bass gate", "Pad gate", "Lead gate"].map((name, index) => ({ id: index + 28, name, section: "Gates", min: 8, max: 128, display: (v: number) => `${(v / 32).toFixed(2)} steps` })),
  { id: 31, name: "Bass density", section: "Euclidean rhythm", min: 8, max: 24, display: (v: number) => `${v - 16}` },
  ...["Bass ratchets", "Pad ratchets", "Lead ratchets"].map((name, index) => ({ id: index + 32, name, section: "Ratchets", min: 0, max: 200, display: (v: number) => `${v}%` })),
  { id: 35, name: "Low-lane mode", section: "Low role", min: 0, max: 3, values: [
    { value: 0, label: "Inherit bank" }, { value: 1, label: "Bass" },
    { value: 2, label: "Percussion" }, { value: 3, label: "Hybrid" },
  ] },
  { id: 36, name: "Percussion balance", section: "Low role", min: 0, max: 127, display: v => `${Math.round(v / 127 * 100)}%` },
  { id: 37, name: "Sensor influence", section: "Low role", min: 0, max: 127, display: v => `${Math.round(v / 127 * 100)}%` },
  { id: 38, name: "Variation", section: "Low role", min: 0, max: 127, display: v => `${Math.round(v / 127 * 100)}%` },
  ...Array.from({ length: 6 }, (_, slot) => {
    const start = 39 + slot * 10;
    const section = `Articulation ${slot + 1}`;
    return [
      { id: start, name: "Sound", section, min: 0, max: 7, values: [
        "Kick", "Tom", "Snare", "Closed hat", "Open hat", "Clap", "Rim / wood", "Shaker / metal",
      ].map((label, value) => ({ value, label })) },
      { id: start + 1, name: "Rhythmic role", section, min: 0, max: 4, values: [
        "Anchor", "Backbeat", "Offbeat", "Fill", "Free",
      ].map((label, value) => ({ value, label })) },
      { id: start + 2, name: "Weight", section, min: 0, max: 127 },
      { id: start + 3, name: "Level", section, min: 0, max: 127 },
      { id: start + 4, name: "Tune", section, min: 0, max: 48, display: (v: number) => `${v - 24 >= 0 ? "+" : ""}${v - 24} st` },
      { id: start + 5, name: "Tone", section, min: 0, max: 127 },
      { id: start + 6, name: "Body / noise", section, min: 0, max: 127, display: (v: number) => `${Math.round(v / 127 * 100)}% noise` },
      { id: start + 7, name: "Decay", section, min: 0, max: 127 },
      { id: start + 8, name: "Transient", section, min: 0, max: 127 },
      { id: start + 9, name: "Ratchet response", section, min: 0, max: 127 },
    ];
  }).flat(),
  { id: 99, name: "Pad density", section: "Euclidean rhythm", min: 8, max: 24, display: (v: number) => `${v - 16}` },
  { id: 100, name: "Lead density", section: "Euclidean rhythm", min: 8, max: 24, display: (v: number) => `${v - 16}` },
  { id: 101, name: "Breath maximum override", section: "Patch sensor routing", min: 0, max: 127, display: v => v === 0 ? "Use bank" : `${v} / 127` },
  { id: 102, name: "Pitch-bend response", section: "Patch sensor routing", min: 0, max: 200, display: v => `${v}%` },
  { id: 103, name: "Ratchet response", section: "Patch sensor routing", min: 0, max: 200, display: v => `${v}%` },
  { id: 104, name: "Amp decay motion", section: "Patch envelope motion", min: 0, max: 127 },
  { id: 105, name: "Amp sustain motion", section: "Patch envelope motion", min: 0, max: 127 },
  { id: 106, name: "Amp release motion", section: "Patch envelope motion", min: 0, max: 127 },
  { id: 107, name: "Pressure octave span", section: "Patch register", min: 0, max: 2, display: v => `${v} octave${v === 1 ? "" : "s"}` },
  { id: 108, name: "Expression octave threshold", section: "Patch register", min: 0, max: 63, display: v => v === 0 ? "Off" : `${Math.round(v / 63 * 100)}%` },
  { id: 109, name: "Cutoff motion", section: "Patch sensor routing", min: 0, max: 200, display: v => `${v}%` },
  { id: 110, name: "Resonance motion", section: "Patch sensor routing", min: 0, max: 200, display: v => `${v}%` },
  { id: 111, name: "Morph motion", section: "Patch sensor routing", min: 0, max: 200, display: v => `${v}%` },
  { id: 112, name: "LFO-rate motion", section: "Patch sensor routing", min: 0, max: 200, display: v => `${v}%` },
];

export const bankParameters: Parameter[] = [
  ["Tempo multiplier", "Clock", 40, 200], ["Breath maximum", "Sensor routing", 0, 127],
  ["Modulation maximum", "Sensor routing", 0, 127], ["Cutoff range", "Sensor routing", 0, 127],
  ["Resonance range", "Sensor routing", 0, 127], ["Morph range", "Sensor routing", 0, 127],
  ["LFO rate range", "Sensor routing", 0, 127], ["Bend response", "Sensor routing", 0, 200],
  ["Density offset", "Rhythm", 8, 24], ["Ratchet response", "Rhythm", 0, 200],
  ["Gate multiplier", "Rhythm", 25, 200], ["Bass motion", "Lane response", 0, 200],
  ["Pad motion", "Lane response", 0, 200], ["Lead motion", "Lane response", 0, 200],
  ["LED red", "Display colour", 0, 127], ["LED green", "Display colour", 0, 127],
  ["LED blue", "Display colour", 0, 127],
  ["Default low-lane mode", "Low role", 1, 3], ["Percussion balance", "Low role", 0, 127],
].map(([name, section, min, max], id) => ({ id, name: String(name), section: String(section), min: Number(min), max: Number(max), display: id === 8 ? (v: number) => `${v - 16}` : [0, 7, 9, 10, 11, 12, 13].includes(id) ? (v: number) => `${v}%` : undefined }));

bankParameters[17].values = [
  { value: 1, label: "Bass" }, { value: 2, label: "Percussion" },
  { value: 3, label: "Hybrid" },
];
bankParameters[18].display = value => `${Math.round(value / 127 * 100)}%`;

export const globalParameters: Parameter[] = [
  { id: 0, name: "Root MIDI note", section: "Performance", min: 24, max: 72, display: value => `${midiNoteName(value)} · MIDI ${value}` },
  { id: 1, name: "Sensitivity", section: "Performance", min: 0, max: 7, display: v => `${v + 1} / 8` },
  { id: 2, name: "Master volume", section: "Performance", min: 0, max: 11, display: v => `${v + 1} / 12` },
  { id: 3, name: "Note duration", section: "Performance", min: 0, max: 7, display: v => `${v + 1} / 8` },
  { id: 4, name: "Sensor pitch bend", section: "MIDI", min: 0, max: 1, display: v => v ? "On" : "Off" },
  { id: 5, name: "MIDI channels", section: "MIDI", min: 0, max: 1, display: v => v ? "Channels 1–3" : "Channel 1" },
  { id: 6, name: "LED master brightness", section: "Display", min: 0, max: 127 },
  { id: 7, name: "Response window", section: "Sensor calibration", min: 4, max: 24, display: v => `${v} intervals` },
  { id: 8, name: "Noise rejection", section: "Sensor calibration", min: 500, max: 10000, display: v => `${v} µs` },
  { id: 9, name: "Adaptive normalization", section: "Sensor calibration", min: 0, max: 100, display: v => `${v}%` },
  { id: 10, name: "Pressure smoothing", section: "Sensor calibration", min: 1, max: 100, display: v => `${v}%` },
  { id: 11, name: "Expression smoothing", section: "Sensor calibration", min: 1, max: 100, display: v => `${v}%` },
  { id: 12, name: "Variation gain", section: "Sensor calibration", min: 1, max: 200, display: v => `${(v / 10).toFixed(1)}×` },
  { id: 13, name: "Transient gain", section: "Sensor calibration", min: 0, max: 200, display: v => `${v}%` },
  { id: 14, name: "Transient decay", section: "Sensor calibration", min: 1, max: 100, display: v => `${v}%` },
  { id: 15, name: "Activity timeout", section: "Sensor calibration", min: 100, max: 10000, display: v => `${(v / 1000).toFixed(1)} s` },
  { id: 16, name: "Calibration learning", section: "Sensor calibration", min: 0, max: 1, display: v => v ? "Learn" : "Freeze" },
  { id: 17, name: "Calibration recovery", section: "Sensor calibration", min: 1, max: 50, display: v => `${(v / 10).toFixed(1)}% / window` },
];

export const voiceParameters: Parameter[] = [
  { id: 0, name: "Enabled", section: "Voice trigger", min: 0, max: 1,
    values: [{ value: 0, label: "Off" }, { value: 1, label: "On" }] },
  { id: 1, name: "Voice level", section: "Voice trigger", min: 0, max: 127,
    display: value => `${Math.round(value / 127 * 100)}%` },
  { id: 2, name: "Speed", section: "SAM voice", min: 1, max: 255,
    display: value => `${value} · ${value < 55 ? "fast" : value > 90 ? "slow" : "natural"}` },
  { id: 3, name: "Pitch", section: "SAM voice", min: 0, max: 255 },
  { id: 4, name: "Mouth", section: "SAM voice", min: 0, max: 255 },
  { id: 5, name: "Throat", section: "SAM voice", min: 0, max: 255 },
  { id: 6, name: "Phrase density", section: "Voice trigger", min: 0, max: 127,
    display: value => `${Math.round(value / 127 * 100)}% base chance / bar` },
  { id: 7, name: "Sensor influence", section: "Voice trigger", min: 0, max: 127,
    display: value => `${Math.round(value / 127 * 100)}%` },
  { id: 8, name: "Motion chance", section: "Voice motion", min: 0, max: 127,
    display: value => `${Math.round(value / 127 * 100)}% of phrases` },
  { id: 9, name: "Motion amount", section: "Voice motion", min: 0, max: 127,
    display: value => `${Math.round(value / 127 * 100)}%` },
];

export const voicePresets = [
  { name: "SAM", speed: 72, pitch: 64, mouth: 128, throat: 128 },
  { name: "Elf", speed: 72, pitch: 64, mouth: 110, throat: 160 },
  { name: "Little robot", speed: 92, pitch: 60, mouth: 190, throat: 190 },
  { name: "Stuffy", speed: 82, pitch: 72, mouth: 105, throat: 110 },
  { name: "Old lady", speed: 82, pitch: 32, mouth: 145, throat: 145 },
  { name: "Extra-terrestrial", speed: 100, pitch: 64, mouth: 200, throat: 150 },
] as const;

export const bankNames = [
  "Legacy", "Foundation", "Organic", "Percussive", "Bass & Lead", "Atmosphere", "Spectral", "Extreme",
  "Dub Techno", "Motorik", "Polyrhythmic Organic", "Cinematic", "Acid & Electro", "Broken Beat & IDM", "Minimal Phase", "Chiptune",
];
