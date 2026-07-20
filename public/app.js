const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const state = {
  mode: "pulse",
  mission: null,
  paletteId: null,
  palettes: [],
  constellation: null,
  constellationSeed: Math.floor(Math.random() * 10000),
  pointer: { x: 0.5, y: 0.5, active: false },
  time: 0,
  warp: false,
  warpAmount: 0,
  telemetry: { history: [] },
  shootingStars: [],
  nextShootingStarAt: 0,
  prevStarPositions: [],
  prevNodePositions: []
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
  shortcutOverlay: document.querySelector("#shortcutOverlay")
};

const ctx = elements.canvas.getContext("2d");
const seedWords = ["kinetic", "harbor", "cedar", "lumen", "summit", "signal", "maker", "atlas", "bright", "orbit"];

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
      elements.serverVersion.textContent = data.version || "—";
    }
  } catch {
    elements.serverStatus.textContent = "offline";
    elements.serverStatus.classList.remove("online", "live");
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
    elements.serverMetrics.textContent = "Metrics unavailable";
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
    elements.serverStatus.textContent = "reconnecting";
    elements.serverStatus.classList.remove("online", "live");
    elements.serverStatus.classList.add("reconnecting");
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
 * Mission / palettes / constellation data
 * ------------------------------------------------------------------------- */

async function loadPalettes() {
  if (!elements.palettePicker) return;
  try {
    const response = await fetch("/api/palettes");
    if (!response.ok) throw new Error("palettes failed");
    const data = await response.json();
    state.palettes = data.palettes || [];
    renderPalettePicker();
  } catch (error) {
    console.warn("palettes endpoint unavailable", error);
  }
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

async function loadConstellation() {
  try {
    const points = 20 + Math.floor(Number(elements.intensity.value) / 5);
    const response = await fetch(`/api/constellation?seed=${state.constellationSeed}&points=${points}`);
    if (!response.ok) throw new Error("constellation failed");
    state.constellation = await response.json();
    state.prevStarPositions = [];
    if (reducedMotion.matches) {
      drawCanvas(false);
    }
  } catch (error) {
    console.warn("constellation endpoint unavailable", error);
    state.constellation = null;
  }
}

async function loadMission() {
  elements.generate.disabled = true;
  try {
    const response = await fetch(apiUrl());
    if (!response.ok) throw new Error("mission request failed");
    state.mission = await response.json();
    if (state.mission.paletteId) {
      state.paletteId = state.mission.paletteId;
      renderPalettePicker();
    }
    state.prevNodePositions = [];
    renderMission();
    await loadConstellation();
    if (reducedMotion.matches) {
      drawCanvas(false);
    }
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

  elements.missionName.textContent = mission.missionName;
  elements.missionTagline.textContent = mission.tagline;
  elements.updatedAt.textContent = `Updated ${new Date(mission.updatedAt).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`;
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
 * Canvas rendering: nebula, grid, constellation, nodes, shooting stars, warp
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

function drawNebula(width, height) {
  nebula.blobs.forEach((blob) => {
    const x = width * (0.5 + 0.34 * Math.sin(state.time * blob.speed + blob.phaseX)) - blob.canvas.width / 2;
    const y = height * (0.5 + 0.3 * Math.cos(state.time * blob.speed * 0.8 + blob.phaseY)) - blob.canvas.height / 2;
    ctx.drawImage(blob.canvas, x, y);
  });
}

function maybeSpawnShootingStar(width, height) {
  if (reducedMotion.matches) return;
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
  if (!state.shootingStars.length) return;
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
      ctx.globalAlpha = 0.08 + (index % 3) * 0.03;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    });
  }

  stars.forEach((star) => {
    ctx.globalAlpha = 0.25 + star.brightness * 0.55;
    ctx.fillStyle = palette[1] || "#f7f2e8";
    ctx.beginPath();
    ctx.arc(star.x, star.y, star.size * 0.55, 0, Math.PI * 2);
    ctx.fill();
  });
  ctx.restore();
}

function drawCanvas(advance = true) {
  const width = elements.canvas.clientWidth;
  const height = elements.canvas.clientHeight;
  const mission = state.mission;
  if (advance) {
    state.time += 0.008 + Number(elements.tempo.value) / 28000;
    state.warpAmount += ((state.warp ? 1 : 0) - state.warpAmount) * 0.06;
  }

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#171717";
  ctx.fillRect(0, 0, width, height);

  if (mission) {
    const palette = mission.palette;
    drawNebula(width, height);
    drawGrid(width, height, palette);
    drawConstellationLayer(width, height, palette);

    const drifting = !reducedMotion.matches;
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
    mission.links.forEach((link, index) => {
      const a = nodes[link[0]];
      const b = nodes[link[1]];
      if (!a || !b) return;
      ctx.strokeStyle = index % 3 === 0 ? palette[2] : index % 3 === 1 ? palette[3] : palette[4];
      ctx.globalAlpha = elements.trails.checked ? 0.27 : 0.14;
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
      ctx.globalAlpha = 0.9;
      ctx.fillStyle = color;
      ctx.strokeStyle = "rgba(251, 250, 246, 0.78)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.rect(node.x - node.size, node.y - node.size, node.size * 2, node.size * 2);
      ctx.fill();
      if (node.energy > 70) ctx.stroke();
      ctx.restore();
    });

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
        toggleShortcutOverlay(false);
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
      await loadConstellation();
      elements.reseedConstellation.disabled = false;
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
    await navigator.clipboard.writeText(JSON.stringify(state.mission, null, 2));
    elements.copy.textContent = "Copied";
    window.setTimeout(() => {
      elements.copy.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 7a3 3 0 0 1 3-3h6a3 3 0 0 1 3 3v6a3 3 0 0 1-3 3h-1v1a3 3 0 0 1-3 3H7a3 3 0 0 1-3-3v-6a3 3 0 0 1 3-3h1V7zm3-1a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1h-6zM7 10a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1v-1h-3a3 3 0 0 1-3-3v-3H7z"></path></svg>Copy JSON';
    }, 1200);
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

resizeCanvas();
bindEvents();
bindShortcuts();
checkHealth();
loadPalettes();
loadMission();
loadMetrics();
connectTelemetry();
startAnimation();
