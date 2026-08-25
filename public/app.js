const CLIENT_VERSION = "2.4.0";
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const FALLBACK_PALETTES = [
  { id: "ember", name: "Ember Desk", colors: ["#171717", "#f7f2e8", "#247c76", "#d85d4c", "#c79a34", "#6e62a6"] },
  { id: "harbor", name: "Harbor Studio", colors: ["#222226", "#f4efe3", "#2f6f9f", "#cf5b39", "#8fa34a", "#7b4f87"] },
  { id: "aurora", name: "Aurora Field", colors: ["#161616", "#fbfaf6", "#18706a", "#b94f5f", "#d0a13f", "#476b9b"] },
  { id: "midnight", name: "Midnight Forge", colors: ["#0f1115", "#e8eef7", "#3d7ea6", "#e07a5f", "#f2cc8f", "#81b29a"] },
  { id: "orchid", name: "Orchid Signal", colors: ["#1a1423", "#f6f0ff", "#7b5ea7", "#e07a9a", "#f4c95f", "#4ecdc4"] }
];

const PLANET_NAMES = [
  "Aether", "Nyx", "Helios", "Selene", "Atlas", "Lyra", "Vega", "Rigel",
  "Electra", "Thalassa", "Hyperion", "Andromeda", "Cassiopeia", "Orion",
  "Perseus", "Io"
];

const state = {
  mode: "pulse",
  mission: null,
  paletteId: null,
  palettes: [],
  constellation: null,
  constellationSeed: Math.floor(Math.random() * 10000),
  sky: null,
  orbit: null,
  comets: null,
  dust: [],
  usingFallback: false,
  pointer: { x: 0.5, y: 0.5, active: false },
  time: 0,
  timeScale: 1,
  warp: false,
  warpAmount: 0,
  telemetry: { history: [] },
  shootingStars: [],
  nextShootingStarAt: 0,
  prevStarPositions: [],
  prevNodePositions: [],
  planetHits: [],
  inspected: null
};

const elements = {
  canvas: document.querySelector("#skyCanvas"),
  serverStatus: document.querySelector("#serverStatus"),
  uptimeValue: document.querySelector("#uptimeValue"),
  requestCount: document.querySelector("#requestCount"),
  serverVersion: document.querySelector("#serverVersion"),
  serverMetrics: document.querySelector("#serverMetrics"),
  missionName: document.querySelector("#missionName"),
  missionTagline: document.querySelector("#missionTagline"),
  updatedAt: document.querySelector("#updatedAt"),
  intensity: document.querySelector("#intensityInput"),
  intensityValue: document.querySelector("#intensityValue"),
  tempo: document.querySelector("#tempoInput"),
  tempoValue: document.querySelector("#tempoValue"),
  seed: document.querySelector("#seedInput"),
  grid: document.querySelector("#gridToggle"),
  trails: document.querySelector("#trailToggle"),
  orbitToggle: document.querySelector("#orbitToggle"),
  nebulaToggle: document.querySelector("#nebulaToggle"),
  cometToggle: document.querySelector("#cometToggle"),
  generate: document.querySelector("#generateButton"),
  randomizeSeed: document.querySelector("#randomizeSeed"),
  reseedConstellation: document.querySelector("#reseedConstellation"),
  copy: document.querySelector("#copyButton"),
  warpButton: document.querySelector("#warpButton"),
  metrics: document.querySelector("#metricStrip"),
  priorities: document.querySelector("#priorityList"),
  waypoints: document.querySelector("#waypoints"),
  swatches: document.querySelector("#swatches"),
  paletteName: document.querySelector("#paletteName"),
  palettePicker: document.querySelector("#palettePicker"),
  modeBadge: document.querySelector("#modeBadge"),
  sparkCanvas: document.querySelector("#sparkCanvas"),
  rpsValue: document.querySelector("#rpsValue"),
  latencyP50: document.querySelector("#latencyP50"),
  latencyP99: document.querySelector("#latencyP99"),
  statusChips: document.querySelector("#statusChips"),
  telemetryTick: document.querySelector("#telemetryTick"),
  shortcutOverlay: document.querySelector("#shortcutOverlay"),
  planetInspect: document.querySelector("#planetInspect"),
  inspectName: document.querySelector("#inspectName"),
  inspectOrbit: document.querySelector("#inspectOrbit"),
  inspectPeriod: document.querySelector("#inspectPeriod"),
  inspectMoons: document.querySelector("#inspectMoons"),
  inspectRing: document.querySelector("#inspectRing"),
  inspectClose: document.querySelector("#inspectClose")
};

const ctx = elements.canvas.getContext("2d");

function readUrlState() {
  const params = new URLSearchParams(window.location.search);
  if (params.has("seed") && elements.seed) elements.seed.value = params.get("seed");
  if (params.has("mode")) state.mode = params.get("mode");
  if (params.has("intensity") && elements.intensity) elements.intensity.value = params.get("intensity");
  if (params.has("tempo") && elements.tempo) elements.tempo.value = params.get("tempo");
  if (params.has("palette")) state.paletteId = params.get("palette");
}

function writeUrlState() {
  const params = new URLSearchParams({
    seed: elements.seed?.value || "sebby",
    mode: state.mode,
    intensity: elements.intensity?.value || "68",
    tempo: elements.tempo?.value || "42",
  });
  if (state.paletteId) params.set("palette", state.paletteId);
  const next = `${window.location.pathname}?${params.toString()}`;
  window.history.replaceState({}, "", next);
}

const seedWords = ["kinetic", "harbor", "cedar", "lumen", "summit", "signal", "maker", "atlas", "bright", "orbit"];

function nebulaEnabled() {
  return !elements.nebulaToggle || elements.nebulaToggle.checked;
}

function orbitEnabled() {
  return !elements.orbitToggle || elements.orbitToggle.checked;
}

function cometEnabled() {
  return !elements.cometToggle || elements.cometToggle.checked;
}

function stableSeed(value) {
  let hash = 2166136261;
  const text = String(value);
  for (let i = 0; i < text.length; i += 1) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
}

