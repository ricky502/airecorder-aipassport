#include "inspiration_ui.h"

#include <stdio.h>
#include <time.h>
#include "bsp_battery.h"
#include "inspiration_adpcm.h"
#include "inspiration_config.h"
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
#define PANEL 0x173B47

static lv_obj_t *s_home, *s_library, *s_library_title, *s_library_text, *s_library_cursor;
static lv_obj_t *s_library_actions, *s_library_panel;
static lv_obj_t *s_player_icon, *s_player_volume, *s_player_track;
static lv_obj_t *s_player_fill, *s_player_knob, *s_player_controls;
static lv_obj_t *s_top, *s_status, *s_footer, *s_illustration, *s_meter[12];
static uint8_t s_stop_ticks;
static int s_hour_frame = 0;
static uint32_t s_library_chunks[16];
static uint32_t s_library_bytes[16];
static size_t s_library_count, s_library_selected;
static bool s_delete_confirm;
static bool s_delete_selected;
static const char *s_library_notice;

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
    (void)unused;
    if (s_library_count >= sizeof(s_library_chunks) / sizeof(s_library_chunks[0])) return false;
    s_library_chunks[s_library_count++] = sequence;
    s_library_bytes[s_library_count - 1U] = bytes;
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
                swap = s_library_bytes[i];
                s_library_bytes[i] = s_library_bytes[j];
                s_library_bytes[j] = swap;
            }
        }
    }
    if (s_library_selected >= s_library_count) s_library_selected = 0;
}

