import "./style.css";
import { PocketScionConnection, Command, Scope, valueBytes, type Capabilities } from "./protocol";
import { sceneParameters, patchSharedParameters, bankParameters, globalParameters, voiceParameters, voicePresets, bankNames, midiNoteName, scalePresets, type Parameter } from "./parameters";

type Tab = "bass" | "pad" | "lead" | "voice" | "sequence" | "articulation" | "motion" | "bank" | "globals" | "sensor" | "visuals";
interface PatchData { lanes: number[][]; shared: number[]; speech: number[]; phrases: string[] }
interface EditorState extends PatchData { bank: number[]; globals: number[] }
interface PatchFile extends PatchData { schema: "pocket-scion-patch"; version: 1 | 2 | 3 | 4 | 5; patchId: number }
interface BankFile { schema: "pocket-scion-bank"; version: 1 | 2 | 3 | 4 | 5; bankIndex: number; settings: number[]; patches: PatchFile[] }

const connection = new PocketScionConnection();
let capabilities: Capabilities | undefined;
let state: EditorState = { lanes: [[], [], []], shared: [], speech: [], phrases: [], bank: [], globals: [] };
let activeTab: Tab = "bass";
let dirty = new Set<"patch" | "bank" | "globals">();
let sensorTimer: number | undefined;
let sensorSnapshotSupported: boolean | undefined;
interface SensorSample { time: number; values: number[] }
let sensorHistory: SensorSample[] = [];
let sensorGraphPaused = false;
let sensorGraphWindowMs = 30000;
let sensorWindowRate = 0;
let sensorWindowRateBaseline: { time: number; counter: number } | undefined;
let loadGeneration = 0;
const sendTimers = new Map<string, number>();
let parameterOutputRefreshers: (() => void)[] = [];
let visualAnimation: number | undefined;
let visualMidiHandler: ((event: Event) => void) | undefined;
let visualPatchFollower: ((target: number) => boolean) | undefined;
class LoadSuperseded extends Error {}

const $ = <T extends HTMLElement>(selector: string): T => document.querySelector(selector) as T;
const bankSelect = $("#bank") as HTMLSelectElement;
const patchSelect = $("#patch") as HTMLSelectElement;
const editor = $("#editor");
const status = $("#status");

function populateBanks(count = bankNames.length): void {
  const selected = Number(bankSelect.value) || 0;
  bankSelect.replaceChildren();
  bankNames.slice(0, count).forEach((name, index) =>
    bankSelect.add(new Option(`${index}. ${name}`, String(index))));
  bankSelect.value = String(Math.min(selected, Math.max(0, count - 1)));
}
populateBanks();
Array.from({ length: 16 }, (_, index) => patchSelect.add(new Option(`Patch ${index + 1}`, String(index))));

const actions = ["reload", "save-patch", "save-bank", "save-globals", "export-patch", "export-bank", "import-file", "revert", "restore"];
function enableActions(enabled: boolean): void { actions.forEach(id => (($(`#${id}`) as HTMLButtonElement).disabled = !enabled)); }
function patchId(): number { return Number(bankSelect.value) * 16 + Number(patchSelect.value); }
function addressed(scope: number, target: number, tail: number[] = []): number[] {
  return [scope, ...connection.targetBytes(target), ...tail];
}
function setStatus(message: string, error = false): void {
  status.textContent = message;
  status.style.color = error ? "#e49a8c" : "#aab3a3";
  if (error && !capabilities) {
    $("#connection-label").textContent = message;
    $("#connection-label").classList.add("error");
  }
}
function markDirty(scope: "patch" | "bank" | "globals"): void { dirty.add(scope); renderDirty(); }
function renderDirty(): void { $("#dirty").textContent = dirty.size ? `Unsaved: ${[...dirty].join(", ")}` : "Saved"; }

async function requestValue(scope: number, target: number, lane: number, parameter: number): Promise<number> {
  const response = await connection.request(Command.Get, addressed(scope, target, [lane, parameter]));
  return response[response.length - 2] | (response[response.length - 1] << 7);
}

async function readPatch(id: number, generation?: number,
                         activePhraseReads = false): Promise<PatchData> {
  const lanes: number[][] = [[], [], []];
  for (let lane = 0; lane < 3; lane++) {
    for (const parameter of sceneParameters) {
      if (lane !== 1 && parameter.id >= 41) {
        lanes[lane][parameter.id] = 0;
        continue;
      }
      lanes[lane][parameter.id] = await requestValue(Scope.Patch, id, lane, parameter.id);
      if (generation !== undefined && generation !== loadGeneration) throw new LoadSuperseded();
    }
  }
  const shared: number[] = [];
  for (const parameter of patchSharedParameters) {
    shared[parameter.id] = await requestValue(Scope.Patch, id, 3, parameter.id);
    if (generation !== undefined && generation !== loadGeneration) throw new LoadSuperseded();
  }
  const speech: number[] = [];
  const phrases: string[] = [];
  if ((capabilities?.speechParameters ?? 0) > 0) {
    for (const parameter of voiceParameters.slice(0, capabilities!.speechParameters)) {
      speech[parameter.id] = await requestValue(Scope.Patch, id, 4, parameter.id);
      if (generation !== undefined && generation !== loadGeneration) throw new LoadSuperseded();
    }
    for (let phrase = 0; phrase < capabilities!.speechPhrases; phrase++) {
      phrases[phrase] = activePhraseReads
        ? await connection.getActivePhrase(phrase)
        : await connection.getPhrase(id, phrase);
      if (generation !== undefined && generation !== loadGeneration) throw new LoadSuperseded();
    }
  }
  return { lanes, shared, speech, phrases };
}

async function loadCurrent(target = patchId(), selectDevice = true): Promise<void> {
  if (!capabilities) return;
  const generation = ++loadGeneration;
  const bankIndex = Math.floor(target / 16);
  enableActions(false); setStatus("Reading patch and shared settings…");
  try {
    if (selectDevice) await connection.request(Command.Select, connection.targetBytes(target));
    if (generation !== loadGeneration) throw new LoadSuperseded();
    // The target has either just been selected by us, or was selected by the
    // hardware event that started this load. Read phrases through the
    // no-selection active alias so a stale request cannot switch back.
    const patch = await readPatch(target, generation, true);
    const bank: number[] = [];
    for (const parameter of bankParameters) {
      bank[parameter.id] = await requestValue(Scope.Bank, bankIndex, 0, parameter.id);
      if (generation !== loadGeneration) throw new LoadSuperseded();
    }
    const globals: number[] = [];
    for (const parameter of globalParameters) {
      if (parameter.id >= capabilities.globalParameters) {
        globals[parameter.id] = parameter.min;
        continue;
      }
      globals[parameter.id] = await requestValue(Scope.Global, 0, 0, parameter.id);
      if (generation !== loadGeneration) throw new LoadSuperseded();
    }
    state = { ...patch, bank, globals }; dirty.clear(); renderDirty(); render(); enableActions(true);
    setStatus(`Connected to firmware ${capabilities.firmware} (${capabilities.flashMiB} MiB flash detected). Patch ${target + 1} is ready.`);
  } catch (error) {
    if (!(error instanceof LoadSuperseded)) throw error;
  }
}

function grouped(parameters: Parameter[]): Map<string, Parameter[]> {
  const result = new Map<string, Parameter[]>();
  parameters.forEach(parameter => result.set(parameter.section, [...(result.get(parameter.section) ?? []), parameter]));
  return result;
}

function displayedParameterValue(parameter: Parameter, value: number, scope: "patch" | "bank" | "globals", lane: number): string {
  if (scope === "patch" && lane === 3 && parameter.id < 7) {
    const offset = value - 24;
    const root = state.globals[0] ?? 45;
    return `${midiNoteName(root + offset)} · ${offset >= 0 ? "+" : ""}${offset} st`;
  }
  if (scope === "patch" && lane === 3 && parameter.id >= 7 && parameter.id < 23) {
    const degree = value % 7;
    const root = state.globals[0] ?? 45;
    const padOffset = (state.shared[degree] ?? 24) - 24;
    const leadDegree = (degree + 2) % 7;
    const leadOffset = (state.shared[leadDegree] ?? 24) - 24;
    return `D${degree + 1} · P ${midiNoteName(root + padOffset)} · L ${midiNoteName(root + leadOffset + 12)}`;
  }
  return parameter.display ? parameter.display(value) : String(value);
}

function renderScalePresetPicker(block: HTMLElement, values: number[]): void {
  const bar = document.createElement("div"); bar.className = "scale-preset-bar";
  const label = document.createElement("label"); label.textContent = "Known scale";
  const select = document.createElement("select"); select.setAttribute("aria-label", "Known scale preset");
  select.add(new Option("Custom", ""));
  scalePresets.forEach((preset, index) => select.add(new Option(preset.name, String(index))));
  const currentOffsets = values.slice(0, 7).map(value => value - 24);
  const currentPreset = scalePresets.findIndex(preset => preset.offsets.every((offset, index) => offset === currentOffsets[index]));
  select.value = currentPreset < 0 ? "" : String(currentPreset);
  label.append(select);
  const apply = document.createElement("button"); apply.textContent = "Apply scale"; apply.disabled = currentPreset < 0;
  select.addEventListener("change", () => { apply.disabled = select.value === ""; });
  apply.addEventListener("click", () => {
    const preset = scalePresets[Number(select.value)];
    if (!preset) return;
    void run(async () => {
      const target = patchId();
      preset.offsets.forEach((offset, degree) => {
        const key = `${Scope.Patch}:${target}:3:${degree}`;
        window.clearTimeout(sendTimers.get(key)); sendTimers.delete(key);
        values[degree] = offset + 24;
      });
      render(); markDirty("patch");
      for (let degree = 0; degree < 7; degree++) {
        await connection.request(Command.Set, addressed(Scope.Patch, target, [3, degree, ...valueBytes(values[degree])]));
      }
      setStatus(`${preset.name} applied to patch ${target + 1}. Save the patch to keep it.`);
    }, `Applying ${preset.name}…`);
  });
  bar.append(label, apply); block.append(bar);
}