function makeRng(seed) {
  let a = seed >>> 0;
  return function next() {
    a |= 0;
    a = a + 0x6D2B79F5 | 0;
    let t = Math.imul(a ^ a >>> 15, 1 | a);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

function hslToHex(h, s, l) {
  const sat = Math.max(0, Math.min(1, s));
  const light = Math.max(0, Math.min(1, l));
  const a = sat * Math.min(light, 1 - light);
  const f = (n) => {
    const k = (n + h / 30) % 12;
    const color = light - a * Math.max(Math.min(k - 3, 9 - k, 1), -1);
    return Math.round(255 * color).toString(16).padStart(2, "0");
  };
  return `#${f(0)}${f(8)}${f(4)}`;
}

function shuffleInPlace(list, rng) {
  for (let i = list.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rng() * (i + 1));
    const swap = list[i];
    list[i] = list[j];
    list[j] = swap;
  }
  return list;
}

function generateSky(seed, layers) {
  const seedText = seed || "sebby";
  const count = Math.max(2, Math.min(8, layers || 4));
  const rng = makeRng(stableSeed(`${seedText}:sky:${count}`));
  const packLayers = [];
  for (let i = 0; i < count; i += 1) {
    packLayers.push({
      hue: rng() * 360,
      sat: 28 + rng() * 64,
      light: 18 + rng() * 54,
      x: 0.08 + rng() * 0.84,
      y: 0.08 + rng() * 0.84,
      radius: 0.16 + rng() * 0.62,
      alpha: 0.12 + rng() * 0.5
    });
  }
  return {
    seed: seedText,
    version: CLIENT_VERSION,
    haze: rng(),
    layers: packLayers,
    dust: 20 + Math.floor(rng() * 201),
    aurora: {
      enabled: rng() > 0.32,
      hue: rng() * 360,
      amplitude: rng(),
      speed: rng()
    }
  };
}

function generateOrbit(seed, planets) {
  const s = Math.max(0, Math.min(1000000, Number(seed) || 42));
  const count = Math.max(3, Math.min(10, planets || 6));
  const rng = makeRng((Math.imul(s, 2654435761) + 1337) >>> 0);
  const names = shuffleInPlace(PLANET_NAMES.slice(), rng);
  const span = 0.80;
  const gap = count > 1 ? span / (count - 1) : 0;
  const starHue = 32 + rng() * 28;
  const list = [];
  for (let i = 0; i < count; i += 1) {
    const jitter = (rng() - 0.5) * gap * 0.22;
    const orbit = Math.max(0.12, Math.min(0.92, 0.12 + gap * i + jitter));
    list.push({
      name: names[i % names.length],
      color: hslToHex(rng() * 360, 0.42 + rng() * 0.45, 0.38 + rng() * 0.28),
      orbit,
      radius: 0.01 + rng() * 0.04,
      period: Math.max(4, Math.min(40, 4 + 36 * ((orbit - 0.12) / span) + (rng() - 0.5) * 3)),
      phase: rng() * 6.2832,
      moons: Math.floor(rng() * 4),
      ring: rng() < 0.22
    });
  }
  return {
    seed: s,
    version: CLIENT_VERSION,
    star: {
      color: hslToHex(starHue, 0.82 + rng() * 0.16, 0.62 + rng() * 0.12),
      radius: 0.07 + rng() * 0.03,
      flare: 0.18 + rng() * 0.62
    },
    planets: list
  };
}

function generateComet(seed, count) {
  const parsedSeed = Number(seed);
  const seedValue = Number.isFinite(parsedSeed)
    ? Math.max(0, Math.min(1000000, parsedSeed))
    : 7;
  const parsedCount = Number(count);
  const n = Math.max(1, Math.min(6, Number.isFinite(parsedCount) ? parsedCount : 2));
  const rng = makeRng((Math.imul(seedValue, 2654435761) + 2407) >>> 0);
  const comets = [];
  for (let i = 0; i < n; i += 1) {
    comets.push({
      x: rng(),
      y: rng(),
      dx: -0.4 + rng() * 0.8,
      dy: -0.2 + rng() * 0.4,
      len: 0.05 + rng() * 0.2,
      hue: rng() * 360
    });
  }
  return { seed: seedValue, version: CLIENT_VERSION, comets };
}

function generateConstellation(seed, points) {
  const s = Math.max(0, Math.min(1000000, Number(seed) || 42));
  const n = Math.max(4, Math.min(128, points || 24));
  const rng = makeRng((Math.imul(s, 2654435761) + 97) >>> 0);
  const stars = [];
  const links = [];
  for (let i = 0; i < n; i += 1) {
    stars.push({
      x: 0.05 + rng() * 0.9,
      y: 0.05 + rng() * 0.9,
      size: 1.5 + rng() * 4,
      brightness: 0.35 + rng() * 0.65
    });
  }
  for (let i = 0; i < n - 1; i += 1) {
    const jump = 1 + Math.floor(rng() * Math.min(4, n - 1));
    links.push([i, (i + jump) % n]);
  }
  return { seed: s, points: n, stars, links };
}

function generateMission() {
  const seed = elements.seed?.value || "sebby";
  const mode = state.mode || "pulse";
  const intensity = Number(elements.intensity?.value) || 68;
  const tempo = Number(elements.tempo?.value) || 42;
  const rng = makeRng(stableSeed(`${seed}:${mode}:${intensity}:${tempo}:${state.paletteId || ""}`));
  const palettes = state.palettes.length ? state.palettes : FALLBACK_PALETTES;
  let palette = palettes[stableSeed(mode) % palettes.length];
  if (state.paletteId) {
    const found = palettes.find((item) => item.id === state.paletteId);
    if (found) palette = found;
  }
  const pick = (values) => values[Math.floor(rng() * values.length)];
  const metric = (base) => Math.max(1, Math.min(99, base + Math.floor(rng() * 24) - 9));
  const prefixes = ["Aurora", "Vector", "Signal", "Lumen", "Civic", "Keystone", "Nova", "Harbor"];
  const nouns = ["Circuit", "Studio", "Atlas", "Engine", "Desk", "Forge", "Field", "Pulse"];
  const taglines = [
    "Turn rough sparks into a focused launch board.",
    "Shape a crisp interface around messy momentum.",
    "Make the next move visible, measurable, and satisfying.",
    "Blend strategy, rhythm, and craft into one working surface."
  ];
  const priorities = shuffleInPlace([
    "Prototype the most useful interaction first",
    "Name the one metric that proves traction",
    "Polish the path from idea to visible result",
    "Keep the dashboard dense, calm, and quick to scan",
    "Ship a tiny loop that feels complete",
    "Use motion only where it clarifies state"
  ], rng).slice(0, 4);
  const stages = ["Map", "Focus", "Build", "Tune", "Launch", "Learn"];
  const nodeCount = 18 + Math.floor(intensity / 8);
  const nodes = [];
  const links = [];
  for (let i = 0; i < nodeCount; i += 1) {
    nodes.push({
      x: 0.08 + rng() * 0.84,
      y: 0.08 + rng() * 0.84,
      size: 3 + Math.floor(rng() * 8),
      energy: 28 + Math.floor(rng() * 72)
    });
  }
  for (let i = 0; i < nodeCount - 1; i += 1) {
    links.push([i, (i + 1 + Math.floor(rng() * 4)) % nodeCount]);
  }
  return {
    app: "AsterForge",
    version: CLIENT_VERSION,
    seed,
    mode,
    intensity,
    tempo,
    updatedAt: new Date().toISOString(),
    missionName: `${pick(prefixes)} ${pick(nouns)}`,
    tagline: pick(taglines),
    paletteId: palette.id,
    paletteName: palette.name,
    metrics: [
      { label: "Momentum", value: metric(58 + Math.floor(intensity / 3)), unit: "%" },
      { label: "Clarity", value: metric(54 + Math.floor(tempo / 4)), unit: "%" },
      { label: "Delight", value: metric(62 + Math.floor((intensity + tempo) / 8)), unit: "%" },
      { label: "Risk", value: metric(34 + Math.floor((100 - tempo) / 5)), unit: "%" }
    ],
    priorities,
    waypoints: stages.map((label, index) => ({
      label,
      minutes: 12 + index * 7 + Math.floor(tempo / 9),
      score: metric(48 + index * 6 + Math.floor(intensity / 8))
    })),
    palette: palette.colors.slice(),
    nodes,
    links
  };
}

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${url} failed`);
  return response.json();
}

function resizeCanvas() {
  const rect = elements.canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  elements.canvas.width = Math.max(1, Math.floor(rect.width * scale));
  elements.canvas.height = Math.max(1, Math.floor(rect.height * scale));
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
  if (reducedMotion.matches) {
    drawCanvas(false);
  }
}

function modeTitle(value) {
  return value.charAt(0).toUpperCase() + value.slice(1);
}

function formatUptime(seconds) {
  const s = Math.max(0, Number(seconds) || 0);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  const rem = s % 60;
  if (m < 60) return `${m}m ${rem}s`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}m`;
}

