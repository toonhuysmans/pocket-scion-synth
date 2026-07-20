import "./style.css";
import { PocketScionConnection, Command, Scope, valueBytes, type Capabilities } from "./protocol";
import { sceneParameters, patchSharedParameters, bankParameters, globalParameters, bankNames, midiNoteName, scalePresets, type Parameter } from "./parameters";

type Tab = "bass" | "pad" | "lead" | "sequence" | "articulation" | "motion" | "bank" | "globals" | "sensor";
interface PatchData { lanes: number[][]; shared: number[] }
interface EditorState extends PatchData { bank: number[]; globals: number[] }
interface PatchFile extends PatchData { schema: "pocket-scion-patch"; version: 1 | 2 | 3 | 4; patchId: number }
interface BankFile { schema: "pocket-scion-bank"; version: 1 | 2 | 3 | 4; bankIndex: number; settings: number[]; patches: PatchFile[] }

const connection = new PocketScionConnection();
let capabilities: Capabilities | undefined;
let state: EditorState = { lanes: [[], [], []], shared: [], bank: [], globals: [] };
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
function setStatus(message: string, error = false): void { status.textContent = message; status.style.color = error ? "#e49a8c" : "#aab3a3"; }
function markDirty(scope: "patch" | "bank" | "globals"): void { dirty.add(scope); renderDirty(); }
function renderDirty(): void { $("#dirty").textContent = dirty.size ? `Unsaved: ${[...dirty].join(", ")}` : "Saved"; }

async function requestValue(scope: number, target: number, lane: number, parameter: number): Promise<number> {
  const response = await connection.request(Command.Get, addressed(scope, target, [lane, parameter]));
  return response[response.length - 2] | (response[response.length - 1] << 7);
}

async function readPatch(id: number, generation?: number): Promise<PatchData> {
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
  return { lanes, shared };
}