function renderParameters(parameters: Parameter[], values: number[], scope: "patch" | "bank" | "globals", lane: number,
  destination: HTMLElement = editor, replace = true): void {
  editor.classList.remove("empty");
  if (replace) destination.replaceChildren();
  parameterOutputRefreshers = [];
  for (const [section, items] of grouped(parameters)) {
    const block = document.createElement("section"); block.className = "parameter-section";
    const heading = document.createElement("h2"); heading.textContent = section; block.append(heading);
    if (section === "Scale" && scope === "patch" && lane === 3) renderScalePresetPicker(block, values);
    const grids: HTMLDivElement[] = [];
    if (section === "Motif") {
      const groups = document.createElement("div"); groups.className = "motif-groups";
      for (let groupIndex = 0; groupIndex < 4; groupIndex++) {
        const group = document.createElement("section"); group.className = "motif-group";
        const groupHeading = document.createElement("h3");
        groupHeading.textContent = `Steps ${groupIndex * 4 + 1}–${groupIndex * 4 + 4}`;
        const groupGrid = document.createElement("div"); groupGrid.className = "parameter-grid motif-grid";
        grids.push(groupGrid); group.append(groupHeading, groupGrid); groups.append(group);
      }
      block.append(groups);
    } else {
      const grid = document.createElement("div"); grid.className = "parameter-grid";
      grids.push(grid); block.append(grid);
    }
    for (const parameter of items) {
      const control = document.createElement("div"); control.className = "parameter";
      const label = document.createElement("label"); label.textContent = parameter.name;
      const output = document.createElement("output");
      const input = document.createElement("input"); input.type = "range"; input.min = String(parameter.min); input.max = String(parameter.max); input.step = "1";
      const inputId = `${scope}-${lane}-${parameter.id}`;
      input.id = inputId; input.className = "knob-input"; input.title = "Drag, scroll, or use the arrow keys";
      label.htmlFor = inputId; output.htmlFor = inputId;
      const knob = document.createElement("div"); knob.className = "knob";
      const dial = document.createElement("div"); dial.className = "knob-dial"; knob.append(dial, input);
      const choices = parameter.values;
      const initialValue = values[parameter.id] ?? parameter.min;
      input.min = choices ? "0" : String(parameter.min);
      input.max = choices ? String(choices.length - 1) : String(parameter.max);
      input.value = choices
        ? String(Math.max(0, choices.findIndex(choice => choice.value === initialValue)))
        : String(initialValue);
      const readValue = () => choices?.[Number(input.value)]?.value ?? Number(input.value);
      const updateOutput = () => {
        const controlMin = Number(input.min), controlMax = Number(input.max);
        const amount = (Number(input.value) - controlMin) / Math.max(1, controlMax - controlMin);
        dial.style.setProperty("--knob-angle", `${-135 + amount * 270}deg`);
        dial.style.setProperty("--knob-progress", `${amount * 75}%`);
        const choice = choices?.[Number(input.value)];
        const value = readValue();
        output.textContent = choice?.label ?? displayedParameterValue(parameter, value, scope, lane);
        if (scope === "patch" && lane === 3 && parameter.id >= 7 && parameter.id < 23) {
          output.title = "Base pad and lead notes before sensor-controlled octave transposition";
        }
      };
      parameterOutputRefreshers.push(updateOutput);
      updateOutput();
      input.addEventListener("pointerdown", event => {
        event.preventDefault(); input.focus(); input.setPointerCapture(event.pointerId);
        const startX = event.clientX, startY = event.clientY, startValue = Number(input.value);
        const minimum = Number(input.min), maximum = Number(input.max), span = maximum - minimum;
        const travel = Math.min(160, Math.max(24, span));
        const move = (moveEvent: PointerEvent) => {
          const movement = (startY - moveEvent.clientY) + (moveEvent.clientX - startX);
          input.value = String(Math.max(minimum, Math.min(maximum, Math.round(startValue + movement * span / travel))));
          input.dispatchEvent(new Event("input", { bubbles: true }));
        };
        const stop = () => {
          input.removeEventListener("pointermove", move);
          input.removeEventListener("pointerup", stop);
          input.removeEventListener("pointercancel", stop);
        };
        input.addEventListener("pointermove", move);
        input.addEventListener("pointerup", stop);
        input.addEventListener("pointercancel", stop);
      });
      input.addEventListener("wheel", event => {
        event.preventDefault();
        input.value = String(Math.max(Number(input.min), Math.min(Number(input.max), Number(input.value) + (event.deltaY < 0 ? 1 : -1))));
        input.dispatchEvent(new Event("input", { bubbles: true }));
      }, { passive: false });
      input.addEventListener("input", () => {
        const value = readValue(); values[parameter.id] = value;
        if (scope === "patch" && lane === 3 && parameter.id < 7) parameterOutputRefreshers.forEach(refresh => refresh());
        else updateOutput();
        markDirty(scope);
        const target = scope === "patch" ? patchId() : scope === "bank" ? Number(bankSelect.value) : 0;
        const protocolScope = scope === "patch" ? Scope.Patch : scope === "bank" ? Scope.Bank : Scope.Global;
        const key = `${protocolScope}:${target}:${lane}:${parameter.id}`;
        window.clearTimeout(sendTimers.get(key));
        sendTimers.set(key, window.setTimeout(async () => {
          try { await connection.request(Command.Set, addressed(protocolScope, target, [lane, parameter.id, ...valueBytes(value)])); }
          catch (error) { setStatus((error as Error).message, true); }
        }, parameter.settleMs ?? 55));
      });
      const grid = section === "Motif" ? grids[Math.floor((parameter.id - 7) / 4)] : grids[0];
      control.append(label, knob, output); grid.append(control);
    }
    destination.append(block);
  }
}

function updateSensorMeters(values: number[]): void {
  values.forEach((value, id) => {
    const meter = editor.querySelector<HTMLElement>(`[data-sensor="${id}"]`);
    if (!meter) return;
    const percent = Math.round(value / 10);
    meter.querySelector("output")!.textContent = `${percent}%`;
    (meter.querySelector(".meter-fill") as HTMLElement).style.width = `${percent}%`;
  });
  if (!sensorGraphPaused && values.length >= 4) {
    const now = performance.now();
    sensorHistory.push({ time: now, values: [...values] });
    sensorHistory = sensorHistory.filter(sample => sample.time >= now - 60000);
  }
  if (values.length >= 14) updateSensorDiagnostics(values);
  drawSensorGraph();
}

function updateSensorDiagnostics(values: number[]): void {
  const statusValue = values[10];
  const windowCounter = statusValue >> 3 & 0xff;
  const now = performance.now();
  if (!sensorWindowRateBaseline) {
    sensorWindowRateBaseline = { time: now, counter: windowCounter };
  } else if (now - sensorWindowRateBaseline.time >= 500) {
    const elapsed = (now - sensorWindowRateBaseline.time) / 1000;
    const windows = (windowCounter - sensorWindowRateBaseline.counter + 256) & 0xff;
    sensorWindowRate = windows / elapsed;
    sensorWindowRateBaseline = { time: now, counter: windowCounter };
  }
  const diagnostics = [
    (statusValue & 1) !== 0 ? "Learning" : "Frozen",
    (statusValue & 2) !== 0 ? "Active" : "Idle",
    `${sensorWindowRate.toFixed(1)} Hz`,
    String(values[11]), String(values[12]),
    values[13] >= 16383 ? "No signal" : `${values[13]} ms`,
  ];
  diagnostics.forEach((text, index) => {
    const output = editor.querySelector<HTMLOutputElement>(`[data-diagnostic="${index}"] output`);
    if (output) output.textContent = text;
  });
}

const sensorGraphSeries = [
  { name: "Pressure", color: "#d9b76d" },
  { name: "Expression", color: "#76c893" },
  { name: "Transient", color: "#e47f72" },
  { name: "Pitch motion", color: "#8eb7e8" },
];

function drawSensorGraph(): void {
  const canvas = editor.querySelector<HTMLCanvasElement>("#sensor-graph");
  if (!canvas) return;
  const width = Math.max(320, Math.floor(canvas.clientWidth));
  const height = Math.max(180, Math.floor(canvas.clientHeight));
  const ratio = window.devicePixelRatio || 1;
  if (canvas.width !== width * ratio || canvas.height !== height * ratio) {
    canvas.width = width * ratio; canvas.height = height * ratio;
  }
  const context = canvas.getContext("2d");
  if (!context) return;
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);
  const left = 38, right = 10, top = 12, bottom = 24;
  const plotWidth = width - left - right, plotHeight = height - top - bottom;
  context.font = "10px Inter, system-ui, sans-serif";
  context.fillStyle = "#7f8979";
  context.strokeStyle = "#2e382b";
  context.lineWidth = 1;
  for (let step = 0; step <= 4; step++) {
    const y = top + plotHeight * step / 4;
    context.beginPath(); context.moveTo(left, y); context.lineTo(width - right, y); context.stroke();
    context.fillText(`${100 - step * 25}%`, 2, y + 3);
  }
  for (let step = 0; step <= 3; step++) {
    const x = left + plotWidth * step / 3;
    context.beginPath(); context.moveTo(x, top); context.lineTo(x, top + plotHeight); context.stroke();
    const seconds = Math.round((sensorGraphWindowMs / 1000) * (1 - step / 3));
    context.fillText(step === 3 ? "now" : `−${seconds}s`, x - (step === 3 ? 18 : 10), height - 5);
  }
  const endTime = sensorGraphPaused && sensorHistory.length
    ? sensorHistory[sensorHistory.length - 1].time : performance.now();
  const startTime = endTime - sensorGraphWindowMs;
  sensorGraphSeries.forEach((series, seriesIndex) => {
    context.beginPath(); context.strokeStyle = series.color; context.lineWidth = 1.8;
    let started = false;
    sensorHistory.forEach(sample => {
      if (sample.time < startTime || sample.time > endTime) return;
      const x = left + (sample.time - startTime) / sensorGraphWindowMs * plotWidth;
      const y = top + (1 - Math.max(0, Math.min(1000, sample.values[seriesIndex])) / 1000) * plotHeight;
      if (!started) { context.moveTo(x, y); started = true; } else context.lineTo(x, y);
    });
    if (started) context.stroke();
  });
  drawCalibrationGraph("interval-calibration-graph", 4, 5, 6,
    value => value * 0.1, value => `${value.toFixed(value < 10 ? 2 : 1)} ms`);
  drawCalibrationGraph("variation-calibration-graph", 7, 8, 9,
    value => value / 100, value => `${value.toFixed(2)}%`);
}

function drawCalibrationGraph(canvasId: string, currentIndex: number,
  lowIndex: number, highIndex: number, transform: (value: number) => number,
  format: (value: number) => string): void {
  const canvas = editor.querySelector<HTMLCanvasElement>(`#${canvasId}`);
  if (!canvas) return;
  const width = Math.max(240, Math.floor(canvas.clientWidth));
  const height = Math.max(150, Math.floor(canvas.clientHeight));
  const ratio = window.devicePixelRatio || 1;
  if (canvas.width !== width * ratio || canvas.height !== height * ratio) {
    canvas.width = width * ratio; canvas.height = height * ratio;
  }
  const context = canvas.getContext("2d");
  if (!context) return;
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.clearRect(0, 0, width, height);
  const endTime = sensorGraphPaused && sensorHistory.length
    ? sensorHistory[sensorHistory.length - 1].time : performance.now();
  const startTime = endTime - sensorGraphWindowMs;
  const samples = sensorHistory.filter(sample =>
    sample.time >= startTime && sample.time <= endTime &&
    sample.values.length > 10 && sample.values.length > highIndex &&
    (sample.values[10] & 4) !== 0);
  context.font = "10px Inter, system-ui, sans-serif";
  if (!samples.length) {
    context.fillStyle = "#7f8979";
    context.fillText("Calibration diagnostics require the updated firmware.", 12, 25);
    return;
  }
  const latest = samples[samples.length - 1];
  const summary = canvas.closest(".calibration-chart")?.querySelector("output");
  if (summary) summary.textContent =
    `${format(transform(latest.values[currentIndex]))} · range ${format(transform(latest.values[lowIndex]))}–${format(transform(latest.values[highIndex]))}`;

  const left = 50, right = 10, top = 10, bottom = 22;
  const plotWidth = width - left - right, plotHeight = height - top - bottom;
  const allValues = samples.flatMap(sample => [currentIndex, lowIndex, highIndex]
    .map(index => transform(sample.values[index])));
  let minimum = Math.min(...allValues), maximum = Math.max(...allValues);
  const padding = Math.max((maximum - minimum) * 0.12,
    Math.max(Math.abs(maximum), 0.01) * 0.03);
  minimum = Math.max(0, minimum - padding); maximum += padding;
  if (maximum <= minimum) maximum = minimum + 1;
  const point = (sample: SensorSample, index: number): [number, number] => [
    left + (sample.time - startTime) / sensorGraphWindowMs * plotWidth,
    top + (maximum - transform(sample.values[index])) /
      (maximum - minimum) * plotHeight,
  ];
  context.strokeStyle = "#2e382b"; context.fillStyle = "#7f8979";
  context.lineWidth = 1;
  for (let step = 0; step <= 2; step++) {
    const y = top + plotHeight * step / 2;
    context.beginPath(); context.moveTo(left, y); context.lineTo(width - right, y); context.stroke();
    context.fillText(format(maximum - (maximum - minimum) * step / 2), 2, y + 3);
  }
  context.beginPath();
  samples.forEach((sample, index) => {
    const [x, y] = point(sample, highIndex);
    if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
  });
  [...samples].reverse().forEach(sample => {
    const [x, y] = point(sample, lowIndex); context.lineTo(x, y);
  });
  context.closePath(); context.fillStyle = "rgba(113, 143, 87, .24)"; context.fill();
  [lowIndex, highIndex].forEach(index => {
    context.beginPath(); context.strokeStyle = "#718f57"; context.lineWidth = 1;
    samples.forEach((sample, sampleIndex) => {
      const [x, y] = point(sample, index);
      if (sampleIndex === 0) context.moveTo(x, y); else context.lineTo(x, y);
    });
    context.stroke();
  });
  context.beginPath(); context.strokeStyle = "#e3bf72"; context.lineWidth = 1.8;
  samples.forEach((sample, index) => {
    const [x, y] = point(sample, currentIndex);
    if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
  });
  context.stroke();
}

