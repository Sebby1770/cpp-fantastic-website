const state = {
  mode: "orbit",
  mission: null,
  pointer: { x: 0.5, y: 0.5, active: false },
  time: 0
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
  generate: document.querySelector("#generateButton"),
  randomizeSeed: document.querySelector("#randomizeSeed"),
  copy: document.querySelector("#copyButton"),
  copyLink: document.querySelector("#copyLinkButton"),
  downloadPng: document.querySelector("#downloadPngButton"),
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
  snapshotList: document.querySelector("#snapshotList")
};

const ctx = elements.canvas.getContext("2d");
const copyMarkup = elements.copy.innerHTML;
const copyLinkMarkup = elements.copyLink.innerHTML;
const downloadPngMarkup = elements.downloadPng.innerHTML;
const SNAPSHOT_KEY = "asterforge-snapshots-v1";
const MAX_SNAPSHOTS = 6;
let snapshots = [];
let presets = [];
const seedWords = [
  "velvet", "copper", "lumen", "tide", "citadel", "prism",
  "ember", "atlas", "signal", "orbit", "glow", "foundry"
];

function resizeCanvas() {
  const rect = elements.canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  elements.canvas.width = Math.max(1, Math.floor(rect.width * scale));
  elements.canvas.height = Math.max(1, Math.floor(rect.height * scale));
  ctx.setTransform(scale, 0, 0, scale, 0, 0);
}

function modeTitle(value) {
  return value.charAt(0).toUpperCase() + value.slice(1);
}

function supportedModes() {
  return ["orbit", "bloom", "forge", "night"];
}

