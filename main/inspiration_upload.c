#include "inspiration_upload.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_http_client.h"
#include "nvs.h"
#include "inspiration_storage.h"

#define UPLOAD_NAMESPACE "inspiration"
#define UPLOAD_ENDPOINT_KEY "endpoint"
#define UPLOAD_SESSION_KEY "session"

static char s_endpoint[128];
static char s_session[40];

esp_err_t inspiration_upload_load_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open(UPLOAD_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return ESP_ERR_NOT_FOUND;
    size_t endpoint_size = sizeof(s_endpoint);
    size_t session_size = sizeof(s_session);
    esp_err_t err = nvs_get_str(nvs, UPLOAD_ENDPOINT_KEY, s_endpoint, &endpoint_size);
    if (err == ESP_OK) err = nvs_get_str(nvs, UPLOAD_SESSION_KEY, s_session, &session_size);
    nvs_close(nvs);
    return err;
}

bool inspiration_upload_configured(void) { return s_endpoint[0] && s_session[0]; }

esp_err_t inspiration_upload_next(inspiration_chunk_queue_t *queue)
{
    if (!inspiration_upload_configured()) return ESP_ERR_INVALID_STATE;
    inspiration_chunk_t *chunk = inspiration_chunk_queue_next_ready(queue);
    if (!chunk) return ESP_ERR_NOT_FOUND;
    char url[200];
    int length = snprintf(url, sizeof(url), "%s/v1/passport/sessions/%s/chunks/%06" PRIu32,
                          s_endpoint, s_session, chunk->sequence);
    if (length < 0 || (size_t)length >= sizeof(url)) {
        inspiration_chunk_queue_retry(queue, chunk->sequence);
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = NULL;
    esp_err_t err = inspiration_storage_open_chunk_read(chunk->sequence, &file);
    if (err != ESP_OK) { inspiration_chunk_queue_retry(queue, chunk->sequence); return err; }
    esp_http_client_config_t config = {.url = url, .timeout_ms = 12000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client || esp_http_client_set_method(client, HTTP_METHOD_POST) != ESP_OK ||
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream") != ESP_OK ||
        esp_http_client_open(client, chunk->bytes) != ESP_OK) {
        if (client) esp_http_client_cleanup(client);
        fclose(file); inspiration_chunk_queue_retry(queue, chunk->sequence); return ESP_FAIL;
    }
    uint8_t buffer[512];
    size_t total = 0;
    while (total < chunk->bytes) {
        size_t read = fread(buffer, 1, sizeof(buffer), file);
        if (!read || esp_http_client_write(client, (const char *)buffer, read) != (int)read) break;
        total += read;
    }
    fclose(file);
    int status = esp_http_client_fetch_headers(client) >= 0 ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    if (total == chunk->bytes && status >= 200 && status < 300 &&
        inspiration_storage_delete_chunk(chunk->sequence) == ESP_OK &&
        inspiration_chunk_queue_acknowledge(queue, chunk->sequence)) return ESP_OK;
    inspiration_chunk_queue_retry(queue, chunk->sequence);
    return ESP_FAIL;
}
