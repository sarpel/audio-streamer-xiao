// Configuration Page JavaScript

async function loadConfiguration() {
    try {
        // Load all configuration
        const config = await API.config.getAll();

        // WiFi Configuration
        if (config.wifi) {
            setElementValue('wifi-ssid', config.wifi.ssid);
            setElementValue('wifi-static-ip', config.wifi.use_static_ip);
            setElementValue('wifi-static-ip-addr', config.wifi.static_ip);
            setElementValue('wifi-gateway', config.wifi.gateway);
            setElementValue('wifi-subnet', config.wifi.subnet);
            setElementValue('wifi-dns-primary', config.wifi.dns_primary);
            setElementValue('wifi-dns-secondary', config.wifi.dns_secondary);
            
            // Show/hide static IP fields
            toggleStaticIpFields();
        }

        // TCP Configuration
        if (config.tcp) {
            setElementValue('tcp-server-ip', config.tcp.server_ip);
            setElementValue('tcp-server-port', config.tcp.server_port);
        }

        // Audio Configuration
        if (config.audio) {
            setElementValue('audio-sample-rate', config.audio.sample_rate);
            setElementValue('audio-bits', config.audio.bits_per_sample);
            setElementValue('audio-channels', config.audio.channels);
            setElementValue('audio-bck-pin', config.audio.bck_pin);
            setElementValue('audio-ws-pin', config.audio.ws_pin);
            setElementValue('audio-data-pin', config.audio.data_in_pin);
        }

        // Buffer Configuration
        if (config.buffer) {
            setElementValue('buffer-ring-size', config.buffer.ring_buffer_size);
            setElementValue('buffer-dma-count', config.buffer.dma_buf_count);
            setElementValue('buffer-dma-len', config.buffer.dma_buf_len);
        }

        // Debug Configuration
        if (config.debug) {
            setElementValue('debug-enabled', config.debug.debug_enabled);
            setElementValue('debug-stack-monitoring', config.debug.stack_monitoring);
            setElementValue('debug-auto-reboot', config.debug.auto_reboot);
            setElementValue('debug-i2s-reinit', config.debug.i2s_reinit);
            setElementValue('debug-buffer-drain', config.debug.buffer_drain);
        }

        // Update status badges
        const status = await API.system.getStatus();
        updateStatusBadge('wifi-status', status.wifi.connected, 'WiFi');
        updateStatusBadge('tcp-status', status.tcp.connected, 'TCP');

    } catch (error) {
        console.error('Failed to load configuration:', error);
        showAlert('Failed to load configuration', 'error');
    }
}

function toggleStaticIpFields() {
    const checkbox = document.getElementById('wifi-static-ip');
    const fields = document.getElementById('static-ip-fields');
    if (checkbox && fields) {
        fields.style.display = checkbox.checked ? 'block' : 'none';
    }
}

// WiFi Form Handler
document.getElementById('wifi-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    try {
        const data = {
            ssid: getElementValue('wifi-ssid'),
            password: getElementValue('wifi-password') || '********',
            use_static_ip: document.getElementById('wifi-static-ip').checked,
            static_ip: getElementValue('wifi-static-ip-addr'),
            gateway: getElementValue('wifi-gateway'),
            subnet: getElementValue('wifi-subnet'),
            dns_primary: getElementValue('wifi-dns-primary'),
            dns_secondary: getElementValue('wifi-dns-secondary')
        };

        const response = await API.config.setWifi(data);
        showAlert(response.message, 'success');
        
        if (response.restart_required) {
            showAlert('Device restart required. Please restart manually.', 'warning');
        }
    } catch (error) {
        showAlert('Failed to save WiFi configuration: ' + error.message, 'error');
    }
});

// TCP Form Handler
document.getElementById('tcp-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    try {
        const data = {
            server_ip: getElementValue('tcp-server-ip'),
            server_port: parseInt(getElementValue('tcp-server-port'))
        };

        const response = await API.config.setTcp(data);
        showAlert(response.message, 'success');
        
        if (response.restart_required) {
            showAlert('Device restart required. Please restart manually.', 'warning');
        }
    } catch (error) {
        showAlert('Failed to save TCP configuration: ' + error.message, 'error');
    }
});

// Audio Form Handler
document.getElementById('audio-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    try {
        const data = {
            sample_rate: parseInt(getElementValue('audio-sample-rate')),
            bits_per_sample: parseInt(getElementValue('audio-bits')),
            channels: parseInt(getElementValue('audio-channels')),
            bck_pin: parseInt(getElementValue('audio-bck-pin')),
            ws_pin: parseInt(getElementValue('audio-ws-pin')),
            data_in_pin: parseInt(getElementValue('audio-data-pin'))
        };

        const response = await API.config.setAudio(data);
        showAlert(response.message, 'success');
        
        if (response.restart_required) {
            showAlert('Device restart required. Please restart manually.', 'warning');
        }
    } catch (error) {
        showAlert('Failed to save audio configuration: ' + error.message, 'error');
    }
});

// Buffer Form Handler
document.getElementById('buffer-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    try {
        const data = {
            ring_buffer_size: parseInt(getElementValue('buffer-ring-size')),
            dma_buf_count: parseInt(getElementValue('buffer-dma-count')),
            dma_buf_len: parseInt(getElementValue('buffer-dma-len'))
        };

        const response = await API.config.setBuffer(data);
        showAlert(response.message, 'success');
        
        if (response.restart_required) {
            showAlert('Device restart required. Please restart manually.', 'warning');
        }
    } catch (error) {
        showAlert('Failed to save buffer configuration: ' + error.message, 'error');
    }
});

// Debug Form Handler
document.getElementById('debug-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    
    try {
        const data = {
            debug_enabled: document.getElementById('debug-enabled').checked,
            stack_monitoring: document.getElementById('debug-stack-monitoring').checked,
            auto_reboot: document.getElementById('debug-auto-reboot').checked,
            i2s_reinit: document.getElementById('debug-i2s-reinit').checked,
            buffer_drain: document.getElementById('debug-buffer-drain').checked
        };

        const response = await API.config.setDebug(data);
        showAlert(response.message, 'success');
    } catch (error) {
        showAlert('Failed to save debug configuration: ' + error.message, 'error');
    }
});

// Initialize configuration page
document.addEventListener('DOMContentLoaded', () => {
    loadConfiguration();
    
    // Setup static IP toggle
    const staticIpCheckbox = document.getElementById('wifi-static-ip');
    if (staticIpCheckbox) {
        staticIpCheckbox.addEventListener('change', toggleStaticIpFields);
    }
});
