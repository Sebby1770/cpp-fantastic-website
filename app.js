const MODES = ["orbit", "bloom", "forge", "night", "pulse", "drift"];
const SNAPSHOT_KEY = "asterforge-snapshots-v3";
const MAX_SNAPSHOTS = 6;
const FALLBACK_PRESETS = [
  { id: "orbit", label: "Orbit", description: "Balanced routes with readable movement.", seed: "sebby-orbit", intensity: 68, tempo: 54, density: 72 },
  { id: "bloom", label: "Bloom", description: "Dense color, high energy, and wider clusters.", seed: "bloom-signal", intensity: 82, tempo: 48, density: 88 },
  { id: "forge", label: "Forge", description: "Sharper tension with faster route pressure.", seed: "forge-line", intensity: 76, tempo: 74, density: 66 },
  { id: "night", label: "Night", description: "Slower motion and sparse late-session focus.", seed: "night-map", intensity: 46, tempo: 34, density: 42 },
  { id: "pulse", label: "Pulse", description: "Rhythmic bursts with tight clusters and high tempo.", seed: "pulse-core", intensity: 88, tempo: 81, density: 58 },
  { id: "drift", label: "Drift", description: "Slow lateral wander with sparse luminous fields.", seed: "drift-field", intensity: 38, tempo: 22, density: 54 },
];
const PALETTES = [
  { id: "emberglass", colors: ["#080908", "#f7f0df", "#10b8a6", "#ef5e4d", "#f2b544", "#8c6cf5", "#77c66e"] },
  { id: "tidewire", colors: ["#071013", "#edf7f2", "#1f8fb3", "#ff6b57", "#d9b847", "#4bbf83", "#d46fb0"] },
  { id: "citrus-noir", colors: ["#0b0b10", "#fff4ce", "#95d839", "#ff5d35", "#49a7ff", "#b877ff", "#f1c232"] },
  { id: "violet-oxide", colors: ["#100c13", "#f3efe7", "#b888ff", "#d85f7d", "#43c6a8", "#f0aa3b", "#6ea8fe"] },
  { id: "nova-pulse", colors: ["#12060c", "#f6e6ef", "#ff3d81", "#39d0ff", "#ffe16a", "#b388ff", "#4dffc0"] },
  { id: "driftwood", colors: ["#0a1014", "#e4eef2", "#6aa7c8", "#c48b5a", "#9ad0b1", "#7a8cff", "#d2c4a8"] },
];

const state = {
  mode: "orbit",
  mission: null,
  sky: null,
  orbit: null,
  live: false,
  time: 0,
  timeScale: 1,
  selected: -1,
  reducedMotion: window.matchMedia("(prefers-reduced-motion: reduce)").matches,
};

const elements = {
  canvas: document.querySelector("#skyCanvas"),
  serverStatus: document.querySelector("#serverStatus"),
  missionName: document.querySelector("#missionName"),
  missionTagline: document.querySelector("#missionTagline"),
  weatherLabel: document.querySelector("#weatherLabel"),
  updatedAt: document.querySelector("#updatedAt"),
  intensity: document.querySelector("#intensityInput"),
  intensityValue: document.querySelector("#intensityValue"),
  tempo: document.querySelector("#tempoInput"),
  tempoValue: document.querySelector("#tempoValue"),
  density: document.querySelector("#densityInput"),
  densityValue: document.querySelector("#densityValue"),
  seed: document.querySelector("#seedInput"),
  grid: document.querySelector("#gridToggle"),
  trails: document.querySelector("#trailToggle"),
  motion: document.querySelector("#motionToggle"),
  orbits: document.querySelector("#orbitToggle"),
  generate: document.querySelector("#generateButton"),
  randomizeSeed: document.querySelector("#randomizeSeed"),
  copy: document.querySelector("#copyButton"),
  copyLink: document.querySelector("#copyLinkButton"),
  downloadPng: document.querySelector("#downloadPngButton"),
  helpButton: document.querySelector("#helpButton"),
  helpOverlay: document.querySelector("#helpOverlay"),
  closeHelp: document.querySelector("#closeHelp"),
  metrics: document.querySelector("#metricStrip"),
  priorities: document.querySelector("#priorityList"),
  waypoints: document.querySelector("#waypoints"),
  notes: document.querySelector("#notesList"),
  swatches: document.querySelector("#swatches"),
  paletteName: document.querySelector("#paletteName"),
  modeBadge: document.querySelector("#modeBadge"),
  nodeCount: document.querySelector("#nodeCount"),
  routeCount: document.querySelector("#routeCount"),
  signature: document.querySelector("#signature"),
  presets: document.querySelector("#presetsList"),
  presetsStatus: document.querySelector("#presetsStatus"),
  saveSnapshot: document.querySelector("#saveSnapshot"),
  snapshotList: document.querySelector("#snapshotList"),
  nodeIntel: document.querySelector("#nodeIntel"),
  selectedNodeLabel: document.querySelector("#selectedNodeLabel"),
  telemetryStrip: document.querySelector("#telemetryStrip"),
  telRequests: document.querySelector("#telRequests"),
  telP99: document.querySelector("#telP99"),
  tel2xx: document.querySelector("#tel2xx"),
  tel4xx: document.querySelector("#tel4xx"),
  tel5xx: document.querySelector("#tel5xx"),
};

const copyMarkup = elements.copy.innerHTML;
const copyLinkMarkup = elements.copyLink.innerHTML;
const downloadPngMarkup = elements.downloadPng.innerHTML;
const seedWords = ["velvet", "copper", "lumen", "tide", "citadel", "prism", "ember", "atlas", "signal", "orbit", "glow", "foundry"];

let snapshots = [];
let presets = [];
let rendererApi = null;
let streamHandle = null;

function fnv1a(text) {
  let hash = 2166136261;
  for (let i = 0; i < text.length; i += 1) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
}

function mulberry32(seed) {
  let t = seed >>> 0;
  return () => {
    t += 0x6d2b79f5;
    let x = t;
    x = Math.imul(x ^ (x >>> 15), x | 1);
    x ^= x + Math.imul(x ^ (x >>> 7), x | 61);
    return ((x ^ (x >>> 14)) >>> 0) / 4294967296;
  };
}

function clamp(value, lo, hi) {
  return Math.max(lo, Math.min(hi, value));
}

