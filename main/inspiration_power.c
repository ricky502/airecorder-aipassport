#include "inspiration_power.h"

#include "bsp_audio.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "inspiration_recorder.h"
#include "inspiration_wifi.h"

#define POWER_DIM_AFTER_MS       (60U * 1000U)
#define POWER_OFF_AFTER_MS       (3U * 60U * 1000U)
#define POWER_SLEEP_AFTER_MS     (10U * 60U * 1000U)
#define POWER_DIM_LEVEL          20U
#define POWER_RECORDING_LEVEL    18U

static const char *TAG = "inspiration_power";
static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_last_activity_ms;
static uint8_t s_stage;

static uint32_t last_activity(void)
{
    uint32_t value;
    portENTER_CRITICAL(&s_lock);
    value = s_last_activity_ms;
    portEXIT_CRITICAL(&s_lock);
    return value;
}

void inspiration_power_note_activity(void)
{
    portENTER_CRITICAL(&s_lock);
    s_last_activity_ms = esp_log_timestamp();
    s_stage = 0;
    portEXIT_CRITICAL(&s_lock);
    bsp_display_backlight(100);
}

static bool recorder_active(void)
{
    inspiration_state_t state;
    inspiration_recorder_snapshot(&state, NULL);
    return state.phase == INSPIRATION_RECORDING ||
           state.phase == INSPIRATION_PAUSED;
}

static bool recorder_busy(void)
{
    return recorder_active() || inspiration_recorder_is_playing();
}

static void enter_light_sleep(void)
{
    if (recorder_busy() || inspiration_wifi_ready()) return;
    // The three keys share GPIO0 through an ADC ladder. A GPIO level wake on
    // that same pin is not reliable: resistor tolerance and ADC noise can
    // wake the ESP32-C3 and look like a phantom click. Use a deterministic
    // screen-off idle state instead; the button component remains active and
    // a debounced real gesture calls inspiration_power_note_activity().
    inspiration_wifi_end_upload_window();
    bsp_audio_suspend();
    bsp_display_backlight(0);
    s_stage = 2;
    ESP_LOGI(TAG, "待机级别 3: 关闭背光、音频和 Wi-Fi，等待按键");
}

static void power_task(void *unused)
{
    (void)unused;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t elapsed = esp_log_timestamp() - last_activity();
        if (recorder_active()) {
            // Recording is normally hands-free. Keep a visible low-light
            // status screen rather than treating an active recording as user
            // activity and holding the backlight at full power.
            if (s_stage != 3) {
                bsp_display_backlight(POWER_RECORDING_LEVEL);
                s_stage = 3;
                ESP_LOGI(TAG, "录音中: 背光保持 %u%%", POWER_RECORDING_LEVEL);
            }
            continue;
        }
        if (inspiration_recorder_is_playing()) {
            if (s_stage != 0) inspiration_power_note_activity();
            continue;
        }
        if (elapsed >= POWER_SLEEP_AFTER_MS) {
            enter_light_sleep();
            continue;
        }
        if (elapsed >= POWER_OFF_AFTER_MS && s_stage < 2) {
            // Do not cut a live upload. If the window is only connecting (not
            // ready), close it here so the radio does not remain active.
            if (!inspiration_wifi_ready()) {
                inspiration_wifi_end_upload_window();
                bsp_audio_suspend();
                bsp_display_backlight(0);
                s_stage = 2;
                ESP_LOGI(TAG, "待机级别 2: 背光、音频已关闭");
            }
        }
        else if (elapsed >= POWER_DIM_AFTER_MS && s_stage < 1) {
            bsp_display_backlight(POWER_DIM_LEVEL);
            s_stage = 1;
            ESP_LOGI(TAG, "待机级别 1: 背光降至 %u%%", POWER_DIM_LEVEL);
        }
    }
}

void inspiration_power_init(void)
{
    s_last_activity_ms = esp_log_timestamp();
    s_stage = 0;
    if (xTaskCreate(power_task, "inspiration_power", 3072, NULL, 2, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "待机任务创建失败");
    }
}
