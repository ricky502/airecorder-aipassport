#include "inspiration_wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inspiration_config.h"
#include "inspiration_upload.h"
#include "nvs_flash.h"

static bool s_ready;
static bool s_started;
static bool s_sntp_started;
static bool s_setup_starting;
static httpd_handle_t s_setup_server;

static const char SETUP_PAGE[] =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Passport setup</title><style>body{font:16px system-ui;max-width:32em;margin:2em auto;padding:0 1em}"
    "input{width:100%;box-sizing:border-box;margin:.4em 0 1.2em;padding:.7em}button{padding:.8em 1.2em}</style>"
    "<h2>AI Passport · 灵感记忆卡</h2><p>填好后卡片会重启并保存设置。</p>"
    "<form method=post action=/save><label>Wi-Fi 名称</label><input name=ssid maxlength=32 required>"
    "<label>Wi-Fi 密码</label><input name=pass type=password maxlength=64>"
    "<label>中转地址（可留空，自动发现本电脑）</label><input name=endpoint type=url maxlength=127 value='" INSPIRATION_DEFAULT_BACKEND_ENDPOINT "' placeholder='留空即可自动发现'>"
    "<button>保存并重启</button></form>";

static esp_err_t setup_page_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, SETUP_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t setup_save_handler(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 300) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid form");
        return ESP_FAIL;
    }
    char body[301] = {0}, ssid[33] = {0}, password[65] = {0}, endpoint[128] = {0};
    wifi_config_t station = {0};
    int received = httpd_req_recv(request, body, request->content_len);
    if (received != request->content_len ||
        httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK ||
        httpd_query_key_value(body, "pass", password, sizeof(password)) != ESP_OK ||
        !ssid[0] || strlen(ssid) > sizeof(station.sta.ssid) ||
        strlen(password) > sizeof(station.sta.password) || (password[0] && strlen(password) < 8)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "check Wi-Fi name and password");
        return ESP_FAIL;
    }
    memcpy(station.sta.ssid, ssid, strlen(ssid));
    memcpy(station.sta.password, password, strlen(password));
    station.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK ||
        inspiration_upload_set_endpoint(endpoint) != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(request, "Saved. Restarting Passport…");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
    return ESP_OK;
}

static void setup_task(void *unused)
{
    (void)unused;
    if (s_started) inspiration_wifi_end_upload_window();
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Passport-%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "inspireme");
    ap.ap.ssid_len = strlen((const char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK &&
        esp_wifi_set_config(WIFI_IF_AP, &ap) == ESP_OK && esp_wifi_start() == ESP_OK) {
        s_started = true;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 2;
        if (httpd_start(&s_setup_server, &config) == ESP_OK) {
            httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = setup_page_handler};
            httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = setup_save_handler};
            httpd_register_uri_handler(s_setup_server, &page);
            httpd_register_uri_handler(s_setup_server, &save);
        }
    }
    s_setup_starting = false;
    vTaskDelete(NULL);
}

static void start_clock_sync(void)
{
    if (s_sntp_started) return;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    s_sntp_started = true;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ready = false;
        // Reconnect only while an upload window owns the radio.  This keeps
        // retries alive on a weak home network without leaving Wi-Fi on in
        // standby.
        if (s_started) esp_wifi_connect();
    }
    if (id == IP_EVENT_STA_GOT_IP) {
        s_ready = true;
        start_clock_sync();
    }
}

esp_err_t inspiration_wifi_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Do not erase NVS: it may hold Passport identity and provisioned data.
        return err;
    }
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    return err;
}

esp_err_t inspiration_wifi_begin_upload_window(void)
{
    if (s_started) return ESP_OK;
    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK) s_started = true;
    return err;
}

void inspiration_wifi_end_upload_window(void)
{
    if (!s_started) return;
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_ready = false;
    s_started = false;
}

bool inspiration_wifi_ready(void) { return s_ready; }

void inspiration_wifi_begin_setup(void)
{
    if (s_setup_starting || s_setup_server) return;
    s_setup_starting = true;
    if (xTaskCreate(setup_task, "passport_setup", 4096, NULL, 3, NULL) != pdPASS) s_setup_starting = false;
}