function apiUrl(path, params) {
  const url = new URL(`.${path.startsWith("/") ? path : `/${path}`}`, window.location.href);
  if (params) {
    Object.entries(params).forEach(([key, value]) => url.searchParams.set(key, String(value)));
  }
  return url.toString();
}

function modeTitle(value) {
  return value.charAt(0).toUpperCase() + value.slice(1);
}

function currentConfig() {
  return {
    seed: elements.seed.value || "sebby",
    mode: state.mode,
    intensity: Number(elements.intensity.value),
    tempo: Number(elements.tempo.value),
    density: Number(elements.density.value),
  };
}

function setMode(mode) {
  state.mode = MODES.includes(mode) ? mode : "orbit";
  elements.modeBadge.textContent = modeTitle(state.mode);
  document.querySelectorAll(".mode-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.mode === state.mode);
  });
  document.querySelectorAll(".preset-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.mode === state.mode);
  });
}

function updateRangeLabels() {
  elements.intensityValue.textContent = elements.intensity.value;
  elements.tempoValue.textContent = elements.tempo.value;
  elements.densityValue.textContent = elements.density.value;
}

function applyConfig(config) {
  if (!config) return;
  setMode(config.mode);
  elements.seed.value = config.seed || elements.seed.value || "sebby";
  elements.intensity.value = config.intensity ?? elements.intensity.value;
  elements.tempo.value = config.tempo ?? elements.tempo.value;
  elements.density.value = config.density ?? elements.density.value;
  updateRangeLabels();
}

function syncUrl() {
  const params = new URLSearchParams(currentConfig());
  window.history.replaceState(null, "", `${window.location.pathname}?${params}`);
}

function applyInitialUrlState() {
  const params = new URLSearchParams(window.location.search);
  const config = {};
  ["seed", "mode", "intensity", "tempo", "density"].forEach((key) => {
    if (params.has(key)) config[key] = params.get(key);
  });
  applyConfig(config);
}

function shareUrl() {
  const params = new URLSearchParams(currentConfig());
  return `${window.location.origin}${window.location.pathname}?${params}`;
}

function setButtonFeedback(button, originalMarkup, message) {
  button.textContent = message;
  window.setTimeout(() => {
    button.innerHTML = originalMarkup;
  }, 1200);
}

function downloadBlob(filename, blob) {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  URL.revokeObjectURL(url);
}

function downloadDataUrl(filename, dataUrl) {
  const anchor = document.createElement("a");
  anchor.href = dataUrl;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
}

function downloadText(filename, content) {
  downloadBlob(filename, new Blob([content], { type: "text/plain;charset=utf-8" }));
}

function setTheme(palette) {
  if (!palette || palette.length < 7) return;
  const root = document.documentElement;
  root.style.setProperty("--ink", palette[0]);
  root.style.setProperty("--paper", palette[1]);
  root.style.setProperty("--accent-a", palette[2]);
  root.style.setProperty("--accent-b", palette[3]);
  root.style.setProperty("--accent-c", palette[4]);
  root.style.setProperty("--accent-d", palette[5]);
  root.style.setProperty("--accent-e", palette[6]);
}

function makeText(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  node.textContent = text;
  return node;
}

function buildMeter(value, className = "meter") {
  const meter = document.createElement("div");
  meter.className = className;
  const fill = document.createElement("span");
  fill.style.width = `${clamp(value, 0, 100)}%`;
  meter.append(fill);
  return meter;
}

function pick(rng, values) {
  return values[Math.floor(rng() * values.length)];
}

function localMission(config) {
  const seed = config.seed || "sebby";
  const mode = MODES.includes(config.mode) ? config.mode : "orbit";
  const intensity = clamp(Number(config.intensity) || 68, 1, 100);
  const tempo = clamp(Number(config.tempo) || 54, 1, 100);
  const density = clamp(Number(config.density) || 72, 1, 100);
  const key = `${seed}:${mode}:${intensity}:${tempo}:${density}`;
  const rng = mulberry32(fnv1a(key));
  const palette = PALETTES[fnv1a(mode + seed) % PALETTES.length];
  const nodeCount = 20 + Math.floor(density / 5) + Math.floor(intensity / 12);
  const prefixes = ["Velvet", "Copper", "Solar", "Nocturne", "Meridian", "Echo", "Glass", "Kinetic"];
  const nouns = ["Observatory", "Relay", "Garden", "Cartograph", "Engine", "Harbor", "Foundry", "Signal"];
  const weather = ["clear signal", "charged air", "useful tension", "clean pressure", "late-night focus", "pulse weather"];
  const nodes = [];
  for (let i = 0; i < nodeCount; i += 1) {
    const t = i / Math.max(1, nodeCount - 1);
    let x = 0.08 + rng() * 0.84;
    let y = 0.08 + rng() * 0.84;
    let z = 0.08 + rng() * 0.84;
    if (mode === "orbit") {
      const angle = t * Math.PI * 2;
      const radius = 0.18 + rng() * 0.28;
      x = 0.5 + Math.cos(angle) * radius;
      y = 0.5 + (rng() - 0.5) * 0.22;
      z = 0.5 + Math.sin(angle) * radius;
    } else if (mode === "bloom") {
      const angle = rng() * Math.PI * 2;
      const radius = rng() ** 0.55 * 0.42;
      x = 0.5 + Math.cos(angle) * radius;
      y = 0.5 + (rng() - 0.5) * 0.55;
      z = 0.5 + Math.sin(angle) * radius * 0.85;
    } else if (mode === "forge") {
      x = 0.12 + t * 0.76;
      y = 0.5 + Math.sin(t * 9) * 0.22;
      z = 0.5 + Math.cos(t * 6) * 0.18;
    } else if (mode === "pulse") {
      const angle = t * 12.5;
      const radius = 0.12 + (i % 4) * 0.1;
      x = 0.5 + Math.cos(angle) * radius;
      y = 0.5 + Math.sin(t * 8) * 0.16;
      z = 0.5 + Math.sin(angle) * radius;
    } else if (mode === "drift") {
      x = 0.1 + t * 0.8;
      y = 0.18 + (i % 3) * 0.22 + rng() * 0.12;
      z = 0.2 + rng() * 0.6;
    }
    nodes.push({
      x: clamp(x, 0, 1),
      y: clamp(y, 0, 1),
      z: clamp(z, 0, 1),
      size: 3 + Math.floor(rng() * 8),
      energy: 28 + Math.floor(rng() * 72),
      phase: rng() * Math.PI * 2,
      kind: Math.floor(rng() * 4),
    });
  }
  const links = [];
  const linkCount = nodeCount + Math.floor(density / 3);
  for (let i = 0; i < linkCount; i += 1) {
    const start = Math.floor(rng() * nodeCount);
    const jump = 1 + Math.floor(rng() * 7);
    links.push([start, (start + jump) % nodeCount, 24 + Math.floor(rng() * 76)]);
  }
  const rings = Array.from({ length: 5 }, (_, i) => ({
    x: rng(),
    y: rng(),
    z: rng(),
    radius: 0.16 + 0.05 * i,
    speed: 0.18 + rng() * 0.6,
    color: 2 + (i % 5),
  }));
  const metric = (base) => clamp(Math.round(base + (rng() - 0.4) * 18), 1, 99);
  return {
    app: "AsterForge",
    shortId: fnv1a(key).toString(16).padStart(8, "0"),
    seed,
    mode,
    intensity,
    tempo,
    density,
    updatedAt: new Date().toISOString(),
    missionName: `${pick(rng, prefixes)} ${pick(rng, nouns)}`,
    tagline: "A local observatory packet generated without the C++ server.",
    weather: pick(rng, weather),
    metrics: [
      { label: "Velocity", value: metric(44 + intensity / 2), unit: "%" },
      { label: "Clarity", value: metric(50 + tempo / 3), unit: "%" },
      { label: "Wonder", value: metric(48 + (intensity + density) / 5), unit: "%" },
      { label: "Tension", value: metric(30 + (100 - tempo) / 4), unit: "%" },
      { label: "Finish", value: metric(38 + (tempo + density) / 5), unit: "%" },
    ],
    priorities: [
      "Prototype the interaction that makes the whole idea feel inevitable",
      "Name the one metric that proves the signal is real",
      "Polish the path from first touch to visible result",
      "Keep the surface dense, calm, and quick to scan",
      "Ship a tiny loop that feels complete in the hand",
    ],
    waypoints: ["Spark", "Shape", "Wire", "Stress", "Reveal", "Launch", "Echo"].map((label, i) => ({
      label,
      minutes: 9 + i * 6 + Math.floor(tempo / 10),
      score: metric(42 + i * 7),
    })),
    notes: [
      "Pages fallback is active; JSON shapes still match the live observatory.",
      "Drag the sky to orbit. Click a node to inspect it.",
      "Seed, mode, intensity, tempo, and density remain shareable in the URL.",
    ],
    palette: palette.colors,
    paletteName: palette.id,
    nodes,
    links,
    rings,
    signature: `${seed} / ${mode} / local sky`,
  };
}

