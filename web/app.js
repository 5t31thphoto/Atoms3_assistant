/**
 * Fox Voice Assistant — Web Flasher frontend
 * Collects config, triggers build (GitHub Actions or local demo),
 * then hands a merged binary / manifest to ESP Web Tools / esptool-js.
 */

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

function collectConfig() {
  const mode = document.querySelector('input[name="mode"]:checked')?.value || "groq";
  return {
    mode,
    groq: {
      apiKey: $("#groq-key").value.trim(),
      model: $("#groq-model").value,
      systemPrompt: $("#system-prompt").value,
    },
    fox: {
      name: $("#fox-name").value.trim() || "Ember",
      colorPrimary: $("#color-primary").value,
      colorAccent: $("#color-accent").value,
      colorBg: $("#color-bg").value,
      splashText: $("#splash-text").value.trim(),
      lipSync: $("#enable-lip-sync").checked,
    },
    tools: {
      bleRadar: $("#tool-ble-radar").checked,
      wifiScan: $("#tool-wifi-scan").checked,
      ir: $("#tool-ir").checked,
      imu: $("#tool-imu").checked,
      context: $("#tool-context").checked,
    },
    stream: {
      weightsUrl: $("#weights-url").value.trim(),
    },
    timestamp: new Date().toISOString(),
  };
}

function updateModeUI() {
  const mode = document.querySelector('input[name="mode"]:checked')?.value;
  $("#groq-card").style.display = (mode === "groq" || mode === "hybrid") ? "block" : "none";
  $("#stream-card").style.display = mode === "stream" ? "block" : "none";
}

$$('input[name="mode"]').forEach((el) => {
  el.addEventListener("change", updateModeUI);
});
updateModeUI();

const btnBuild = $("#btn-build");
const statusEl = $("#build-status");
const flashArea = $("#flash-area");
const installBtn = $("#install-btn");

btnBuild.addEventListener("click", async () => {
  const config = collectConfig();

  if ((config.mode === "groq" || config.mode === "hybrid") && !config.groq.apiKey) {
    statusEl.textContent = "Please enter a Groq API key (or switch to On-device mode).";
    statusEl.className = "status error";
    return;
  }

  if (config.mode === "stream" && !config.stream.weightsUrl) {
    statusEl.textContent = "Please provide a Weights Base URL for experimental mode.";
    statusEl.className = "status error";
    return;
  }

  btnBuild.disabled = true;
  statusEl.textContent = "Preparing build…";
  statusEl.className = "status";

  // In a real deployment this would:
  // 1. POST config to a GitHub Actions workflow_dispatch (via a small backend or gh api with token)
  // 2. Poll for the artifact
  // 3. Point the install button at the resulting manifest.json
  //
  // For this starter archive we simulate a successful local build and
  // point at a placeholder manifest that you replace after CI runs.

  try {
    // Store config so the user can download it or the firmware can embed it later
    localStorage.setItem("fox-config", JSON.stringify(config, null, 2));

    // Demo: pretend CI finished
    await new Promise((r) => setTimeout(r, 1800));

    statusEl.textContent = "Build complete (demo). In production this would be a real CI artifact.";
    statusEl.className = "status success";

    // Point ESP Web Tools at a local or hosted manifest.
    // After you run the real GitHub Action, replace this with the published URL:
    // e.g. https://youruser.github.io/fox-voice-assistant/builds/latest/manifest.json
    const manifestUrl = "manifest-demo.json";
    installBtn.setAttribute("manifest", manifestUrl);
    flashArea.style.display = "block";
  } catch (err) {
    statusEl.textContent = "Build failed: " + err.message;
    statusEl.className = "status error";
  } finally {
    btnBuild.disabled = false;
  }
});

// Optional: expose config download
window.downloadConfig = () => {
  const blob = new Blob([JSON.stringify(collectConfig(), null, 2)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "fox-config.json";
  a.click();
};