async function pollSensor(): Promise<void> {
  if (activeTab !== "sensor") return;
  try {
    let values: number[] | undefined;
    if (sensorSnapshotSupported !== false) {
      try {
        values = await connection.sensorSnapshot();
        sensorSnapshotSupported = true;
      } catch {
        sensorSnapshotSupported = false;
      }
    }
    if (!values) {
      values = [];
      for (let id = 0; id < 4; id++) values.push(await requestValue(Scope.Sensor, 0, 0, id));
    }
    updateSensorMeters(values);
  } catch {
    // A disconnected device leaves the last coherent snapshot visible.
  } finally {
    if (activeTab === "sensor") {
      sensorTimer = window.setTimeout(pollSensor,
        sensorSnapshotSupported === true ? 50 : 200);
    }
  }
}

function renderSensor(): void {
  editor.classList.remove("empty"); editor.replaceChildren();
  sensorWindowRateBaseline = undefined; sensorWindowRate = 0;
  const graph = document.createElement("section"); graph.className = "sensor-graph-panel";
  const graphHeader = document.createElement("div"); graphHeader.className = "sensor-graph-header";
  const graphTitle = document.createElement("div");
  graphTitle.innerHTML = "<h2>Sensor dynamics</h2><p>Processed sensor parameters over time</p>";
  const graphTools = document.createElement("div"); graphTools.className = "sensor-graph-tools";
  const duration = document.createElement("select"); duration.setAttribute("aria-label", "Graph time range");
  [[10000, "10 seconds"], [30000, "30 seconds"], [60000, "60 seconds"]].forEach(([value, label]) =>
    duration.add(new Option(String(label), String(value))));
  duration.value = String(sensorGraphWindowMs);
  duration.addEventListener("change", () => { sensorGraphWindowMs = Number(duration.value); drawSensorGraph(); });
  const pause = document.createElement("button"); pause.textContent = sensorGraphPaused ? "Resume" : "Pause";
  pause.addEventListener("click", () => { sensorGraphPaused = !sensorGraphPaused; pause.textContent = sensorGraphPaused ? "Resume" : "Pause"; drawSensorGraph(); });
  const clear = document.createElement("button"); clear.textContent = "Clear";
  clear.addEventListener("click", () => { sensorHistory = []; drawSensorGraph(); });
  const reset = document.createElement("button"); reset.textContent = "Reset calibration";
  reset.disabled = (capabilities?.globalParameters ?? 0) < 17;
  reset.addEventListener("click", () => void run(async () => {
    await connection.request(Command.Set, addressed(Scope.Global, 0, [0, 16, ...valueBytes(2)]));
    state.globals[16] = 1;
    sensorHistory = [];
    setStatus("Sensor calibration reset; adaptive learning is active.");
  }, "Resetting sensor calibration…"));
  graphTools.append(duration, pause, clear, reset); graphHeader.append(graphTitle, graphTools);
  const canvas = document.createElement("canvas"); canvas.id = "sensor-graph";
  graph.append(graphHeader, canvas);

  const calibrationGraphs = document.createElement("section");
  calibrationGraphs.className = "calibration-graphs";
  const calibrationHeading = document.createElement("div");
  calibrationHeading.className = "calibration-heading";
  calibrationHeading.innerHTML = "<h2>Learned calibration</h2><p>Gold is the current measurement; the green band is the learned minimum–maximum range.</p>";
  const calibrationGrid = document.createElement("div");
  calibrationGrid.className = "calibration-graph-grid";
  [["Interval mean", "Pressure is derived from position inside this interval range.", "interval-calibration-graph"],
   ["Relative variation", "Expression is derived from position inside this variation range.", "variation-calibration-graph"]]
    .forEach(([title, description, id]) => {
      const chart = document.createElement("article"); chart.className = "calibration-chart";
      const heading = document.createElement("h3"); heading.textContent = title;
      const copy = document.createElement("p"); copy.textContent = description;
      const output = document.createElement("output"); output.textContent = "Waiting for sensor data…";
      const chartCanvas = document.createElement("canvas"); chartCanvas.id = id;
      chart.append(heading, copy, output, chartCanvas); calibrationGrid.append(chart);
    });
  calibrationGraphs.append(calibrationHeading, calibrationGrid);

  const diagnostics = document.createElement("div"); diagnostics.className = "sensor-diagnostics";
  ["Calibration", "Input", "Analysis windows", "Dropped edges", "Rejected fast edges", "Last edge"]
    .forEach((name, index) => {
      const item = document.createElement("div"); item.className = "sensor-diagnostic";
      item.dataset.diagnostic = String(index);
      const label = document.createElement("span"); label.textContent = name;
      const output = document.createElement("output"); output.textContent = "—";
      item.append(label, output); diagnostics.append(item);
    });
  const grid = document.createElement("div"); grid.className = "sensor-grid";
  ["Pressure / proximity", "Variation / expression", "Transient", "Pitch motion"].forEach((name, id) => {
    const meter = document.createElement("div"); meter.className = "meter"; meter.dataset.sensor = String(id);
    const heading = document.createElement("h2");
    const dot = document.createElement("span"); dot.className = "meter-dot";
    dot.style.setProperty("--series-colour", sensorGraphSeries[id].color);
    heading.append(dot, name);
    const output = document.createElement("output"); output.textContent = "0%";
    const track = document.createElement("div"); track.className = "meter-track";
    const fill = document.createElement("div"); fill.className = "meter-fill"; track.append(fill);
    meter.append(heading, output, track); grid.append(meter);
  });
  editor.append(graph, calibrationGraphs);
  const calibrationParameters = globalParameters.filter(parameter =>
    parameter.id >= 7 && parameter.id < (capabilities?.globalParameters ?? 0));
  if (calibrationParameters.length) {
    renderParameters(calibrationParameters, state.globals, "globals", 0,
      editor, false);
  }
  editor.append(diagnostics, grid);
  drawSensorGraph();
  void pollSensor();
}

interface VisualParticle {
  x: number; y: number; vx: number; vy: number; life: number; size: number;
  lane: number; note: number; drag: number; curl: number; gravity: number;
  twinkle: number; shape: number; seed: number;
}
interface VisualWave { x: number; y: number; radius: number; life: number; lane: number }
interface VisualFractal {
  x: number; y: number; radius: number; life: number; lane: number;
  branches: number; depth: number; angle: number; spin: number;
}
interface VisualWord {
  text: string; x: number; y: number; vx: number; vy: number; life: number;
  maxLife: number; size: number; rotation: number; rotationVelocity: number; lane: number;
}
interface VisualForm {
  kind: number; x: number; y: number; width: number; height: number;
  life: number; maxLife: number; lane: number; angle: number; spin: number;
  phase: number; segments: number; variant: number;
}

function stopVisualPerformance(): void {
  if (visualAnimation !== undefined) cancelAnimationFrame(visualAnimation);
  visualAnimation = undefined;
  if (visualMidiHandler) connection.removeEventListener("midi", visualMidiHandler);
  visualMidiHandler = undefined;
  visualPatchFollower = undefined;
}

