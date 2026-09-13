#include "inspiration_upload.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_http_client.h"
#include "esp_mac.h"
#include "nvs.h"
#include "esp_random.h"
#include "inspiration_config.h"
#include "inspiration_storage.h"

#define UPLOAD_NAMESPACE "inspiration"
#define UPLOAD_ENDPOINT_KEY "endpoint"
#define UPLOAD_SESSION_KEY "session"

static char s_endpoint[128];
static char s_session[40];

esp_err_t inspiration_upload_load_config(void)
{
    s_endpoint[0] = '\0';
    s_session[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(UPLOAD_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        snprintf(s_endpoint, sizeof(s_endpoint), "%s", INSPIRATION_DEFAULT_BACKEND_ENDPOINT);
        return ESP_OK;
    }
    size_t endpoint_size = sizeof(s_endpoint);
    size_t session_size = sizeof(s_session);
    esp_err_t err = nvs_get_str(nvs, UPLOAD_ENDPOINT_KEY, s_endpoint, &endpoint_size);
    if (err == ESP_OK) {
        // A missing session is normal before the first recording.
        esp_err_t session_err = nvs_get_str(nvs, UPLOAD_SESSION_KEY, s_session, &session_size);
        if (session_err == ESP_ERR_NVS_NOT_FOUND) session_err = ESP_OK;
        if (session_err != ESP_OK) err = session_err;
    }
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        snprintf(s_endpoint, sizeof(s_endpoint), "%s", INSPIRATION_DEFAULT_BACKEND_ENDPOINT);
        return ESP_OK;
    }
    return err;
}

bool inspiration_upload_configured(void) { return s_endpoint[0]; }
bool inspiration_upload_session_active(void) { return s_session[0]; }

esp_err_t inspiration_upload_begin_session(void)
{
    if (s_session[0]) return ESP_OK;
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    unsigned int nonce = esp_random();
    int written = snprintf(s_session, sizeof(s_session), "p%02x%02x%02x-%08x",
                           mac[3], mac[4], mac[5], nonce);
    if (written < 0 || (size_t)written >= sizeof(s_session)) return ESP_ERR_INVALID_SIZE;
    nvs_handle_t nvs;
    err = nvs_open(UPLOAD_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, UPLOAD_SESSION_KEY, s_session);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err != ESP_OK) s_session[0] = '\0';
    return err;
}

esp_err_t inspiration_upload_clear_session(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(UPLOAD_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, UPLOAD_SESSION_KEY);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err == ESP_OK) s_session[0] = '\0';
    return err;
}

esp_err_t inspiration_upload_chunk(const inspiration_chunk_t *chunk)
{
    if (!inspiration_upload_configured() || !inspiration_upload_session_active()) return ESP_ERR_INVALID_STATE;
    if (!chunk) return ESP_ERR_INVALID_ARG;
    char url[200];
    int length = snprintf(url, sizeof(url), "%s/v1/passport/sessions/%s/chunks/%06" PRIu32,
                          s_endpoint, s_session, chunk->sequence);
    if (length < 0 || (size_t)length >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = NULL;
    esp_err_t err = inspiration_storage_open_chunk_read(chunk->sequence, &file);
    if (err != ESP_OK) return err;
    esp_http_client_config_t config = {.url = url, .timeout_ms = 12000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client || esp_http_client_set_method(client, HTTP_METHOD_POST) != ESP_OK ||
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream") != ESP_OK ||
        esp_http_client_open(client, chunk->bytes) != ESP_OK) {
        if (client) esp_http_client_cleanup(client);
        fclose(file); return ESP_FAIL;
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
        inspiration_storage_delete_chunk(chunk->sequence) == ESP_OK) return ESP_OK;
    return ESP_FAIL;
}

esp_err_t inspiration_upload_complete(void)
{
    if (!inspiration_upload_configured() || !inspiration_upload_session_active()) return ESP_ERR_INVALID_STATE;
    char url[200];
    int length = snprintf(url, sizeof(url), "%s/v1/passport/sessions/%s/complete", s_endpoint, s_session);
    if (length < 0 || (size_t)length >= sizeof(url)) return ESP_ERR_INVALID_SIZE;
    esp_http_client_config_t config = {.url = url, .timeout_ms = 12000};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client || esp_http_client_set_method(client, HTTP_METHOD_POST) != ESP_OK ||
        esp_http_client_open(client, 0) != ESP_OK) {
        if (client) esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    int status = esp_http_client_fetch_headers(client) >= 0 ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    return status >= 200 && status < 300 ? ESP_OK : ESP_FAIL;
}
