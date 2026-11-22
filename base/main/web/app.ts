type PadType = "Drum" | "Cymbal" | "Pedal";
type Position = { x: number | null; y: number | null; z: number | null };
type Pad = { audio: string } & (
  | { type: "Pedal"; position: number | null }
  | {
      type: "Drum" | "Cymbal";
      position: Position;
      radius: number | null;
      tilt?: number | null;
      alternate_radius?: number | null;
      alternate_audio?: string;
    }
);

function element<T extends HTMLElement = HTMLElement>(id: string): T {
  return document.getElementById(id) as T;
}

const list = element<HTMLOListElement>("pad-list");
const template = element<HTMLTemplateElement>("pad-row");
const inspector = element("inspector-heading");
const editor = element<HTMLFieldSetElement>("pad-editor");
const statusMessage = element("kit-status");
const retry = element<HTMLButtonElement>("retry-load");
const addButton = element<HTMLButtonElement>("add-pad");
const saveButton = element<HTMLButtonElement>("save-kit");
const moveUp = element<HTMLButtonElement>("move-up");
const moveDown = element<HTMLButtonElement>("move-down");
const typeInput = element<HTMLSelectElement>("pad-type");
const pedalInput = element<HTMLSelectElement>("pedal-input");
const audioInput = element<HTMLInputElement>("pad-audio");
const x = element<HTMLInputElement>("position-x");
const y = element<HTMLInputElement>("position-y");
const z = element<HTMLInputElement>("position-z");
const radius = element<HTMLInputElement>("pad-radius");
const tilt = element<HTMLInputElement>("pad-tilt");
const alternate = element<HTMLInputElement>("alternate-enabled");
const alternateRadius = element<HTMLInputElement>("alternate-radius");
const alternateAudio = element<HTMLInputElement>("alternate-audio");
const alternateFields = element("alternate-fields");
const sampleDialog = element<HTMLDialogElement>("sample-dialog");
const sampleList = element("sample-list");
const samplesStatus = element("samples-status");

const maxPads = 30;
const pedalInputs = [1, 2, 3, 4];
const preview = location.protocol === "file:" || new URLSearchParams(location.search).get("preview") === "1";
const previewKey = "band-hero-kit-preview";
const previewPads: Pad[] = [
  { type: "Drum", position: { x: -.18, y: .32, z: -.08 }, radius: .10, tilt: .10, audio: "/sdcard/sounds/snare.mp3" },
  { type: "Drum", position: { x: 0, y: .42, z: .02 }, radius: .09, audio: "/sdcard/sounds/rack-tom.mp3" },
  { type: "Drum", position: { x: .23, y: .29, z: -.10 }, radius: .12, audio: "/sdcard/sounds/floor-tom.mp3" },
  { type: "Cymbal", position: { x: -.28, y: .37, z: .14 }, radius: .12, audio: "/sdcard/sounds/hi-hat.mp3" },
  { type: "Cymbal", position: { x: .25, y: .40, z: .19 }, radius: .13, audio: "/sdcard/sounds/crash.mp3" },
  { type: "Pedal", position: 1, audio: "/sdcard/sounds/kick.mp3" },
];

let pads: Pad[] = [];
let selected = 0;
let saved: string | undefined;
let saving = false;
let sampleTarget = audioInput;
let loadingSamples = false;

function kitJson(): string {
  return JSON.stringify({ pads });
}

function showStatus(message: string, error = false): void {
  if (statusMessage.textContent !== message) {
    statusMessage.textContent = message;
  }
  statusMessage.classList.toggle("error", error);
}

function fileName(path: string): string {
  return path.slice(path.lastIndexOf("/") + 1);
}

function padName(pad: Pad): string {
  const name = fileName(pad.audio).replace(/\.mp3$/i, "").replace(/[-_]+/g, " ").trim();
  return name ? name.charAt(0).toUpperCase() + name.slice(1) : `New ${pad.type.toLowerCase()}`;
}

