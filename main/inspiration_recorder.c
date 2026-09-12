#include "inspiration_recorder.h"

#include <stdio.h>

#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "inspiration_adpcm.h"
#include "inspiration_chunk_queue.h"
#include "inspiration_config.h"
#include "inspiration_storage.h"
#include "inspiration_upload.h"
#include "inspiration_wifi.h"

#define RECORDER_BLOCK_SAMPLES 320U
#define RECORDER_PACKET_BYTES (INSPIRATION_ADPCM_HEADER_BYTES + RECORDER_BLOCK_SAMPLES / 2U)
#define RECORDER_QUEUE_LENGTH 4U
#define UPLOADER_TASK_STACK 4096U

typedef enum { RECORDER_TOGGLE, RECORDER_STOP } recorder_event_t;

static QueueHandle_t s_events;
static inspiration_state_t s_state;
static inspiration_chunk_queue_t s_chunks;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t s_peak;
static uint32_t s_next_sequence = 1;
static uint32_t s_chunk_samples;
static FILE *s_chunk_file;

static void state_fail(void)
{
    portENTER_CRITICAL(&s_lock);
    inspiration_state_fail(&s_state);
    portEXIT_CRITICAL(&s_lock);
}

static bool state_is_recording(void)
{
    bool recording;
    portENTER_CRITICAL(&s_lock);
    recording = s_state.phase == INSPIRATION_RECORDING;
    portEXIT_CRITICAL(&s_lock);
    return recording;
}

static bool state_is_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_lock);
    active = s_state.phase == INSPIRATION_RECORDING || s_state.phase == INSPIRATION_PAUSED;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

static esp_err_t open_chunk(void)
{
    s_chunk_samples = 0;
    return inspiration_storage_open_chunk(s_next_sequence, &s_chunk_file);
}

static bool finalize_chunk(void)
{
    if (!s_chunk_file) return true;
    long bytes = ftell(s_chunk_file);
    bool closed = fclose(s_chunk_file) == 0;
    s_chunk_file = NULL;
    if (!closed || bytes <= 0 ||
        !inspiration_chunk_queue_enqueue(&s_chunks, s_next_sequence, (uint32_t)bytes)) return false;
    s_next_sequence++;
    portENTER_CRITICAL(&s_lock);
    inspiration_state_chunk_queued(&s_state);
    portEXIT_CRITICAL(&s_lock);
    return true;
}

static void service_upload_window(void)
{
    if (!s_chunks.count || !inspiration_upload_configured()) return;
    if (!inspiration_wifi_ready()) {
        inspiration_wifi_begin_upload_window();
        return;
    }
    inspiration_upload_next(&s_chunks);
    if (!s_chunks.count) inspiration_wifi_end_upload_window();
}

