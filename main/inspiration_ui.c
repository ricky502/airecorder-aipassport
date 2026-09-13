#include "inspiration_ui.h"

#include <stdio.h>
#include <time.h>
#include "bsp_battery.h"
#include "inspiration_recorder.h"
#include "inspiration_storage.h"
#include "inspiration_wifi.h"
#include "meditation_hours_images.h"
#include "lvgl.h"

#define INK 0x102A33
#define LIME 0xC7F36B
#define MINT 0x58D6BE
#define RED 0xFF5E64
#define AMBER 0xF6C75A

static lv_obj_t *s_home, *s_library, *s_library_text;
static lv_obj_t *s_top, *s_status, *s_footer, *s_illustration, *s_meter[12];
static uint8_t s_stop_ticks;
static int s_hour_frame = 0;
static uint32_t s_library_chunks[16];
static size_t s_library_count, s_library_selected;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static bool collect_library_chunk(uint32_t sequence, uint32_t bytes, void *unused)
{
    (void)bytes; (void)unused;
    if (s_library_count >= sizeof(s_library_chunks) / sizeof(s_library_chunks[0])) return false;
    s_library_chunks[s_library_count++] = sequence;
    return true;
}

static void refresh_library(void)
{
    s_library_count = 0;
    inspiration_storage_for_each_chunk(collect_library_chunk, NULL);
    for (size_t i = 0; i < s_library_count; i++) {
        for (size_t j = i + 1; j < s_library_count; j++) {
            if (s_library_chunks[j] < s_library_chunks[i]) {
                uint32_t swap = s_library_chunks[i];
                s_library_chunks[i] = s_library_chunks[j];
                s_library_chunks[j] = swap;
            }
        }
    }
    if (s_library_selected >= s_library_count) s_library_selected = 0;
}

static void render_library(void)
{
    if (!s_library_text) return;
    char text[420];
    int offset = snprintf(text, sizeof(text), "VOICE INBOX  %u\n", (unsigned)s_library_count);
    if (!s_library_count) offset += snprintf(text + offset, sizeof(text) - (size_t)offset, "\nNo offline clips.\n");
    for (size_t i = 0; i < s_library_count && offset > 0 && (size_t)offset < sizeof(text); i++) {
        offset += snprintf(text + offset, sizeof(text) - (size_t)offset, "%s #%06u  about 1 min\n",
                           i == s_library_selected ? ">" : " ", (unsigned)s_library_chunks[i]);
    }
    if (offset > 0 && (size_t)offset < sizeof(text)) {
        if (inspiration_recorder_is_playing()) {
            snprintf(text + offset, sizeof(text) - (size_t)offset,
                     "\nPLAYING  VOL %u%%\nUP +  DOWN -  OK stop\nHold UP to return",
                     (unsigned)inspiration_recorder_playback_volume());
        } else {
            snprintf(text + offset, sizeof(text) - (size_t)offset,
                     "\nnot uploaded yet\nUP/DOWN choose  OK listen\nHold UP to return");
        }
    }
    lv_label_set_text(s_library_text, text);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    inspiration_state_t state;
    uint16_t peak;
    uint8_t waveform[12] = {0};
    inspiration_recorder_snapshot(&state, &peak);
    inspiration_recorder_waveform(waveform);
    inspiration_recorder_set_wifi_ready(inspiration_wifi_ready());
    if (s_library) {
        render_library();
        return;
    }
    uint32_t color = MINT;
    const char *word = "";
    if (state.phase == INSPIRATION_RECORDING) { color = RED; word = "● REC"; }
    else if (state.phase == INSPIRATION_PAUSED) { color = AMBER; word = "Ⅱ PAUSE"; }
    else if (state.stop_indicator) { color = AMBER; word = "■"; if (++s_stop_ticks > 8) inspiration_recorder_clear_stop_indicator(); }
    else s_stop_ticks = 0;
    lv_label_set_text(s_status, word);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    for (int i = 0; i < 12; i++) {
        int height = state.phase == INSPIRATION_RECORDING ? waveform[i] : 2;
        lv_obj_set_height(s_meter[i], height);
        lv_obj_set_y(s_meter[i], 276 - height);
        lv_obj_set_style_bg_color(s_meter[i], lv_color_hex(color), 0);
    }
    int battery = bsp_battery_soc();
    size_t total = 0, used = 0;
    inspiration_storage_info(&total, &used);
    size_t free_bytes = total > used ? total - used : 0;
    unsigned remaining = (unsigned)(free_bytes / 4096U);
    char battery_text[8];
    if (battery < 0) snprintf(battery_text, sizeof(battery_text), "--%%");
    else {
        unsigned display_battery = (unsigned)battery;
        if (display_battery > 100U) display_battery = 100U;
        snprintf(battery_text, sizeof(battery_text), "%u%%", display_battery);
    }
    lv_label_set_text_fmt(s_footer, "WIFI %s    CACHE %02u:%02u",
                          state.wifi_ready ? "READY" : "OFFLINE",
                          remaining / 60U, remaining % 60U);
    time_t now = time(NULL);
    struct tm local = {0};
    if (now > 1704067200 && localtime_r(&now, &local)) {
        int frame = local.tm_hour % MEDITATION_HOUR_FRAME_COUNT;
        if (frame != s_hour_frame) {
            lv_image_set_src(s_illustration, &meditation_hour_images[frame]);
            s_hour_frame = frame;
        }
        lv_label_set_text_fmt(s_top, "%02d/%02d  %02d:%02d                         %s",
                              local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, battery_text);
    } else {
        lv_label_set_text_fmt(s_top, "--/--  --:--                         %s", battery_text);
    }
}

