// network.js - Network Diagnostics Page Logic

// Elements
const scanWifiBtn = document.getElementById("scan-wifi");
const scanStatus = document.getElementById("scan-status");
const scanResults = document.getElementById("scan-results");
const startPingBtn = document.getElementById("start-ping");
const pingHost = document.getElementById("ping-host");
const pingCount = document.getElementById("ping-count");
const pingResults = document.getElementById("ping-results");
const dnsLookupBtn = document.getElementById("dns-lookup");
const dnsHostname = document.getElementById("dns-hostname");
const dnsResults = document.getElementById("dns-results");
const speedTestBtn = document.getElementById("speed-test");
const speedResults = document.getElementById("speed-results");
const refreshStatsBtn = document.getElementById("refresh-stats");

// Load current network status
async function loadNetworkStatus() {
  try {
    const data = await fetchWithAuth("/api/network/status");

    document.getElementById("network-status").textContent = data.connected
      ? "Connected"
      : "Disconnected";
    document.getElementById("network-ssid").textContent = data.ssid || "-";
    document.getElementById("network-ip").textContent = data.ip || "-";
    document.getElementById("network-gateway").textContent = data.gateway || "-";
    document.getElementById("network-subnet").textContent = data.subnet || "-";
    document.getElementById("network-signal").textContent = data.rssi
      ? `${data.rssi} dBm`
      : "-";
    document.getElementById("network-mac").textContent = data.mac || "-";
    document.getElementById("network-channel").textContent = data.channel || "-";
  } catch (error) {
    console.error("Failed to load network status:", error);
  }
}

// WiFi scan
async function scanWiFi() {
  scanWifiBtn.disabled = true;
  scanWifiBtn.textContent = "Scanning...";
  scanStatus.style.display = "block";
  scanStatus.innerHTML =
    '<div class="alert alert-info">Scanning for WiFi networks...</div>';
  scanResults.innerHTML = "";

  try {
    const data = await fetchWithAuth("/api/network/scan");

    if (data.networks && data.networks.length > 0) {
      scanStatus.innerHTML = `<div class="alert alert-success">Found ${data.networks.length} networks</div>`;

      const networksHTML = data.networks
        .map((network) => {
          let signalClass = "signal-weak";
          if (network.rssi > -50) signalClass = "signal-excellent";
          else if (network.rssi > -60) signalClass = "signal-good";
          else if (network.rssi > -70) signalClass = "signal-fair";

          return `
          <div class="wifi-network">
            <div class="wifi-info">
              <div class="wifi-ssid">${network.ssid}</div>
              <div class="wifi-details">
                Channel: ${network.channel} |
                Security: ${network.auth || "Open"} |
                BSSID: ${network.bssid || "N/A"}
              </div>
            </div>
            <span class="wifi-signal ${signalClass}">${network.rssi} dBm</span>
          </div>
        `;
        })
        .join("");

      scanResults.innerHTML = networksHTML;
    } else {
      scanStatus.innerHTML =
        '<div class="alert alert-warning">No networks found</div>';
    }
  } catch (error) {
    console.error("WiFi scan failed:", error);
    scanStatus.innerHTML = `<div class="alert alert-error">Scan failed: ${error.message}</div>`;
  } finally {
    scanWifiBtn.disabled = false;
    scanWifiBtn.textContent = "Start WiFi Scan";
  }
}