async function loadCurrent(target = patchId(), selectDevice = true): Promise<void> {
  if (!capabilities) return;
  const generation = ++loadGeneration;
  const bankIndex = Math.floor(target / 16);
  enableActions(false); setStatus("Reading patch and shared settings…");
  try {
    if (selectDevice) await connection.request(Command.Select, connection.targetBytes(target));
    if (generation !== loadGeneration) throw new LoadSuperseded();
    const patch = await readPatch(target, generation);
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

function render(): void {
  if (sensorTimer) { window.clearTimeout(sensorTimer); sensorTimer = undefined; }
  if (!capabilities) return;
  if (activeTab === "bass" || activeTab === "pad" || activeTab === "lead") {
    const lane = { bass: 0, pad: 1, lead: 2 }[activeTab];
    const visible = sceneParameters.filter(parameter => lane === 1 || !["Chorus", "Delay"].includes(parameter.section)).map(parameter =>
      lane === 1 && ["Chorus", "Delay"].includes(parameter.section)
        ? { ...parameter, section: "Shared effects", name: `${parameter.section} ${parameter.name}` }
        : parameter);
    renderParameters(visible, state.lanes[lane], "patch", lane);
  } else if (activeTab === "sequence") renderParameters(patchSharedParameters.filter(parameter => parameter.id < 35 || parameter.id === 99 || parameter.id === 100), state.shared, "patch", 3);
  else if (activeTab === "articulation") renderParameters(patchSharedParameters.filter(parameter => parameter.id >= 35 && parameter.id < 99), state.shared, "patch", 3);
  else if (activeTab === "motion") renderParameters(patchSharedParameters.filter(parameter => parameter.id >= 101), state.shared, "patch", 3);
  else if (activeTab === "bank") renderParameters(bankParameters, state.bank, "bank", 0);
  else if (activeTab === "globals") renderParameters(
    globalParameters.filter(parameter => parameter.id < capabilities!.globalParameters),
    state.globals, "globals", 0);
  else renderSensor();
}

const tabs: [Tab, string][] = [["bass", "Bass"], ["pad", "Pad + effects"], ["lead", "Lead"], ["sequence", "Sequence"], ["articulation", "Low articulation"], ["motion", "Patch motion"], ["bank", "Bank interaction"], ["globals", "Globals"], ["sensor", "Sensor monitor"]];
tabs.forEach(([id, label]) => {
  const button = document.createElement("button"); button.textContent = label; button.dataset.tab = id; if (id === activeTab) button.classList.add("active");
  button.addEventListener("click", () => { activeTab = id; $("#tabs").querySelectorAll("button").forEach(item => item.classList.toggle("active", item === button)); render(); });
  $("#tabs").append(button);
});

async function run(action: () => Promise<void>, pending: string): Promise<void> {
  try { setStatus(pending); await action(); } catch (error) { setStatus((error as Error).message, true); }
}

$("#connect").addEventListener("click", () => run(async () => {
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
function patchFile(id: number, data: PatchData): PatchFile { return { schema: "pocket-scion-patch", version: 4, patchId: id, ...data }; }
$("#export-patch").addEventListener("click", () => download(`pocket-scion-patch-${patchId() + 1}.json`, patchFile(patchId(), state)));
$("#export-bank").addEventListener("click", () => run(async () => {
  const bankIndex = Number(bankSelect.value); const patches: PatchFile[] = [];
  for (let program = 0; program < 16; program++) { setStatus(`Reading bank: patch ${program + 1} of 16…`); const id = bankIndex * 16 + program; patches.push(patchFile(id, await readPatch(id))); }
  download(`pocket-scion-bank-${bankIndex + 1}.json`, { schema: "pocket-scion-bank", version: 4, bankIndex, settings: state.bank, patches } satisfies BankFile);
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
  return p?.schema === "pocket-scion-patch" && (p.version === 1 || p.version === 2 || p.version === 3 || p.version === 4) && Number.isInteger(p.patchId) && p.patchId >= 0 && p.patchId < (capabilities?.patchCount ?? 256) && p.lanes?.length === 3 && p.lanes.every(lane => valid(lane, sceneParameters)) && valid(p.shared, sharedParameters);
}
async function transmitPatch(file: PatchFile, target: number): Promise<void> {
  for (let lane = 0; lane < 3; lane++) for (let parameter = 0; parameter < 47; parameter++) {
    if (lane !== 1 && parameter >= 41) continue;
    await connection.request(Command.Set, addressed(Scope.Patch, target, [lane, parameter, ...valueBytes(file.lanes[lane][parameter])]));
  }
  for (let parameter = 0; parameter < file.shared.length; parameter++) await connection.request(Command.Set, addressed(Scope.Patch, target, [3, parameter, ...valueBytes(file.shared[parameter])]));
}
$("#import-file").addEventListener("click", () => ($("#file") as HTMLInputElement).click());
$("#file").addEventListener("change", () => run(async () => {
  const input = $("#file") as HTMLInputElement; const file = input.files?.[0]; if (!file) return;
  const data: unknown = JSON.parse(await file.text());
  if (validPatch(data)) { await transmitPatch(data, patchId()); state.lanes = data.lanes.map(lane => [...lane]); state.shared = Object.assign([...state.shared], data.shared); dirty.add("patch"); renderDirty(); render(); setStatus("Patch imported for audition. Use Save patch to keep it."); }
  else {
    const bank = data as BankFile;
    const settingsLength = bank?.version === 1 ? 17 : bankParameters.length;
    const validSettings = bank?.settings?.length === settingsLength && bank.settings.every((value, index) => Number.isInteger(value) && value >= bankParameters[index].min && value <= bankParameters[index].max);
    if (bank?.schema !== "pocket-scion-bank" || (bank.version !== 1 && bank.version !== 2 && bank.version !== 3 && bank.version !== 4) || bank.patches?.length !== 16 || !bank.patches.every(validPatch) || !validSettings) throw new Error("This is not a compatible Pocket SCION patch or bank file.");
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