function localSky(seed) {
  const rng = mulberry32(fnv1a(`sky:${seed}`));
  const palette = PALETTES[fnv1a(seed) % PALETTES.length].colors;
  return {
    seed,
    layers: Array.from({ length: 4 }, (_, i) => ({
      id: i,
      color: palette[2 + (i % 5)],
      alpha: 0.12 + i * 0.03,
      scale: 0.9 + rng() * 1.2,
      drift: 0.05 + rng() * 0.14,
      blobs: Array.from({ length: 5 }, () => ({
        x: rng(),
        y: rng(),
        z: rng(),
        r: 0.12 + rng() * 0.28,
        e: 0.4 + rng() * 0.6,
      })),
    })),
  };
}

function localOrbit(seed) {
  const rng = mulberry32(fnv1a(`orbit:${seed}`));
  const palette = PALETTES[fnv1a(`${seed}:orbit`) % PALETTES.length].colors;
  const names = ["Kepler", "Vesper", "Helion", "Nyx", "Iota"];
  return {
    seed,
    star: { color: palette[4], radius: 0.08 },
    planets: names.map((name, i) => ({
      name,
      radius: 0.018 + rng() * 0.03,
      orbit: 0.22 + i * 0.12,
      period: 8 + i * 3.5,
      color: palette[2 + (i % 5)],
      inclination: (rng() - 0.5) * 0.4,
      moons: rng() > 0.5 ? [{ radius: 0.008, orbit: 0.05, period: 3 + rng() * 4, color: palette[1] }] : [],
    })),
  };
}

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`request failed ${response.status}`);
  return response.json();
}

function setServerStatus(kind, label) {
  elements.serverStatus.textContent = label;
  elements.serverStatus.classList.toggle("online", kind === "online");
  elements.serverStatus.classList.toggle("local", kind === "local");
}

async function checkHealth() {
  try {
    const payload = await fetchJson(apiUrl("/api/health"));
    if (payload.status !== "ok") throw new Error("unhealthy");
    state.live = true;
    setServerStatus("online", "online");
  } catch {
    state.live = false;
    setServerStatus("local", "local sky");
  }
}

function worldPos(node) {
  return [(node.x - 0.5) * 8, (node.y - 0.5) * 8, ((node.z ?? 0.5) - 0.5) * 8];
}

function kindName(kind) {
  return ["core", "relay", "forge", "beacon"][kind] || "core";
}

function renderNodeIntel(index) {
  const mission = state.mission;
  if (!mission || index < 0 || !mission.nodes[index]) {
    elements.selectedNodeLabel.textContent = "click a node";
    elements.nodeIntel.replaceChildren(
      makeText("p", "empty-state", "Select a star in the observatory to inspect energy, phase, and kind."),
    );
    return;
  }
  const node = mission.nodes[index];
  elements.selectedNodeLabel.textContent = `node ${index}`;
  const dl = document.createElement("dl");
  const rows = [
    ["Index", String(index)],
    ["Kind", kindName(node.kind)],
    ["Energy", `${node.energy}`],
    ["Size", `${Number(node.size).toFixed(1)}`],
    ["Phase", `${Number(node.phase).toFixed(2)}`],
    ["XYZ", `${Number(node.x).toFixed(2)}, ${Number(node.y).toFixed(2)}, ${Number(node.z ?? 0.5).toFixed(2)}`],
  ];
  rows.forEach(([key, value]) => {
    dl.append(makeText("dt", "", key), makeText("dd", "", value));
  });
  elements.nodeIntel.replaceChildren(dl);
}

