import { describe, expect, it } from "vitest";
import { decodeBankSelection, decodeMessage, decodePatchSelection, decodeProgramChange, decodeRootNote, encodeMessage, valueBytes } from "./protocol";
import { bankNames, bankParameters, globalParameters, midiNoteName, patchSharedParameters, scalePresets, sceneParameters } from "./parameters";

describe("Pocket SCION SysEx codec", () => {
  it("round-trips a 14-bit parameter value", () => {
    const message = encodeMessage(4, 19, [0, 127, 2, 23, ...valueBytes(240)]);
    const decoded = decodeMessage(Uint8Array.from(message));
    expect(decoded).toEqual({ response: 4, request: 19, payload: [0, 127, 2, 23, 112, 1] });
  });

  it("rejects corruption and wrong signatures", () => {
    const corrupt = encodeMessage(1, 1, []);
    corrupt[5] ^= 1;
    expect(decodeMessage(Uint8Array.from(corrupt))).toBeUndefined();
    const wrong = encodeMessage(1, 1, []);
    wrong[2] = 0x51;
    expect(decodeMessage(Uint8Array.from(wrong))).toBeUndefined();
  });

  it("recognizes the combined patch CC on every MIDI channel", () => {
    expect(decodePatchSelection(Uint8Array.from([0xb0, 23, 37]))).toBe(37);
    expect(decodePatchSelection(Uint8Array.from([0xb2, 23, 127]))).toBe(127);
    expect(decodePatchSelection(Uint8Array.from([0xb0, 22, 37]))).toBeUndefined();
  });

  it("recognizes root-note CC updates", () => {
    expect(decodeRootNote(Uint8Array.from([0xb0, 22, 45]))).toBe(45);
    expect(decodeRootNote(Uint8Array.from([0xb1, 22, 60]))).toBe(60);
    expect(decodeRootNote(Uint8Array.from([0xb0, 23, 45]))).toBeUndefined();
  });

  it("combines standard bank and program messages for upper banks", () => {
    expect(decodeBankSelection(Uint8Array.from([0xb2, 0, 12]))).toBe(12);
    expect(decodeProgramChange(Uint8Array.from([0xc2, 8]))).toBe(8);
    expect(valueBytes(200)).toEqual([72, 1]);
    expect(valueBytes(255)).toEqual([127, 1]);
  });
});

describe("editor schema", () => {
  it("matches the firmware capability counts", () => {
    expect(sceneParameters).toHaveLength(47);
    expect(patchSharedParameters).toHaveLength(113);
    expect(bankParameters).toHaveLength(19);
    expect(globalParameters).toHaveLength(7);
    expect(bankNames).toHaveLength(16);
    expect(sceneParameters[18].values?.map(choice => choice.value)).toEqual([2, 4, 5]);
    expect(patchSharedParameters[35].values?.map(choice => choice.value)).toEqual([0, 1, 2, 3]);
    expect(patchSharedParameters[39].values).toHaveLength(8);
    expect(patchSharedParameters[40].values).toHaveLength(5);
    expect(patchSharedParameters[99].name).toBe("Pad density");
    expect(patchSharedParameters[100].name).toBe("Lead density");
    expect(patchSharedParameters[101].name).toBe("Breath maximum override");
    expect(patchSharedParameters[112].name).toBe("LFO-rate motion");
    expect(patchSharedParameters.map(parameter => parameter.id)).toEqual(
      Array.from({ length: 113 }, (_, id) => id),
    );
  });

  it("renders standard MIDI note names and octaves", () => {
    expect(midiNoteName(45)).toBe("A2");
    expect(midiNoteName(60)).toBe("C4");
    expect(midiNoteName(61)).toBe("C♯4");
  });

  it("defines valid seven-degree known scales", () => {
    expect(scalePresets.length).toBeGreaterThanOrEqual(12);
    for (const preset of scalePresets) {
      expect(preset.offsets).toHaveLength(7);
      expect(preset.offsets.every(offset => offset >= -24 && offset <= 24)).toBe(true);
    }
  });

  it("keeps all lane-density controls in the Euclidean section", () => {
    expect([31, 99, 100].map(id => patchSharedParameters[id].section)).toEqual([
      "Euclidean rhythm", "Euclidean rhythm", "Euclidean rhythm",
    ]);
  });
});