static void render_library(void)
{
    if (!s_library_title || !s_library_text || !s_library_cursor || !s_library_actions) return;
    char list[400];
    char actions[160];
    int offset = 0;
    lv_label_set_text_fmt(s_library_title, LV_SYMBOL_AUDIO "  INBOX                         %02u", (unsigned)s_library_count);
    if (!s_library_count) offset = snprintf(list, sizeof(list), "No offline clips.\n");

    // The upper region is deliberately generous: 10 clip rows plus a header
    // and range marker. The controls are rendered in their own bottom region,
    // so they never get pushed away by a long offline list.
    const size_t visible_rows = 10;
    size_t first = s_library_selected > visible_rows / 2U ? s_library_selected - visible_rows / 2U : 0;
    if (s_library_count > visible_rows && first + visible_rows > s_library_count) {
        first = s_library_count - visible_rows;
    }
    size_t end = first + visible_rows;
    if (end > s_library_count) end = s_library_count;
    for (size_t i = first; i < end && offset >= 0 && (size_t)offset < sizeof(list); i++) {
        const unsigned packet_bytes = INSPIRATION_ADPCM_HEADER_BYTES + 320U / 2U;
        const unsigned packets = s_library_bytes[i] / packet_bytes;
        const unsigned packet_ms = 320U * 1000U / INSPIRATION_SAMPLE_RATE_HZ;
        const unsigned seconds = (packets * packet_ms + 999U) / 1000U;
        offset += snprintf(list + offset, sizeof(list) - (size_t)offset,
                           LV_SYMBOL_AUDIO "  #%06u        %02u:%02u\n", (unsigned)s_library_chunks[i],
                           seconds / 60U, seconds % 60U);
    }
    if (s_library_count) {
        int line_height = lv_font_get_line_height(lv_obj_get_style_text_font(s_library_text, LV_PART_MAIN));
        lv_obj_set_pos(s_library_cursor, 12, 34 + (int)(s_library_selected - first) * line_height);
        lv_obj_remove_flag(s_library_cursor, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_library_cursor, LV_OBJ_FLAG_HIDDEN);
    }
    bool playing = inspiration_recorder_is_playing();
    if (playing) {
        uint8_t volume = inspiration_recorder_playback_volume();
        int fill_width = (int)(volume * 116U / 100U);
        if (fill_width < 3) fill_width = 3;
        lv_obj_add_flag(s_library_actions, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_player_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_player_volume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_player_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_player_fill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_player_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_player_controls, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_player_fill, fill_width);
        lv_obj_set_x(s_player_knob, 52 + fill_width);
        lv_label_set_text_fmt(s_player_volume, "%u%%", (unsigned)volume);
    } else {
        lv_obj_remove_flag(s_library_actions, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_player_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_player_volume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_player_track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_player_fill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_player_knob, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_player_controls, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_delete_confirm && s_library_count) {
        snprintf(actions, sizeof(actions), LV_SYMBOL_WARNING "  #%06u\n%s KEEP\n%s " LV_SYMBOL_TRASH "  DELETE\n" LV_SYMBOL_UP "/" LV_SYMBOL_DOWN " move   " LV_SYMBOL_OK " confirm",
                 (unsigned)s_library_chunks[s_library_selected],
                 s_delete_selected ? " " : ">",
                 s_delete_selected ? ">" : " ");
        lv_obj_set_style_text_color(s_library_actions, lv_color_hex(RED), 0);
    } else {
        snprintf(actions, sizeof(actions), LV_SYMBOL_PLAY "  OK\n" LV_SYMBOL_TRASH "  HOLD OK\n"
                 LV_SYMBOL_LEFT "  HOLD UP");
        lv_obj_set_style_text_color(s_library_actions, lv_color_hex(AMBER), 0);
    }
    lv_label_set_text(s_library_text, list);
    if (!playing) lv_label_set_text(s_library_actions, actions);
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
        // Keep the live waveform short enough to share one visual row with
        // the recorder state, rather than wasting a separate line below it.
        int height = state.phase == INSPIRATION_RECORDING ? (waveform[i] + 1) / 2 : 2;
        lv_obj_set_height(s_meter[i], height);
        lv_obj_set_y(s_meter[i], 277 - height);
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
        int frame = local.tm_hour / 2;
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
    // Full-size source artwork avoids scaler bleed at the card's lower edge.
    lv_obj_set_size(s_illustration, 216, 235);
    lv_obj_set_pos(s_illustration, 12, 29);
    s_status = lv_label_create(screen);
    lv_obj_set_pos(s_status, 13, 266);
    // A slim, deliberately secondary waveform: audio state should be legible
    // without competing with the main visual card.
    for (int i = 0; i < 12; i++) s_meter[i] = box(screen, 79 + i * 6, 275, 3, 2, MINT);
    s_footer = lv_label_create(screen);
    lv_obj_set_pos(s_footer, 12, 284);
    lv_obj_set_style_text_color(s_footer, lv_color_hex(LIME), 0);
    lv_screen_load(screen);
    lv_timer_create(tick, 400, NULL);
}

void inspiration_ui_open_library(void)
{
    if (s_library) return;
    inspiration_recorder_set_library_active(true);
    refresh_library();
    s_delete_confirm = false;
    s_delete_selected = false;
    s_library_notice = NULL;
    s_library = lv_obj_create(NULL);
    lv_obj_remove_flag(s_library, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_library, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(s_library, 0, 0);
    lv_obj_set_style_pad_all(s_library, 0, 0);
    s_library_title = lv_label_create(s_library);
    lv_obj_set_pos(s_library_title, 12, 8);
    lv_obj_set_size(s_library_title, 216, 20);
    lv_obj_set_style_text_color(s_library_title, lv_color_hex(LIME), 0);
    s_library_cursor = lv_label_create(s_library);
    lv_label_set_text(s_library_cursor, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(s_library_cursor, lv_color_hex(MINT), 0);
    s_library_text = lv_label_create(s_library);
    lv_obj_set_pos(s_library_text, 34, 34);
    lv_obj_set_size(s_library_text, 190, 184);
    lv_label_set_long_mode(s_library_text, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_library_text, lv_color_hex(LIME), 0);
    s_library_panel = box(s_library, 8, 226, 224, 88, PANEL);
    lv_obj_set_style_radius(s_library_panel, 12, 0);
    s_library_actions = lv_label_create(s_library);
    lv_obj_set_pos(s_library_actions, 20, 237);
    lv_obj_set_size(s_library_actions, 200, 68);
    lv_label_set_long_mode(s_library_actions, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(s_library_actions, lv_color_hex(AMBER), 0);
    // Playback is deliberately its own fixed layout: one state icon, one
    // horizontal volume control, and one row of physical-key affordances.
    s_player_icon = lv_label_create(s_library_panel);
    lv_label_set_text(s_player_icon, LV_SYMBOL_PLAY);
    lv_obj_set_pos(s_player_icon, 14, 12);
    lv_obj_set_style_text_font(s_player_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_player_icon, lv_color_hex(MINT), 0);
    s_player_volume = lv_label_create(s_library_panel);
    lv_obj_set_pos(s_player_volume, 184, 13);
    lv_obj_set_style_text_color(s_player_volume, lv_color_hex(LIME), 0);
    s_player_track = box(s_library_panel, 56, 20, 116, 4, 0x355B66);
    lv_obj_set_style_radius(s_player_track, 3, 0);
    s_player_fill = box(s_library_panel, 56, 20, 3, 4, MINT);
    lv_obj_set_style_radius(s_player_fill, 3, 0);
    s_player_knob = box(s_library_panel, 55, 18, 8, 8, LIME);
    lv_obj_set_style_radius(s_player_knob, 4, 0);
    s_player_controls = lv_label_create(s_library_panel);
    lv_obj_set_pos(s_player_controls, 56, 42);
    lv_obj_set_size(s_player_controls, 150, 40);
    lv_label_set_text(s_player_controls, LV_SYMBOL_UP "/" LV_SYMBOL_DOWN "   " LV_SYMBOL_STOP "  OK\n" LV_SYMBOL_LEFT "  HOLD UP");
    lv_obj_set_style_text_color(s_player_controls, lv_color_hex(MINT), 0);
    lv_obj_add_flag(s_player_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_player_volume, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_player_track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_player_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_player_knob, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_player_controls, LV_OBJ_FLAG_HIDDEN);
    render_library();
    lv_screen_load(s_library);
}

bool inspiration_ui_handle_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!s_library) return false;
    if (button == BSP_BTN_UP && event == BSP_BTN_LONG) {
        inspiration_recorder_stop_playback();
        inspiration_recorder_set_library_active(false);
        lv_screen_load(s_home);
        lv_obj_delete(s_library);
        s_library = NULL;
        s_library_title = NULL;
        s_library_text = NULL;
        s_library_cursor = NULL;
        s_library_actions = NULL;
        s_library_panel = NULL;
        s_player_icon = NULL;
        s_player_volume = NULL;
        s_player_track = NULL;
        s_player_fill = NULL;
        s_player_knob = NULL;
        s_player_controls = NULL;
        return true;
    }
    if (button == BSP_BTN_OK && event == BSP_BTN_LONG && !inspiration_recorder_is_playing() && s_library_count) {
        s_delete_confirm = true;
        s_delete_selected = false; // Default to the safe, non-destructive choice.
        render_library();
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
    if (s_delete_confirm) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_delete_selected = !s_delete_selected;
            render_library();
            return true;
        }
        if (button == BSP_BTN_OK) {
            if (s_delete_selected) {
                bool deleted = inspiration_recorder_delete_chunk(s_library_chunks[s_library_selected]);
                refresh_library();
                s_library_notice = deleted ? "DELETED" : "BUSY — wait for upload";
            } else {
                s_library_notice = "KEPT";
            }
        }
        s_delete_confirm = false;
        s_delete_selected = false;
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
    s_library_notice = NULL;
    render_library();
    return true;
}