function renderMission() {
  const mission = state.mission;
  if (!mission) return;
  setTheme(mission.palette);
  setMode(mission.mode);
  elements.missionName.textContent = mission.missionName;
  elements.missionTagline.textContent = mission.tagline;
  elements.weatherLabel.textContent = mission.weather || "mission";
  elements.updatedAt.textContent = `Updated ${new Date(mission.updatedAt).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`;
  elements.paletteName.textContent = mission.paletteName || mission.seed;
  elements.nodeCount.textContent = `${mission.nodes.length} nodes`;
  elements.routeCount.textContent = `${mission.links.length} routes`;
  elements.signature.textContent = mission.shortId
    ? `${mission.shortId} · ${mission.signature}`
    : mission.signature;

  elements.metrics.replaceChildren(
    ...mission.metrics.map((metric) => {
      const card = document.createElement("article");
      card.className = "metric-card";
      card.append(
        makeText("span", "", metric.label),
        makeText("strong", "", `${metric.value}${metric.unit}`),
        buildMeter(metric.value),
        makeText("em", "", "live"),
      );
      return card;
    }),
  );
  elements.priorities.replaceChildren(
    ...mission.priorities.map((item) => {
      const row = document.createElement("li");
      row.textContent = item;
      return row;
    }),
  );
  elements.waypoints.replaceChildren(
    ...mission.waypoints.map((waypoint) => {
      const row = document.createElement("div");
      row.className = "waypoint";
      row.append(
        makeText("span", "", waypoint.label),
        buildMeter(waypoint.score, "waypoint-meter"),
        makeText("span", "", `${waypoint.minutes}m`),
      );
      return row;
    }),
  );
  elements.notes.replaceChildren(...mission.notes.map((note) => makeText("p", "note", note)));
  elements.swatches.replaceChildren(
    ...mission.palette.map((color) => {
      const swatch = document.createElement("span");
      swatch.className = "swatch";
      swatch.style.background = color;
      swatch.title = color;
      return swatch;
    }),
  );
  if (state.selected >= mission.nodes.length) state.selected = -1;
  renderNodeIntel(state.selected);
  if (rendererApi) rendererApi.sync();
}

async function loadMission() {
  elements.generate.disabled = true;
  const config = currentConfig();
  try {
    if (state.live) {
      const [mission, sky, orbit] = await Promise.all([
        fetchJson(apiUrl("/api/mission", config)),
        fetchJson(apiUrl("/api/sky", { seed: config.seed, layers: 4 })).catch(() => localSky(config.seed)),
        fetchJson(apiUrl("/api/orbit", { seed: config.seed, planets: 5 })).catch(() => localOrbit(config.seed)),
      ]);
      state.mission = mission;
      state.sky = sky;
      state.orbit = orbit;
    } else {
      state.mission = localMission(config);
      state.sky = localSky(config.seed);
      state.orbit = localOrbit(config.seed);
    }
    renderMission();
    syncUrl();
  } catch (error) {
    state.live = false;
    setServerStatus("local", "local sky");
    state.mission = localMission(config);
    state.sky = localSky(config.seed);
    state.orbit = localOrbit(config.seed);
    renderMission();
    console.error(error);
  } finally {
    elements.generate.disabled = false;
  }
}

function renderPresets() {
  if (presets.length === 0) {
    elements.presets.replaceChildren(makeText("p", "empty-state", "No presets available."));
    return;
  }
  elements.presets.replaceChildren(
    ...presets.map((preset) => {
      const button = document.createElement("button");
      button.className = "preset-button";
      button.type = "button";
      button.dataset.mode = preset.id;
      button.append(makeText("strong", "", preset.label), makeText("span", "", preset.description));
      button.addEventListener("click", () => {
        applyConfig({ ...preset, mode: preset.id });
        loadMission();
      });
      return button;
    }),
  );
  setMode(state.mode);
}

async function loadPresets() {
  try {
    const payload = await fetchJson(apiUrl("/api/presets"));
    presets = Array.isArray(payload.presets) ? payload.presets : FALLBACK_PRESETS;
    elements.presetsStatus.textContent = `${presets.length} modes`;
  } catch {
    presets = FALLBACK_PRESETS;
    elements.presetsStatus.textContent = "local";
  }
  renderPresets();
}

function loadSnapshots() {
  try {
    const saved = JSON.parse(localStorage.getItem(SNAPSHOT_KEY) || "[]");
    snapshots = Array.isArray(saved) ? saved.slice(0, MAX_SNAPSHOTS) : [];
  } catch {
    snapshots = [];
  }
}

function saveSnapshots() {
  localStorage.setItem(SNAPSHOT_KEY, JSON.stringify(snapshots));
}

function renderSnapshots() {
  if (snapshots.length === 0) {
    elements.snapshotList.replaceChildren(makeText("p", "empty-state", "No saved missions."));
    return;
  }
  elements.snapshotList.replaceChildren(
    ...snapshots.map((snapshot) => {
      const row = document.createElement("div");
      row.className = "snapshot-row";
      const restore = document.createElement("button");
      restore.className = "snapshot-main";
      restore.type = "button";
      restore.append(
        makeText("strong", "", snapshot.name),
        makeText("span", "", `${modeTitle(snapshot.mode)} / ${snapshot.seed}`),
      );
      restore.addEventListener("click", () => {
        applyConfig(snapshot);
        loadMission();
      });
      const remove = document.createElement("button");
      remove.className = "icon-button remove-snapshot";
      remove.type = "button";
      remove.setAttribute("aria-label", `Remove ${snapshot.name}`);
      remove.textContent = "x";
      remove.addEventListener("click", () => {
        snapshots = snapshots.filter((item) => item.id !== snapshot.id);
        saveSnapshots();
        renderSnapshots();
      });
      row.append(restore, remove);
      return row;
    }),
  );
}

function saveCurrentSnapshot() {
  if (!state.mission) return;
  const snapshot = {
    ...currentConfig(),
    id: state.mission.shortId || `${Date.now()}`,
    name: state.mission.missionName,
  };
  snapshots = [snapshot, ...snapshots.filter((item) => item.id !== snapshot.id)].slice(0, MAX_SNAPSHOTS);
  saveSnapshots();
  renderSnapshots();
}