function renderVisualPerformance(): void {
  stopVisualPerformance();
  editor.replaceChildren(); editor.classList.remove("empty");
  const shell = document.createElement("section"); shell.className = "visual-performance";
  const toolbar = document.createElement("div"); toolbar.className = "visual-toolbar";
  const activity = document.createElement("div"); activity.className = "visual-activity";
  const dot = document.createElement("span"); dot.className = "visual-midi-dot";
  const activityText = document.createElement("output"); activityText.textContent = "Waiting for MIDI";
  activity.append(dot, activityText);
  const trailsLabel = document.createElement("label"); trailsLabel.textContent = "Persistence";
  const trails = document.createElement("input"); trails.type = "range"; trails.min = "1"; trails.max = "20"; trails.value = "10";
  trailsLabel.append(trails);
  const clear = document.createElement("button"); clear.textContent = "Clear";
  const fullscreen = document.createElement("button"); fullscreen.textContent = "Full screen";
  toolbar.append(activity, trailsLabel, clear, fullscreen);
  const stage = document.createElement("div"); stage.className = "visual-stage";
  const canvas = document.createElement("canvas");
  canvas.setAttribute("role", "img");
  canvas.setAttribute("aria-label", "Generative visual performance driven by incoming Pocket SCION MIDI notes and controllers");
  const laneLegend = document.createElement("div"); laneLegend.className = "visual-lane-legend";
  ["Bass / percussion", "Pad", "Lead"].forEach((name, lane) => {
    const item = document.createElement("span"); item.dataset.lane = String(lane); item.textContent = name; laneLegend.append(item);
  });
  const phraseDisplay = document.createElement("div"); phraseDisplay.className = "visual-phrase";
  phraseDisplay.setAttribute("aria-live", "polite");
  const motionDisplay = document.createElement("div"); motionDisplay.className = "visual-motion";
  const instrumentLabel = document.createElement("div"); instrumentLabel.className = "visual-instrument";
  const bankTitle = document.createElement("span"); bankTitle.textContent = "Bank";
  const visualBankSelect = document.createElement("select");
  const instrumentTitle = document.createElement("span"); instrumentTitle.textContent = "Instrument";
  const instrumentSelect = document.createElement("select");
  const motionBars = new Map<string, HTMLElement>();
  ["Cutoff", "Resonance", "Modulation", "Breath", "Bend"].forEach(name => {
    const row = document.createElement("div");
    const label = document.createElement("span"); label.textContent = name;
    const track = document.createElement("i"); const fill = document.createElement("b");
    track.append(fill); row.append(label, track); motionDisplay.append(row);
    motionBars.set(name, fill);
  });
  instrumentLabel.append(bankTitle, visualBankSelect, instrumentTitle, instrumentSelect);
  stage.append(canvas, phraseDisplay, instrumentLabel, motionDisplay, laneLegend); shell.append(toolbar, stage); editor.append(shell);

  const context = canvas.getContext("2d");
  if (!context) return;
  const particles: VisualParticle[] = [];
  const waves: VisualWave[] = [];
  const fractals: VisualFractal[] = [];
  const words: VisualWord[] = [];
  const forms: VisualForm[] = [];
  const activeNotes = new Map<string, { note: number; lane: number; velocity: number }>();
  const controls = { cutoff: 64, resonance: 32, modulation: 0, breath: 0, bend: 8192 };
  const updateMotion = () => {
    const values = new Map<string, number>([
      ["Cutoff", controls.cutoff / 127], ["Resonance", controls.resonance / 127],
      ["Modulation", controls.modulation / 127], ["Breath", controls.breath / 127],
      ["Bend", controls.bend / 16383],
    ]);
    values.forEach((value, name) => motionBars.get(name)?.style.setProperty("--motion", `${Math.max(0, Math.min(1, value)) * 100}%`));
  };
  updateMotion();
  const phraseTimers: number[] = [];
  let clockPhase = 0, eventEnergy = 0, flashEnergy = 0, distortionEnergy = 0;
  let cameraGlitchX = 0, cameraGlitchY = 0, cameraGlitchZoom = 0, cameraGlitchTilt = 0;
  let lastCameraGlitchAt = -Infinity;
  let eventCount = 0, compositionMode = (patchId() + Number(bankSelect.value) * 3) % 5;
  const sceneCharacters = [
    { name: "Rooted", camera: .35, scale: 1.15, particles: .55, contrast: .55, geometry: 1 },
    { name: "Blooming", camera: .6, scale: 1.3, particles: .75, contrast: .72, geometry: 4 },
    { name: "Liquid", camera: .95, scale: 1.15, particles: .7, contrast: .62, geometry: 4 },
    { name: "Pulse", camera: .48, scale: .8, particles: 1.15, contrast: .9, geometry: 2 },
    { name: "Air", camera: .72, scale: 1.5, particles: .42, contrast: .48, geometry: 5 },
    { name: "Orbit", camera: .85, scale: 1.05, particles: .62, contrast: .78, geometry: 3 },
    { name: "Glass", camera: .3, scale: .72, particles: .85, contrast: 1, geometry: 1 },
    { name: "Storm", camera: 1.4, scale: 1.2, particles: 1.35, contrast: 1, geometry: 0 },
    { name: "Deep", camera: .28, scale: 1.55, particles: .35, contrast: .68, geometry: 5 },
    { name: "Motorik", camera: .5, scale: .78, particles: .8, contrast: .82, geometry: 2 },
    { name: "Tangled", camera: 1.05, scale: .95, particles: 1.05, contrast: .88, geometry: 2 },
    { name: "Cinema", camera: .62, scale: 1.65, particles: .3, contrast: .58, geometry: 5 },
    { name: "Acid", camera: 1.15, scale: .82, particles: 1.1, contrast: 1, geometry: 3 },
    { name: "Fractured", camera: 1.35, scale: .68, particles: 1.4, contrast: .96, geometry: 2 },
    { name: "Phase", camera: .78, scale: 1.05, particles: .55, contrast: .7, geometry: 3 },
    { name: "Pixel", camera: .42, scale: .62, particles: 1.25, contrast: .94, geometry: 0 },
  ];
  const scenePalettes = [
    { background: [7, 10, 7], colours: [[218, 169, 75], [150, 68, 45], [91, 124, 67], [238, 224, 183], [51, 73, 57], [201, 112, 54]] },
    { background: [17, 6, 15], colours: [[244, 91, 106], [231, 113, 183], [255, 190, 52], [112, 190, 113], [255, 224, 194], [150, 82, 183]] },
    { background: [4, 10, 18], colours: [[38, 190, 220], [37, 119, 173], [75, 220, 176], [145, 103, 211], [216, 242, 236], [242, 120, 69]] },
    { background: [8, 8, 8], colours: [[239, 56, 45], [247, 240, 222], [250, 194, 32], [37, 92, 204], [239, 62, 149], [119, 215, 190]] },
    { background: [7, 10, 15], colours: [[173, 214, 239], [196, 181, 231], [225, 232, 226], [238, 208, 140], [112, 204, 207], [125, 147, 191]] },
    { background: [5, 6, 10], colours: [[51, 91, 221], [245, 105, 35], [64, 194, 223], [251, 202, 37], [217, 79, 137], [221, 229, 239]] },
    { background: [6, 11, 14], colours: [[169, 231, 210], [226, 239, 232], [145, 189, 225], [188, 159, 222], [241, 179, 71], [74, 141, 157]] },
    { background: [5, 4, 8], colours: [[143, 65, 232], [190, 240, 45], [239, 55, 60], [45, 205, 225], [244, 241, 232], [241, 91, 190]] },
    { background: [3, 8, 14], colours: [[31, 89, 139], [58, 142, 172], [129, 42, 57], [224, 173, 68], [150, 189, 204], [14, 35, 52]] },
    { background: [7, 7, 7], colours: [[213, 217, 210], [218, 48, 36], [242, 117, 34], [237, 221, 179], [89, 117, 128], [48, 62, 65]] },
    { background: [5, 12, 8], colours: [[145, 210, 55], [225, 61, 151], [241, 135, 42], [231, 227, 185], [42, 151, 133], [112, 87, 196]] },
    { background: [8, 7, 6], colours: [[233, 174, 61], [183, 50, 40], [237, 223, 192], [41, 129, 132], [151, 101, 53], [101, 74, 116]] },
    { background: [6, 6, 7], colours: [[216, 238, 31], [238, 39, 155], [25, 202, 223], [244, 104, 26], [145, 69, 224], [239, 238, 210]] },
    { background: [5, 5, 6], colours: [[241, 239, 224], [236, 49, 41], [35, 92, 222], [128, 232, 44], [244, 121, 31], [199, 58, 187]] },
    { background: [9, 5, 14], colours: [[171, 116, 224], [60, 191, 222], [226, 96, 153], [238, 222, 190], [54, 77, 188], [132, 211, 175]] },
    { background: [4, 5, 5], colours: [[240, 47, 42], [43, 204, 79], [38, 102, 229], [246, 207, 36], [232, 48, 190], [231, 237, 229]] },
  ];
  let sceneCharacter = sceneCharacters[patchId() % 16];
  let scenePalette = scenePalettes[patchId() % 16];
  let lastTime = performance.now(), clearRequested = true, lastNoteAt = 0;
  const bankColour = () => {
    const red = Math.round((state.bank[14] ?? 72) / 127 * 255);
    const green = Math.round((state.bank[15] ?? 84) / 127 * 255);
    const blue = Math.round((state.bank[16] ?? 42) / 127 * 255);
    return [red, green, blue];
  };
  const laneColour = (lane: number, alpha: number) => {
    const [red, green, blue] = bankColour();
    const accent = scenePalette.colours[(lane * 2 + compositionMode) % scenePalette.colours.length];
    return `rgba(${Math.round(accent[0] * .82 + red * .18)},${Math.round(accent[1] * .82 + green * .18)},${Math.round(accent[2] * .82 + blue * .18)},${alpha})`;
  };
  const formColour = (lane: number, variant: number, alpha: number) => {
    const [red, green, blue] = bankColour();
    const accent = scenePalette.colours[
      (variant + lane * 2 + compositionMode) % scenePalette.colours.length];
    const mix = .62 + sceneCharacter.contrast * .3;
    return `rgba(${Math.round(accent[0] * mix + red * (1 - mix))},${Math.round(accent[1] * mix + green * (1 - mix))},${Math.round(accent[2] * mix + blue * (1 - mix))},${alpha})`;
  };
  const spawnForms = (note: number, velocity: number, lane: number, x: number, y: number) => {
    const width = canvas.clientWidth || 800, height = canvas.clientHeight || 500;
    const count = 1 + Math.round(sceneCharacter.particles) + (velocity > 84 ? 1 : 0);
    for (let index = 0; index < count; index++) {
      const sequence = eventCount * 7 + index * 11 + note + lane * 17;
      const anchorX = ((sequence * .61803398875) % 1 + 1) % 1;
      const anchorY = ((sequence * .41421356237) % 1 + 1) % 1;
      const scale = .55 + velocity / 150;
      forms.push({
        kind: (sequence + compositionMode + sceneCharacter.geometry) % 5,
        x: index === 0 ? x : width * (.06 + anchorX * .88),
        y: index === 0 ? y : height * (.08 + anchorY * .84),
        width: Math.min(width * .58, (54 + ((sequence * 29) % 170)) * scale * sceneCharacter.scale),
        height: Math.min(height * .48, (32 + ((sequence * 13) % 105)) * scale * sceneCharacter.scale),
        life: 0, maxLife: 180 + ((sequence * 19) % 230), lane,
        angle: ((sequence * 41) % 360) * Math.PI / 180,
        spin: (((sequence * 23) % 21) - 10) * .00008,
        phase: sequence * .37, segments: 3 + sequence % 8,
        variant: sequence % 6,
      });
    }
    // Periodically replace local decoration with one broad compositional field.
    if (eventCount % 7 === 0) {
      forms.push({ kind: 5, x: width * (.25 + anchorFraction(eventCount * 5) * .5),
        y: height * (.24 + anchorFraction(eventCount * 9) * .52),
        width: width * (.42 + anchorFraction(eventCount * 13) * .38),
        height: height * (.34 + anchorFraction(eventCount * 17) * .34),
        life: 0, maxLife: 520, lane, angle: (eventCount % 12) * .21,
        spin: .000018 * (lane - 1), phase: eventCount, segments: 4, variant: eventCount % 6 });
    }
    if (forms.length > 110) forms.splice(0, forms.length - 110);
  };
  const anchorFraction = (value: number) => ((value * .61803398875) % 1 + 1) % 1;
  bankNames.slice(0, capabilities?.bankCount ?? bankNames.length).forEach((name, bank) =>
    visualBankSelect.add(new Option(`${bank} · ${name}`, String(bank))));
  visualBankSelect.value = bankSelect.value;
  sceneCharacters.forEach((character, program) =>
    instrumentSelect.add(new Option(`${program + 1} · ${character.name}`, String(program))));
  instrumentSelect.value = String(patchId() % 16);
  let visualTarget = patchId();
  let phraseRefreshGeneration = 0;
  const applyVisualTarget = (target: number) => {
    const targetChanged = target !== visualTarget;
    visualTarget = target;
    const program = target % 16;
    bankSelect.value = String(Math.floor(target / 16));
    visualBankSelect.value = bankSelect.value;
    patchSelect.value = String(program);
    instrumentSelect.value = String(program);
    sceneCharacter = sceneCharacters[program];
    scenePalette = scenePalettes[program];
    compositionMode = (target + Math.floor(target / 16) * 3) % 5;
    eventCount += 5; distortionEnergy = Math.max(distortionEnergy, .18);
    activityText.textContent = `Instrument ${program + 1} · ${sceneCharacter.name}`;
    if (targetChanged && (capabilities?.speechPhrases ?? 0) > 0) {
      const generation = ++phraseRefreshGeneration;
      void (async () => {
        const refreshed: string[] = [];
        for (let phrase = 0; phrase < capabilities!.speechPhrases; phrase++) {
          if (generation !== phraseRefreshGeneration || patchId() !== target) return;
          refreshed[phrase] = await connection.getActivePhrase(phrase);
        }
        if (generation === phraseRefreshGeneration && patchId() === target) {
          state.phrases = refreshed;
        }
      })().catch(error => setStatus((error as Error).message, true));
    }
    return true;
  };
  visualPatchFollower = applyVisualTarget;
  instrumentSelect.addEventListener("change", () => void run(async () => {
    const target = Number(visualBankSelect.value) * 16 + Number(instrumentSelect.value);
    applyVisualTarget(target);
    await connection.request(Command.Select, connection.targetBytes(target));
  }, "Changing instrument…"));
  visualBankSelect.addEventListener("change", () => void run(async () => {
    const target = Number(visualBankSelect.value) * 16 + Number(instrumentSelect.value);
    applyVisualTarget(target);
    await connection.request(Command.Select, connection.targetBytes(target));
  }, "Changing bank…"));
  const spawnNote = (note: number, velocity: number, lane: number) => {
    const width = canvas.clientWidth || 800, height = canvas.clientHeight || 500;
    eventCount++;
    // Pitch, lane, and event order select a rotating set of anchors across the
    // full canvas. Lanes are distinguished by colour and motion, not by being
    // trapped inside three horizontal strips.
    const x = width * (.04 + anchorFraction(note * 3 + eventCount * 11 + lane * 23) * .92);
    const y = height * (.04 + anchorFraction(note * 7 + eventCount * 17 + lane * 31) * .92);
    const count = Math.max(2, Math.round((3 + velocity / 25) * sceneCharacter.particles));
    for (let index = 0; index < count; index++) {
      const mode = Math.random();
      const angle = mode < .28
        ? (Math.random() < .5 ? 0 : Math.PI) + (Math.random() - .5) * .45
        : Math.random() * Math.PI * 2;
      const speed = .45 + Math.random() * (2.2 + velocity / 30);
      const distributed = index > 0 && index % 3 !== 0;
      const originX = distributed
        ? width * (.025 + anchorFraction(note * 13 + eventCount * 29 + index * 7) * .95) : x;
      const originY = distributed
        ? height * (.025 + anchorFraction(note * 19 + eventCount * 37 + index * 11) * .95) : y;
      particles.push({ x: originX, y: originY, vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed - (lane - 1) * .18,
        life: 1, size: .8 + Math.random() * (3.5 + velocity / 24), lane, note,
        drag: .986 + Math.random() * .013, curl: (Math.random() - .5) * .055,
        gravity: (Math.random() - .5) * .018, twinkle: Math.random() * Math.PI * 2,
        shape: Math.floor(Math.random() * 3), seed: Math.random() * 1000 });
    }
    waves.push({ x, y, radius: 4, life: 1, lane });
    spawnForms(note, velocity, lane, x, y);
    if (velocity > 62 || Math.random() < .3) {
      fractals.push({ x, y, radius: 3, life: 1, lane,
        branches: 5 + Math.floor(Math.random() * 3), depth: 2 + Math.floor(Math.random() * 3),
        angle: Math.random() * Math.PI * 2, spin: (Math.random() - .5) * .007 });
    }
    if (particles.length > 420) particles.splice(0, particles.length - 420);
    if (waves.length > 80) waves.splice(0, waves.length - 80);
    if (fractals.length > 9) fractals.splice(0, fractals.length - 9);
    eventEnergy = Math.min(1, eventEnergy + velocity / 150);
    const now = performance.now();
    const ratchet = now - lastNoteAt < 145;
    lastNoteAt = now;
    const glitchChance = ratchet ? .42 : .075;
    if (now - lastCameraGlitchAt > 420 && Math.random() < glitchChance) {
      const cameraKick = velocity / 127 * (ratchet ? 1 : .35);
      cameraGlitchX += (Math.random() - .5) * width * .025 * cameraKick;
      cameraGlitchY += (Math.random() - .5) * height * .022 * cameraKick;
      cameraGlitchZoom += (.004 + Math.random() * .012) * cameraKick;
      cameraGlitchTilt += (Math.random() - .5) * .035 * cameraKick;
      cameraGlitchX = Math.max(-width * .04, Math.min(width * .04, cameraGlitchX));
      cameraGlitchY = Math.max(-height * .04, Math.min(height * .04, cameraGlitchY));
      cameraGlitchZoom = Math.min(.05, cameraGlitchZoom);
      cameraGlitchTilt = Math.max(-.04, Math.min(.04, cameraGlitchTilt));
      lastCameraGlitchAt = now;
    }
    flashEnergy = Math.min(1, flashEnergy + velocity / (ratchet ? 100 : 230));
    distortionEnergy = Math.min(1, distortionEnergy + velocity / (ratchet ? 130 : 360));
    dot.classList.add("active");
    activityText.textContent = `${["Bass", "Pad", "Lead"][lane] ?? `Ch ${lane + 1}`} · ${midiNoteName(note)} · ${velocity}`;
    window.setTimeout(() => dot.classList.remove("active"), 90);
  };
  const spawnPhraseWords = (phrase: string) => {
    const width = canvas.clientWidth || 800, height = canvas.clientHeight || 500;
    phrase.split(/\s+/).filter(Boolean).forEach((text, index) => {
      const edge = index % 4;
      const horizontal = edge < 2;
      const x = edge === 0 ? -80 : edge === 1 ? width + 80 : width * (.18 + Math.random() * .64);
      const y = edge === 2 ? -50 : edge === 3 ? height + 50 : height * (.14 + Math.random() * .72);
      const targetX = width * (.34 + Math.random() * .32), targetY = height * (.35 + Math.random() * .3);
      const travel = 220 + Math.random() * 100;
      words.push({ text, x, y, vx: (targetX - x) / travel + (Math.random() - .5) * .25,
        vy: (targetY - y) / travel + (Math.random() - .5) * .25,
        life: -index * 14, maxLife: 280 + Math.random() * 140,
        size: Math.max(20, Math.min(58, 17 + text.length * 3.4)),
        rotation: (Math.random() - .5) * .1,
        rotationVelocity: (Math.random() - .5) * .0025,
        lane: index % 3 });
      if (horizontal) eventEnergy = Math.min(1, eventEnergy + .015);
    });
    if (words.length > 30) words.splice(0, words.length - 30);
  };
  const flashPhraseWords = (phrase: string) => {
    phraseTimers.splice(0).forEach(timer => window.clearTimeout(timer));
    const phraseWords = phrase.split(/\s+/).filter(Boolean);
    const wordDuration = Math.max(220, 145 + (state.speech[2] ?? 72) * 3.2);
    phraseDisplay.setAttribute("aria-label", phrase);
    phraseWords.forEach((word, index) => {
      phraseTimers.push(window.setTimeout(() => {
        phraseDisplay.textContent = word;
        phraseDisplay.classList.remove("visible");
        void phraseDisplay.offsetWidth;
        phraseDisplay.classList.add("visible");
        flashEnergy = Math.min(1, flashEnergy + .24);
      }, index * wordDuration));
    });
    phraseTimers.push(window.setTimeout(() => phraseDisplay.classList.remove("visible"),
      phraseWords.length * wordDuration + 180));
  };
  visualMidiHandler = (event: Event) => {
    const data = (event as CustomEvent<number[]>).detail;
    if (!data?.length) return;
    const statusByte = data[0], type = statusByte & 0xf0, channel = statusByte & 0x0f;
    const lane = Math.min(2, channel);
    if (type === 0x90 && (data[2] ?? 0) > 0) {
      activeNotes.set(`${channel}:${data[1]}`, { note: data[1], lane, velocity: data[2] });
      spawnNote(data[1], data[2], lane);
    } else if (type === 0x80 || (type === 0x90 && (data[2] ?? 0) === 0)) {
      activeNotes.delete(`${channel}:${data[1]}`);
    } else if (type === 0xb0) {
      if (data[1] === 119) {
        const phraseIndex = (data[2] ?? 0) % Math.max(1, state.phrases.length);
        const phrase = state.phrases[phraseIndex] ?? `Phrase ${phraseIndex + 1}`;
        flashPhraseWords(phrase);
        spawnPhraseWords(phrase);
      } else if (data[1] === 74) controls.cutoff = data[2];
      else if (data[1] === 71) controls.resonance = data[2];
      else if (data[1] === 1) controls.modulation = data[2];
      else if (data[1] === 2) controls.breath = data[2];
      updateMotion();
    } else if (type === 0xe0) {
      controls.bend = (data[1] ?? 0) | ((data[2] ?? 64) << 7);
      updateMotion();
    } else if (type === 0xc0) {
      compositionMode = ((data[1] ?? 0) + Number(bankSelect.value) * 3) % 5;
      eventCount += 3;
      distortionEnergy = Math.max(distortionEnergy, .12);
    } else if (statusByte === 0xf8) {
      clockPhase = (clockPhase + 1) % 24;
      if (clockPhase % 6 === 0) eventEnergy = Math.min(1, eventEnergy + .06);
    }
  };
  connection.addEventListener("midi", visualMidiHandler);
  clear.addEventListener("click", () => { particles.length = 0; waves.length = 0; words.length = 0; fractals.length = 0; forms.length = 0; clearRequested = true; });
  fullscreen.addEventListener("click", () => void stage.requestFullscreen());

  const drawFractalBranch = (x: number, y: number, length: number, angle: number,
    depth: number, spread: number, lane: number, alpha: number): void => {
    if (depth <= 0 || length < 1) return;
    const endX = x + Math.cos(angle) * length;
    const endY = y + Math.sin(angle) * length;
    context.beginPath(); context.moveTo(x, y); context.lineTo(endX, endY);
    context.strokeStyle = laneColour(lane, alpha); context.lineWidth = Math.max(.45, depth * .42);
    context.stroke();
    if (depth === 1) {
      context.beginPath(); context.fillStyle = laneColour(lane, alpha * (Math.random() > .78 ? 1.8 : .7));
      context.arc(endX, endY, Math.random() > .86 ? 2.3 : .75, 0, Math.PI * 2); context.fill();
      return;
    }
    drawFractalBranch(endX, endY, length * .64, angle + spread, depth - 1, spread * .88, lane, alpha * .82);
    drawFractalBranch(endX, endY, length * .64, angle - spread, depth - 1, spread * .88, lane, alpha * .82);
  };

  const draw = (time: number) => {
    if (activeTab !== "visuals" || !canvas.isConnected) { stopVisualPerformance(); return; }
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(320, Math.floor(stage.clientWidth));
    const height = Math.max(320, Math.floor(stage.clientHeight));
    if (canvas.width !== Math.floor(width * ratio) || canvas.height !== Math.floor(height * ratio)) {
      canvas.width = Math.floor(width * ratio); canvas.height = Math.floor(height * ratio);
      clearRequested = true;
    }
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    const persistence = Number(trails.value);
    const [backgroundRed, backgroundGreen, backgroundBlue] = scenePalette.background;
    context.fillStyle = clearRequested
      ? `rgb(${backgroundRed},${backgroundGreen},${backgroundBlue})`
      : `rgba(${backgroundRed},${backgroundGreen},${backgroundBlue},${.025 + (20 - persistence) * .012})`;
    context.fillRect(0, 0, width, height); clearRequested = false;
    const elapsed = Math.min(2.5, (time - lastTime) / 16.67); lastTime = time;
    const bend = (controls.bend - 8192) / 8192;
    const swirl = controls.modulation / 127;
    const glow = controls.breath / 127;
    const centerX = width * (.5 + bend * .23 + Math.sin(time * .00013) * (.035 + swirl * .07));
    const centerY = height * (.5 + Math.cos(time * .00011) * (.025 + glow * .055));

    // Treat the canvas as a moving camera rather than moving every object in
    // lockstep. Each instrument profile has its own restrained pan, zoom,
    // rotation, and shear range; live CC motion animates within that range.
    const cameraDrive = sceneCharacter.camera;
    // Independent time-based camera cycles continue whether or not MIDI events
    // or instrument changes occur: pan ≈55 s, zoom ≈90 s, tilt ≈70 s.
    const slowPanPhase = time * .000114 + compositionMode * 1.13;
    const slowZoomPhase = time * .00007 + compositionMode * .71;
    const slowPanX = (Math.sin(slowPanPhase) + Math.sin(slowPanPhase * .43 + 1.8) * .42) *
      width * (.17 + cameraDrive * .075);
    const slowPanY = (Math.cos(slowPanPhase * .69) + Math.sin(slowPanPhase * .31) * .35) *
      height * (.14 + cameraDrive * .065);
    const slowZoom = .32 + (.5 + .5 * Math.sin(slowZoomPhase)) *
      (.28 + cameraDrive * .12);
    const slowTilt = Math.sin(time * .00009 + compositionMode * .9) *
      (.16 + cameraDrive * .08);
    const cameraPanX = slowPanX + cameraGlitchX +
      Math.sin(time * (.000075 + swirl * .00008) + compositionMode) *
      width * (.003 + cameraDrive * .006) + bend * width * .012;
    const cameraPanY = slowPanY + cameraGlitchY +
      Math.cos(time * (.000061 + glow * .000055) + compositionMode * .7) *
      height * (.003 + cameraDrive * .005);
    const cameraScale = 1.025 + slowZoom + cameraGlitchZoom + cameraDrive * .025 +
      Math.sin(time * .00009 + eventEnergy) * (.006 + glow * .018);
    const cameraAngle = slowTilt + cameraGlitchTilt +
      Math.sin(time * .000052 + compositionMode) * cameraDrive * .006 + bend * .008;
    const cameraShear = Math.sin(time * .000043 + controls.resonance) * swirl * cameraDrive * .006;
    context.save(); context.translate(width * .5 + cameraPanX, height * .5 + cameraPanY);
    context.rotate(cameraAngle); context.transform(1, cameraShear, -cameraShear * .45, 1, 0, 0);
    context.scale(cameraScale, cameraScale); context.translate(-width * .5, -height * .5);

    // DANS-inspired slow macro layers keep the field alive between events.
    const [ambientRed, ambientGreen, ambientBlue] = bankColour();
    for (let layer = 0; layer < 4; layer++) {
      const phase = time * (.00006 + layer * .000012) + layer * 1.7;
      const x = width * (.18 + layer * .21) + Math.sin(phase) * width * .08;
      const y = height * (.28 + (layer & 1) * .3) + Math.cos(phase * .73) * height * .08;
      const radius = Math.min(width, height) * (.18 + layer * .035) *
        (.8 + controls.breath / 260);
      const orb = context.createRadialGradient(x, y, 0, x, y, radius);
      orb.addColorStop(0, `rgba(${ambientRed},${ambientGreen},${ambientBlue},${.018 + controls.modulation / 9000})`);
      orb.addColorStop(1, `rgba(${ambientRed},${ambientGreen},${ambientBlue},0)`);
      context.fillStyle = orb; context.fillRect(x - radius, y - radius, radius * 2, radius * 2);
    }

    // Notes assemble a changing graphic composition: broad colour fields,
    // translucent beams, orbital discs, line constellations and curved ribbons.
    // Particles remain only as a fine layer between these larger structures.
    forms.forEach(form => {
      form.life += elapsed; form.angle += form.spin * elapsed;
      const progress = form.life / form.maxLife;
      const fade = Math.min(1, progress * 9, (1 - progress) * 5);
      if (fade <= 0) return;
      const breathe = 1 + Math.sin(time * .0007 + form.phase) * (.018 + controls.breath / 4200);
      const width = form.width * breathe, height = form.height * breathe;
      context.save(); context.translate(form.x, form.y); context.rotate(form.angle);
      if (form.kind === 5) {
        const field = context.createRadialGradient(0, 0, height * .04, 0, 0, width * .58);
        field.addColorStop(0, formColour(form.lane, form.variant, fade * .105));
        field.addColorStop(.72, formColour(form.lane, form.variant + 1, fade * .06));
        field.addColorStop(1, formColour(form.lane, form.variant + 1, 0));
        context.fillStyle = field; context.beginPath();
        context.ellipse(0, 0, width * .55, height * .55, 0, 0, Math.PI * 2); context.fill();
      } else if (form.kind === 0) {
        const beam = context.createLinearGradient(-width * .55, 0, width * .55, 0);
        beam.addColorStop(0, formColour(form.lane, form.variant, fade * .08));
        beam.addColorStop(.72, formColour(form.lane, form.variant + 1, fade * .22));
        beam.addColorStop(1, formColour(form.lane, form.variant + 2, fade * .5));
        context.fillStyle = beam; context.beginPath();
        context.moveTo(-width * .5, -height * .12); context.lineTo(width * .42, -height * .48);
        context.arc(width * .42, 0, height * .48, -Math.PI / 2, Math.PI / 2);
        context.lineTo(-width * .5, height * .12); context.closePath(); context.fill();
        context.strokeStyle = formColour(form.lane, form.variant + 2, fade * .34);
        context.lineWidth = .8; context.stroke();
      } else if (form.kind === 1) {
        const disc = context.createRadialGradient(-width * .08, -height * .08, 1, 0, 0, width * .5);
        disc.addColorStop(0, formColour(form.lane, form.variant + 2, fade * .72));
        disc.addColorStop(.58, formColour(form.lane, form.variant, fade * .34));
        disc.addColorStop(1, formColour(form.lane, form.variant + 4, fade * .04));
        context.fillStyle = disc; context.beginPath();
        context.ellipse(0, 0, width * .5, height * .5, 0, 0, Math.PI * 2); context.fill();
        for (let ring = 1; ring <= 3; ring++) {
          context.beginPath(); context.strokeStyle = formColour(form.lane, form.variant + ring, fade * (.15 / ring));
          context.lineWidth = .6; context.ellipse(0, 0, width * (.18 + ring * .09),
            height * (.18 + ring * .09), 0, time * .0001 * ring, Math.PI * (1.1 + ring * .22)); context.stroke();
        }
      } else if (form.kind === 2) {
        context.strokeStyle = formColour(form.lane, form.variant, fade * .3);
        context.lineWidth = .6 + controls.resonance / 180;
        context.beginPath(); context.moveTo(-width * .5, height * .28);
        for (let segment = 0; segment <= form.segments; segment++) {
          const px = -width * .5 + width * segment / form.segments;
          const py = Math.sin(segment * 2.7 + form.phase) * height * .28;
          context.lineTo(px, py);
        }
        context.stroke();
        for (let segment = 0; segment <= form.segments; segment++) {
          const px = -width * .5 + width * segment / form.segments;
          const py = Math.sin(segment * 2.7 + form.phase) * height * .28;
          const stem = height * (.15 + anchorFraction(segment * 7 + form.phase) * .65);
          context.beginPath(); context.strokeStyle = formColour(form.lane, form.variant + segment, fade * .23);
          context.moveTo(px, py - stem * .5); context.lineTo(px, py + stem * .5); context.stroke();
          context.beginPath(); context.fillStyle = formColour(form.lane, form.variant + segment + 2, fade * .8);
          context.arc(px, py, segment % 3 === 0 ? 2.4 : 1.1, 0, Math.PI * 2); context.fill();
        }
      } else if (form.kind === 3) {
        for (let segment = 0; segment < form.segments; segment++) {
          const start = segment / form.segments * Math.PI * 1.65 + time * .00008;
          const radius = width * (.15 + segment / form.segments * .35);
          context.beginPath(); context.strokeStyle = formColour(form.lane, form.variant + segment, fade * .22);
          context.lineWidth = .65 + (segment % 3) * .45;
          context.arc(0, 0, radius, start, start + Math.PI * (.32 + controls.modulation / 500)); context.stroke();
          context.beginPath(); context.moveTo(0, 0);
          context.lineTo(Math.cos(start) * radius, Math.sin(start) * radius * height / width);
          context.strokeStyle = formColour(form.lane, form.variant + segment + 1, fade * .1); context.stroke();
        }
      } else {
        context.beginPath(); context.moveTo(-width * .52, 0);
        context.bezierCurveTo(-width * .2, -height * (.55 + swirl * .35),
          width * .18, height * (.55 + glow * .25), width * .52, 0);
        context.strokeStyle = formColour(form.lane, form.variant, fade * .13);
        context.lineWidth = height * (.28 + controls.resonance / 380); context.lineCap = "round"; context.stroke();
        context.strokeStyle = formColour(form.lane, form.variant + 2, fade * .6);
        context.lineWidth = 1; context.stroke();
      }
      context.restore();
    });

    context.globalCompositeOperation = "lighter";
    // The continuously changing CC values form a breathing orbital field,
    // making parameter motion visible even between discrete note bursts.
    for (let orbit = 0; orbit < 3; orbit++) {
      const baseRadius = width * (.1 + orbit * .13) *
        (.65 + controls.cutoff / 160);
      context.beginPath();
      for (let point = 0; point <= 72; point++) {
        const angle = point / 72 * Math.PI * 2;
        const wobble = Math.sin(angle * (2 + orbit) + time * (.0002 + controls.modulation / 280000)) *
          (4 + controls.resonance / 5);
        const radius = baseRadius + wobble;
        const x = centerX + Math.cos(angle) * radius;
        const y = centerY + Math.sin(angle) * radius * (.55 + controls.breath / 280);
        if (point === 0) context.moveTo(x, y); else context.lineTo(x, y);
      }
      context.strokeStyle = laneColour(orbit, .045 + eventEnergy * .08);
      context.lineWidth = .7 + controls.resonance / 100;
      context.stroke();
    }
    for (const note of activeNotes.values()) {
      if (Math.random() < .025 + note.velocity / 4000) spawnNote(note.note, Math.max(20, note.velocity / 2), note.lane);
    }
    const linkDistance = 75 + controls.resonance * .9;
    for (let first = 0; first < particles.length; first += particles.length > 700 ? 2 : 1) {
      const a = particles[first];
      for (let second = first + 1; second < Math.min(particles.length, first + 8); second++) {
        const b = particles[second];
        const distance = Math.hypot(a.x - b.x, a.y - b.y);
        if (distance > linkDistance) continue;
        const alpha = (1 - distance / linkDistance) * Math.min(a.life, b.life) *
          (.035 + controls.modulation / 2500);
        context.beginPath(); context.strokeStyle = laneColour(a.lane, alpha);
        context.lineWidth = .45 + controls.resonance / 210;
        context.moveTo(a.x, a.y); context.lineTo(b.x, b.y); context.stroke();
      }
    }
    particles.forEach(particle => {
      const dx = particle.x - centerX, dy = particle.y - centerY;
      const distance = Math.max(90, Math.hypot(dx, dy));
      particle.vx += (-dy / distance) * (swirl * .022 + particle.curl) * elapsed;
      particle.vy += ((dx / distance) * (swirl * .022 + particle.curl) + particle.gravity) * elapsed;
      particle.vx += Math.sin(time * .0007 + particle.seed) * controls.modulation / 95000 * elapsed;
      particle.vy += Math.cos(time * .0009 + particle.seed) * controls.breath / 110000 * elapsed;
      particle.vx *= Math.pow(particle.drag, elapsed);
      particle.vy *= Math.pow(particle.drag, elapsed);
      particle.x += particle.vx * elapsed; particle.y += particle.vy * elapsed;
      if (particle.x < -55 && particle.vx < 0) particle.vx = Math.abs(particle.vx) * .92;
      if (particle.x > width + 55 && particle.vx > 0) particle.vx = -Math.abs(particle.vx) * .92;
      if (particle.y < -55 && particle.vy < 0) particle.vy = Math.abs(particle.vy) * .92;
      if (particle.y > height + 55 && particle.vy > 0) particle.vy = -Math.abs(particle.vy) * .92;
      particle.life -= (.0026 + controls.cutoff / 62000) * elapsed;
      const shimmer = .55 + .45 * Math.sin(time * (.004 + particle.twinkle * .0003) + particle.seed);
      const alpha = Math.max(0, particle.life) * (.2 + glow * .48) * shimmer;
      const size = particle.size * (1 + glow * .9);
      context.beginPath(); context.fillStyle = laneColour(particle.lane, alpha);
      context.strokeStyle = laneColour(particle.lane, alpha * .82);
      context.lineWidth = Math.max(.5, size * .36);
      if (particle.shape === 0) context.arc(particle.x, particle.y, size, 0, Math.PI * 2);
      else if (particle.shape === 1) context.rect(particle.x - size * .65, particle.y - size * .65, size * 1.3, size * 1.3);
      else { context.moveTo(particle.x - size * 2, particle.y); context.lineTo(particle.x + size * 2, particle.y + particle.vy); }
      particle.shape === 2 ? context.stroke() : context.fill();
    });
    waves.forEach(wave => {
      wave.radius += (1.1 + controls.resonance / 42) * elapsed; wave.life -= .012 * elapsed;
      context.beginPath(); context.strokeStyle = laneColour(wave.lane, Math.max(0, wave.life) * .55);
      context.lineWidth = 1 + controls.resonance / 52;
      context.arc(wave.x, wave.y, wave.radius, 0, Math.PI * 2); context.stroke();
    });
    fractals.forEach(fractal => {
      fractal.radius += (.42 + controls.cutoff / 180) * elapsed;
      fractal.life -= (.0025 + controls.resonance / 90000) * elapsed;
      fractal.angle += fractal.spin * elapsed;
      const grow = Math.min(1, (1 - fractal.life) * 4.5);
      const glimmer = .45 + .55 * Math.sin(time * .01 + fractal.angle * 7);
      const alpha = Math.max(0, fractal.life) * (.045 + glimmer * .13 + flashEnergy * .08);
      for (let branch = 0; branch < fractal.branches; branch++) {
        const angle = fractal.angle + branch / fractal.branches * Math.PI * 2;
        drawFractalBranch(fractal.x, fractal.y, fractal.radius * grow, angle,
          fractal.depth, .5 + controls.modulation / 420, fractal.lane, alpha);
      }
    });
    // Spoken phrases become a temporary constellation of words. Nearby words
    // form faint field lines and drift toward the parameter-controlled centre.
    for (let first = 0; first < words.length; first++) {
      const a = words[first];
      if (a.life <= 0) continue;
      for (let second = first + 1; second < Math.min(words.length, first + 5); second++) {
        const b = words[second]; if (b.life <= 0) continue;
        const distance = Math.hypot(a.x - b.x, a.y - b.y);
        if (distance > 190) continue;
        context.beginPath(); context.strokeStyle = laneColour(a.lane, (1 - distance / 190) * .1);
        context.lineWidth = 1; context.moveTo(a.x, a.y); context.lineTo(b.x, b.y); context.stroke();
      }
    }
    words.forEach(word => {
      word.life += elapsed;
      if (word.life <= 0) return;
      const fadeIn = Math.min(1, word.life / 20);
      const fadeOut = Math.min(1, (word.maxLife - word.life) / 80);
      const alpha = Math.max(0, fadeIn * fadeOut);
      word.vx += (centerX - word.x) * .000018 * elapsed +
        Math.sin(time * .001 + word.lane) * controls.modulation / 180000 * elapsed;
      word.vy += (centerY - word.y) * .000018 * elapsed +
        Math.cos(time * .0012 + word.lane) * controls.resonance / 190000 * elapsed;
      word.vx *= Math.pow(.997, elapsed); word.vy *= Math.pow(.997, elapsed);
      word.x += word.vx * elapsed; word.y += word.vy * elapsed;
      word.rotation += word.rotationVelocity * elapsed;
      context.save(); context.translate(word.x, word.y); context.rotate(word.rotation);
      context.textAlign = "center"; context.textBaseline = "middle";
      context.font = `500 ${word.size}px Inter, system-ui, sans-serif`;
      context.shadowBlur = 12 + controls.breath / 4;
      context.shadowColor = laneColour(word.lane, alpha * .8);
      context.fillStyle = laneColour(word.lane, alpha * .82);
      context.fillText(word.text, 0, 0);
      context.shadowBlur = 0; context.strokeStyle = laneColour(word.lane, alpha * .22);
      context.lineWidth = 1; context.strokeText(word.text, 0, 0); context.restore();
    });
    for (let index = particles.length - 1; index >= 0; index--) if (particles[index].life <= 0) particles.splice(index, 1);
    for (let index = waves.length - 1; index >= 0; index--) if (waves[index].life <= 0) waves.splice(index, 1);
    for (let index = words.length - 1; index >= 0; index--) if (words[index].life >= words[index].maxLife) words.splice(index, 1);
    for (let index = fractals.length - 1; index >= 0; index--) if (fractals[index].life <= 0) fractals.splice(index, 1);
    for (let index = forms.length - 1; index >= 0; index--) if (forms[index].life >= forms[index].maxLife) forms.splice(index, 1);
    eventEnergy *= Math.pow(.985, elapsed);
    context.globalCompositeOperation = "source-over";
    const pulse = 5 + eventEnergy * 28 + (clockPhase % 6 === 0 ? 2 : 0);
    const gradient = context.createRadialGradient(centerX, centerY, 0, centerX, centerY, pulse * 5);
    const [red, green, blue] = bankColour();
    gradient.addColorStop(0, `rgba(${red},${green},${blue},${.08 + eventEnergy * .18})`);
    gradient.addColorStop(1, `rgba(${red},${green},${blue},0)`);
    context.fillStyle = gradient; context.fillRect(centerX - pulse * 5, centerY - pulse * 5, pulse * 10, pulse * 10);

    context.restore();

    if (distortionEnergy > .025) {
      context.save(); context.globalAlpha = Math.min(.32, distortionEnergy * .3);
      const slices = 1 + Math.floor(distortionEnergy * 7);
      for (let slice = 0; slice < slices; slice++) {
        const sliceY = Math.random() * height;
        const sliceHeight = 2 + Math.random() * (5 + distortionEnergy * 24);
        const shift = (Math.random() - .5) * width * .12 * distortionEnergy;
        context.drawImage(canvas, 0, sliceY * ratio, canvas.width,
          sliceHeight * ratio, shift, sliceY, width, sliceHeight);
        context.fillStyle = slice & 1
          ? `rgba(${red},${Math.round(green * .3)},${blue},${distortionEnergy * .08})`
          : `rgba(${Math.round(red * .25)},${green},${blue},${distortionEnergy * .06})`;
        context.fillRect(0, sliceY, width, Math.max(1, sliceHeight * .18));
      }
      context.restore();
    }
    if (flashEnergy > .012) {
      // Ratchets illuminate the edges of the composition rather than washing
      // the complete screen white. Multiple offset contours make the flash
      // readable at a distance while preserving colour and depth beneath it.
      context.save(); context.globalCompositeOperation = "lighter";
      const contourAlpha = Math.min(.62, flashEnergy * .52);
      const contourCount = 2 + Math.floor(flashEnergy * 4);
      context.strokeStyle = `rgba(244,247,236,${contourAlpha})`;
      context.lineCap = "round";
      for (let contour = 0; contour < contourCount; contour++) {
        const offset = contour * (7 + controls.resonance / 20);
        const radiusX = Math.min(width, height) * (.08 + contour * .045) + eventEnergy * 28;
        const radiusY = radiusX * (.48 + controls.breath / 260);
        const start = time * .00022 + contour * 1.37 + bend;
        context.beginPath(); context.lineWidth = contour === 0 ? 1.8 : .65;
        context.ellipse(centerX + Math.cos(start) * offset,
          centerY + Math.sin(start * 1.3) * offset, radiusX, radiusY,
          start * .18, start, start + Math.PI * (1.15 + swirl * .65));
        context.stroke();
      }
      // Briefly retrace the newest large forms in white. These are outlines,
      // not duplicate fills, so their internal colour remains visible.
      forms.slice(-Math.min(8, forms.length)).forEach((form, index) => {
        const progress = form.life / form.maxLife;
        if (progress >= 1) return;
        context.save(); context.translate(form.x, form.y); context.rotate(form.angle);
        context.strokeStyle = `rgba(244,247,236,${contourAlpha * (1 - index / 10)})`;
        context.lineWidth = index === 0 ? 1.5 : .55;
        context.beginPath();
        if (form.kind === 0) {
          context.moveTo(-form.width * .5, -form.height * .12);
          context.lineTo(form.width * .42, -form.height * .48);
          context.arc(form.width * .42, 0, form.height * .48, -Math.PI / 2, Math.PI / 2);
          context.lineTo(-form.width * .5, form.height * .12); context.closePath();
        } else if (form.kind === 4) {
          context.moveTo(-form.width * .52, 0);
          context.bezierCurveTo(-form.width * .2, -form.height * (.55 + swirl * .35),
            form.width * .18, form.height * (.55 + glow * .25), form.width * .52, 0);
        } else {
          context.ellipse(0, 0, form.width * .5, form.height * .5, 0,
            form.phase % Math.PI, form.phase % Math.PI + Math.PI * 1.62);
        }
        context.stroke(); context.restore();
      });
      context.restore();
    }
    flashEnergy *= Math.pow(.82, elapsed);
    distortionEnergy *= Math.pow(.91, elapsed);
    const cameraGlitchDecay = Math.pow(.72, elapsed);
    cameraGlitchX *= cameraGlitchDecay;
    cameraGlitchY *= cameraGlitchDecay;
    cameraGlitchZoom *= Math.pow(.66, elapsed);
    cameraGlitchTilt *= Math.pow(.69, elapsed);
    visualAnimation = requestAnimationFrame(draw);
  };
  visualAnimation = requestAnimationFrame(draw);
}

