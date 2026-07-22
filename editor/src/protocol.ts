export const Scope = { Patch: 0, Bank: 1, Global: 2, Sensor: 3 } as const;
export const Command = {
  Hello: 0x01, Select: 0x02, Get: 0x03, Set: 0x04,
  Commit: 0x05, Revert: 0x06, Restore: 0x07, SensorSnapshot: 0x08,
  GetPhrase: 0x09, SetPhrase: 0x0a,
} as const;
const Response = { Ack: 0x40, Capabilities: 0x41, Value: 0x42, SensorSnapshot: 0x43, Phrase: 0x44, Nack: 0x7f };
const SIGNATURE = [0x7d, 0x50, 0x53, 0x01];

interface MidiMessageEventLike { data: Uint8Array }
interface MidiPortLike { name?: string | null; state?: string; connection?: string; open(): Promise<unknown> }
interface MidiInputLike extends MidiPortLike { onmidimessage: ((event: MidiMessageEventLike) => void) | null }
interface MidiOutputLike extends MidiPortLike { send(data: number[] | Uint8Array): void }
interface MidiAccessLike {
  inputs: Map<string, MidiInputLike>;
  outputs: Map<string, MidiOutputLike>;
  onstatechange: (() => void) | null;
}

export interface Capabilities {
  firmware: string;
  patchCount: number;
  bankCount: number;
  sceneParameters: number;
  patchSharedParameters: number;
  bankParameters: number;
  globalParameters: number;
  flashMiB: number;
  speechParameters: number;
  speechPhrases: number;
  speechPhraseLength: number;
}

interface Pending {
  command: number;
  resolve: (payload: number[]) => void;
  reject: (error: Error) => void;
  timer: number;
}

function checksum(bytes: number[]): number {
  const sum = bytes.reduce((total, byte) => (total + byte) & 0x7f, 0);
  return (128 - sum) & 0x7f;
}

export function encodeMessage(command: number, request: number, payload: number[]): number[] {
  const body = [...SIGNATURE, command & 0x7f, request & 0x7f, ...payload.map(value => value & 0x7f)];
  return [0xf0, ...body, checksum(body), 0xf7];
}

export function decodeMessage(data: Uint8Array): { response: number; request: number; payload: number[] } | undefined {
  const bytes = [...data];
  if (bytes[0] !== 0xf0 || bytes.at(-1) !== 0xf7) return undefined;
  const body = bytes.slice(1, -1);
  if (body.length < 7 || SIGNATURE.some((value, index) => body[index] !== value)) return undefined;
  if ((body.reduce((sum, byte) => sum + byte, 0) & 0x7f) !== 0) return undefined;
  return { response: body[4], request: body[5], payload: body.slice(6, -1) };
}

export function decodePatchSelection(data: Uint8Array): number | undefined {
  if (data.length < 3 || (data[0] & 0xf0) !== 0xb0 || data[1] !== 23) return undefined;
  return data[2] & 0x7f;
}

export function decodeBankSelection(data: Uint8Array): number | undefined {
  if (data.length < 3 || (data[0] & 0xf0) !== 0xb0 || data[1] !== 0) return undefined;
  return data[2] & 0x7f;
}

export function decodeProgramChange(data: Uint8Array): number | undefined {
  if (data.length < 2 || (data[0] & 0xf0) !== 0xc0) return undefined;
  return data[1] & 0x7f;
}

export function decodeRootNote(data: Uint8Array): number | undefined {
  if (data.length < 3 || (data[0] & 0xf0) !== 0xb0 || data[1] !== 22) return undefined;
  return data[2] & 0x7f;
}

export class PocketScionConnection extends EventTarget {
  private access?: MidiAccessLike;
  private input?: MidiInputLike;
  private output?: MidiOutputLike;
  private requestId = 0;
  private pending = new Map<number, Pending>();
  private chain: Promise<unknown> = Promise.resolve();
  private patchCount = 128;
  private selectedBank = 0;
  private haveBankSelection = false;

  get connected(): boolean { return Boolean(this.input && this.output); }

  async connect(): Promise<Capabilities> {
    const requestMIDIAccess = (navigator as Navigator & {
      requestMIDIAccess?: (options: { sysex: boolean }) => Promise<MidiAccessLike>
    }).requestMIDIAccess;
    if (!requestMIDIAccess) throw new Error("Web MIDI is unavailable. Use desktop Chrome or Edge.");
    this.access = await requestMIDIAccess.call(navigator, { sysex: true });
    const matches = (port: MidiPortLike) => /pocket\s*scion|pra32/i.test(port.name ?? "");
    this.input = [...this.access.inputs.values()].find(matches);
    this.output = [...this.access.outputs.values()].find(matches);
    if (!this.input || !this.output) throw new Error("Pocket SCION MIDI ports were not found.");
    await Promise.all([this.input.open(), this.output.open()]);
    this.input.onmidimessage = event => this.receive(event.data);
    this.access.onstatechange = () => this.dispatchEvent(new Event("statechange"));
    const response = await this.request(Command.Hello, []);
    if (response[0] !== Response.Capabilities) throw new Error("Unexpected discovery response.");
    const p = response.slice(2);
    const capabilities = {
      firmware: `${p[0]}.${p[1]}.${p[2]}`,
      patchCount: p[3] | (p[4] << 7), bankCount: p[5],
      sceneParameters: p[6], patchSharedParameters: p[7],
      bankParameters: p[8], globalParameters: p[9],
      flashMiB: p[10] ?? 0,
      speechParameters: p[11] ?? 0,
      speechPhrases: p[12] ?? 0,
      speechPhraseLength: p[13] ?? 0,
    };
    this.patchCount = capabilities.patchCount;
    return capabilities;
  }