function padNumber(index: number): string {
  return String(index + 1).padStart(2, "0");
}

function syncButtons(): void {
  const locked = saved === undefined || saving;
  editor.disabled = locked;
  addButton.disabled = locked || pads.length === maxPads;
  saveButton.disabled = locked || kitJson() === saved;
  moveUp.disabled = locked || selected === 0;
  moveDown.disabled = locked || selected >= pads.length - 1;
}

function changed(): void {
  showStatus(kitJson() === saved ? "" : "Unsaved changes");
  syncButtons();
}

function renderRows(): void {
  list.replaceChildren();
  pads.forEach((pad, index) => {
    const row = template.content.cloneNode(true) as DocumentFragment;
    const button = row.querySelector("button")!;
    row.querySelector(".pad-index")!.textContent = padNumber(index);
    row.querySelector("strong")!.textContent = padName(pad);
    row.querySelector("small")!.textContent = pad.type;
    button.setAttribute("aria-pressed", String(index === selected));
    button.addEventListener("click", () => {
      list.querySelector('[aria-pressed="true"]')?.setAttribute("aria-pressed", "false");
      selected = index;
      button.setAttribute("aria-pressed", "true");
      renderInspector();
      if (window.matchMedia("(max-width: 620px)").matches) {
        inspector.scrollIntoView({ block: "nearest" });
      }
    });
    list.append(row);
  });
  list.hidden = !pads.length;
  element("pad-count").textContent = `${pads.length} / ${maxPads}`;
  element("list-empty").hidden = Boolean(pads.length);
  element("list-message").textContent = "Add a pad to get started.";
  element("order-note").hidden = !pads.some(pad => pad.type !== "Pedal");
}

function updateTitle(pad: Pad): void {
  inspector.textContent = `Pad ${padNumber(selected)} · ${padName(pad)}`;
}

function renderInspector(): void {
  const pad = pads[selected];
  editor.hidden = !pad;
  element("order-actions").hidden = !pad;
  element("inspector-empty").hidden = Boolean(pad);
  if (!pad) {
    inspector.textContent = "Pad inspector";
    syncButtons();
    return;
  }

  updateTitle(pad);
  typeInput.value = pad.type;
  audioInput.value = pad.audio;
  element("stick-fields").hidden = pad.type === "Pedal";
  element("pedal-fields").hidden = pad.type !== "Pedal";
  element("alternate-section").hidden = pad.type === "Pedal";
  if (pad.type === "Pedal") {
    pedalInput.value = String(pad.position ?? "");
  } else {
    x.value = String(pad.position.x ?? "");
    y.value = String(pad.position.y ?? "");
    z.value = String(pad.position.z ?? "");
    radius.value = String(pad.radius ?? "");
    tilt.value = String(pad.tilt ?? "");
    alternate.checked = pad.alternate_radius !== undefined || pad.alternate_audio !== undefined;
    alternateFields.hidden = !alternate.checked;
    alternateRadius.value = String(pad.alternate_radius ?? "");
    alternateAudio.value = pad.alternate_audio ?? "";
    element("alternate-zone").textContent = pad.type === "Drum" ? "outer zone" : "inner zone";
  }
  syncButtons();
}

function render(): void {
  renderRows();
  renderInspector();
}

function newStickPad(type: "Drum" | "Cymbal", audio = ""): Pad {
  return { type, position: { x: null, y: null, z: null }, radius: null, audio };
}

function changeType(): void {
  const pad = pads[selected];
  const type = typeInput.value as PadType;
  if (type === "Pedal") {
    const position = pedalInputs.find(input => !pads.some(pad => pad.type === "Pedal" && pad.position === input)) ?? null;
    pads[selected] = { type, position, audio: pad.audio };
  } else if (pad.type === "Pedal") {
    pads[selected] = newStickPad(type, pad.audio);
  } else {
    pad.type = type;
  }
  render();
  changed();
}

