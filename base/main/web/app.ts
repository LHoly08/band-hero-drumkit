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
const uploadInput = element<HTMLInputElement>("mp3-upload");

const maxPads = 30;
const pedalInputs = [1, 2, 3, 4];
const preview = location.protocol === "file:" || new URLSearchParams(location.search).get("preview") === "1";
const previewKey = "band-hero-kit-preview";
const settings = element<HTMLFieldSetElement>("kit-settings");
const toleranceCm = element<HTMLInputElement>("tolerance");
const macAddress = element<HTMLInputElement>("hub-mac");
const macPattern = /^(?:[0-9a-f]{12}|(?:[0-9a-f]{2}:){5}[0-9a-f]{2})$/i;
type Kit = { pads: Pad[]; tolerance?: number; macAddress?: string };

let pads: Pad[] = [];
let selected = 0;
let saved: string | undefined;
let saving = false;
let alternateUpload = false;
let uploading = false;

function toleranceMeters(): number | null {
  const cm = toleranceCm.valueAsNumber;
  // The editor uses centimeters. Lua and the hit test use meters at float32
  // precision.
  const meters = Math.fround(cm / 100);
  return cm >= 0 && Number.isFinite(meters) ? meters : null;
}

function displayCentimeters(meters: number): string {
  // Use the shortest decimal precision that preserves the stored float on save.
  const stored = Math.fround(meters);
  for (let precision = 1; precision <= 9; precision++) {
    const cm = Number((stored * 100).toPrecision(precision));
    if (Math.fround(cm / 100) === stored) {
      return String(cm);
    }
  }
  return String(stored * 100);
}

function kitJson(): string {
  return JSON.stringify({ pads, tolerance: toleranceMeters(), macAddress: macAddress.value.trim() });
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
  const locked = saved === undefined || saving || uploading;
  editor.disabled = locked;
  settings.disabled = locked;
  addButton.disabled = locked || pads.length === maxPads;
  saveButton.disabled = locked || kitJson() === saved;
  moveUp.disabled = locked || selected === 0;
  moveDown.disabled = locked || selected >= pads.length - 1;
}

function changed(): void {
  showStatus(kitJson() === saved ? "" : "Unsaved changes");
  syncButtons();
}

function selectPad(index: number): void {
  selected = index;
  list.querySelectorAll("button").forEach((row, i) => {
    row.setAttribute("aria-pressed", String(i === selected));
  });
  syncPacketSelection();
  renderInspector();
  if (window.matchMedia("(max-width: 620px)").matches) {
    inspector.scrollIntoView({ block: "nearest" });
  }
}