function makeGlowTexture(THREE) {
  const size = 128;
  const canvas = document.createElement("canvas");
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext("2d");
  const gradient = ctx.createRadialGradient(64, 64, 0, 64, 64, 64);
  gradient.addColorStop(0, "rgba(255,255,255,1)");
  gradient.addColorStop(0.18, "rgba(255,255,255,0.55)");
  gradient.addColorStop(0.42, "rgba(255,255,255,0.12)");
  gradient.addColorStop(1, "rgba(255,255,255,0)");
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, size, size);
  const texture = new THREE.CanvasTexture(canvas);
  texture.needsUpdate = true;
  return texture;
}

function hexColor(THREE, value, fallback = "#10b8a6") {
  try {
    return new THREE.Color(value || fallback);
  } catch {
    return new THREE.Color(fallback);
  }
}

function createThreeRenderer(THREE, OrbitControls) {
  const canvas = elements.canvas;
  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: true,
    alpha: false,
    preserveDrawingBuffer: true,
  });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.setClearColor(0x03050a, 1);
  renderer.outputColorSpace = THREE.SRGBColorSpace;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 200);
  camera.position.set(0.8, 1.4, 9.5);

  const controls = new OrbitControls(camera, canvas);
  controls.enableDamping = true;
  controls.dampingFactor = 0.06;
  controls.minDistance = 3;
  controls.maxDistance = 28;
  controls.target.set(0, 0, 0);
  controls.autoRotate = true;
  controls.autoRotateSpeed = 0.55;
  const flyKeys = { w: false, a: false, s: false, d: false, q: false, e: false };
  let piloting = false;
  const flyDir = new THREE.Vector3();
  const flyRight = new THREE.Vector3();
  window.addEventListener("keydown", (event) => {
    const key = event.key.toLowerCase();
    if (key in flyKeys) flyKeys[key] = true;
    if (key === "f") {
      piloting = !piloting;
      controls.enabled = !piloting;
      canvas.style.cursor = piloting ? "crosshair" : "grab";
    }
  });
  window.addEventListener("keyup", (event) => {
    const key = event.key.toLowerCase();
    if (key in flyKeys) flyKeys[key] = false;
  });

  const glow = makeGlowTexture(THREE);
  const starsGeo = new THREE.BufferGeometry();
  const starCount = 3500;
  const starPos = new Float32Array(starCount * 3);
  for (let i = 0; i < starCount; i += 1) {
    const radius = 38 + Math.random() * 40;
    const theta = Math.random() * Math.PI * 2;
    const phi = Math.acos(2 * Math.random() - 1);
    starPos[i * 3] = radius * Math.sin(phi) * Math.cos(theta);
    starPos[i * 3 + 1] = radius * Math.cos(phi);
    starPos[i * 3 + 2] = radius * Math.sin(phi) * Math.sin(theta);
  }
  starsGeo.setAttribute("position", new THREE.BufferAttribute(starPos, 3));
  const stars = new THREE.Points(
    starsGeo,
    new THREE.PointsMaterial({ color: 0xd7e7ff, size: 0.12, transparent: true, opacity: 0.85 }),
  );
  scene.add(stars);

  const nebulaGroup = new THREE.Group();
  scene.add(nebulaGroup);
  const constellation = new THREE.Group();
  scene.add(constellation);
  const orbitGroup = new THREE.Group();
  orbitGroup.visible = false;
  scene.add(orbitGroup);
  const grid = new THREE.GridHelper(16, 24, 0x1b8f86, 0x14202a);
  grid.position.y = -4.2;
  scene.add(grid);

  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  const nodeMeshes = [];
  let linkLines = null;
  let raf = 0;

  function resize() {
    const width = canvas.clientWidth || 1;
    const height = canvas.clientHeight || 1;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }

  function clearGroup(group) {
    while (group.children.length) {
      const child = group.children[0];
      group.remove(child);
      if (child.geometry) child.geometry.dispose();
      if (child.material) {
        if (Array.isArray(child.material)) child.material.forEach((item) => item.dispose());
        else child.material.dispose();
      }
    }
  }

  function rebuildNebula() {
    clearGroup(nebulaGroup);
    const layers = state.sky && state.sky.layers ? state.sky.layers : [];
    layers.forEach((layer) => {
      (layer.blobs || []).forEach((blob) => {
        const sprite = new THREE.Sprite(
          new THREE.SpriteMaterial({
            map: glow,
            color: hexColor(THREE, layer.color),
            transparent: true,
            opacity: (layer.alpha || 0.16) * (blob.e || 1),
            blending: THREE.AdditiveBlending,
            depthWrite: false,
          }),
        );
        const [x, y, z] = worldPos(blob);
        sprite.position.set(x, y, z);
        const scale = (blob.r || 0.2) * 18 * (layer.scale || 1);
        sprite.scale.set(scale, scale, 1);
        sprite.userData.drift = layer.drift || 0.1;
        nebulaGroup.add(sprite);
      });
    });
  }

  function rebuildOrbit() {
    clearGroup(orbitGroup);
    if (!state.orbit) return;
    const starMesh = new THREE.Mesh(
      new THREE.SphereGeometry(state.orbit.star.radius * 8, 32, 32),
      new THREE.MeshBasicMaterial({ color: hexColor(THREE, state.orbit.star.color, "#f2b544") }),
    );
    const starGlow = new THREE.Sprite(
      new THREE.SpriteMaterial({
        map: glow,
        color: hexColor(THREE, state.orbit.star.color, "#f2b544"),
        blending: THREE.AdditiveBlending,
        transparent: true,
        opacity: 0.8,
        depthWrite: false,
      }),
    );
    starGlow.scale.set(3.2, 3.2, 1);
    orbitGroup.add(starMesh, starGlow);
    (state.orbit.planets || []).forEach((planet) => {
      const pivot = new THREE.Group();
      pivot.rotation.x = planet.inclination || 0;
      const mesh = new THREE.Mesh(
        new THREE.SphereGeometry(planet.radius * 8, 24, 24),
        new THREE.MeshBasicMaterial({ color: hexColor(THREE, planet.color) }),
      );
      mesh.position.x = planet.orbit * 8;
      const ring = new THREE.Mesh(
        new THREE.RingGeometry(planet.orbit * 8 - 0.01, planet.orbit * 8 + 0.01, 64),
        new THREE.MeshBasicMaterial({
          color: hexColor(THREE, planet.color),
          side: THREE.DoubleSide,
          transparent: true,
          opacity: 0.28,
        }),
      );
      ring.rotation.x = Math.PI / 2;
      pivot.add(ring, mesh);
      pivot.userData.period = planet.period || 10;
      (planet.moons || []).forEach((moon) => {
        const moonPivot = new THREE.Group();
        const moonMesh = new THREE.Mesh(
          new THREE.SphereGeometry(moon.radius * 8, 12, 12),
          new THREE.MeshBasicMaterial({ color: hexColor(THREE, moon.color, "#f7f0df") }),
        );
        moonMesh.position.x = moon.orbit * 8;
        moonPivot.add(moonMesh);
        moonPivot.userData.period = moon.period || 4;
        mesh.add(moonPivot);
      });
      orbitGroup.add(pivot);
    });
  }

  function nodeGeometry(kind) {
    if (kind === 1) return new THREE.BoxGeometry(1, 1, 1);
    if (kind === 2) return new THREE.OctahedronGeometry(0.7, 0);
    if (kind === 3) return new THREE.TetrahedronGeometry(0.75, 0);
    return new THREE.SphereGeometry(0.55, 18, 18);
  }

  function rebuildConstellation() {
    clearGroup(constellation);
    nodeMeshes.length = 0;
    const mission = state.mission;
    if (!mission) return;
    const palette = mission.palette || [];
    mission.nodes.forEach((node, index) => {
      const color = hexColor(THREE, palette[2 + (index % 5)]);
      const group = new THREE.Group();
      const mesh = new THREE.Mesh(
        nodeGeometry(node.kind),
        new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.95 }),
      );
      const scale = 0.12 + (node.size || 4) * 0.035 + node.energy / 400;
      mesh.scale.setScalar(scale);
      const sprite = new THREE.Sprite(
        new THREE.SpriteMaterial({
          map: glow,
          color,
          blending: THREE.AdditiveBlending,
          transparent: true,
          opacity: 0.22 + node.energy / 220,
          depthWrite: false,
        }),
      );
      sprite.scale.set(scale * 6, scale * 6, 1);
      group.add(sprite, mesh);
      const pos = worldPos(node);
      group.position.set(...pos);
      group.userData.index = index;
      group.userData.phase = node.phase || 0;
      group.userData.energy = node.energy || 50;
      group.userData.baseY = pos[1];
      constellation.add(group);
      nodeMeshes.push(mesh);
    });
    const positions = [];
    mission.links.forEach((link) => {
      const a = mission.nodes[link[0]];
      const b = mission.nodes[link[1]];
      if (!a || !b) return;
      positions.push(...worldPos(a), ...worldPos(b));
    });
    const geo = new THREE.BufferGeometry();
    geo.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
    linkLines = new THREE.LineSegments(
      geo,
      new THREE.LineBasicMaterial({
        color: hexColor(THREE, palette[2], "#10b8a6"),
        transparent: true,
        opacity: 0.28,
      }),
    );
    constellation.add(linkLines);
  }

  function pickNode(event) {
    const rect = canvas.getBoundingClientRect();
    pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(pointer, camera);
    const hits = raycaster.intersectObjects(nodeMeshes, false);
    if (!hits.length) return;
    const group = hits[0].object.parent;
    state.selected = group.userData.index;
    renderNodeIntel(state.selected);
  }

  function tick() {
    const moving = elements.motion.checked && !state.reducedMotion;
    if (moving) {
      state.time += (0.008 + Number(elements.tempo.value) / 28000) * state.timeScale;
    }
    controls.enableDamping = moving;
    controls.update();
    grid.visible = elements.grid.checked;
    orbitGroup.visible = elements.orbits.checked;
    if (linkLines) {
      linkLines.material.opacity = elements.trails.checked ? 0.38 : 0.16;
    }
    constellation.children.forEach((child) => {
      if (child.userData && child.userData.phase != null) {
        const pulse = Math.sin(state.time * 1.4 + child.userData.phase) * 0.12 * (child.userData.energy / 80);
        child.position.y = child.userData.baseY + pulse;
        child.rotation.y = state.time * 0.15 + child.userData.phase;
      }
    });
    nebulaGroup.children.forEach((sprite, index) => {
      sprite.rotation.z = state.time * (sprite.userData.drift || 0.1) + index * 0.01;
    });
    orbitGroup.children.forEach((pivot) => {
      if (pivot.userData.period) {
        pivot.rotation.y = state.time * (6.28 / Math.max(1, pivot.userData.period));
        pivot.children.forEach((child) => {
          if (child.userData && child.userData.period) {
            child.rotation.y = state.time * (6.28 / child.userData.period);
          }
        });
      }
    });
    controls.autoRotate = moving && !piloting;
    if (piloting) {
      camera.getWorldDirection(flyDir);
      flyRight.crossVectors(flyDir, camera.up).normalize();
      const speed = (0.12 + Number(elements.tempo.value) / 400) * state.timeScale;
      if (flyKeys.w) camera.position.addScaledVector(flyDir, speed);
      if (flyKeys.s) camera.position.addScaledVector(flyDir, -speed);
      if (flyKeys.d) camera.position.addScaledVector(flyRight, speed);
      if (flyKeys.a) camera.position.addScaledVector(flyRight, -speed);
      if (flyKeys.e) camera.position.y += speed;
      if (flyKeys.q) camera.position.y -= speed;
      controls.target.copy(camera.position).addScaledVector(flyDir, 6);
    }
    renderer.render(scene, camera);
    raf = requestAnimationFrame(tick);
  }

  canvas.addEventListener("pointerdown", pickNode);
  window.addEventListener("resize", resize);
  resize();
  raf = requestAnimationFrame(tick);

  return {
    engine: "webgl",
    sync() {
      rebuildNebula();
      rebuildOrbit();
      rebuildConstellation();
    },
    capture() {
      return renderer.domElement.toDataURL("image/png");
    },
    dispose() {
      cancelAnimationFrame(raf);
      window.removeEventListener("resize", resize);
      renderer.dispose();
    },
  };
}