function setMode(mode) {
  state.mode = supportedModes().includes(mode) ? mode : "orbit";
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

function currentConfig() {
  return {
    seed: elements.seed.value || "sebby",
    mode: state.mode,
    intensity: elements.intensity.value,
    tempo: elements.tempo.value,
    density: elements.density.value
  };
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
  const nextUrl = `${window.location.pathname}?${params}`;
  window.history.replaceState(null, "", nextUrl);
}

function applyInitialUrlState() {
  const params = new URLSearchParams(window.location.search);
  const config = {};
  ["seed", "mode", "intensity", "tempo", "density"].forEach((key) => {
    if (params.has(key)) config[key] = params.get(key);
  });
  applyConfig(config);
}

function apiUrl() {
  const params = new URLSearchParams(currentConfig());
  return `/api/mission?${params}`;
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
  fill.style.width = `${Math.max(0, Math.min(100, value))}%`;
  meter.append(fill);
  return meter;
}

async function checkHealth() {
  try {
    const response = await fetch("/api/health");
    if (!response.ok) throw new Error("health check failed");
    elements.serverStatus.textContent = "online";
    elements.serverStatus.classList.add("online");
  } catch {
    elements.serverStatus.textContent = "offline";
    elements.serverStatus.classList.remove("online");
  }
}

async function loadMission() {
  elements.generate.disabled = true;
  try {
    const response = await fetch(apiUrl());
    if (!response.ok) throw new Error("mission request failed");
    state.mission = await response.json();
    renderMission();
    syncUrl();
  } catch (error) {
    elements.missionName.textContent = "Signal interrupted";
    elements.missionTagline.textContent = "The C++ server did not return a mission packet.";
    console.error(error);
  } finally {
    elements.generate.disabled = false;
  }
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
  elements.modeBadge.textContent = modeTitle(mission.mode);
  elements.paletteName.textContent = mission.paletteName || mission.seed;
  elements.nodeCount.textContent = `${mission.nodes.length} nodes`;
  elements.routeCount.textContent = `${mission.links.length} routes`;
  elements.signature.textContent = mission.shortId ? `${mission.shortId} - ${mission.signature}` : mission.signature;

  elements.metrics.replaceChildren(...mission.metrics.map((metric) => {
    const card = document.createElement("article");
    card.className = "metric-card";
    card.append(
      makeText("span", "", metric.label),
      makeText("strong", "", `${metric.value}${metric.unit}`),
      buildMeter(metric.value),
      makeText("em", "", "live")
    );
    return card;
  }));

  elements.priorities.replaceChildren(...mission.priorities.map((item) => {
    const row = document.createElement("li");
    row.textContent = item;
    return row;
  }));

  elements.waypoints.replaceChildren(...mission.waypoints.map((waypoint) => {
    const row = document.createElement("div");
    row.className = "waypoint";
    row.append(
      makeText("span", "", waypoint.label),
      buildMeter(waypoint.score, "waypoint-meter"),
      makeText("span", "", `${waypoint.minutes}m`)
    );
    return row;
  }));

  elements.notes.replaceChildren(...mission.notes.map((note) => makeText("p", "note", note)));

  elements.swatches.replaceChildren(...mission.palette.map((color) => {
    const swatch = document.createElement("span");
    swatch.className = "swatch";
    swatch.style.background = color;
    swatch.title = color;
    return swatch;
  }));
}

async function loadPresets() {
  try {
    const response = await fetch("/api/presets");
    if (!response.ok) throw new Error("preset request failed");
    const payload = await response.json();
    presets = Array.isArray(payload.presets) ? payload.presets : [];
    elements.presetsStatus.textContent = `${presets.length} modes`;
  } catch (error) {
    console.error(error);
    presets = [];
    elements.presetsStatus.textContent = "offline";
  }
  renderPresets();
}

function renderPresets() {
  if (presets.length === 0) {
    elements.presets.replaceChildren(makeText("p", "empty-state", "No presets available."));
    return;
  }

  elements.presets.replaceChildren(...presets.map((preset) => {
    const button = document.createElement("button");
    button.className = "preset-button";
    button.type = "button";
    button.dataset.mode = preset.id;
    button.append(
      makeText("strong", "", preset.label),
      makeText("span", "", preset.description)
    );
    button.addEventListener("click", () => {
      applyConfig({ ...preset, mode: preset.id });
      loadMission();
    });
    return button;
  }));
  setMode(state.mode);
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

  elements.snapshotList.replaceChildren(...snapshots.map((snapshot) => {
    const row = document.createElement("div");
    row.className = "snapshot-row";

    const restore = document.createElement("button");
    restore.className = "snapshot-main";
    restore.type = "button";
    restore.append(
      makeText("strong", "", snapshot.name),
      makeText("span", "", `${modeTitle(snapshot.mode)} / ${snapshot.seed}`)
    );
    restore.addEventListener("click", () => {
      applyConfig(snapshot);
      loadMission();
    });

    const remove = document.createElement("button");
    remove.className = "icon-button remove-snapshot";
    remove.type = "button";
    remove.setAttribute("aria-label", `Remove ${snapshot.name}`);
    remove.title = "Remove snapshot";
    remove.textContent = "x";
    remove.addEventListener("click", () => {
      snapshots = snapshots.filter((item) => item.id !== snapshot.id);
      saveSnapshots();
      renderSnapshots();
    });

    row.append(restore, remove);
    return row;
  }));
}

function saveCurrentSnapshot() {
  if (!state.mission) return;
  const snapshot = {
    ...currentConfig(),
    id: state.mission.shortId || `${Date.now()}`,
    name: state.mission.missionName
  };
  snapshots = [snapshot, ...snapshots.filter((item) => item.id !== snapshot.id)].slice(0, MAX_SNAPSHOTS);
  saveSnapshots();
  renderSnapshots();
}

function drawBackground(width, height, palette) {
  const gradient = ctx.createLinearGradient(0, 0, width, height);
  gradient.addColorStop(0, palette[0] || "#0b0d0c");
  gradient.addColorStop(0.48, "#111411");
  gradient.addColorStop(1, "#090d12");
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, width, height);

  if (!elements.grid.checked) return;

  ctx.save();
  ctx.strokeStyle = "rgba(247, 240, 223, 0.055)";
  ctx.lineWidth = 1;
  const gap = 42;
  const drift = (state.time * 24) % gap;
  for (let x = -gap + drift; x <= width + gap; x += gap) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x + height * 0.32, height);
    ctx.stroke();
  }
  for (let y = -gap; y <= height + gap; y += gap) {
    ctx.beginPath();
    ctx.moveTo(0, y + drift);
    ctx.lineTo(width, y - width * 0.18 + drift);
    ctx.stroke();
  }
  ctx.strokeStyle = palette[4] || "#f2b544";
  ctx.globalAlpha = 0.16;
  ctx.strokeRect(24, 24, width - 48, height - 48);
  ctx.restore();
}

