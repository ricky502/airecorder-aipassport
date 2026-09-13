#include "inspiration_upload.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_mac.h"
#include "mdns.h"
#include "nvs.h"
#include "esp_random.h"
#include "inspiration_config.h"
#include "inspiration_storage.h"

#define UPLOAD_NAMESPACE "inspiration"
#define UPLOAD_ENDPOINT_KEY "endpoint"
#define UPLOAD_SESSION_KEY "session"
#define UPLOAD_RECEIVER_ID_KEY "receiver_id"
#define RECEIVER_SERVICE "_aipassport"
#define RECEIVER_PROTO "_tcp"

static char s_endpoint[128];
static char s_session[40];
static char s_receiver_id[40];

esp_err_t inspiration_upload_load_config(void)
{
    s_endpoint[0] = '\0';
    s_session[0] = '\0';
    s_receiver_id[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(UPLOAD_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        snprintf(s_endpoint, sizeof(s_endpoint), "%s", INSPIRATION_DEFAULT_BACKEND_ENDPOINT);
        return ESP_OK;
    }
    size_t endpoint_size = sizeof(s_endpoint);
    size_t session_size = sizeof(s_session);
    size_t receiver_id_size = sizeof(s_receiver_id);
    esp_err_t err = nvs_get_str(nvs, UPLOAD_ENDPOINT_KEY, s_endpoint, &endpoint_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    // A missing session or receiver identity is normal before first use.
    esp_err_t session_err = nvs_get_str(nvs, UPLOAD_SESSION_KEY, s_session, &session_size);
    if (session_err == ESP_ERR_NVS_NOT_FOUND) session_err = ESP_OK;
    esp_err_t receiver_err = nvs_get_str(nvs, UPLOAD_RECEIVER_ID_KEY, s_receiver_id, &receiver_id_size);
    if (receiver_err == ESP_ERR_NVS_NOT_FOUND) receiver_err = ESP_OK;
    nvs_close(nvs);
    if (err != ESP_OK) return err;
    if (session_err != ESP_OK) return session_err;
    if (receiver_err != ESP_OK) return receiver_err;
    if (!s_endpoint[0]) snprintf(s_endpoint, sizeof(s_endpoint), "%s", INSPIRATION_DEFAULT_BACKEND_ENDPOINT);
    return ESP_OK;
}

esp_err_t inspiration_upload_set_endpoint(const char *endpoint)
{
    if (!endpoint || strlen(endpoint) >= sizeof(s_endpoint)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(UPLOAD_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = endpoint[0] ? nvs_set_str(nvs, UPLOAD_ENDPOINT_KEY, endpoint) : nvs_erase_key(nvs, UPLOAD_ENDPOINT_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND && !endpoint[0]) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) snprintf(s_endpoint, sizeof(s_endpoint), "%s", endpoint);
    return err;
}

static esp_err_t save_receiver_id(const char *receiver_id)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(UPLOAD_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_str(nvs, UPLOAD_RECEIVER_ID_KEY, receiver_id);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t inspiration_upload_discover_receiver(void)
{
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(RECEIVER_SERVICE, RECEIVER_PROTO, 1800, 8, &results);
    if (err != ESP_OK) return err;
    esp_err_t found = ESP_ERR_NOT_FOUND;
    for (mdns_result_t *result = results; result; result = result->next) {
        if (result->port == 0 || result->ip_protocol != MDNS_IP_PROTOCOL_V4 || !result->addr) continue;
        const char *receiver_id = NULL;
        for (size_t i = 0; i < result->txt_count; ++i) {
            if (result->txt[i].key && strcmp(result->txt[i].key, "id") == 0) {
                receiver_id = result->txt[i].value;
                break;
            }
        }
        if (!receiver_id || !receiver_id[0] || strlen(receiver_id) >= sizeof(s_receiver_id)) continue;
        if (s_receiver_id[0] && strcmp(s_receiver_id, receiver_id) != 0) continue;
        char address[48] = {0};
        ip4addr_ntoa_r((const ip4_addr_t *)&result->addr->addr.u_addr.ip4, address, sizeof(address));
        if (!address[0]) continue;
        int written = snprintf(s_endpoint, sizeof(s_endpoint), "http://%s:%u", address, result->port);
        if (written < 0 || (size_t)written >= sizeof(s_endpoint)) continue;
        if (!s_receiver_id[0]) {
            if (save_receiver_id(receiver_id) != ESP_OK) { s_endpoint[0] = '\0'; continue; }
            snprintf(s_receiver_id, sizeof(s_receiver_id), "%s", receiver_id);
        }
        found = ESP_OK;
        break;
    }
    mdns_query_results_free(results);
    return found;
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