function createCanvasRenderer() {
  const canvas = elements.canvas;
  const ctx = canvas.getContext("2d");
  const cam = { theta: 0.55, phi: 1.05, radius: 14, vx: 0, vy: 0 };
  let dragging = false;
  let last = { x: 0, y: 0 };
  let raf = 0;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const scale = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.max(1, Math.floor(rect.width * scale));
    canvas.height = Math.max(1, Math.floor(rect.height * scale));
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
  }

  function project(x, y, z, width, height) {
    const cosT = Math.cos(cam.theta);
    const sinT = Math.sin(cam.theta);
    const cosP = Math.cos(cam.phi);
    const sinP = Math.sin(cam.phi);
    const cx = cam.radius * sinP * sinT;
    const cy = cam.radius * cosP;
    const cz = cam.radius * sinP * cosT;
    const fx = -cx;
    const fy = -cy;
    const fz = -cz;
    const flen = Math.hypot(fx, fy, fz) || 1;
    const fxx = fx / flen;
    const fyy = fy / flen;
    const fzz = fz / flen;
    let rx = -fz;
    let rz = fx;
    const rlen = Math.hypot(rx, rz) || 1;
    rx /= rlen;
    rz /= rlen;
    const ux = fyy * rz;
    const uy = fzz * rx - fxx * rz;
    const uz = -fyy * rx;
    const dx = x - cx;
    const dy = y - cy;
    const dz = z - cz;
    const pz = dx * fxx + dy * fyy + dz * fzz;
    const px = dx * rx + dz * rz;
    const py = dx * ux + dy * uy + dz * uz;
    const depth = Math.max(0.35, pz);
    const scale = (Math.min(width, height) * 0.62) / depth;
    return { x: width / 2 + px * scale, y: height / 2 - py * scale, z: depth, s: scale };
  }

  function draw() {
    const width = canvas.clientWidth || 1;
    const height = canvas.clientHeight || 1;
    const moving = elements.motion.checked && !state.reducedMotion;
    if (moving) {
      state.time += (0.008 + Number(elements.tempo.value) / 28000) * state.timeScale;
      cam.theta += cam.vx;
      cam.phi = clamp(cam.phi + cam.vy, 0.2, Math.PI - 0.2);
      cam.vx *= 0.92;
      cam.vy *= 0.92;
    }
    const palette = (state.mission && state.mission.palette) || ["#03050a", "#fff", "#10b8a6", "#ef5e4d", "#f2b544", "#8c6cf5", "#77c66e"];
    ctx.globalCompositeOperation = "source-over";
    const bg = ctx.createLinearGradient(0, 0, width, height);
    bg.addColorStop(0, palette[0] || "#03050a");
    bg.addColorStop(1, "#050814");
    ctx.fillStyle = bg;
    if (elements.trails.checked) {
      ctx.globalAlpha = 0.22;
      ctx.fillRect(0, 0, width, height);
      ctx.globalAlpha = 1;
    } else {
      ctx.fillRect(0, 0, width, height);
    }

    if (state.sky) {
      ctx.globalCompositeOperation = "lighter";
      state.sky.layers.forEach((layer) => {
        (layer.blobs || []).forEach((blob) => {
          const p = project(...worldPos(blob), width, height);
          const radius = (blob.r || 0.2) * p.s * 1.4;
          const gradient = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, radius);
          gradient.addColorStop(0, layer.color);
          gradient.addColorStop(1, "rgba(0,0,0,0)");
          ctx.globalAlpha = (layer.alpha || 0.16) * (blob.e || 1);
          ctx.fillStyle = gradient;
          ctx.beginPath();
          ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
          ctx.fill();
        });
      });
      ctx.globalAlpha = 1;
      ctx.globalCompositeOperation = "source-over";
    }

    if (elements.grid.checked) {
      ctx.strokeStyle = "rgba(16,184,166,0.18)";
      ctx.lineWidth = 1;
      for (let i = -4; i <= 4; i += 1) {
        const a = project(i, -4.2, -4, width, height);
        const b = project(i, -4.2, 4, width, height);
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(b.x, b.y);
        ctx.stroke();
        const c = project(-4, -4.2, i, width, height);
        const d = project(4, -4.2, i, width, height);
        ctx.beginPath();
        ctx.moveTo(c.x, c.y);
        ctx.lineTo(d.x, d.y);
        ctx.stroke();
      }
    }

    const mission = state.mission;
    if (mission) {
      const projected = mission.nodes.map((node, index) => {
        const spin = state.time * 0.6 + (node.phase || 0);
        const [x, y, z] = worldPos(node);
        const p = project(x, y + Math.sin(spin) * 0.12, z, width, height);
        p.index = index;
        p.node = node;
        p.color = palette[2 + (index % 5)] || palette[2];
        return p;
      });
      ctx.lineWidth = 1.1;
      mission.links.forEach((link) => {
        const a = projected[link[0]];
        const b = projected[link[1]];
        if (!a || !b) return;
        ctx.strokeStyle = palette[2];
        ctx.globalAlpha = elements.trails.checked ? 0.28 + (link[2] || 40) / 400 : 0.12;
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(b.x, b.y);
        ctx.stroke();
      });
      ctx.globalAlpha = 1;
      ctx.globalCompositeOperation = "lighter";
      projected
        .slice()
        .sort((a, b) => b.z - a.z)
        .forEach((p) => {
          const radius = Math.max(1.8, (p.node.size || 4) * (8 / p.z));
          ctx.fillStyle = p.color;
          ctx.globalAlpha = 0.22;
          ctx.beginPath();
          ctx.arc(p.x, p.y, radius * 3.2, 0, Math.PI * 2);
          ctx.fill();
          ctx.globalAlpha = 0.95;
          ctx.beginPath();
          ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
          ctx.fill();
          if (p.index === state.selected) {
            ctx.strokeStyle = palette[1];
            ctx.globalAlpha = 0.9;
            ctx.stroke();
          }
        });
      ctx.globalAlpha = 1;
      ctx.globalCompositeOperation = "source-over";
    }
    raf = requestAnimationFrame(draw);
  }

  canvas.addEventListener("pointerdown", (event) => {
    dragging = true;
    last = { x: event.clientX, y: event.clientY };
    canvas.setPointerCapture(event.pointerId);
    const rect = canvas.getBoundingClientRect();
    const width = rect.width;
    const height = rect.height;
    if (!state.mission) return;
    const px = event.clientX - rect.left;
    const py = event.clientY - rect.top;
    let best = -1;
    let bestDist = 18;
    state.mission.nodes.forEach((node, index) => {
      const p = project(...worldPos(node), width, height);
      const dist = Math.hypot(p.x - px, p.y - py);
      if (dist < bestDist) {
        best = index;
        bestDist = dist;
      }
    });
    if (best >= 0) {
      state.selected = best;
      renderNodeIntel(best);
    }
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!dragging) return;
    const dx = event.clientX - last.x;
    const dy = event.clientY - last.y;
    last = { x: event.clientX, y: event.clientY };
    cam.theta -= dx * 0.005;
    cam.phi = clamp(cam.phi - dy * 0.005, 0.2, Math.PI - 0.2);
    cam.vx = -dx * 0.0008;
    cam.vy = -dy * 0.0008;
  });
  canvas.addEventListener("pointerup", () => {
    dragging = false;
  });
  canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    cam.radius = clamp(cam.radius + event.deltaY * 0.01, 6, 32);
  }, { passive: false });
  window.addEventListener("resize", resize);
  resize();
  raf = requestAnimationFrame(draw);

  return {
    engine: "canvas",
    sync() {},
    capture() {
      return canvas.toDataURL("image/png");
    },
    dispose() {
      cancelAnimationFrame(raf);
    },
  };
}