function apiUrl() {
  writeUrlState();
  const params = new URLSearchParams({
    seed: elements.seed.value || "sebby",
    mode: state.mode,
    intensity: elements.intensity.value,
    tempo: elements.tempo.value
  });
  if (state.paletteId) {
    params.set("palette", state.paletteId);
  }
  return `/api/mission?${params}`;
}

async function checkHealth() {
  try {
    const response = await fetch("/api/health");
    if (!response.ok) throw new Error("health check failed");
    const data = await response.json();
    if (!elements.serverStatus.classList.contains("live")) {
      elements.serverStatus.textContent = "online";
      elements.serverStatus.classList.add("online");
    }
    if (elements.uptimeValue) {
      elements.uptimeValue.textContent = formatUptime(data.uptime_seconds);
    }
    if (elements.requestCount) {
      elements.requestCount.textContent = String(data.request_count ?? "—");
    }
    if (elements.serverVersion) {
      elements.serverVersion.textContent = data.version || CLIENT_VERSION;
    }
  } catch {
    elements.serverStatus.textContent = "offline";
    elements.serverStatus.classList.remove("online", "live");
    if (elements.serverVersion && (elements.serverVersion.textContent === "—" || state.usingFallback)) {
      elements.serverVersion.textContent = `${CLIENT_VERSION}*`;
    }
  }
}

function metricsMarkup(data) {
  const paths = Object.entries(data.by_path || {})
    .sort((a, b) => b[1] - a[1])
    .slice(0, 6)
    .map(([path, count]) => `<li><code>${path}</code> <strong>${count}</strong></li>`)
    .join("");
  return `
    <p class="metrics-summary">
      <span>total <strong>${data.total_requests}</strong></span>
      <span>up <strong>${formatUptime(data.uptime_seconds)}</strong></span>
    </p>
    <ul class="path-metrics">${paths || "<li>No traffic yet</li>"}</ul>
  `;
}

async function loadMetrics() {
  if (!elements.serverMetrics) return;
  try {
    const response = await fetch("/api/metrics");
    if (!response.ok) throw new Error("metrics failed");
    const data = await response.json();
    elements.serverMetrics.innerHTML = metricsMarkup(data);
  } catch {
    elements.serverMetrics.textContent = state.usingFallback
      ? "Static demo — C++ APIs unavailable, local sky/orbit generators are running."
      : "Metrics unavailable";
  }
}

/* ---------------------------------------------------------------------------
 * Live telemetry over Server-Sent Events, with polling as the fallback.
 * ------------------------------------------------------------------------- */

let pollTimer = null;

function startPolling() {
  if (pollTimer) return;
  checkHealth();
  loadMetrics();
  pollTimer = window.setInterval(() => {
    checkHealth();
    loadMetrics();
  }, 4000);
}

function stopPolling() {
  if (!pollTimer) return;
  window.clearInterval(pollTimer);
  pollTimer = null;
}

function connectTelemetry() {
  if (!window.EventSource) {
    startPolling();
    return;
  }
  const source = new EventSource("/api/stream");
  source.addEventListener("open", () => {
    stopPolling();
    elements.serverStatus.textContent = "live";
    elements.serverStatus.classList.add("online", "live");
    elements.serverStatus.classList.remove("reconnecting");
  });
  source.addEventListener("telemetry", (event) => {
    let data;
    try {
      data = JSON.parse(event.data);
    } catch {
      return;
    }
    applyTelemetry(data);
  });
  source.addEventListener("error", () => {
    elements.serverStatus.textContent = state.usingFallback ? "offline" : "reconnecting";
    elements.serverStatus.classList.remove("online", "live");
    if (!state.usingFallback) {
      elements.serverStatus.classList.add("reconnecting");
    }
    startPolling();
  });
}

function applyTelemetry(data) {
  const history = state.telemetry.history;
  history.push({ at: performance.now(), total: Number(data.total_requests) || 0 });
  while (history.length > 60) {
    history.shift();
  }

  elements.serverStatus.textContent = "live";
  elements.serverStatus.classList.add("online", "live");
  elements.serverStatus.classList.remove("reconnecting");
  if (elements.uptimeValue) {
    elements.uptimeValue.textContent = formatUptime(data.uptime_seconds);
  }
  if (elements.requestCount) {
    elements.requestCount.textContent = String(data.total_requests ?? "—");
  }
  if (elements.telemetryTick) {
    elements.telemetryTick.textContent = `tick ${data.tick ?? "—"}`;
  }

  if (elements.rpsValue) {
    elements.rpsValue.textContent = requestsPerSecond().toFixed(1);
  }
  const latency = data.latency_ms || {};
  if (elements.latencyP50) {
    elements.latencyP50.textContent = `${Number(latency.p50 ?? 0).toFixed(2)}ms`;
  }
  if (elements.latencyP99) {
    elements.latencyP99.textContent = `${Number(latency.p99 ?? 0).toFixed(2)}ms`;
  }
  renderStatusChips(data.status || {});
  drawSparkline();

  if (elements.serverMetrics) {
    elements.serverMetrics.innerHTML = metricsMarkup(data);
  }
}

function requestsPerSecond() {
  const history = state.telemetry.history;
  if (history.length < 2) return 0;
  const first = history[Math.max(0, history.length - 6)];
  const last = history[history.length - 1];
  const dt = (last.at - first.at) / 1000;
  if (dt <= 0) return 0;
  return Math.max(0, (last.total - first.total) / dt);
}

function renderStatusChips(status) {
  if (!elements.statusChips) return;
  const classes = ["2xx", "3xx", "4xx", "5xx"];
  elements.statusChips.innerHTML = classes.map((name) => `
    <span class="chip status-${name}"><span>${name}</span><strong>${status[name] ?? 0}</strong></span>
  `).join("");
}

function cssColor(name, fallback) {
  const value = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  return value || fallback;
}

function drawSparkline() {
  const canvas = elements.sparkCanvas;
  if (!canvas) return;
  const rect = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  const height = rect.height || 64;
  canvas.width = Math.max(1, Math.floor(rect.width * scale));
  canvas.height = Math.max(1, Math.floor(height * scale));
  const sctx = canvas.getContext("2d");
  sctx.setTransform(scale, 0, 0, scale, 0, 0);
  sctx.clearRect(0, 0, rect.width, height);

  const history = state.telemetry.history;
  const deltas = [];
  for (let i = 1; i < history.length; i += 1) {
    const dt = (history[i].at - history[i - 1].at) / 1000;
    deltas.push(dt > 0 ? Math.max(0, (history[i].total - history[i - 1].total) / dt) : 0);
  }
  if (!deltas.length) return;

  const max = Math.max(1, ...deltas);
  const stroke = cssColor("--teal", "#247c76");
  const stepX = deltas.length > 1 ? rect.width / (deltas.length - 1) : rect.width;
  const pointY = (value) => height - 4 - (value / max) * (height - 10);

  sctx.beginPath();
  deltas.forEach((value, index) => {
    const x = deltas.length > 1 ? index * stepX : rect.width;
    if (index === 0) {
      sctx.moveTo(x, pointY(value));
    } else {
      sctx.lineTo(x, pointY(value));
    }
  });
  sctx.strokeStyle = stroke;
  sctx.lineWidth = 2;
  sctx.lineJoin = "round";
  sctx.stroke();

  sctx.lineTo(deltas.length > 1 ? (deltas.length - 1) * stepX : rect.width, height);
  sctx.lineTo(0, height);
  sctx.closePath();
  sctx.globalAlpha = 0.14;
  sctx.fillStyle = stroke;
  sctx.fill();
  sctx.globalAlpha = 1;
}

