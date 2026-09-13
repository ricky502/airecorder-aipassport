#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "inspiration_recorder.h"
#include "inspiration_ui.h"
#include "inspiration_wifi.h"

static const char *TAG = "inspiration";

// Button callbacks stay short; I2S and Flash work run in the recorder worker.
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (button == BSP_BTN_UP && event == BSP_BTN_LONG) {
        inspiration_wifi_begin_setup();
        return;
    }
    if (event != BSP_BTN_CLICK) return;
    if (button == BSP_BTN_OK) inspiration_recorder_toggle();
    if (button == BSP_BTN_DOWN) inspiration_recorder_stop();
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
}