async function bootRenderer() {
  try {
    const THREE = await import("three");
    const { OrbitControls } = await import("three/addons/controls/OrbitControls.js");
    rendererApi = createThreeRenderer(THREE, OrbitControls);
  } catch (error) {
    console.warn("Three.js unavailable, using canvas observatory", error);
    rendererApi = createCanvasRenderer();
  }
}

function connectTelemetry() {
  if (streamHandle) {
    streamHandle.close();
    streamHandle = null;
  }
  if (!state.live || typeof EventSource === "undefined") {
    elements.telemetryStrip.hidden = true;
    return;
  }
  try {
    const source = new EventSource(apiUrl("/api/stream"));
    streamHandle = source;
    source.addEventListener("telemetry", (event) => {
      try {
        const payload = JSON.parse(event.data);
        elements.telemetryStrip.hidden = false;
        elements.telRequests.textContent = `req ${payload.requests ?? 0}`;
        elements.telP99.textContent = `p99 ${Number(payload.p99 || 0).toFixed(1)}ms`;
        elements.tel2xx.textContent = `2xx ${payload["2xx"] ?? 0}`;
        elements.tel4xx.textContent = `4xx ${payload["4xx"] ?? 0}`;
        elements.tel5xx.textContent = `5xx ${payload["5xx"] ?? 0}`;
      } catch {
        elements.telemetryStrip.hidden = true;
      }
    });
    source.onerror = () => {
      source.close();
      streamHandle = null;
      elements.telemetryStrip.hidden = true;
    };
  } catch {
    elements.telemetryStrip.hidden = true;
  }
}