/* ---------------------------------------------------------------------------
 * Mission / palettes / constellation / sky / orbit / comet
 * ------------------------------------------------------------------------- */

async function loadPalettes() {
  if (!elements.palettePicker) return;
  try {
    const data = await fetchJson("/api/palettes");
    state.palettes = data.palettes || FALLBACK_PALETTES;
  } catch (error) {
    console.warn("palettes endpoint unavailable", error);
    state.palettes = FALLBACK_PALETTES;
  }
  renderPalettePicker();
}

function renderPalettePicker() {
  if (!elements.palettePicker || !state.palettes.length) return;
  elements.palettePicker.innerHTML = state.palettes.map((palette) => {
    const active = state.paletteId === palette.id ? " active" : "";
    const chips = (palette.colors || []).slice(0, 6)
      .map((color) => `<i style="background:${color}"></i>`)
      .join("");
    return `
      <button type="button" class="palette-option${active}" data-palette-id="${palette.id}" role="option" aria-selected="${state.paletteId === palette.id}">
        <span class="palette-option-name">${palette.name}</span>
        <span class="palette-option-chips">${chips}</span>
      </button>
    `;
  }).join("");

  elements.palettePicker.querySelectorAll(".palette-option").forEach((button) => {
    button.addEventListener("click", () => {
      state.paletteId = button.dataset.paletteId;
      renderPalettePicker();
      loadMission();
    });
  });
}

function layerCountFromIntensity() {
  return Math.max(2, Math.min(8, 2 + Math.floor(Number(elements.intensity?.value || 68) / 20)));
}

function planetCountFromIntensity() {
  return Math.max(3, Math.min(10, 3 + Math.floor(Number(elements.intensity?.value || 68) / 16)));
}

function cometCountFromIntensity() {
  return Math.max(1, Math.min(6, 1 + Math.floor(Number(elements.intensity?.value || 68) / 20)));
}

function rebuildDust() {
  const sky = state.sky;
  const n = sky ? Math.max(20, Math.min(220, Number(sky.dust) || 80)) : 0;
  const rng = makeRng(stableSeed(`${(sky && sky.seed) || "dust"}:dust:${n}`));
  state.dust = Array.from({ length: n }, () => ({
    x: rng(),
    y: rng(),
    z: 0.28 + rng() * 0.72,
    tw: rng() * Math.PI * 2
  }));
}

async function loadConstellation() {
  const points = 20 + Math.floor(Number(elements.intensity.value) / 5);
  try {
    state.constellation = await fetchJson(
      `/api/constellation?seed=${state.constellationSeed}&points=${points}`
    );
  } catch (error) {
    console.warn("constellation endpoint unavailable", error);
    state.constellation = generateConstellation(state.constellationSeed, points);
  }
  state.prevStarPositions = [];
  if (reducedMotion.matches) {
    drawCanvas(false);
  }
}

async function loadSkyAndOrbit() {
  const seedText = elements.seed?.value || "sebby";
  const layers = layerCountFromIntensity();
  const planets = planetCountFromIntensity();
  try {
    state.sky = await fetchJson(`/api/sky?seed=${encodeURIComponent(seedText)}&layers=${layers}`);
  } catch (error) {
    console.warn("sky endpoint unavailable", error);
    state.sky = generateSky(seedText, layers);
  }
  try {
    state.orbit = await fetchJson(`/api/orbit?seed=${state.constellationSeed}&planets=${planets}`);
  } catch (error) {
    console.warn("orbit endpoint unavailable", error);
    state.orbit = generateOrbit(state.constellationSeed, planets);
  }
  const count = cometCountFromIntensity();
  try {
    state.comets = await fetchJson(`/api/comet?seed=${state.constellationSeed}&count=${count}`);
  } catch (error) {
    console.warn("comet endpoint unavailable", error);
    state.comets = generateComet(state.constellationSeed, count);
  }
  rebuildDust();
  if (state.inspected) {
    const bodies = (state.orbit && state.orbit.planets) || [];
    if (state.inspected.index >= bodies.length) {
      hidePlanetInspect();
    } else {
      showPlanetInspect(state.inspected.index);
    }
  }
}

async function loadMission() {
  elements.generate.disabled = true;
  try {
    try {
      const response = await fetch(apiUrl());
      if (!response.ok) throw new Error("mission request failed");
      state.mission = await response.json();
      state.usingFallback = false;
    } catch (error) {
      console.warn("mission endpoint unavailable, using local generator", error);
      state.mission = generateMission();
      state.usingFallback = true;
    }
    if (state.mission.paletteId) {
      state.paletteId = state.mission.paletteId;
      renderPalettePicker();
    }
    state.prevNodePositions = [];
    await Promise.all([loadConstellation(), loadSkyAndOrbit()]);
    renderMission();
    if (reducedMotion.matches) {
      drawCanvas(false);
    }
  } catch (error) {
    elements.missionName.textContent = "Signal interrupted";
    elements.missionTagline.textContent = "The workspace could not build a mission packet.";
    console.error(error);
  } finally {
    elements.generate.disabled = false;
  }
}

function renderMission() {
  const mission = state.mission;
  if (!mission) return;

  elements.missionName.textContent = mission.missionName;
  elements.missionTagline.textContent = mission.tagline;
  if (state.usingFallback) {
    elements.updatedAt.textContent = "Local generator — static demo without the C++ server";
  } else {
    elements.updatedAt.textContent = `Updated ${new Date(mission.updatedAt).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`;
  }
  elements.modeBadge.textContent = modeTitle(mission.mode);
  elements.paletteName.textContent = mission.paletteName || mission.seed;

  elements.metrics.innerHTML = mission.metrics.map((metric) => `
    <article class="metric-card">
      <span>${metric.label}</span>
      <strong>${metric.value}${metric.unit}</strong>
    </article>
  `).join("");

  elements.priorities.innerHTML = mission.priorities.map((item) => `<li>${item}</li>`).join("");

  elements.waypoints.innerHTML = mission.waypoints.map((waypoint) => `
    <div class="waypoint">
      <span>${waypoint.label}</span>
      <div class="waypoint-meter" aria-hidden="true"><span style="width:${waypoint.score}%"></span></div>
      <span>${waypoint.minutes}m</span>
    </div>
  `).join("");

  elements.swatches.innerHTML = mission.palette.map((color) => `
    <span class="swatch" style="background:${color}" title="${color}"></span>
  `).join("");

  buildNebula(mission.palette);
}

/* ---------------------------------------------------------------------------
 * Canvas rendering: nebula, dust, aurora, orrery, constellation, warp
 * ------------------------------------------------------------------------- */

const nebula = { blobs: [], paletteKey: "" };

