// Monitoring Page JavaScript

let autoRefreshInterval = null;

async function loadMonitoringData() {
  try {
    const status = await API.system.getStatus();

    // Update status badges
    updateStatusBadge("wifi-status", status.wifi.connected, "WiFi");
    updateStatusBadge("tcp-status", status.tcp.connected, "TCP");

    // Update real-time stats
    setElementText("monitor-heap", formatBytes(status.memory.free_heap));
    setElementText(
      "monitor-min-heap",
      formatBytes(status.memory.min_free_heap)
    );
    setElementText("monitor-buffer", status.buffer.usage_percent + "%");
    setElementText("monitor-uptime", formatUptime(status.uptime_sec));

    // Update network monitoring
    setElementText(
      "monitor-wifi-connected",
      status.wifi.connected ? "✅ Yes" : "❌ No"
    );
    setElementText(
      "monitor-tcp-connected",
      status.tcp.connected ? "✅ Yes" : "❌ No"
    );
    setElementText("monitor-bytes-sent", formatBytes(status.tcp.bytes_sent));
    setElementText("monitor-reconnects", status.tcp.reconnects);

    // Update audio monitoring
    setElementText("monitor-sample-rate", status.audio.sample_rate + " Hz");
    setElementText(
      "monitor-buffer-size",
      formatBytes(status.buffer.size_kb * 1024)
    );
  } catch (error) {
    console.error("Failed to load monitoring data:", error);
    showAlert("Failed to load monitoring data", "error");
  }
}

function toggleAutoRefresh() {
  const checkbox = document.getElementById("auto-refresh");

  if (checkbox.checked) {
    // Start auto-refresh
    if (!autoRefreshInterval) {
      autoRefreshInterval = setInterval(loadMonitoringData, 2000);
    }
  } else {
    // Stop auto-refresh
    if (autoRefreshInterval) {
      clearInterval(autoRefreshInterval);
      autoRefreshInterval = null;
    }
  }
}

// Initialize monitoring page
document.addEventListener("DOMContentLoaded", () => {
  loadMonitoringData();

  // Setup auto-refresh toggle
  const autoRefreshCheckbox = document.getElementById("auto-refresh");
  if (autoRefreshCheckbox) {
    autoRefreshCheckbox.addEventListener("change", toggleAutoRefresh);

    // Start auto-refresh if checked by default
    if (autoRefreshCheckbox.checked) {
      toggleAutoRefresh();
    }
  }
});

// Cleanup on page unload
window.addEventListener("beforeunload", () => {
  if (autoRefreshInterval) {
    clearInterval(autoRefreshInterval);
  }
});
