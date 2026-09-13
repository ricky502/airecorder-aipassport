#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "inspiration_recorder.h"
#include "inspiration_power.h"
#include "inspiration_ui.h"
#include "inspiration_wifi.h"

static const char *TAG = "inspiration";
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_event_t;
static QueueHandle_t s_key_events;

static void key_task(void *unused)
{
    (void)unused;
    key_event_t key;
    for (;;) {
        if (xQueueReceive(s_key_events, &key, portMAX_DELAY) != pdTRUE) continue;
        bool handled = false;
        if (bsp_lvgl_lock(200)) {
            handled = inspiration_ui_handle_key(key.button, key.event);
            bsp_lvgl_unlock();
        }
        if (handled) continue;
        if (key.button == BSP_BTN_UP && key.event == BSP_BTN_LONG) {
            inspiration_wifi_begin_setup();
            continue;
        }
        if (key.event != BSP_BTN_CLICK) continue;
        if (key.button == BSP_BTN_OK) inspiration_recorder_toggle();
        if (key.button == BSP_BTN_DOWN) inspiration_recorder_stop();
        if (key.button == BSP_BTN_UP && bsp_lvgl_lock(200)) {
            inspiration_ui_open_library();
            bsp_lvgl_unlock();
        }
    }
}

// This is called by the button component's timer task. Queue only: rendering,
// filesystem and networking must not hold up the ADC key scanner.
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    inspiration_power_note_activity();
    if (!s_key_events) return;
    const key_event_t key = { .button = button, .event = event };
    xQueueSend(s_key_events, &key, 0);
}

void app_main(void)
{
    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display initialization failed");
        return;
    }
    bsp_display_backlight(100);
    bsp_battery_init();
    if (bsp_audio_init() != ESP_OK || bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "recorder initialization failed");
        return;
    }
    // NVS is initialized here before the recorder loads its saved endpoint and
    // session.  A radio failure never prevents offline recording.
    if (inspiration_wifi_init() != ESP_OK) ESP_LOGW(TAG, "Wi-Fi unavailable; offline cache remains active");
    if (inspiration_recorder_init() != ESP_OK) {
        ESP_LOGE(TAG, "recorder initialization failed");
        return;
    }
    if (bsp_lvgl_lock(1000)) {
        inspiration_ui_start();
        bsp_lvgl_unlock();
    }
    s_key_events = xQueueCreate(12, sizeof(key_event_t));
    if (!s_key_events || xTaskCreate(key_task, "inspiration_keys", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "key handler initialization failed");
    }
    inspiration_power_init();
}