function renderVoice(): void {
  if (!capabilities?.speechParameters) {
    editor.replaceChildren(); editor.classList.add("empty");
    editor.textContent = "SAM voice requires the experimental voice firmware.";
    return;
  }
  renderParameters(voiceParameters.slice(0, capabilities.speechParameters),
    state.speech, "patch", 4);
  const presetSection = document.createElement("section");
  presetSection.className = "parameter-section voice-presets";
  const presetHeading = document.createElement("h2"); presetHeading.textContent = "Voice character presets";
  const presetBar = document.createElement("div"); presetBar.className = "scale-preset-bar";
  const presetLabel = document.createElement("label"); presetLabel.textContent = "Starting character";
  const presetSelect = document.createElement("select");
  presetSelect.add(new Option("Custom", ""));
  voicePresets.forEach((preset, index) => presetSelect.add(new Option(preset.name, String(index))));
  const apply = document.createElement("button"); apply.textContent = "Apply voice";
  apply.addEventListener("click", () => void run(async () => {
    const preset = voicePresets[Number(presetSelect.value)];
    if (!preset) return;
    const values = [preset.speed, preset.pitch, preset.mouth, preset.throat];
    for (let index = 0; index < values.length; index++) {
      state.speech[index + 2] = values[index];
      await connection.request(Command.Set, addressed(Scope.Patch, patchId(),
        [4, index + 2, ...valueBytes(values[index])]));
    }
    markDirty("patch"); renderVoice(); setStatus(`${preset.name} voice applied.`);
  }, "Applying voice character…"));
  presetLabel.append(presetSelect); presetBar.append(presetLabel, apply);
  presetSection.append(presetHeading, presetBar); editor.append(presetSection);

  const phrases = document.createElement("section"); phrases.className = "parameter-section";
  const heading = document.createElement("h2"); heading.textContent = "Generative sentences";
  const help = document.createElement("p"); help.className = "voice-help";
  help.textContent = "Up to ten phrases are selected by the generative sequence. SAM punctuation and phonetic spelling can shape the delivery.";
  const grid = document.createElement("div"); grid.className = "phrase-grid";
  for (let index = 0; index < capabilities.speechPhrases; index++) {
    const label = document.createElement("label"); label.textContent = `Phrase ${index + 1}`;
    const area = document.createElement("textarea"); area.maxLength = capabilities.speechPhraseLength - 1;
    area.rows = 3; area.value = state.phrases[index] ?? "";
    const counter = document.createElement("output");
    const refresh = () => { counter.textContent = `${area.value.length}/${area.maxLength}`; };
    refresh();
    area.addEventListener("input", () => {
      state.phrases[index] = area.value; refresh(); markDirty("patch");
      const key = `phrase:${patchId()}:${index}`;
      window.clearTimeout(sendTimers.get(key));
      sendTimers.set(key, window.setTimeout(async () => {
        try { await connection.setPhrase(patchId(), index, area.value); }
        catch (error) { setStatus((error as Error).message, true); }
      }, 350));
    });
    label.append(area, counter); grid.append(label);
  }
  phrases.append(heading, help, grid); editor.append(phrases);
}

