// ota.js - OTA Update Page Logic

let otaInProgress = false;

// Elements
const firmwareFile = document.getElementById("firmwareFile");
const uploadBtn = document.getElementById("uploadBtn");
const rollbackBtn = document.getElementById("rollbackBtn");
const uploadProgress = document.getElementById("uploadProgress");
const progressBar = document.getElementById("progressBar");
const progressText = document.getElementById("progressText");
const uploadStatus = document.getElementById("uploadStatus");
const runningPartition = document.getElementById("runningPartition");
const bootPartition = document.getElementById("bootPartition");
const otaStatus = document.getElementById("otaStatus");

// Load OTA status
async function loadOTAStatus() {
  try {
    const data = await fetchWithAuth("/api/ota/status");
    runningPartition.textContent = data.running_partition || "Unknown";
    bootPartition.textContent = data.boot_partition || "Unknown";
    otaStatus.textContent = data.in_progress ? "🔄 In Progress" : "✅ Idle";
    otaInProgress = data.in_progress;
  } catch (error) {
    console.error("Failed to load OTA status:", error);
    showAlert(uploadStatus, "Failed to load OTA status", "error");
  }
}

// Upload firmware
async function uploadFirmware() {
  const file = firmwareFile.files[0];

  if (!file) {
    showAlert(uploadStatus, "Please select a firmware file", "error");
    return;
  }

  if (!file.name.endsWith(".bin")) {
    showAlert(uploadStatus, "Please select a valid .bin file", "error");
    return;
  }

  if (
    !confirm(
      "Are you sure you want to update the firmware? The device will reboot after update."
    )
  ) {
    return;
  }

  uploadBtn.disabled = true;
  uploadProgress.style.display = "block";
  uploadStatus.style.display = "none";

  try {
    const xhr = new XMLHttpRequest();

    xhr.upload.addEventListener("progress", (e) => {
      if (e.lengthComputable) {
        const percentComplete = Math.round((e.loaded / e.total) * 100);
        progressBar.style.width = percentComplete + "%";
        progressText.textContent = `Uploading... ${percentComplete}%`;
      }
    });

    xhr.addEventListener("load", () => {
      if (xhr.status === 200) {
        progressBar.style.width = "100%";
        progressText.textContent = "Upload complete! Device rebooting...";
        showAlert(
          uploadStatus,
          "Firmware uploaded successfully. Device is rebooting...",
          "success"
        );

        // Redirect to home after 5 seconds
        setTimeout(() => {
          window.location.href = "/";
        }, 5000);
      } else {
        throw new Error(`Upload failed with status ${xhr.status}`);
      }
    });

    xhr.addEventListener("error", () => {
      throw new Error("Network error during upload");
    });

    xhr.open("POST", "/api/ota/upload");

    // Add Basic Auth header
    const auth = btoa(`${getStoredUsername()}:${getStoredPassword()}`);
    xhr.setRequestHeader("Authorization", `Basic ${auth}`);

    xhr.send(file);
  } catch (error) {
    console.error("OTA upload error:", error);
    showAlert(uploadStatus, `Upload failed: ${error.message}`, "error");
    uploadBtn.disabled = false;
    uploadProgress.style.display = "none";
  }
}

// Rollback to previous firmware
async function rollbackFirmware() {
  if (
    !confirm(
      "Are you sure you want to rollback to the previous firmware? The device will reboot."
    )
  ) {
    return;
  }

  rollbackBtn.disabled = true;

  try {
    await fetchWithAuth("/api/ota/rollback", {
      method: "POST",
    });

    showAlert(
      uploadStatus,
      "Rollback initiated. Device is rebooting...",
      "success"
    );

    // Redirect after 5 seconds
    setTimeout(() => {
      window.location.href = "/";
    }, 5000);
  } catch (error) {
    console.error("Rollback error:", error);
    showAlert(uploadStatus, `Rollback failed: ${error.message}`, "error");
    rollbackBtn.disabled = false;
  }
}

// Event listeners
uploadBtn.addEventListener("click", uploadFirmware);
rollbackBtn.addEventListener("click", rollbackFirmware);

// Initial load
loadOTAStatus();