function numberValue(input: HTMLInputElement): number | null {
  return Number.isFinite(input.valueAsNumber) ? input.valueAsNumber : null;
}

function updateFields(event: Event): void {
  const pad = pads[selected];
  if (event.target === typeInput || !pad) {
    return;
  }
  if (event.target === audioInput) {
    pad.audio = audioInput.value;
    list.querySelector('[aria-pressed="true"] strong')!.textContent = padName(pad);
    updateTitle(pad);
  } else if (pad.type === "Pedal") {
    pad.position = Number(pedalInput.value) || null;
  } else {
    pad.position = { x: numberValue(x), y: numberValue(y), z: numberValue(z) };
    pad.radius = numberValue(radius);
    pad.tilt = tilt.value || tilt.validity.badInput ? numberValue(tilt) : undefined;
    pad.alternate_radius = alternate.checked ? numberValue(alternateRadius) : undefined;
    pad.alternate_audio = alternate.checked ? alternateAudio.value : undefined;
    alternateFields.hidden = !alternate.checked;
  }
  changed();
}

function finite(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(Math.fround(value));
}

function validRadius(value: unknown): value is number {
  if (!finite(value)) {
    return false;
  }
  const rounded = Math.fround(value);
  return rounded > 0 && Number.isFinite(Math.fround(rounded * rounded));
}

function validPath(path: unknown): path is string {
  return typeof path === "string" && Boolean(path.trim()) && !path.includes("\0");
}

function padError(pad: Pad, usedInputs: Set<number>): string | undefined {
  if (!pad || !["Drum", "Cymbal", "Pedal"].includes(pad.type)) {
    return "Choose a pad type.";
  }
  if (pad.type === "Pedal") {
    if (pad.position === null || !pedalInputs.includes(pad.position)) {
      return "Choose a pedal input.";
    }
    if (usedInputs.has(pad.position)) {
      return `Pedal input ${pad.position} is already assigned.`;
    }
    usedInputs.add(pad.position);
  } else {
    const pos = pad.position;
    if (!pos || ![pos.x, pos.y, pos.z].every(finite)) {
      return "Enter finite X, Y and Z coordinates.";
    }
    if (!validRadius(pad.radius)) {
      return "Enter a positive radius.";
    }
    if (pad.tilt !== undefined && !finite(pad.tilt)) {
      return "Enter a finite tilt or leave it blank.";
    }
    if (pad.alternate_radius !== undefined || pad.alternate_audio !== undefined) {
      if (!validRadius(pad.alternate_radius) || pad.alternate_radius > pad.radius) {
        return "Enter a positive zone boundary within the pad radius.";
      }
      if (!validPath(pad.alternate_audio)) {
        return "Choose a second audio sample.";
      }
    }
  }
  if (!validPath(pad.audio)) {
    return "Choose an audio sample.";
  }
}

function firstError(entries: Pad[]) {
  const usedInputs = new Set<number>();
  for (const [index, pad] of entries.entries()) {
    const message = padError(pad, usedInputs);
    if (message) {
      return { index, message: `Pad ${padNumber(index)}. ${message}` };
    }
  }
}

function readKit(kit: { pads: Pad[] } | null): Pad[] {
  if (!Array.isArray(kit?.pads) || kit.pads.length > maxPads) {
    throw new Error("Invalid kit.");
  }
  const error = firstError(kit.pads);
  if (error) {
    throw new Error(error.message);
  }
  return kit.pads;
}

async function request(path: string, init?: RequestInit): Promise<Response> {
  const response = await fetch(path, { cache: "no-store", signal: AbortSignal.timeout(10000), ...init });
  if (!response.ok) {
    const error = await response.json().catch(() => null);
    throw new Error(typeof error?.message === "string" ? error.message : `HTTP ${response.status}`);
  }
  return response;
}

function loadPreview(): Pad[] {
  try {
    const stored = localStorage.getItem(previewKey);
    if (stored) {
      return readKit(JSON.parse(stored));
    }
  } catch {
    // The example kit also works when browser storage is unavailable.
  }
  return structuredClone(previewPads);
}

