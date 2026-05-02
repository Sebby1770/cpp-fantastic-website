const state = {
  mode: "pulse",
  mission: null,
  pointer: { x: 0.5, y: 0.5, active: false },
  time: 0
};

const elements = {
  canvas: document.querySelector("#skyCanvas"),
  serverStatus: document.querySelector("#serverStatus"),
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
  copy: document.querySelector("#copyButton"),
  metrics: document.querySelector("#metricStrip"),
  priorities: document.querySelector("#priorityList"),
  waypoints: document.querySelector("#waypoints"),
  swatches: document.querySelector("#swatches"),
  paletteName: document.querySelector("#paletteName"),
  modeBadge: document.querySelector("#modeBadge")
};

const ctx = elements.canvas.getContext("2d");
const seedWords = ["kinetic", "harbor", "cedar", "lumen", "summit", "signal", "maker", "atlas", "bright", "orbit"];

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

function apiUrl() {
  const params = new URLSearchParams({
    seed: elements.seed.value || "sebby",
    mode: state.mode,
    intensity: elements.intensity.value,
    tempo: elements.tempo.value
  });
  return `/api/mission?${params}`;
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
  elements.paletteName.textContent = mission.seed;

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

function drawCanvas() {
  const width = elements.canvas.clientWidth;
  const height = elements.canvas.clientHeight;
  const mission = state.mission;
  state.time += 0.008 + Number(elements.tempo.value) / 28000;

  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#171717";
  ctx.fillRect(0, 0, width, height);

  if (mission) {
    const palette = mission.palette;
    drawGrid(width, height, palette);

    const nodes = mission.nodes.map((node, index) => {
      const drift = Math.sin(state.time * (1.5 + index * 0.03) + index) * 10;
      const pullX = state.pointer.active ? (state.pointer.x - 0.5) * 24 : 0;
      const pullY = state.pointer.active ? (state.pointer.y - 0.5) * 24 : 0;
      return {
        x: node.x * width + drift + pullX,
        y: node.y * height + Math.cos(state.time + index) * 8 + pullY,
        size: node.size,
        energy: node.energy
      };
    });

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

  requestAnimationFrame(drawCanvas);
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
checkHealth();
loadMission();
drawCanvas();