function render(): void {
  stopVisualPerformance();
  if (sensorTimer) { window.clearTimeout(sensorTimer); sensorTimer = undefined; }
  if (!capabilities) return;
  if (activeTab === "bass" || activeTab === "pad" || activeTab === "lead") {
    const lane = { bass: 0, pad: 1, lead: 2 }[activeTab];
    const visible = sceneParameters.filter(parameter => lane === 1 || !["Chorus", "Delay"].includes(parameter.section)).map(parameter =>
      lane === 1 && ["Chorus", "Delay"].includes(parameter.section)
        ? { ...parameter, section: "Shared effects", name: `${parameter.section} ${parameter.name}` }
        : parameter);
    renderParameters(visible, state.lanes[lane], "patch", lane);
  } else if (activeTab === "voice") renderVoice();
  else if (activeTab === "visuals") renderVisualPerformance();
  else if (activeTab === "sequence") renderParameters(patchSharedParameters.filter(parameter => parameter.id < 35 || parameter.id === 99 || parameter.id === 100), state.shared, "patch", 3);
  else if (activeTab === "articulation") renderParameters(patchSharedParameters.filter(parameter => parameter.id >= 35 && parameter.id < 99), state.shared, "patch", 3);
  else if (activeTab === "motion") renderParameters(patchSharedParameters.filter(parameter => parameter.id >= 101), state.shared, "patch", 3);
  else if (activeTab === "bank") renderParameters(bankParameters, state.bank, "bank", 0);
  else if (activeTab === "globals") renderParameters(
    globalParameters.filter(parameter => parameter.id < capabilities!.globalParameters),
    state.globals, "globals", 0);
  else renderSensor();
}