function hexToRgba(hex, alpha) {
  const value = (hex || "").replace("#", "");
  if (value.length !== 6) return `rgba(36, 124, 118, ${alpha})`;
  const r = parseInt(value.slice(0, 2), 16);
  const g = parseInt(value.slice(2, 4), 16);
  const b = parseInt(value.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function buildNebula(palette) {
  const key = (palette || []).join(",");
  if (!palette || nebula.paletteKey === key) return;
  nebula.paletteKey = key;
  nebula.blobs = [2, 3, 5].map((paletteIndex, index) => {
    const size = 420 + index * 170;
    const canvas = document.createElement("canvas");
    canvas.width = size;
    canvas.height = size;
    const bctx = canvas.getContext("2d");
    const gradient = bctx.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
    gradient.addColorStop(0, hexToRgba(palette[paletteIndex], 0.08));
    gradient.addColorStop(0.65, hexToRgba(palette[paletteIndex], 0.04));
    gradient.addColorStop(1, hexToRgba(palette[paletteIndex], 0));
    bctx.fillStyle = gradient;
    bctx.fillRect(0, 0, size, size);
    return { canvas, speed: 0.24 + index * 0.14, phaseX: index * 2.1, phaseY: 1.1 + index * 1.4 };
  });
}

function drawPaletteNebula(width, height) {
  nebula.blobs.forEach((blob) => {
    const x = width * (0.5 + 0.34 * Math.sin(state.time * blob.speed + blob.phaseX)) - blob.canvas.width / 2;
    const y = height * (0.5 + 0.3 * Math.cos(state.time * blob.speed * 0.8 + blob.phaseY)) - blob.canvas.height / 2;
    ctx.drawImage(blob.canvas, x, y);
  });
}

function drawSkyNebula(width, height) {
  const sky = state.sky;
  if (!sky || !Array.isArray(sky.layers)) return;
  sky.layers.forEach((layer) => {
    const x = (layer.x ?? 0.5) * width;
    const y = (layer.y ?? 0.5) * height;
    const radius = Math.max(40, (layer.radius ?? 0.4) * Math.max(width, height));
    const gradient = ctx.createRadialGradient(x, y, 0, x, y, radius);
    const hue = layer.hue ?? 220;
    const sat = layer.sat ?? 60;
    const light = layer.light ?? 40;
    const alpha = layer.alpha ?? 0.3;
    gradient.addColorStop(0, `hsla(${hue}, ${sat}%, ${light}%, ${alpha})`);
    gradient.addColorStop(0.55, `hsla(${hue}, ${sat}%, ${light}%, ${alpha * 0.35})`);
    gradient.addColorStop(1, `hsla(${hue}, ${sat}%, ${light}%, 0)`);
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, width, height);
  });
  ctx.fillStyle = `rgba(6, 8, 16, ${0.1 + (sky.haze ?? 0) * 0.32})`;
  ctx.fillRect(0, 0, width, height);
}

function drawDust(width, height) {
  if (!state.dust.length) return;
  ctx.save();
  state.dust.forEach((mote) => {
    const twinkle = reducedMotion.matches
      ? 0.45
      : 0.22 + 0.78 * (0.5 + 0.5 * Math.sin(state.time * 2.4 + mote.tw));
    ctx.globalAlpha = twinkle * (0.35 + mote.z * 0.65);
    ctx.fillStyle = "#e8eef7";
    const size = 0.7 + mote.z * 1.6;
    ctx.fillRect(mote.x * width, mote.y * height, size, size);
  });
  ctx.restore();
}

function drawAurora(width, height) {
  const aurora = state.sky && state.sky.aurora;
  if (!aurora || !aurora.enabled) return;
  const amp = (aurora.amplitude || 0.4) * height * 0.16;
  const speed = aurora.speed || 0.4;
  const t = reducedMotion.matches ? 0 : state.time * (0.55 + speed * 2.1);
  ctx.save();
  ctx.globalCompositeOperation = "lighter";
  ctx.lineCap = "round";
  for (let band = 0; band < 3; band += 1) {
    ctx.beginPath();
    const y0 = height * (0.16 + band * 0.065);
    ctx.moveTo(0, y0);
    for (let x = 0; x <= width; x += 6) {
      const y = y0
        + Math.sin(x * 0.009 + t + band) * amp * (1 - band * 0.2)
        + Math.sin(x * 0.021 - t * 1.35 + band * 1.7) * amp * 0.38;
      ctx.lineTo(x, y);
    }
    ctx.strokeStyle = `hsla(${(aurora.hue || 140) + band * 16}, 82%, 64%, ${0.2 - band * 0.045})`;
    ctx.lineWidth = 8 - band * 2;
    ctx.stroke();
  }
  ctx.restore();
}

function maybeSpawnShootingStar(width, height) {
  if (reducedMotion.matches || !cometEnabled()) return;
  const now = performance.now();
  if (state.nextShootingStarAt === 0) {
    state.nextShootingStarAt = now + 4000 + Math.random() * 5000;
    return;
  }
  if (now < state.nextShootingStarAt) return;
  state.nextShootingStarAt = now + 4000 + Math.random() * 5000;
  const fromTop = Math.random() < 0.5;
  state.shootingStars.push({
    x: fromTop ? Math.random() * width * 0.8 : -24,
    y: fromTop ? -24 : Math.random() * height * 0.45,
    vx: 6 + Math.random() * 5,
    vy: 3.2 + Math.random() * 2.6,
    trail: []
  });
}

function drawShootingStars(width, height, palette) {
  if (!cometEnabled() || !state.shootingStars.length) return;
  ctx.save();
  ctx.lineCap = "round";
  state.shootingStars = state.shootingStars.filter((star) => {
    star.trail.push({ x: star.x, y: star.y });
    if (star.trail.length > 12) {
      star.trail.shift();
    }
    star.x += star.vx;
    star.y += star.vy;

    for (let i = 1; i < star.trail.length; i += 1) {
      const a = star.trail[i - 1];
      const b = star.trail[i];
      ctx.globalAlpha = (i / star.trail.length) * 0.55;
      ctx.strokeStyle = palette[1] || "#f7f2e8";
      ctx.lineWidth = 1 + (i / star.trail.length) * 1.6;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }
    ctx.globalAlpha = 0.9;
    ctx.fillStyle = palette[1] || "#f7f2e8";
    ctx.beginPath();
    ctx.arc(star.x, star.y, 1.8, 0, Math.PI * 2);
    ctx.fill();

    return star.x < width + 40 && star.y < height + 40;
  });
  ctx.restore();
}

function warpPosition(x, y, width, height) {
  if (state.warpAmount < 0.01) return { x, y };
  const cx = width / 2;
  const cy = height / 2;
  const tempoScale = 0.6 + Number(elements.tempo.value) / 55;
  const pulse = 1 + state.warpAmount *
    (0.16 + 0.24 * (0.5 + 0.5 * Math.sin(state.time * 2.6 * tempoScale)));
  return { x: cx + (x - cx) * pulse, y: cy + (y - cy) * pulse };
}

function drawWarpStreaks(current, previous, color) {
  if (state.warpAmount < 0.05) return;
  ctx.save();
  ctx.strokeStyle = color;
  ctx.lineCap = "round";
  current.forEach((point, index) => {
    const prev = previous[index];
    if (!prev) return;
    const dx = point.x - prev.x;
    const dy = point.y - prev.y;
    if (dx * dx + dy * dy < 0.4) return;
    ctx.globalAlpha = 0.35 * state.warpAmount;
    ctx.lineWidth = 1.2;
    ctx.beginPath();
    ctx.moveTo(prev.x, prev.y);
    ctx.lineTo(point.x, point.y);
    ctx.stroke();
  });
  ctx.restore();
}

function drawGrid(width, height, palette) {
  if (!elements.grid.checked) return;
  ctx.save();
  ctx.strokeStyle = "rgba(251, 250, 246, 0.07)";
  ctx.lineWidth = 1;
  const gap = 44;
  for (let x = 0; x <= width; x += gap) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }
  for (let y = 0; y <= height; y += gap) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
  ctx.strokeStyle = palette[4] || "rgba(199, 154, 52, 0.4)";
  ctx.globalAlpha = 0.16;
  ctx.strokeRect(24, 24, width - 48, height - 48);
  ctx.restore();
}