  targetBytes(target: number): number[] {
    return this.patchCount > 128 ? valueBytes(target) : [target & 0x7f];
  }

  request(command: number, payload: number[], timeout = 1200): Promise<number[]> {
    const operation = this.chain.then(() => this.sendRequest(command, payload, timeout));
    this.chain = operation.catch(() => undefined);
    return operation;
  }

  async sensorSnapshot(): Promise<number[]> {
    const response = await this.request(Command.SensorSnapshot, []);
    if (response[0] !== Response.SensorSnapshot ||
        ![10, 14, 30].includes(response.length)) {
      throw new Error("Unexpected sensor snapshot response.");
    }
    const payload = response.slice(2);
    return Array.from({ length: payload.length / 2 }, (_, index) =>
      payload[index * 2] | (payload[index * 2 + 1] << 7));
  }

  async getPhrase(target: number, phrase: number): Promise<string> {
    const response = await this.request(Command.GetPhrase,
      [...valueBytes(target), phrase]);
    if (response[0] !== Response.Phrase || response[2] !== phrase) {
      throw new Error("Unexpected speech phrase response.");
    }
    const length = response[3] ?? 0;
    return String.fromCharCode(...response.slice(4, 4 + length));
  }

  async getActivePhrase(phrase: number): Promise<string> {
    // 0x3fff is reserved by firmware as a read-only alias for the currently
    // active patch. Unlike getPhrase(target), it can never select a patch.
    return this.getPhrase(0x3fff, phrase);
  }

  async setPhrase(target: number, phrase: number, text: string): Promise<void> {
    const bytes = [...text].map(character => character.charCodeAt(0) & 0x7f);
    const maximum = 47;
    bytes.length = Math.min(bytes.length, maximum);
    const chunkSize = 24;
    if (!bytes.length) {
      await this.request(Command.SetPhrase,
        [...valueBytes(target), phrase, 0, 1]);
      return;
    }
    for (let offset = 0; offset < bytes.length; offset += chunkSize) {
      const chunk = bytes.slice(offset, offset + chunkSize);
      const final = offset + chunk.length >= bytes.length ? 1 : 0;
      await this.request(Command.SetPhrase,
        [...valueBytes(target), phrase, offset, final, ...chunk]);
    }
  }

  private sendRequest(command: number, payload: number[], timeout: number): Promise<number[]> {
    if (!this.output) return Promise.reject(new Error("Pocket SCION is not connected."));
    const request = this.requestId = (this.requestId + 1) & 0x7f;
    const message = encodeMessage(command, request, payload);
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(request);
        reject(new Error(`Pocket SCION request ${request} timed out.`));
      }, timeout);
      this.pending.set(request, { command, resolve, reject, timer });
      this.output!.send(message);
    });
  }

  private receive(data: Uint8Array): void {
    if (data.length && data[0] !== 0xf0) {
      this.dispatchEvent(new CustomEvent<number[]>("midi", { detail: [...data] }));
    }
    const bank = decodeBankSelection(data);
    if (bank !== undefined) {
      this.selectedBank = bank;
      this.haveBankSelection = true;
      return;
    }
    const program = decodeProgramChange(data);
    if (program !== undefined && this.haveBankSelection) {
      this.dispatchEvent(new CustomEvent<number>("patchchange", {
        detail: this.selectedBank * 16 + (program & 15),
      }));
      return;
    }
    const patch = decodePatchSelection(data);
    if (patch !== undefined && !this.haveBankSelection) {
      this.dispatchEvent(new CustomEvent<number>("patchchange", { detail: patch }));
      return;
    }
    const root = decodeRootNote(data);
    if (root !== undefined) {
      this.dispatchEvent(new CustomEvent<number>("rootchange", { detail: root }));
      return;
    }
    const decoded = decodeMessage(data);
    if (!decoded) return;
    const { response, request, payload } = decoded;
    const pending = this.pending.get(request);
    if (!pending) return;
    window.clearTimeout(pending.timer);
    this.pending.delete(request);
    if (response === Response.Nack) {
      pending.reject(new Error(`Device rejected command ${payload[0]} (error ${payload[1]}).`));
    } else {
      pending.resolve([response, request, ...payload]);
    }
  }
}

export function valueBytes(value: number): number[] {
  return [value & 0x7f, (value >> 7) & 0x7f];
}
