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
