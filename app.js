const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

function collectConfig() {
  const mode = document.querySelector('input[name="mode"]:checked')?.value || "groq";
  return {
    mode,
    groq: {
      apiKey: $("#groq-key").value.trim(),
      model: $("#groq-model").value,
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
    stream: { weightsUrl: $("#weights-url").value.trim() },
    timestamp: new Date().toISOString(),
  };
}

function updateModeUI() {
  const mode = document.querySelector('input[name="mode"]:checked')?.value;
  $("#groq-card").style.display = (mode === "groq" || mode === "hybrid") ? "block" : "none";
  $("#stream-card").style.display = mode === "stream" ? "block" : "none";
}
$$('input[name="mode"]').forEach((el) => el.addEventListener("change", updateModeUI));
updateModeUI();

const btnBuild = $("#btn-build");
const statusEl = $("#build-status");
const flashArea = $("#flash-area");
const installBtn = $("#install-btn");

btnBuild.addEventListener("click", async () => {
  const config = collectConfig();
  if (config.mode === "stream" && !config.stream.weightsUrl) {
    statusEl.textContent = "Provide a Weights Base URL for experimental mode.";
    statusEl.className = "status error";
    return;
  }
  btnBuild.disabled = true;
  statusEl.textContent = "Preparing build…";
  statusEl.className = "status";
  try {
    localStorage.setItem("fox-config", JSON.stringify(config, null, 2));
    await new Promise((r) => setTimeout(r, 1500));
    statusEl.textContent = "Build request saved. Flash Fox, join Fox-Setup, and configure Wi-Fi + optional AI there. Your API key stays off the firmware build.";
    statusEl.className = "status success";
    installBtn.removeAttribute("manifest");
    flashArea.style.display = "none";
  } catch (err) {
    statusEl.textContent = "Build failed: " + err.message;
    statusEl.className = "status error";
  } finally {
    btnBuild.disabled = false;
  }
});