const tabs: [Tab, string][] = [["bass", "Bass"], ["pad", "Pad + effects"], ["lead", "Lead"], ["voice", "SAM voice"], ["sequence", "Sequence"], ["articulation", "Low articulation"], ["motion", "Patch motion"], ["bank", "Bank interaction"], ["globals", "Globals"], ["sensor", "Sensor monitor"], ["visuals", "Visual performance"]];
tabs.forEach(([id, label]) => {
  const button = document.createElement("button"); button.textContent = label; button.dataset.tab = id; if (id === activeTab) button.classList.add("active");
  button.addEventListener("click", () => { activeTab = id; $("#tabs").querySelectorAll("button").forEach(item => item.classList.toggle("active", item === button)); render(); });
  $("#tabs").append(button);
});

async function run(action: () => Promise<void>, pending: string): Promise<void> {
  try { setStatus(pending); await action(); } catch (error) { setStatus((error as Error).message, true); }
}

$("#connect").addEventListener("click", () => run(async () => {
  $("#connection-label").classList.remove("error");
  $("#connection-label").textContent = "Connecting…";
  capabilities = await connection.connect();
  populateBanks(capabilities.bankCount);
  sensorSnapshotSupported = undefined;
  $("#connection-light").classList.add("on"); $("#connection-label").textContent = `Pocket SCION ${capabilities.firmware}`;
  await loadCurrent();
}, "Requesting USB MIDI and SysEx access…"));

