#include "inspiration_wifi.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static bool s_ready;
static bool s_started;
static bool s_sntp_started;

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
