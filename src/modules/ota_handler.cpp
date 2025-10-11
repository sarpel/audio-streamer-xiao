#include "ota_handler.h"
#include "web_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_app_format.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "OTA_HANDLER";

static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *update_partition = NULL;
static bool ota_in_progress = false;
static size_t total_size = 0;
static size_t written_size = 0;

bool ota_handler_init(void)
{
    ESP_LOGI(TAG, "OTA handler initialized");
    return true;
}

bool ota_handler_is_active(void)
{
    return ota_in_progress;
}

int ota_handler_get_progress(void)
{
    if (total_size == 0)
        return 0;
    return (written_size * 100) / total_size;
}

// POST /api/ota/upload - Handle OTA firmware upload
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    // Check authentication first
    if (!web_server_check_auth(req))
    {
        return web_server_send_auth_required(req);
    }

    // Add CORS headers for successful auth
    web_server_add_cors_headers(req);

    esp_err_t ret = ESP_OK;
    char buf[1024];
    int received;

    ESP_LOGI(TAG, "OTA upload started");
    ota_in_progress = true;
    written_size = 0;
    total_size = req->content_len;

    // Get update partition
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition)
    {
        ESP_LOGE(TAG, "Failed to find update partition");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No update partition");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             update_partition->subtype, update_partition->address);

    // Begin OTA
    ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    // Write data
    while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0)
    {
        ret = esp_ota_write(ota_handle, buf, received);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(ret));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            ota_in_progress = false;
            return ESP_FAIL;
        }
        written_size += received;

        // Log progress
        if (written_size % (100 * 1024) == 0)
        {
            ESP_LOGI(TAG, "Written %zu of %zu bytes (%d%%)",
                     written_size, total_size, ota_handler_get_progress());
        }
    }

    if (received < 0)
    {
        ESP_LOGE(TAG, "Error receiving data");
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    // Finalize OTA
    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    // Set boot partition
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful! Total: %zu bytes", written_size);
    ota_in_progress = false;

    // Send success response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"OTA complete. Rebooting...\"}");

    // Reboot after 2 seconds
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}

// GET /api/ota/status - Get OTA status and progress
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    // Check authentication
    if (!web_server_check_auth(req))
    {
        return web_server_send_auth_required(req);
    }

    cJSON *root = cJSON_CreateObject();

    // Get current partition info
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    cJSON_AddBoolToObject(root, "in_progress", ota_in_progress);
    cJSON_AddNumberToObject(root, "progress", ota_handler_get_progress());
    cJSON_AddStringToObject(root, "running_partition", running->label);
    cJSON_AddStringToObject(root, "boot_partition", boot->label);

    if (update != NULL)
    {
        cJSON_AddStringToObject(root, "update_partition", update->label);
    }

    // Get app description
    esp_app_desc_t app_desc;
    if (esp_ota_get_partition_description(running, &app_desc) == ESP_OK)
    {
        cJSON_AddStringToObject(root, "version", app_desc.version);
        cJSON_AddStringToObject(root, "project_name", app_desc.project_name);
        cJSON_AddStringToObject(root, "compile_time", app_desc.time);
        cJSON_AddStringToObject(root, "compile_date", app_desc.date);
        cJSON_AddStringToObject(root, "idf_version", app_desc.idf_ver);
    }

    // Check if we're in a pending verify state
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            cJSON_AddStringToObject(root, "status", "pending_verify");
            cJSON_AddBoolToObject(root, "pending_verify", true);
        }
        else
        {
            cJSON_AddStringToObject(root, "status", "valid");
            cJSON_AddBoolToObject(root, "pending_verify", false);
        }
    }

    esp_err_t ret = web_server_send_json_response(req, root, 200);
    cJSON_Delete(root);
    return ret;
}

// POST /api/ota/rollback - Rollback to previous partition
static esp_err_t ota_rollback_handler(httpd_req_t *req)
{
    // Check authentication first
    if (!web_server_check_auth(req))
    {
        return web_server_send_auth_required(req);
    }

    // Add CORS headers for successful auth
    web_server_add_cors_headers(req);

    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();

    if (last_invalid != NULL)
    {
        ESP_LOGI(TAG, "Rolling back to partition %s", last_invalid->label);
        esp_err_t ret = esp_ota_set_boot_partition(last_invalid);
        if (ret != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Rollback failed");
            return ESP_FAIL;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Rolling back. Rebooting...\"}");

        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No partition to rollback to");
    return ESP_FAIL;
}

bool ota_handler_register_endpoints(httpd_handle_t server)
{
    if (!server)
    {
        ESP_LOGE(TAG, "Invalid server handle");
        return false;
    }

    ESP_LOGI(TAG, "Registering OTA endpoints...");

    // POST /api/ota/upload
    httpd_uri_t ota_upload_uri = {
        .uri = "/api/ota/upload",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    esp_err_t ret = httpd_register_uri_handler(server, &ota_upload_uri);
    ESP_LOGI(TAG, "Registered /api/ota/upload: %s", ret == ESP_OK ? "SUCCESS" : "FAILED");

    // GET /api/ota/status
    httpd_uri_t ota_status_uri = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    ret = httpd_register_uri_handler(server, &ota_status_uri);
    ESP_LOGI(TAG, "Registered /api/ota/status: %s", ret == ESP_OK ? "SUCCESS" : "FAILED");

    // POST /api/ota/rollback
    httpd_uri_t ota_rollback_uri = {
        .uri = "/api/ota/rollback",
        .method = HTTP_POST,
        .handler = ota_rollback_handler,
        .user_ctx = NULL,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = NULL};
    ret = httpd_register_uri_handler(server, &ota_rollback_uri);
    ESP_LOGI(TAG, "Registered /api/ota/rollback: %s", ret == ESP_OK ? "SUCCESS" : "FAILED");

    ESP_LOGI(TAG, "OTA endpoints registered");
    return true;
}

void ota_handler_deinit(void)
{
    if (ota_in_progress && ota_handle)
    {
        esp_ota_abort(ota_handle);
        ota_in_progress = false;
    }
    ESP_LOGI(TAG, "OTA handler deinitialized");
}