[bankSelect, patchSelect].forEach(select => select.addEventListener("change", () => run(loadCurrent, "Changing patch…")));
connection.addEventListener("patchchange", event => {
  if (!capabilities) return;
  const target = (event as CustomEvent<number>).detail;
  if (target < capabilities.patchCount && visualPatchFollower?.(target)) return;
  if (target >= capabilities.patchCount || target === patchId()) return;
  bankSelect.value = String(Math.floor(target / 16));
  patchSelect.value = String(target % 16);
  void run(() => loadCurrent(target, false), "Following device patch change…");
});
connection.addEventListener("rootchange", event => {
  const root = (event as CustomEvent<number>).detail;
  if (!capabilities || root < 24 || root > 72 || state.globals[0] === root) return;
  state.globals[0] = root;
  if (activeTab === "sequence" || activeTab === "globals") render();
});
$("#reload").addEventListener("click", () => run(loadCurrent, "Reloading…"));
$("#save-patch").addEventListener("click", () => run(async () => { await connection.request(Command.Commit, addressed(Scope.Patch, patchId()), 5000); dirty.delete("patch"); renderDirty(); setStatus("Patch saved to Pocket SCION flash."); }, "Saving patch…"));
$("#save-bank").addEventListener("click", () => run(async () => { const bank = Number(bankSelect.value); await connection.request(Command.Commit, addressed(Scope.Bank, bank), 5000); dirty.delete("bank"); renderDirty(); setStatus("Bank interaction settings saved."); }, "Saving bank settings…"));
$("#save-globals").addEventListener("click", () => run(async () => { await connection.request(Command.Commit, addressed(Scope.Global, 0), 5000); dirty.delete("globals"); renderDirty(); setStatus("Global settings saved."); }, "Saving globals…"));
$("#revert").addEventListener("click", () => run(async () => { await connection.request(Command.Revert, addressed(Scope.Patch, patchId())); await loadCurrent(); }, "Reverting patch…"));
$("#restore").addEventListener("click", () => { if (confirm("Remove this saved override and restore its compiled default?")) run(async () => { await connection.request(Command.Restore, addressed(Scope.Patch, patchId()), 6000); await loadCurrent(); }, "Restoring default…"); });

function download(name: string, data: unknown): void {
  const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: "application/json" }));
  const anchor = document.createElement("a"); anchor.href = url; anchor.download = name; anchor.click(); URL.revokeObjectURL(url);
}
function patchFile(id: number, data: PatchData): PatchFile { return { schema: "pocket-scion-patch", version: 5, patchId: id, ...data }; }
$("#export-patch").addEventListener("click", () => download(`pocket-scion-patch-${patchId() + 1}.json`, patchFile(patchId(), state)));
$("#export-bank").addEventListener("click", () => run(async () => {
  const bankIndex = Number(bankSelect.value); const patches: PatchFile[] = [];
  for (let program = 0; program < 16; program++) { setStatus(`Reading bank: patch ${program + 1} of 16…`); const id = bankIndex * 16 + program; patches.push(patchFile(id, await readPatch(id))); }
  download(`pocket-scion-bank-${bankIndex + 1}.json`, { schema: "pocket-scion-bank", version: 5, bankIndex, settings: state.bank, patches } satisfies BankFile);
  setStatus("Bank JSON exported.");
}, "Reading bank…"));

function validPatch(value: unknown): value is PatchFile {
  const p = value as PatchFile;
  const valid = (values: number[], parameters: Parameter[]) => values.length === parameters.length && values.every((item, index) => {
    const parameter = parameters[index];
    return Number.isInteger(item) && (parameter.values
      ? parameter.values.some(choice => choice.value === item)
      : item >= parameter.min && item <= parameter.max);
  });
  const sharedParameters = p?.version === 1 ? patchSharedParameters.slice(0, 35)
    : p?.version === 2 ? patchSharedParameters.slice(0, 99)
    : p?.version === 3 ? patchSharedParameters.slice(0, 101) : patchSharedParameters;
  const validSpeech = p?.version !== 5 ||
    (p.speech?.length >= 8 && p.speech.length <= voiceParameters.length &&
     valid(p.speech, voiceParameters.slice(0, p.speech.length)) &&
     p.phrases?.length >= 4 &&
     p.phrases.length <= (capabilities?.speechPhrases ?? 10) &&
     p.phrases.every(phrase => typeof phrase === "string" && phrase.length <= 47));
  return p?.schema === "pocket-scion-patch" && (p.version === 1 || p.version === 2 || p.version === 3 || p.version === 4 || p.version === 5) && Number.isInteger(p.patchId) && p.patchId >= 0 && p.patchId < (capabilities?.patchCount ?? 256) && p.lanes?.length === 3 && p.lanes.every(lane => valid(lane, sceneParameters)) && valid(p.shared, sharedParameters) && validSpeech;
}
async function transmitPatch(file: PatchFile, target: number): Promise<void> {
  for (let lane = 0; lane < 3; lane++) for (let parameter = 0; parameter < 47; parameter++) {
    if (lane !== 1 && parameter >= 41) continue;
    await connection.request(Command.Set, addressed(Scope.Patch, target, [lane, parameter, ...valueBytes(file.lanes[lane][parameter])]));
  }
  for (let parameter = 0; parameter < file.shared.length; parameter++) await connection.request(Command.Set, addressed(Scope.Patch, target, [3, parameter, ...valueBytes(file.shared[parameter])]));
  if (file.version === 5 && capabilities?.speechParameters) {
    for (let parameter = 0; parameter < file.speech.length; parameter++) {
      await connection.request(Command.Set, addressed(Scope.Patch, target,
        [4, parameter, ...valueBytes(file.speech[parameter])]));
    }
    for (let phrase = 0; phrase < file.phrases.length; phrase++) {
      await connection.setPhrase(target, phrase, file.phrases[phrase]);
    }
  }
}
$("#import-file").addEventListener("click", () => ($("#file") as HTMLInputElement).click());
$("#file").addEventListener("change", () => run(async () => {
  const input = $("#file") as HTMLInputElement; const file = input.files?.[0]; if (!file) return;
  const data: unknown = JSON.parse(await file.text());
  if (validPatch(data)) { await transmitPatch(data, patchId()); state.lanes = data.lanes.map(lane => [...lane]); state.shared = Object.assign([...state.shared], data.shared); if (data.version === 5) { state.speech = Object.assign([...state.speech], data.speech); state.phrases = Object.assign([...state.phrases], data.phrases); } dirty.add("patch"); renderDirty(); render(); setStatus("Patch imported for audition. Use Save patch to keep it."); }
  else {
    const bank = data as BankFile;
    const settingsLength = bank?.version === 1 ? 17 : bankParameters.length;
    const validSettings = bank?.settings?.length === settingsLength && bank.settings.every((value, index) => Number.isInteger(value) && value >= bankParameters[index].min && value <= bankParameters[index].max);
    if (bank?.schema !== "pocket-scion-bank" || (bank.version !== 1 && bank.version !== 2 && bank.version !== 3 && bank.version !== 4 && bank.version !== 5) || bank.patches?.length !== 16 || !bank.patches.every(validPatch) || !validSettings) throw new Error("This is not a compatible Pocket SCION patch or bank file.");
    if (!confirm("Import and save all 16 patches in the currently selected bank?")) return;
    const targetBank = Number(bankSelect.value);
    for (let parameter = 0; parameter < bank.settings.length; parameter++) await connection.request(Command.Set, addressed(Scope.Bank, targetBank, [0, parameter, ...valueBytes(bank.settings[parameter])]));
    await connection.request(Command.Commit, addressed(Scope.Bank, targetBank), 5000);
    for (let program = 0; program < 16; program++) { setStatus(`Writing bank: patch ${program + 1} of 16…`); const target = targetBank * 16 + program; await transmitPatch(bank.patches[program], target); await connection.request(Command.Commit, addressed(Scope.Patch, target), 5000); }
    await loadCurrent(); setStatus("Bank imported and saved.");
  }
  input.value = "";
}, "Importing JSON…"));

enableActions(false); renderDirty();
if ("serviceWorker" in navigator) window.addEventListener("load", () => navigator.serviceWorker.register("./sw.js"));