function drawRings(width, height, mission) {
  const palette = mission.palette;
  const base = Math.min(width, height);
  ctx.save();
  mission.rings.forEach((ring, index) => {
    const x = ring.x * width;
    const y = ring.y * height;
    const radius = ring.radius * base;
    const wobble = Math.sin(state.time * ring.speed + index) * 0.12;
    ctx.strokeStyle = palette[ring.color] || palette[2];
    ctx.globalAlpha = 0.12 + index * 0.03;
    ctx.lineWidth = 1.2 + index * 0.35;
    ctx.setLineDash([10 + index * 2, 14 - index]);
    ctx.beginPath();
    ctx.ellipse(x, y, radius * (1.15 + wobble), radius * (0.52 + wobble / 2), state.time * ring.speed, 0, Math.PI * 2);
    ctx.stroke();
  });
  ctx.setLineDash([]);
  ctx.restore();
}

function projectNodes(width, height, mission) {
  const tempo = Number(elements.tempo.value);
  const intensity = Number(elements.intensity.value);
  const modeDrift = state.mode === "bloom" ? 1.35 : state.mode === "night" ? 0.72 : 1;
  const pointerX = state.pointer.active ? (state.pointer.x - 0.5) * 38 : 0;
  const pointerY = state.pointer.active ? (state.pointer.y - 0.5) * 38 : 0;

  return mission.nodes.map((node, index) => {
    const energy = node.energy / 100;
    const spin = state.time * (0.5 + tempo / 120) * modeDrift + node.phase;
    const orbit = Math.sin(spin + index * 0.07) * (8 + intensity / 10);
    const breathe = Math.cos(spin * 0.8) * (5 + energy * 8);
    return {
      x: node.x * width + orbit + pointerX * energy,
      y: node.y * height + breathe + pointerY * energy,
      size: node.size + energy * 2,
      energy: node.energy,
      kind: node.kind
    };
  });
}

function drawLinks(nodes, mission) {
  const palette = mission.palette;
  ctx.save();
  mission.links.forEach((link, index) => {
    const a = nodes[link[0]];
    const b = nodes[link[1]];
    const strength = link[2] || 50;
    if (!a || !b) return;

    const pulse = Math.sin(state.time * 2 + index) * 0.08;
    ctx.strokeStyle = palette[2 + (index % 5)] || palette[2];
    ctx.globalAlpha = elements.trails.checked ? 0.16 + strength / 420 + pulse : 0.09;
    ctx.lineWidth = 0.8 + strength / 85;
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    const midX = (a.x + b.x) / 2 + Math.sin(state.time + index) * 34;
    const midY = (a.y + b.y) / 2 + Math.cos(state.time * 0.8 + index) * 26;
    ctx.quadraticCurveTo(midX, midY, b.x, b.y);
    ctx.stroke();
  });
  ctx.restore();
}

