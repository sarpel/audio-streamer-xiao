// logs.js - System Logs Viewer

let autoRefresh = true;
let refreshInterval = null;
let logs = [];
let filteredLogs = [];
let stats = { total: 0, errors: 0, warnings: 0, info: 0, debug: 0 };

// Elements
const logContainer = document.getElementById("log-container");
const autoRefreshCheckbox = document.getElementById("auto-refresh");
const clearLogsBtn = document.getElementById("clear-logs");
const downloadLogsBtn = document.getElementById("download-logs");
const refreshLogsBtn = document.getElementById("refresh-logs");
const logSearchInput = document.getElementById("log-search");
const levelFilters = document.querySelectorAll(".level-filter");

// Stats elements
const statTotal = document.getElementById("stat-total");
const statErrors = document.getElementById("stat-errors");
const statWarnings = document.getElementById("stat-warnings");
const statInfo = document.getElementById("stat-info");

// Load logs from API
async function loadLogs() {
  try {
    const response = await fetchWithAuth("/api/system/logs");

    if (response && response.logs) {
      logs = response.logs;
      updateStats();
      filterAndDisplayLogs();
    }
  } catch (error) {
    console.error("Failed to load logs:", error);
    logContainer.innerHTML = `
      <div style="color: #ff4444; text-align: center; margin-top: 100px">
        Failed to load logs: ${error.message}
      </div>
    `;
  }
}

// Update statistics
function updateStats() {
  stats = {
    total: logs.length,
    errors: logs.filter((l) => l.level === "ERROR").length,
    warnings: logs.filter((l) => l.level === "WARN").length,
    info: logs.filter((l) => l.level === "INFO").length,
    debug: logs.filter((l) => l.level === "DEBUG").length,
  };

  statTotal.textContent = stats.total;
  statErrors.textContent = stats.errors;
  statWarnings.textContent = stats.warnings;
  statInfo.textContent = stats.info;
}

// Filter logs based on level and search
function filterAndDisplayLogs() {
  const searchTerm = logSearchInput.value.toLowerCase();
  const enabledLevels = Array.from(levelFilters)
    .filter((f) => f.checked)
    .map((f) => f.dataset.level);

  filteredLogs = logs.filter((log) => {
    // Filter by level
    if (!enabledLevels.includes(log.level)) {
      return false;
    }

    // Filter by search term
    if (searchTerm) {
      const searchableText =
        `${log.tag} ${log.message} ${log.timestamp}`.toLowerCase();
      if (!searchableText.includes(searchTerm)) {
        return false;
      }
    }

    return true;
  });

  displayLogs();
}

// Display logs in container
function displayLogs() {
  if (filteredLogs.length === 0) {
    logContainer.innerHTML = `
      <div style="color: #888; text-align: center; margin-top: 100px">
        No logs to display
      </div>
    `;
    return;
  }

  const logsHTML = filteredLogs
    .slice()
    .reverse() // Show newest first
    .map((log) => {
      return `
      <div class="log-entry">
        <span class="log-timestamp">${log.timestamp}</span>
        <span class="log-level log-level-${log.level}">${log.level}</span>
        <span class="log-tag">${log.tag}</span>
        <span class="log-message">${escapeHtml(log.message)}</span>
      </div>
    `;
    })
    .join("");

  logContainer.innerHTML = logsHTML;

  // Auto-scroll to bottom (newest logs)
  logContainer.scrollTop = logContainer.scrollHeight;
}

// Escape HTML to prevent XSS
function escapeHtml(text) {
  const div = document.createElement("div");
  div.textContent = text;
  return div.innerHTML;
}

// Clear all logs
async function clearLogs() {
  if (!confirm("Are you sure you want to clear all logs?")) {
    return;
  }

  try {
    await fetchWithAuth("/api/system/logs/clear", { method: "POST" });
    logs = [];
    filteredLogs = [];
    updateStats();
    displayLogs();
    showAlert("Logs cleared successfully", "success");
  } catch (error) {
    console.error("Failed to clear logs:", error);
    showAlert("Failed to clear logs: " + error.message, "error");
  }
}

// Download logs as text file
function downloadLogs() {
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const filename = `audio-streamer-logs-${timestamp}.txt`;

  const logText = logs
    .map((log) => `[${log.timestamp}] ${log.level} ${log.tag}: ${log.message}`)
    .join("\n");

  const blob = new Blob([logText], { type: "text/plain" });
  const url = URL.createObjectURL(blob);

  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();

  URL.revokeObjectURL(url);
  showAlert(`Downloaded ${logs.length} logs`, "success");
}

// Toggle auto-refresh
function toggleAutoRefresh() {
  autoRefresh = autoRefreshCheckbox.checked;

  if (autoRefresh) {
    refreshInterval = setInterval(loadLogs, 2000);
  } else {
    if (refreshInterval) {
      clearInterval(refreshInterval);
      refreshInterval = null;
    }
  }
}

// Event listeners
autoRefreshCheckbox.addEventListener("change", toggleAutoRefresh);
clearLogsBtn.addEventListener("click", clearLogs);
downloadLogsBtn.addEventListener("click", downloadLogs);
refreshLogsBtn.addEventListener("click", loadLogs);

logSearchInput.addEventListener("input", filterAndDisplayLogs);

levelFilters.forEach((filter) => {
  filter.addEventListener("change", filterAndDisplayLogs);
});

// Initial load
loadLogs();
toggleAutoRefresh();