async function loadKit(): Promise<void> {
  const device = element("device-status");
  retry.hidden = true;
  element("list-message").textContent = "Loading kit…";
  device.textContent = "Connecting to drum base…";
  try {
    pads = preview ? loadPreview() : readKit(await (await request("/api/kit")).json());
    saved = kitJson();
    device.textContent = preview ? "Browser preview" : "Drum base connected";
    render();
  } catch (error) {
    device.textContent = "Drum base unavailable";
    element("list-message").textContent = `Could not load kit. ${(error as Error).message}`;
    retry.hidden = false;
  }
}

async function saveKit(): Promise<void> {
  const error = firstError(pads);
  if (error) {
    selected = error.index;
    render();
    showStatus(error.message, true);
    return;
  }

  saving = true;
  syncButtons();
  showStatus("Saving kit…");
  const draft = kitJson();
  try {
    if (preview) {
      localStorage.setItem(previewKey, draft);
    } else {
      await request("/api/kit", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: draft,
      });
    }
    saved = draft;
    showStatus(preview ? "Preview kit saved in this browser." : "Kit saved. Choose a play mode on the drum base.");
  } catch (error) {
    showStatus(`Could not save kit. ${(error as Error).message}`, true);
  } finally {
    saving = false;
    syncButtons();
  }
}

async function browseSamples(target: HTMLInputElement): Promise<void> {
  sampleTarget = target;
  sampleDialog.showModal();
  if (sampleList.childElementCount || loadingSamples) {
    return;
  }

  loadingSamples = true;
  samplesStatus.textContent = "Loading samples…";
  try {
    const paths: unknown = preview
      ? ["/sdcard/sounds/bell.mp3", "/sdcard/sounds/rimshot.mp3", ...previewPads.map(pad => pad.audio)]
      : await (await request("/api/samples")).json();
    if (!Array.isArray(paths) || !paths.every(validPath)) {
      throw new Error("Invalid sample list.");
    }
    samplesStatus.textContent = paths.length ? "" : "No samples found.";
    for (const path of paths) {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = fileName(path);
      button.title = path;
      button.onclick = () => {
        sampleTarget.value = path;
        sampleTarget.dispatchEvent(new Event("input", { bubbles: true }));
        sampleDialog.close();
        sampleTarget.focus();
      };
      sampleList.append(button);
    }
  } catch (error) {
    samplesStatus.textContent = `Could not load samples. ${(error as Error).message}`;
  } finally {
    loadingSamples = false;
  }
}

function movePad(offset: number): void {
  const next = selected + offset;
  [pads[selected], pads[next]] = [pads[next], pads[selected]];
  selected = next;
  render();
  changed();
  if (offset < 0 && moveUp.disabled) {
    moveDown.focus();
  } else if (offset > 0 && moveDown.disabled) {
    moveUp.focus();
  }
}

addButton.onclick = () => {
  selected = pads.push(newStickPad("Drum")) - 1;
  render();
  changed();
  list.scrollTop = list.scrollHeight;
  x.focus();
};
element("remove-pad").onclick = () => {
  pads.splice(selected, 1);
  selected = Math.max(0, Math.min(selected, pads.length - 1));
  render();
  changed();
  (list.querySelector<HTMLButtonElement>('[aria-pressed="true"]') ?? addButton).focus();
};

editor.addEventListener("input", updateFields);
typeInput.onchange = changeType;
moveUp.onclick = () => movePad(-1);
moveDown.onclick = () => movePad(1);
element("browse-audio").onclick = () => void browseSamples(audioInput);
element("browse-alternate").onclick = () => void browseSamples(alternateAudio);
retry.onclick = () => void loadKit();
saveButton.onclick = () => void saveKit();
window.addEventListener("beforeunload", event => {
  if (saved !== undefined && kitJson() !== saved) {
    event.preventDefault();
    event.returnValue = true;
  }
});
void loadKit();
