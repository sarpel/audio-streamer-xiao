// Utility functions

function formatBytes(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

function formatUptime(seconds) {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;

  if (days > 0) {
    return `${days}d ${hours}h ${minutes}m`;
  } else if (hours > 0) {
    return `${hours}h ${minutes}m ${secs}s`;
  } else if (minutes > 0) {
    return `${minutes}m ${secs}s`;
  } else {
    return `${secs}s`;
  }
}

function showAlert(message, type = "success") {
  const alert = document.createElement("div");
  alert.className = `alert alert-${type} show`;
  alert.textContent = message;

  const main = document.querySelector("main");
  main.insertBefore(alert, main.firstChild);

  setTimeout(() => {
    alert.classList.remove("show");
    setTimeout(() => alert.remove(), 300);
  }, 5000);
}

function updateStatusBadge(elementId, connected, label) {
  const element = document.getElementById(elementId);
  if (!element) return;

  const statusSpan = element.querySelector("span");
  if (connected) {
    statusSpan.textContent = "Connected";
    statusSpan.className = "connected";
  } else {
    statusSpan.textContent = "Disconnected";
    statusSpan.className = "disconnected";
  }
}

function setElementText(id, text) {
  const element = document.getElementById(id);
  if (element) {
    element.textContent = text;
  }
}

function getElementValue(id) {
  const element = document.getElementById(id);
  return element ? element.value : null;
}

function setElementValue(id, value) {
  const element = document.getElementById(id);
  if (element) {
    if (element.type === "checkbox") {
      element.checked = value;
    } else {
      element.value = value;
    }
  }
}

function confirmAction(message) {
  return confirm(message);
}

async function handleFormSubmit(formId, apiCall, successMessage) {
  const form = document.getElementById(formId);
  if (!form) return;

  form.addEventListener("submit", async (e) => {
    e.preventDefault();

    try {
      const response = await apiCall();
      showAlert(response.message || successMessage, "success");

      if (response.restart_required) {
        showAlert(
          "Device restart required for changes to take effect.",
          "warning"
        );
      }
    } catch (error) {
      showAlert("Failed to save configuration: " + error.message, "error");
    }
  });
}

// ===== Authentication Helper Functions =====

// Default credentials - will prompt for real credentials on 401
const DEFAULT_USERNAME = "";
const DEFAULT_PASSWORD = "";

function getStoredUsername() {
  return localStorage.getItem("auth_username") || DEFAULT_USERNAME;
}

function getStoredPassword() {
  return localStorage.getItem("auth_password") || DEFAULT_PASSWORD;
}

function storeCredentials(username, password) {
  localStorage.setItem("auth_username", username);
  localStorage.setItem("auth_password", password);
}

function clearCredentials() {
  localStorage.removeItem("auth_username");
  localStorage.removeItem("auth_password");
}

// Fetch with Basic Authentication
async function fetchWithAuth(url, options = {}) {
  const username = getStoredUsername();
  const password = getStoredPassword();
  
  // Only add Authorization header if we have credentials
  const headers = {
    "Content-Type": "application/json",
    ...(options.headers || {}),
  };
  
  if (username && password) {
    const auth = btoa(`${username}:${password}`);
    headers.Authorization = `Basic ${auth}`;
  }

  const mergedOptions = {
    ...options,
    headers: headers,
  };

  try {
    const response = await fetch(url, mergedOptions);

    if (response.status === 401) {
      // Authentication required - browser will show native auth dialog
      // For now, just throw an error
      throw new Error("Authentication required. Please refresh the page and enter your credentials.");
    }

    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }

    // Check if response is JSON
    const contentType = response.headers.get("content-type");
    if (contentType && contentType.includes("application/json")) {
      return await response.json();
    }

    return response;
  } catch (error) {
    console.error("Fetch with auth failed:", error);
    throw error;
  }
}

// Check if authentication is required (optional - for future use)
async function checkAuthRequired() {
  try {
    const response = await fetch("/api/system/info");
    return response.status === 401;
  } catch (error) {
    return false;
  }
}
