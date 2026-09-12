#include "inspiration_ui.h"

#include <stdio.h>
#include "bsp_battery.h"
#include "inspiration_recorder.h"
#include "inspiration_storage.h"
#include "inspiration_wifi.h"
#include "lvgl.h"

#define INK 0x102A33
#define LIME 0xC7F36B
#define MINT 0x58D6BE
#define RED 0xFF5E64
#define AMBER 0xF6C75A

static lv_obj_t *s_status, *s_footer, *s_meter[18];
static uint8_t s_stop_ticks;

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

static void tick(lv_timer_t *timer)
{
    (void)timer;
    inspiration_state_t state;
    uint16_t peak;
    inspiration_recorder_snapshot(&state, &peak);
    inspiration_recorder_set_wifi_ready(inspiration_wifi_ready());
    uint32_t color = MINT;
    const char *word = "";
    if (state.phase == INSPIRATION_RECORDING) { color = RED; word = "● REC"; }
    else if (state.phase == INSPIRATION_PAUSED) { color = AMBER; word = "Ⅱ PAUSE"; }
    else if (state.stop_indicator) { color = AMBER; word = "■"; if (++s_stop_ticks > 8) inspiration_recorder_clear_stop_indicator(); }
    else s_stop_ticks = 0;
    lv_label_set_text(s_status, word);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    for (int i = 0; i < 18; i++) {
        int height = (state.phase == INSPIRATION_RECORDING) ?
            2 + (int)((peak >> 10) + (uint16_t)(i * 3)) % 19 : 2;
        lv_obj_set_height(s_meter[i], height);
        lv_obj_set_y(s_meter[i], 270 - height);
        lv_obj_set_style_bg_color(s_meter[i], lv_color_hex(color), 0);
    }
    int battery = bsp_battery_soc();
    size_t total = 0, used = 0;
    inspiration_storage_info(&total, &used);
    size_t free_bytes = total > used ? total - used : 0;
    unsigned remaining = (unsigned)(free_bytes / 4096U);
    char battery_text[8];
    if (battery < 0) snprintf(battery_text, sizeof(battery_text), "--%%");
    else snprintf(battery_text, sizeof(battery_text), "%d%%", battery);
    lv_label_set_text_fmt(s_footer, "WIFI %s     CACHE %02u:%02u     BAT %s",
                          state.wifi_ready ? "READY" : "OFFLINE",
                          remaining / 60U, remaining % 60U, battery_text);
}

void inspiration_ui_start(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_t *top = lv_label_create(screen);
    lv_label_set_text(top, "--:--  ·  INSPIRATION                         BAT --%");
    lv_obj_set_pos(top, 12, 10);
    lv_obj_set_style_text_color(top, lv_color_hex(LIME), 0);
    box(screen, 12, 42, 216, 188, 0x163B45);
    lv_obj_t *placeholder = lv_label_create(screen);
    lv_label_set_text(placeholder, "YOUR LITTLE WORLD\n\nreserved for a mood card,\nan electronic pet, or a quiet thought.");
    lv_obj_center(placeholder);
    lv_obj_set_style_text_align(placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(placeholder, lv_color_hex(LIME), 0);
    s_status = lv_label_create(screen);
    lv_obj_set_pos(s_status, 13, 244);
    for (int i = 0; i < 18; i++) s_meter[i] = box(screen, 68 + i * 8, 268, 4, 2, MINT);
    s_footer = lv_label_create(screen);
    lv_obj_set_pos(s_footer, 12, 293);
    lv_obj_set_style_text_color(s_footer, lv_color_hex(LIME), 0);
    lv_screen_load(screen);
    lv_timer_create(tick, 120, NULL);
}