void inspiration_ui_start(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    s_home = screen;
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    s_top = lv_label_create(screen);
    lv_obj_set_pos(s_top, 12, 8);
    lv_obj_set_style_text_color(s_top, lv_color_hex(LIME), 0);
    // The clock label ends around y=22.  Keep only a quiet 7 px breath before
    // the main card, so the future visual (the meditation clock) gets nearly
    // the entire upper screen rather than a decorative empty gap.
    s_illustration = lv_image_create(screen);
    lv_image_set_src(s_illustration, &meditation_hour_images[0]);
    lv_obj_set_pos(s_illustration, 12, 29);
    // Store the visual at two-thirds resolution so all 24 hourly scenes fit
    // safely in the 3 MB app partition; the display's scaler restores it to
    // the 216 x 217 card area.
    lv_image_set_scale(s_illustration, 384);
    s_status = lv_label_create(screen);
    lv_obj_set_pos(s_status, 13, 250);
    // A slim, deliberately secondary waveform: audio state should be legible
    // without competing with the main visual card.
    for (int i = 0; i < 12; i++) s_meter[i] = box(screen, 79 + i * 6, 276, 3, 2, MINT);
    s_footer = lv_label_create(screen);
    lv_obj_set_pos(s_footer, 12, 284);
    lv_obj_set_style_text_color(s_footer, lv_color_hex(LIME), 0);
    lv_screen_load(screen);
    lv_timer_create(tick, 120, NULL);
}

void inspiration_ui_open_library(void)
{
    if (s_library) return;
    refresh_library();
    s_library = lv_obj_create(NULL);
    lv_obj_remove_flag(s_library, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_library, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(s_library, 0, 0);
    lv_obj_set_style_pad_all(s_library, 14, 0);
    s_library_text = lv_label_create(s_library);
    lv_obj_set_pos(s_library_text, 14, 18);
    lv_obj_set_style_text_color(s_library_text, lv_color_hex(LIME), 0);
    render_library();
    lv_screen_load(s_library);
}

bool inspiration_ui_handle_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!s_library) return false;
    if (button == BSP_BTN_UP && event == BSP_BTN_LONG) {
        inspiration_recorder_stop_playback();
        lv_screen_load(s_home);
        lv_obj_delete(s_library);
        s_library = NULL;
        s_library_text = NULL;
        return true;
    }
    if (event != BSP_BTN_CLICK) return true;
    if (inspiration_recorder_is_playing()) {
        if (button == BSP_BTN_UP) inspiration_recorder_adjust_playback_volume(10);
        else if (button == BSP_BTN_DOWN) inspiration_recorder_adjust_playback_volume(-10);
        else if (button == BSP_BTN_OK) inspiration_recorder_stop_playback();
        render_library();
        return true;
    }
    if (button == BSP_BTN_UP && s_library_count) {
        s_library_selected = s_library_selected ? s_library_selected - 1U : s_library_count - 1U;
    } else if (button == BSP_BTN_DOWN && s_library_count) {
        s_library_selected = (s_library_selected + 1U) % s_library_count;
    } else if (button == BSP_BTN_OK && s_library_count) {
        inspiration_recorder_play_chunk(s_library_chunks[s_library_selected]);
    }
    render_library();
    return true;
}
