// API Client for ESP32 Audio Streamer

const API = {
    baseUrl: '',

    // Helper to make API requests
    async request(endpoint, options = {}) {
        const url = `${this.baseUrl}${endpoint}`;
        const defaultOptions = {
            headers: {
                'Content-Type': 'application/json'
            }
        };

        try {
            const response = await fetch(url, { ...defaultOptions, ...options });
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            return await response.json();
        } catch (error) {
            console.error('API request failed:', error);
            throw error;
        }
    },

    // Configuration endpoints
    config: {
        getWifi: () => API.request('/api/config/wifi'),
        setWifi: (data) => API.request('/api/config/wifi', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getTcp: () => API.request('/api/config/tcp'),
        setTcp: (data) => API.request('/api/config/tcp', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getAudio: () => API.request('/api/config/audio'),
        setAudio: (data) => API.request('/api/config/audio', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getBuffer: () => API.request('/api/config/buffer'),
        setBuffer: (data) => API.request('/api/config/buffer', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getTasks: () => API.request('/api/config/tasks'),
        setTasks: (data) => API.request('/api/config/tasks', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getError: () => API.request('/api/config/error'),
        setError: (data) => API.request('/api/config/error', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getDebug: () => API.request('/api/config/debug'),
        setDebug: (data) => API.request('/api/config/debug', {
            method: 'POST',
            body: JSON.stringify(data)
        }),

        getAll: () => API.request('/api/config/all')
    },

    // System endpoints
    system: {
        getStatus: () => API.request('/api/system/status'),
        getInfo: () => API.request('/api/system/info'),
        restart: () => API.request('/api/system/restart', { method: 'POST' }),
        factoryReset: () => API.request('/api/system/factory-reset', { method: 'POST' }),
        save: () => API.request('/api/system/save', { method: 'POST' }),
        load: () => API.request('/api/system/load', { method: 'POST' })
    }
};