// Network I/O may block for seconds.  It must never run from recorder_task,
// where a 40 ms PCM block deadline protects against microphone dropouts.
static void uploader_task(void *unused)
{
    (void)unused;
    for (;;) {
        service_upload_window();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static bool start_recording(void)
{
    if (bsp_audio_set_format(INSPIRATION_SAMPLE_RATE_HZ, INSPIRATION_PCM_BITS, 1) != ESP_OK ||
        open_chunk() != ESP_OK) return false;
    portENTER_CRITICAL(&s_lock);
    inspiration_state_toggle_recording(&s_state);
    portEXIT_CRITICAL(&s_lock);
    return true;
}

static void process_event(recorder_event_t event)
{
    if (event == RECORDER_TOGGLE) {
        portENTER_CRITICAL(&s_lock);
        inspiration_phase_t phase = s_state.phase;
        portEXIT_CRITICAL(&s_lock);
        if (phase == INSPIRATION_IDLE) {
            if (!start_recording()) state_fail();
        } else if (phase == INSPIRATION_RECORDING || phase == INSPIRATION_PAUSED) {
            portENTER_CRITICAL(&s_lock);
            inspiration_state_toggle_recording(&s_state);
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (event == RECORDER_STOP && state_is_active()) {
        portENTER_CRITICAL(&s_lock);
        inspiration_state_stop(&s_state);
        portEXIT_CRITICAL(&s_lock);
        if (!finalize_chunk()) state_fail();
        else {
            portENTER_CRITICAL(&s_lock);
            inspiration_state_finalizing_complete(&s_state);
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

static void recorder_task(void *unused)
{
    (void)unused;
    int16_t pcm[RECORDER_BLOCK_SAMPLES];
    uint8_t packet[RECORDER_PACKET_BYTES];
    for (;;) {
        recorder_event_t event;
        if (!state_is_recording()) {
            if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(200)) == pdTRUE) process_event(event);
            continue;
        }
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) { state_fail(); continue; }
        uint16_t peak = 0;
        for (size_t i = 0; i < RECORDER_BLOCK_SAMPLES; i++) {
            int32_t amplitude = pcm[i] < 0 ? -(int32_t)pcm[i] : pcm[i];
            if ((uint32_t)amplitude > peak) peak = (uint16_t)amplitude;
        }
        size_t packet_bytes = 0;
        if (!inspiration_adpcm_encode_packet(pcm, RECORDER_BLOCK_SAMPLES, packet, sizeof(packet),
                                             &packet_bytes) ||
            fwrite(packet, 1, packet_bytes, s_chunk_file) != packet_bytes) { state_fail(); continue; }
        s_chunk_samples += RECORDER_BLOCK_SAMPLES;
        portENTER_CRITICAL(&s_lock);
        s_peak = peak;
        inspiration_state_tick(&s_state, RECORDER_BLOCK_SAMPLES * 1000U / INSPIRATION_SAMPLE_RATE_HZ);
        portEXIT_CRITICAL(&s_lock);
        if (s_chunk_samples >= INSPIRATION_CHUNK_SECONDS * INSPIRATION_SAMPLE_RATE_HZ) {
            if (!finalize_chunk() || open_chunk() != ESP_OK) state_fail();
            else if (inspiration_upload_configured()) inspiration_wifi_begin_upload_window();
        }
    }
}

esp_err_t inspiration_recorder_init(void)
{
    inspiration_state_init(&s_state);
    inspiration_chunk_queue_init(&s_chunks);
    esp_err_t err = inspiration_storage_init();
    if (err != ESP_OK) return err;
    inspiration_upload_load_config();
    s_events = xQueueCreate(RECORDER_QUEUE_LENGTH, sizeof(recorder_event_t));
    if (!s_events) return ESP_ERR_NO_MEM;
    if (xTaskCreate(recorder_task, "inspiration_rec", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(uploader_task, "inspiration_upload", UPLOADER_TASK_STACK, NULL, 3, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}

void inspiration_recorder_toggle(void)
{
    const recorder_event_t event = RECORDER_TOGGLE;
    if (s_events) xQueueSend(s_events, &event, 0);
}

void inspiration_recorder_stop(void)
{
    const recorder_event_t event = RECORDER_STOP;
    if (s_events) xQueueSend(s_events, &event, 0);
}

void inspiration_recorder_set_wifi_ready(bool ready)
{
    portENTER_CRITICAL(&s_lock);
    s_state.wifi_ready = ready;
    portEXIT_CRITICAL(&s_lock);
}

void inspiration_recorder_clear_stop_indicator(void)
{
    portENTER_CRITICAL(&s_lock);
    inspiration_state_clear_stop_indicator(&s_state);
    portEXIT_CRITICAL(&s_lock);
}

void inspiration_recorder_snapshot(inspiration_state_t *state_out, uint16_t *peak_out)
{
    portENTER_CRITICAL(&s_lock);
    if (state_out) *state_out = s_state;
    if (peak_out) *peak_out = s_peak;
    portEXIT_CRITICAL(&s_lock);
}