function syncPacketSelection(): void {
  for (const button of element("packet-bits").querySelectorAll("button")) {
    button.setAttribute("aria-pressed", String(Number(button.dataset.index) === selected));
  }
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
    button.onclick = () => selectPad(index);
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
  element("inspector-bit").textContent = `Packet bit ${packetOrder().indexOf(pad) + 1}`;
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

function packetOrder(): Pad[] {
  return [...pads.filter(pad => pad.type === "Pedal"), ...pads.filter(pad => pad.type !== "Pedal")];
}

function renderPacketMap(): void {
  const body = element<HTMLTableSectionElement>("packet-bits");
  body.replaceChildren();
  packetOrder().forEach((pad, bit) => {
    const row = body.insertRow();
    row.insertCell().textContent = String(bit + 1);
    row.insertCell().textContent = pad.type;
    const button = document.createElement("button");
    const index = pads.indexOf(pad);
    button.type = "button";
    button.dataset.index = String(index);
    button.onclick = () => selectPad(index);
    button.textContent = padName(pad);
    if (pad.type === "Pedal" && pad.position !== null) {
      button.textContent += ` · input ${pad.position}`;
    }
    row.insertCell().append(button);
  });
  body.closest("table")!.hidden = pads.length === 0;
  const unused = element("unused-bits");
  unused.hidden = pads.length === maxPads;
  unused.textContent = pads.length === maxPads - 1 ? `Bit ${pads.length + 1} unused` : `Bits ${pads.length + 1}–${maxPads} unused`;
  element("packet-map").hidden = false;
  syncPacketSelection();
}

function render(): void {
  renderPacketMap();
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
    renderPacketMap();
  } else if (pad.type === "Pedal") {
    pad.position = Number(pedalInput.value) || null;
    renderPacketMap();
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
        return "Upload a second MP3.";
      }
    }
  }
  if (!validPath(pad.audio)) {
    return "Upload an MP3.";
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

function readKit(kit: Kit | null): Kit {
  if (!Array.isArray(kit?.pads) || kit.pads.length > maxPads) {
    throw new Error("Invalid kit.");
  }
  const error = firstError(kit.pads);
  if (error) {
    throw new Error(error.message);
  }
  if (kit.tolerance !== undefined && (!finite(kit.tolerance) || kit.tolerance < 0)) {
    throw new Error("Invalid hit tolerance.");
  }
  if (kit.macAddress !== undefined && !macPattern.test(kit.macAddress)) {
    throw new Error("Invalid hub MAC address.");
  }
  return kit;
}

async function request(path: string, init?: RequestInit): Promise<Response> {
  const response = await fetch(path, { cache: "no-store", signal: AbortSignal.timeout(10000), ...init });
  if (!response.ok) {
    const error = await response.text();
    throw new Error(error || `HTTP ${response.status}`);
  }
  return response;
}

function loadPreview(): Kit {
  try {
    const stored = localStorage.getItem(previewKey);
    if (stored) {
      return readKit(JSON.parse(stored));
    }
  } catch {
    // Browser storage may be unavailable for a local file.
  }
  return { pads: [] };
}

async function loadKit(): Promise<void> {
  const device = element("device-status");
  retry.hidden = true;
  element("list-message").textContent = "Loading kit…";
  device.textContent = "Connecting to drum base…";
  try {
    const kit = preview ? loadPreview() : readKit(await (await request("/api/kit")).json());
    pads = kit.pads;
    toleranceCm.value = displayCentimeters(kit.tolerance ?? 0.05);
    macAddress.value = kit.macAddress ?? "00:00:00:00:00:00";
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
  if (toleranceMeters() === null) {
    showStatus("Enter a finite, nonnegative hit tolerance in centimeters within the float32 range.", true);
    toleranceCm.focus();
    return;
  }
  if (!macPattern.test(macAddress.value.trim())) {
    showStatus("Enter a six-byte hub MAC address, such as 00:11:22:33:44:55.", true);
    macAddress.focus();
    return;
  }
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
        body: draft, signal: AbortSignal.timeout(120000),
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

function chooseMp3(alternate: boolean): void {
  alternateUpload = alternate;
  uploadInput.value = "";
  uploadInput.click();
}

async function storePreviewMp3(file: File, stem: string): Promise<string> {
  const data = await file.arrayBuffer();
  const bytes = new Uint8Array(data);
  const invalid = "Choose a complete MPEG Layer III file.";
  let offset = 0;
  if (bytes[0] === 0x49 && bytes[1] === 0x44 && bytes[2] === 0x33) {
    if (bytes.length < 10 || bytes[3] < 2 || bytes[3] > 4 ||
      bytes.subarray(6, 10).some(byte => byte >= 128)) {
      throw new Error(invalid);
    }
    offset = 10 + bytes.subarray(6, 10).reduce((size, byte) => size * 128 + byte, 0);
    if (bytes[3] === 4 && (bytes[5] & 16)) {
      offset += 10;
    }
  }
  const mpeg1 = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320];
  const mpeg2 = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160];
  let seconds = 0;
  while (offset + 4 <= bytes.length) {
    if (bytes.length - offset === 128 && bytes[offset] === 84 &&
      bytes[offset + 1] === 65 && bytes[offset + 2] === 71) {
      offset += 128;
      break;
    }
    const version = (bytes[offset + 1] >> 3) & 3;
    const rateIndex = (bytes[offset + 2] >> 2) & 3;
    const kbps = (version === 3 ? mpeg1 : mpeg2)[bytes[offset + 2] >> 4];
    if (bytes[offset] !== 255 || (bytes[offset + 1] & 0xe6) !== 0xe2 ||
      version === 1 || rateIndex === 3 || !kbps) {
      throw new Error(invalid);
    }
    const rate = [44100, 48000, 32000][rateIndex] / (version === 3 ? 1 : version === 2 ? 2 : 4);
    offset += Math.floor((version === 3 ? 144000 : 72000) * kbps / rate) + ((bytes[offset + 2] >> 1) & 1);
    seconds += (version === 3 ? 1152 : 576) / rate;
  }
  if (!seconds || offset !== bytes.length) {
    throw new Error(invalid);
  }
  if (seconds * 48000 > 4 * 1024 * 1024) {
    throw new Error("MP3 exceeds the 4 MiB decoded limit.");
  }
  await new OfflineAudioContext(1, 1, 24000).decodeAudioData(data);
  return new Promise((resolve, reject) => {
    const open = indexedDB.open("band-hero-preview-audio", 1);
    open.onupgradeneeded = () => open.result.createObjectStore("samples", { autoIncrement: true });
    open.onerror = () => reject(open.error);
    open.onsuccess = () => {
      const db = open.result;
      const tx = db.transaction("samples", "readwrite");
      const stored = tx.objectStore("samples").add(file);
      tx.onabort = () => {
        db.close();
        reject(tx.error ?? new Error("Browser storage is unavailable."));
      };
      tx.oncomplete = () => {
        db.close();
        resolve(`preview/${stored.result}/${stem}.mp3`);
      };
    };
  });
}

uploadInput.onchange = async () => {
  const file = uploadInput.files?.[0];
  const pad = pads[selected];
  if (!file || !pad) {
    return;
  }
  if (!/\.mp3$/i.test(file.name) || !file.size || file.size > 2 * 1024 * 1024) {
    showStatus("Choose a nonempty MP3 file up to 2 MiB.", true);
    return;
  }
  const stem = file.name.slice(0, -4).replace(/[^a-z0-9_-]+/gi, "-").slice(0, 48) || "sound";
  uploading = true;
  syncButtons();
  showStatus("Uploading and verifying MP3…");
  try {
    let path: string;
    if (preview) {
      path = await storePreviewMp3(file, stem);
    } else {
      const response = await request("/api/samples", {
        method: "POST", headers: { "Content-Type": "audio/mpeg", "X-Sample-Name": stem },
        body: file, signal: AbortSignal.timeout(120000),
      });
      path = await response.text();
      if (!path.startsWith("/sdcard/sounds/") || !path.endsWith(".mp3")) {
        throw new Error("Invalid upload response.");
      }
    }
    if (alternateUpload && pad.type !== "Pedal") {
      pad.alternate_audio = path;
    } else {
      pad.audio = path;
    }
    render();
    showStatus(preview ? "MP3 verified and stored in this browser. Save the preview kit to keep this assignment." : "MP3 verified. Save the kit to keep this assignment.");
  } catch (error) {
    showStatus(`Upload failed. ${(error as Error).message}`, true);
  } finally {
    uploading = false;
    syncButtons();
  }
};

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

settings.addEventListener("input", changed);
editor.addEventListener("input", updateFields);
typeInput.onchange = changeType;
moveUp.onclick = () => movePad(-1);
moveDown.onclick = () => movePad(1);
element("browse-audio").onclick = () => chooseMp3(false);
element("browse-alternate").onclick = () => chooseMp3(true);
retry.onclick = () => void loadKit();
saveButton.onclick = () => void saveKit();
window.addEventListener("beforeunload", event => {
  if (saved !== undefined && kitJson() !== saved) {
    event.preventDefault();
    event.returnValue = true;
  }
});
void loadKit();