function drawConstellationLayer(width, height, palette) {
  const pack = state.constellation;
  if (!pack || !pack.stars) return;

  const drifting = !reducedMotion.matches;
  const stars = pack.stars.map((star, index) => {
    const drift = drifting ? Math.sin(state.time * 0.9 + index) * 4 : 0;
    const wobble = drifting ? Math.cos(state.time * 0.7 + index) * 3 : 0;
    const warped = warpPosition(star.x * width + drift, star.y * height + wobble, width, height);
    return {
      x: warped.x,
      y: warped.y,
      size: star.size,
      brightness: star.brightness
    };
  });

  drawWarpStreaks(stars, state.prevStarPositions, palette[1] || "#f7f2e8");
  state.prevStarPositions = stars.map((star) => ({ x: star.x, y: star.y }));

  ctx.save();
  if (pack.links) {
    ctx.lineWidth = 1;
    pack.links.forEach((link, index) => {
      const a = stars[link[0]];
      const b = stars[link[1]];
      if (!a || !b) return;
      ctx.strokeStyle = palette[3] || "#d85d4c";
      ctx.globalAlpha = 0.05 + (index % 3) * 0.02;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    });
  }

  stars.forEach((star) => {
    ctx.globalAlpha = 0.18 + star.brightness * 0.4;
    ctx.fillStyle = palette[1] || "#f7f2e8";
    ctx.beginPath();
    ctx.arc(star.x, star.y, star.size * 0.45, 0, Math.PI * 2);
    ctx.fill();
  });
  ctx.restore();
}

function wrap01(value) {
  return ((value % 1) + 1) % 1;
}

function drawComets(width, height) {
  const pack = state.comets;
  if (!cometEnabled() || !pack || !Array.isArray(pack.comets)) return;

  const scale = Math.min(width, height);
  const travel = reducedMotion.matches ? 0 : state.time * state.timeScale * 0.35;

  ctx.save();
  ctx.lineCap = "round";
  ctx.globalCompositeOperation = "lighter";
  pack.comets.forEach((comet) => {
    const dx = comet.dx ?? 0.12;
    const dy = comet.dy ?? 0.04;
    const x = wrap01((comet.x ?? 0.5) + dx * travel);
    const y = wrap01((comet.y ?? 0.5) + dy * travel);
    const len = Math.max(0.05, Math.min(0.25, comet.len ?? 0.12));
    const mag = Math.hypot(dx, dy) || 1;
    const px = x * width;
    const py = y * height;
    const tx = px - (dx / mag) * len * scale;
    const ty = py - (dy / mag) * len * scale;
    const hue = comet.hue ?? 40;
    const gradient = ctx.createLinearGradient(tx, ty, px, py);
    gradient.addColorStop(0, `hsla(${hue}, 90%, 70%, 0)`);
    gradient.addColorStop(0.55, `hsla(${hue}, 95%, 72%, 0.45)`);
    gradient.addColorStop(1, `hsla(${hue}, 100%, 88%, 0.95)`);
    ctx.strokeStyle = gradient;
    ctx.lineWidth = 2.4;
    ctx.beginPath();
    ctx.moveTo(tx, ty);
    ctx.lineTo(px, py);
    ctx.stroke();
    ctx.fillStyle = `hsla(${hue}, 100%, 92%, 0.95)`;
    ctx.beginPath();
    ctx.arc(px, py, 2.2, 0, Math.PI * 2);
    ctx.fill();
  });
  ctx.restore();
}

