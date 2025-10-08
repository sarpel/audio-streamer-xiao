// Main Dashboard JavaScript

async function loadDashboard() {
    try {
        // Load system status
        const status = await API.system.getStatus();
        
        // Update status badges
        updateStatusBadge('wifi-status', status.wifi.connected, 'WiFi');
        updateStatusBadge('tcp-status', status.tcp.connected, 'TCP');

        // Update stats
        setElementText('uptime', formatUptime(status.uptime_sec));
        setElementText('free-memory', formatBytes(status.memory.free_heap));
        setElementText('buffer-usage', status.buffer.usage_percent + '%');
        setElementText('sample-rate', status.audio.sample_rate + ' Hz');

        // Update network info
        setElementText('wifi-ssid', status.wifi.ssid || 'N/A');
        setElementText('tcp-server', status.tcp.server || 'N/A');
        setElementText('bytes-sent', formatBytes(status.tcp.bytes_sent));
        setElementText('reconnects', status.tcp.reconnects);

        // Load device info
        const info = await API.system.getInfo();
        setElementText('chip-model', info.chip_model);
        setElementText('cpu-cores', info.cores);
        setElementText('idf-version', info.idf_version);
        setElementText('mac-address', info.mac_address);

    } catch (error) {
        console.error('Failed to load dashboard:', error);
        showAlert('Failed to load dashboard data', 'error');
    }
}

async function restartDevice() {
    if (confirmAction('Are you sure you want to restart the device?')) {
        try {
            await API.system.restart();
            showAlert('Device is restarting...', 'success');
            setTimeout(() => {
                window.location.reload();
            }, 5000);
        } catch (error) {
            showAlert('Failed to restart device', 'error');
        }
    }
}

async function factoryReset() {
    if (confirmAction('WARNING: This will reset all settings to factory defaults. Are you sure?')) {
        if (confirmAction('This action cannot be undone. Continue?')) {
            try {
                await API.system.factoryReset();
                showAlert('Factory reset complete. Device is restarting...', 'success');
            } catch (error) {
                showAlert('Failed to perform factory reset', 'error');
            }
        }
    }
}

function refreshData() {
    loadDashboard();
    showAlert('Dashboard refreshed', 'success');
}

// Initialize dashboard
document.addEventListener('DOMContentLoaded', () => {
    loadDashboard();
    // Auto-refresh every 5 seconds
    setInterval(loadDashboard, 5000);
});