function toggleHelp(force) {
  const hidden = force == null ? !elements.helpOverlay.hidden : !force;
  elements.helpOverlay.hidden = hidden;
}

function bindEvents() {
  document.querySelectorAll(".mode-button").forEach((button) => {
    button.addEventListener("click", () => {
      setMode(button.dataset.mode);
      loadMission();
    });
  });
  document.querySelectorAll(".scale-button").forEach((button) => {
    button.addEventListener("click", () => {
      state.timeScale = Number(button.dataset.scale) || 1;
      document.querySelectorAll(".scale-button").forEach((item) => {
        item.classList.toggle("active", item === button);
      });
    });
  });
  [elements.intensity, elements.tempo, elements.density].forEach((input) => {
    input.addEventListener("input", updateRangeLabels);
    input.addEventListener("change", loadMission);
  });
  elements.seed.addEventListener("change", loadMission);
  elements.generate.addEventListener("click", loadMission);
  elements.randomizeSeed.addEventListener("click", () => {
    const first = seedWords[Math.floor(Math.random() * seedWords.length)];
    const second = seedWords[Math.floor(Math.random() * seedWords.length)];
    elements.seed.value = `${first}-${second}`;
    loadMission();
  });
  elements.orbits.addEventListener("change", () => {
    if (rendererApi) rendererApi.sync();
  });
  elements.saveSnapshot.addEventListener("click", saveCurrentSnapshot);
  elements.helpButton.addEventListener("click", () => toggleHelp(true));
  elements.closeHelp.addEventListener("click", () => toggleHelp(false));
  elements.helpOverlay.addEventListener("click", (event) => {
    if (event.target === elements.helpOverlay) toggleHelp(false);
  });

  elements.copy.addEventListener("click", async () => {
    if (!state.mission) return;
    try {
      await navigator.clipboard.writeText(JSON.stringify(state.mission, null, 2));
      setButtonFeedback(elements.copy, copyMarkup, "Copied");
    } catch {
      setButtonFeedback(elements.copy, copyMarkup, "Copy failed");
    }
  });
  elements.copyLink.addEventListener("click", async () => {
    const url = shareUrl();
    try {
      await navigator.clipboard.writeText(url);
      setButtonFeedback(elements.copyLink, copyLinkMarkup, "Copied");
    } catch {
      downloadText("asterforge-share-link.txt", `${url}\n`);
      setButtonFeedback(elements.copyLink, copyLinkMarkup, "Downloaded");
    }
  });
  elements.downloadPng.addEventListener("click", () => {
    if (!state.mission || !rendererApi) return;
    try {
      downloadDataUrl(
        `asterforge-${state.mission.shortId || "mission"}.png`,
        rendererApi.capture(),
      );
      setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Saved");
    } catch {
      setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Save failed");
    }
  });

  window.addEventListener("keydown", (event) => {
    const tag = (event.target && event.target.tagName) || "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
      if (event.key === "Escape") event.target.blur();
      return;
    }
    if (event.key === "?" || (event.shiftKey && event.key === "/")) {
      toggleHelp();
    } else if (event.key === "Escape") {
      toggleHelp(false);
    } else if (event.key === "g" || event.key === "G") {
      loadMission();
    } else if (event.key === "r" || event.key === "R") {
      elements.randomizeSeed.click();
    } else if (event.key >= "1" && event.key <= "6") {
      setMode(MODES[Number(event.key) - 1]);
      loadMission();
    }
  });
}

async function boot() {
  applyInitialUrlState();
  updateRangeLabels();
  loadSnapshots();
  renderSnapshots();
  bindEvents();
  await bootRenderer();
  await checkHealth();
  await loadPresets();
  await loadMission();
  connectTelemetry();
}

boot();