function drawOrbitSystem(width, height) {
  const pack = state.orbit;
  state.planetHits = [];
  if (!orbitEnabled() || !pack) return;

  const cx = width / 2;
  const cy = height / 2;
  const scale = Math.min(width, height);
  const t = reducedMotion.matches ? 0 : state.time * state.timeScale;
  const star = pack.star || { color: "#ffcc66", radius: 0.08, flare: 0.4 };
  const planets = pack.planets || [];

  ctx.save();
  planets.forEach((planet) => {
    const rx = (planet.orbit || 0.4) * scale * 0.48;
    const ry = rx * 0.62;
    ctx.beginPath();
    ctx.ellipse(cx, cy, rx, ry, 0, 0, Math.PI * 2);
    ctx.strokeStyle = "rgba(251, 250, 246, 0.11)";
    ctx.lineWidth = 1;
    ctx.stroke();
  });

  const sr = Math.max(8, (star.radius || 0.08) * scale * 0.5);
  const glowR = sr * (2.8 + (star.flare || 0.4) * 4.2);
  const glow = ctx.createRadialGradient(cx, cy, 0, cx, cy, glowR);
  glow.addColorStop(0, hexToRgba(star.color, 0.95));
  glow.addColorStop(0.22, hexToRgba(star.color, 0.45));
  glow.addColorStop(1, hexToRgba(star.color, 0));
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.arc(cx, cy, glowR, 0, Math.PI * 2);
  ctx.fill();

  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(t * 0.04);
  ctx.strokeStyle = hexToRgba(star.color, 0.28 + (star.flare || 0) * 0.35);
  ctx.lineWidth = 1.6;
  ctx.lineCap = "round";
  const spikes = 8;
  for (let i = 0; i < spikes; i += 1) {
    const a = (i / spikes) * Math.PI * 2;
    const len = sr * (2.1 + (star.flare || 0.4) * 2.4);
    ctx.beginPath();
    ctx.moveTo(Math.cos(a) * sr * 0.4, Math.sin(a) * sr * 0.4);
    ctx.lineTo(Math.cos(a) * len, Math.sin(a) * len);
    ctx.stroke();
  }
  ctx.restore();

  const core = ctx.createRadialGradient(cx - sr * 0.25, cy - sr * 0.25, sr * 0.1, cx, cy, sr);
  core.addColorStop(0, "#fff8e6");
  core.addColorStop(0.45, star.color || "#ffcc66");
  core.addColorStop(1, hexToRgba(star.color, 0.85));
  ctx.fillStyle = core;
  ctx.beginPath();
  ctx.arc(cx, cy, sr, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();

  const showLabels = width > 640;
  planets.forEach((planet, index) => {
    const rx = (planet.orbit || 0.4) * scale * 0.48;
    const ry = rx * 0.62;
    const period = Math.max(0.001, planet.period || 12);
    const angle = (planet.phase || 0) + t * ((Math.PI * 2) / period);
    const warped = warpPosition(cx + Math.cos(angle) * rx, cy + Math.sin(angle) * ry, width, height);
    const px = warped.x;
    const py = warped.y;
    const pr = Math.max(3, (planet.radius || 0.02) * scale * 0.5);
    state.planetHits.push({ index, x: px, y: py, r: pr });

    if (planet.ring) {
      ctx.save();
      ctx.translate(px, py);
      ctx.rotate(0.45 + angle * 0.04);
      ctx.strokeStyle = hexToRgba(planet.color, 0.62);
      ctx.lineWidth = Math.max(1.2, pr * 0.28);
      ctx.beginPath();
      ctx.ellipse(0, 0, pr * 2.25, pr * 0.72, 0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.restore();
    }

    const shade = ctx.createRadialGradient(px - pr * 0.35, py - pr * 0.4, pr * 0.12, px, py, pr);
    shade.addColorStop(0, "#fff7e8");
    shade.addColorStop(0.28, planet.color || "#c79a34");
    shade.addColorStop(1, hexToRgba(planet.color, 0.7));
    ctx.fillStyle = shade;
    ctx.beginPath();
    ctx.arc(px, py, pr, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = "rgba(251, 250, 246, 0.22)";
    ctx.lineWidth = 1;
    ctx.stroke();

    if (state.inspected && state.inspected.index === index) {
      ctx.strokeStyle = "rgba(251, 250, 246, 0.88)";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(px, py, pr + 5, 0, Math.PI * 2);
      ctx.stroke();
    }

    const moons = planet.moons | 0;
    for (let m = 0; m < moons; m += 1) {
      const ma = angle * (2.2 + m * 0.35) + m * 1.7 + t * (0.9 + m * 0.2);
      const md = pr * (2.3 + m * 0.85);
      ctx.fillStyle = "rgba(251, 250, 246, 0.88)";
      ctx.beginPath();
      ctx.arc(px + Math.cos(ma) * md, py + Math.sin(ma) * md * 0.68, Math.max(1.1, pr * 0.18), 0, Math.PI * 2);
      ctx.fill();
    }

    if (showLabels && planet.name) {
      ctx.font = "700 11px Inter, ui-sans-serif, system-ui, sans-serif";
      ctx.fillStyle = "rgba(251, 250, 246, 0.72)";
      ctx.fillText(planet.name, px + pr + 6, py - 2);
    }
  });
}

function drawVignette(width, height) {
  const gradient = ctx.createRadialGradient(
    width / 2, height / 2, Math.min(width, height) * 0.18,
    width / 2, height / 2, Math.max(width, height) * 0.72
  );
  gradient.addColorStop(0, "rgba(0, 0, 0, 0)");
  gradient.addColorStop(1, "rgba(4, 5, 10, 0.5)");
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, width, height);
}

function drawCanvas(advance = true) {
  const width = elements.canvas.clientWidth;
  const height = elements.canvas.clientHeight;
  const mission = state.mission;
  const palette = (mission && mission.palette) || FALLBACK_PALETTES[0].colors;
  if (advance) {
    state.time += 0.008 + Number(elements.tempo.value) / 28000;
    state.warpAmount += ((state.warp ? 1 : 0) - state.warpAmount) * 0.06;
  }

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0c0d12";
  ctx.fillRect(0, 0, width, height);

  if (nebulaEnabled()) {
    if (state.sky && state.sky.layers) {
      drawSkyNebula(width, height);
    } else {
      drawPaletteNebula(width, height);
    }
    drawDust(width, height);
    drawAurora(width, height);
  }

  drawGrid(width, height, palette);
  drawConstellationLayer(width, height, palette);

  if (mission && mission.nodes) {
    const drifting = !reducedMotion.matches;
    const dim = orbitEnabled() ? 0.42 : 1;
    const nodes = mission.nodes.map((node, index) => {
      const drift = drifting ? Math.sin(state.time * (1.5 + index * 0.03) + index) * 10 : 0;
      const wobble = drifting ? Math.cos(state.time + index) * 8 : 0;
      const pullX = state.pointer.active ? (state.pointer.x - 0.5) * 24 : 0;
      const pullY = state.pointer.active ? (state.pointer.y - 0.5) * 24 : 0;
      const warped = warpPosition(node.x * width + drift + pullX,
                                  node.y * height + wobble + pullY, width, height);
      return {
        x: warped.x,
        y: warped.y,
        size: node.size,
        energy: node.energy
      };
    });

    drawWarpStreaks(nodes, state.prevNodePositions, palette[2] || "#247c76");
    state.prevNodePositions = nodes.map((node) => ({ x: node.x, y: node.y }));

    ctx.save();
    ctx.lineWidth = 1.4;
    (mission.links || []).forEach((link, index) => {
      const a = nodes[link[0]];
      const b = nodes[link[1]];
      if (!a || !b) return;
      ctx.strokeStyle = index % 3 === 0 ? palette[2] : index % 3 === 1 ? palette[3] : palette[4];
      ctx.globalAlpha = (elements.trails.checked ? 0.2 : 0.1) * dim;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      const midX = (a.x + b.x) / 2 + Math.sin(state.time + index) * 26;
      const midY = (a.y + b.y) / 2 + Math.cos(state.time + index) * 18;
      ctx.quadraticCurveTo(midX, midY, b.x, b.y);
      ctx.stroke();
    });
    ctx.restore();

    nodes.forEach((node, index) => {
      const color = palette[2 + (index % 4)] || "#247c76";
      ctx.save();
      ctx.globalAlpha = 0.55 * dim;
      ctx.fillStyle = color;
      ctx.strokeStyle = "rgba(251, 250, 246, 0.78)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.rect(node.x - node.size, node.y - node.size, node.size * 2, node.size * 2);
      ctx.fill();
      if (node.energy > 70) ctx.stroke();
      ctx.restore();
    });
  }

  drawOrbitSystem(width, height);
  drawVignette(width, height);

  drawComets(width, height);
  maybeSpawnShootingStar(width, height);
  drawShootingStars(width, height, palette);

  if (state.pointer.active) {
    ctx.save();
    ctx.strokeStyle = palette[5] || "#6e62a6";
    ctx.globalAlpha = 0.45;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(state.pointer.x * width, state.pointer.y * height, 56, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }
}

let animating = false;

function animationLoop() {
  if (!animating) return;
  drawCanvas(true);
  requestAnimationFrame(animationLoop);
}

function startAnimation() {
  if (reducedMotion.matches) {
    drawCanvas(false);
    return;
  }
  if (animating) return;
  animating = true;
  requestAnimationFrame(animationLoop);
}

reducedMotion.addEventListener("change", () => {
  if (reducedMotion.matches) {
    animating = false;
    setWarp(false);
    state.shootingStars = [];
    drawCanvas(false);
  } else {
    startAnimation();
  }
});

/* ---------------------------------------------------------------------------
 * Warp mode, keyboard shortcuts, overlay
 * ------------------------------------------------------------------------- */

function hidePlanetInspect() {
  state.inspected = null;
  if (elements.planetInspect) {
    elements.planetInspect.hidden = true;
  }
}

function showPlanetInspect(index) {
  const planet = state.orbit && state.orbit.planets && state.orbit.planets[index];
  if (!planet || !elements.planetInspect) {
    hidePlanetInspect();
    return;
  }
  state.inspected = { index };
  elements.inspectName.textContent = planet.name || `Body ${index + 1}`;
  elements.inspectOrbit.textContent = Number(planet.orbit ?? 0).toFixed(2);
  elements.inspectPeriod.textContent = `${Number(planet.period ?? 0).toFixed(1)}s`;
  elements.inspectMoons.textContent = String(planet.moons | 0);
  elements.inspectRing.textContent = planet.ring ? "yes" : "no";
  elements.planetInspect.hidden = false;
}

function planetHitAt(x, y) {
  let best = null;
  let bestDist = Infinity;
  (state.planetHits || []).forEach((hit) => {
    const dx = x - hit.x;
    const dy = y - hit.y;
    const reach = hit.r + 10;
    const dist = dx * dx + dy * dy;
    if (dist <= reach * reach && dist < bestDist) {
      best = hit;
      bestDist = dist;
    }
  });
  return best;
}

function setWarp(on) {
  if (reducedMotion.matches) {
    on = false;
  }
  state.warp = on;
  if (elements.warpButton) {
    elements.warpButton.classList.toggle("active", on);
    elements.warpButton.setAttribute("aria-pressed", String(on));
  }
}

function toggleShortcutOverlay(force) {
  const overlay = elements.shortcutOverlay;
  if (!overlay) return;
  const show = force !== undefined ? force : overlay.hidden;
  overlay.hidden = !show;
}

function setMode(mode) {
  const button = document.querySelector(`.mode-button[data-mode="${mode}"]`);
  if (button && !button.classList.contains("active")) {
    button.click();
  }
}

function isTypingTarget(target) {
  return target instanceof HTMLInputElement ||
    target instanceof HTMLTextAreaElement ||
    target instanceof HTMLSelectElement ||
    (target instanceof HTMLElement && target.isContentEditable);
}

function bindShortcuts() {
  window.addEventListener("keydown", (event) => {
    if (isTypingTarget(event.target)) return;
    if (event.metaKey || event.ctrlKey || event.altKey) return;
    switch (event.key) {
      case "g":
        loadMission();
        break;
      case "r":
        elements.reseedConstellation?.click();
        break;
      case "w":
        setWarp(!state.warp);
        break;
      case "o":
        if (elements.orbitToggle) {
          elements.orbitToggle.checked = !elements.orbitToggle.checked;
          if (!elements.orbitToggle.checked) hidePlanetInspect();
          if (reducedMotion.matches) drawCanvas(false);
        }
        break;
      case "c":
        if (elements.cometToggle) {
          elements.cometToggle.checked = !elements.cometToggle.checked;
          if (!elements.cometToggle.checked) state.shootingStars = [];
          if (reducedMotion.matches) drawCanvas(false);
        }
        break;
      case "1":
        setMode("pulse");
        break;
      case "2":
        setMode("route");
        break;
      case "3":
        setMode("forge");
        break;
      case "?":
        toggleShortcutOverlay();
        break;
      case "Escape":
        if (elements.planetInspect && !elements.planetInspect.hidden) {
          hidePlanetInspect();
          if (reducedMotion.matches) drawCanvas(false);
        } else {
          toggleShortcutOverlay(false);
        }
        break;
      default:
        return;
    }
    event.preventDefault();
  });
}

function bindEvents() {
  document.querySelectorAll(".mode-button").forEach((button) => {
    button.addEventListener("click", () => {
      document.querySelectorAll(".mode-button").forEach((item) => item.classList.remove("active"));
      button.classList.add("active");
      state.mode = button.dataset.mode;
      loadMission();
    });
  });

  [elements.intensity, elements.tempo].forEach((input) => {
    input.addEventListener("input", () => {
      elements.intensityValue.textContent = elements.intensity.value;
      elements.tempoValue.textContent = elements.tempo.value;
    });
    input.addEventListener("change", loadMission);
  });

  [elements.grid, elements.trails, elements.orbitToggle, elements.nebulaToggle, elements.cometToggle].forEach((input) => {
    input?.addEventListener("change", () => {
      if (input === elements.orbitToggle && !orbitEnabled()) hidePlanetInspect();
      if (input === elements.cometToggle && !cometEnabled()) state.shootingStars = [];
      if (reducedMotion.matches) drawCanvas(false);
    });
  });

  document.querySelectorAll("[data-timescale]").forEach((button) => {
    button.addEventListener("click", () => {
      state.timeScale = Number(button.dataset.timescale) || 1;
      document.querySelectorAll("[data-timescale]").forEach((item) => {
        const active = item === button;
        item.classList.toggle("active", active);
        item.setAttribute("aria-pressed", String(active));
      });
      if (reducedMotion.matches) drawCanvas(false);
    });
  });

  elements.seed.addEventListener("change", loadMission);
  elements.generate.addEventListener("click", loadMission);
  elements.randomizeSeed.addEventListener("click", () => {
    const first = seedWords[Math.floor(Math.random() * seedWords.length)];
    const second = seedWords[Math.floor(Math.random() * seedWords.length)];
    elements.seed.value = `${first}-${second}`;
    loadMission();
  });

  if (elements.reseedConstellation) {
    elements.reseedConstellation.addEventListener("click", async () => {
      state.constellationSeed = Math.floor(Math.random() * 1000000);
      elements.reseedConstellation.disabled = true;
      await Promise.all([loadConstellation(), loadSkyAndOrbit()]);
      elements.reseedConstellation.disabled = false;
      if (reducedMotion.matches) drawCanvas(false);
    });
  }

  if (elements.warpButton) {
    elements.warpButton.addEventListener("click", () => setWarp(!state.warp));
  }

  if (elements.shortcutOverlay) {
    elements.shortcutOverlay.addEventListener("click", (event) => {
      if (event.target === elements.shortcutOverlay) {
        toggleShortcutOverlay(false);
      }
    });
  }

  elements.copy.addEventListener("click", async () => {
    if (!state.mission) return;
    const payload = {
      mission: state.mission,
      sky: state.sky,
      orbit: state.orbit,
      comets: state.comets,
      constellation: state.constellation
    };
    await navigator.clipboard.writeText(JSON.stringify(payload, null, 2));
    elements.copy.textContent = "Copied";
    window.setTimeout(() => {
      elements.copy.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 7a3 3 0 0 1 3-3h6a3 3 0 0 1 3 3v6a3 3 0 0 1-3 3h-1v1a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3v-6a3 3 0 0 1 3-3h1V7zm3-1a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1h-6zM7 10a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1v-1h-3a3 3 0 0 1-3-3v-3H7z"></path></svg>Copy JSON';
    }, 1200);
  });

  elements.canvas.addEventListener("pointermove", (event) => {
    const rect = elements.canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    state.pointer = {
      x: x / rect.width,
      y: y / rect.height,
      active: true
    };
    elements.canvas.style.cursor = planetHitAt(x, y) ? "pointer" : "crosshair";
  });
  elements.canvas.addEventListener("pointerleave", () => {
    state.pointer.active = false;
    elements.canvas.style.cursor = "crosshair";
  });
  elements.canvas.addEventListener("click", (event) => {
    const rect = elements.canvas.getBoundingClientRect();
    const hit = planetHitAt(event.clientX - rect.left, event.clientY - rect.top);
    if (hit) {
      showPlanetInspect(hit.index);
    } else {
      hidePlanetInspect();
    }
    if (reducedMotion.matches) drawCanvas(false);
  });

  if (elements.inspectClose) {
    elements.inspectClose.addEventListener("click", () => {
      hidePlanetInspect();
      if (reducedMotion.matches) drawCanvas(false);
    });
  }

  window.addEventListener("resize", resizeCanvas);
}

readUrlState();
resizeCanvas();
bindEvents();
bindShortcuts();
checkHealth();
setInterval(checkHealth, 5000);
if (typeof loadMetrics === "function") { loadMetrics(); setInterval(loadMetrics, 2000); }
loadPalettes();
loadMission();
loadMetrics();
connectTelemetry();
startAnimation();