// Ping test
async function startPing() {
  const host = pingHost.value.trim();
  const count = parseInt(pingCount.value) || 4;

  if (!host) {
    showAlert("Please enter a host to ping", "error");
    return;
  }

  startPingBtn.disabled = true;
  startPingBtn.textContent = "Pinging...";
  pingResults.style.display = "block";
  pingResults.innerHTML = `<div class="ping-line">Pinging ${host} with ${count} packets...</div>`;

  try {
    const data = await fetchWithAuth("/api/network/ping", {
      method: "POST",
      body: JSON.stringify({ host, count }),
    });

    if (data.results) {
      const resultsHTML = data.results
        .map((result) => {
          const className = result.success ? "ping-success" : "ping-failed";
          return `<div class="ping-line ${className}">${result.message}</div>`;
        })
        .join("");

      pingResults.innerHTML += resultsHTML;

      if (data.summary) {
        pingResults.innerHTML += `
          <div class="ping-line" style="margin-top: 10px; color: #4488ff">
            ${data.summary}
          </div>
        `;
      }
    }
  } catch (error) {
    console.error("Ping failed:", error);
    pingResults.innerHTML += `<div class="ping-line ping-failed">Ping failed: ${error.message}</div>`;
  } finally {
    startPingBtn.disabled = false;
    startPingBtn.textContent = "Start Ping";
  }
}

// DNS lookup
async function dnsLookup() {
  const hostname = dnsHostname.value.trim();

  if (!hostname) {
    showAlert("Please enter a hostname", "error");
    return;
  }

  dnsLookupBtn.disabled = true;
  dnsLookupBtn.textContent = "Looking up...";
  dnsResults.style.display = "block";
  dnsResults.innerHTML = '<div class="alert alert-info">Resolving...</div>';

  try {
    const data = await fetchWithAuth("/api/network/dns", {
      method: "POST",
      body: JSON.stringify({ hostname }),
    });

    if (data.ip) {
      dnsResults.innerHTML = `
        <div class="alert alert-success">
          <strong>${hostname}</strong> resolves to <strong>${data.ip}</strong>
        </div>
      `;
    } else {
      dnsResults.innerHTML = `
        <div class="alert alert-error">
          Failed to resolve ${hostname}
        </div>
      `;
    }
  } catch (error) {
    console.error("DNS lookup failed:", error);
    dnsResults.innerHTML = `
      <div class="alert alert-error">
        DNS lookup failed: ${error.message}
      </div>
    `;
  } finally {
    dnsLookupBtn.disabled = false;
    dnsLookupBtn.textContent = "Lookup";
  }
}

// Speed test
async function runSpeedTest() {
  speedTestBtn.disabled = true;
  speedTestBtn.textContent = "Testing...";
  speedResults.style.display = "block";

  try {
    const data = await fetchWithAuth("/api/network/speedtest");

    document.getElementById("speed-latency").textContent = data.latency
      ? `${data.latency} ms`
      : "-";
    document.getElementById("speed-download").textContent = data.download
      ? `${data.download} KB/s`
      : "-";
    document.getElementById("speed-upload").textContent = data.upload
      ? `${data.upload} KB/s`
      : "-";

    showAlert("Speed test completed", "success");
  } catch (error) {
    console.error("Speed test failed:", error);
    showAlert(`Speed test failed: ${error.message}`, "error");
  } finally {
    speedTestBtn.disabled = false;
    speedTestBtn.textContent = "Run Speed Test";
  }
}

// Load network statistics
async function loadNetworkStats() {
  try {
    const data = await fetchWithAuth("/api/network/stats");

    document.getElementById("stat-packets-sent").textContent =
      data.packets_sent || "0";
    document.getElementById("stat-packets-received").textContent =
      data.packets_received || "0";
    document.getElementById("stat-bytes-sent").textContent = formatBytes(
      data.bytes_sent || 0
    );
    document.getElementById("stat-bytes-received").textContent = formatBytes(
      data.bytes_received || 0
    );
    document.getElementById("stat-drops").textContent = data.connection_drops || "0";
    document.getElementById("stat-reconnects").textContent = data.reconnections || "0";
  } catch (error) {
    console.error("Failed to load network stats:", error);
  }
}

// Event listeners
scanWifiBtn.addEventListener("click", scanWiFi);
startPingBtn.addEventListener("click", startPing);
dnsLookupBtn.addEventListener("click", dnsLookup);
speedTestBtn.addEventListener("click", runSpeedTest);
refreshStatsBtn.addEventListener("click", loadNetworkStats);

// Initial load
loadNetworkStatus();
loadNetworkStats();