function drawNodeShape(node, color, index) {
  const sides = node.kind === 2 ? 3 : node.kind === 3 ? 5 : 4;
  const angleOffset = state.time * 0.7 + index;

  ctx.beginPath();
  if (node.kind === 0) {
    ctx.arc(node.x, node.y, node.size, 0, Math.PI * 2);
  } else {
    for (let i = 0; i < sides; i += 1) {
      const angle = angleOffset + i * (Math.PI * 2 / sides);
      const radius = node.size * (node.kind === 1 ? 1.24 : 1);
      const x = node.x + Math.cos(angle) * radius;
      const y = node.y + Math.sin(angle) * radius;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.closePath();
  }
  ctx.fillStyle = color;
  ctx.fill();
}

function drawNodes(nodes, mission) {
  const palette = mission.palette;
  nodes.forEach((node, index) => {
    const color = palette[2 + (index % 5)] || "#10b8a6";
    ctx.save();
    if (node.energy > 72) {
      ctx.globalAlpha = 0.18;
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(node.x, node.y, node.size * 4.2, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 0.92;
    drawNodeShape(node, color, index);
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(247, 240, 223, 0.72)";
    if (node.energy > 66) ctx.stroke();
    ctx.restore();
  });
}

function drawPointer(width, height, palette) {
  if (!state.pointer.active) return;

  const x = state.pointer.x * width;
  const y = state.pointer.y * height;
  ctx.save();
  ctx.strokeStyle = palette[5] || "#8c6cf5";
  ctx.globalAlpha = 0.5;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.arc(x, y, 46 + Math.sin(state.time * 4) * 7, 0, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = palette[2] || "#10b8a6";
  ctx.globalAlpha = 0.34;
  ctx.beginPath();
  ctx.moveTo(x - 70, y);
  ctx.lineTo(x + 70, y);
  ctx.moveTo(x, y - 70);
  ctx.lineTo(x, y + 70);
  ctx.stroke();
  ctx.restore();
}

function drawCanvas() {
  const width = elements.canvas.clientWidth || 1;
  const height = elements.canvas.clientHeight || 1;
  const mission = state.mission;

  if (elements.motion.checked) {
    state.time += 0.009 + Number(elements.tempo.value) / 24000;
  }

  if (!mission) {
    ctx.fillStyle = "#0b0d0c";
    ctx.fillRect(0, 0, width, height);
    requestAnimationFrame(drawCanvas);
    return;
  }

  drawBackground(width, height, mission.palette);
  drawRings(width, height, mission);
  const nodes = projectNodes(width, height, mission);
  drawLinks(nodes, mission);
  drawNodes(nodes, mission);
  drawPointer(width, height, mission.palette);

  requestAnimationFrame(drawCanvas);
}

function bindEvents() {
  document.querySelectorAll(".mode-button").forEach((button) => {
    button.addEventListener("click", () => {
      setMode(button.dataset.mode);
      loadMission();
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

  elements.saveSnapshot.addEventListener("click", saveCurrentSnapshot);

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
    if (!state.mission) return;
    const filename = `asterforge-${state.mission.shortId || "mission"}.png`;
    setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Preparing");

    if (typeof elements.canvas.toBlob === "function") {
      elements.canvas.toBlob((blob) => {
        if (!blob) {
          setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Save failed");
          return;
        }
        downloadBlob(filename, blob);
        setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Saved");
      }, "image/png");
      return;
    }

    if (typeof elements.canvas.toDataURL === "function") {
      try {
        downloadDataUrl(filename, elements.canvas.toDataURL("image/png"));
        setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Saved");
      } catch {
        setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Save failed");
      }
      return;
    }

    setButtonFeedback(elements.downloadPng, downloadPngMarkup, "Unavailable");
  });

  elements.canvas.addEventListener("pointermove", (event) => {
    const rect = elements.canvas.getBoundingClientRect();
    state.pointer = {
      x: (event.clientX - rect.left) / rect.width,
      y: (event.clientY - rect.top) / rect.height,
      active: true
    };
  });

  elements.canvas.addEventListener("pointerleave", () => {
    state.pointer.active = false;
  });

  window.addEventListener("resize", resizeCanvas);
}

applyInitialUrlState();
updateRangeLabels();
loadSnapshots();
renderSnapshots();
resizeCanvas();
bindEvents();
checkHealth();
loadPresets();
loadMission();
drawCanvas();
