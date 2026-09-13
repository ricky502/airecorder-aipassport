#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <stdint.h>

#include "esp_err.h"
#include "inspiration_state.h"

esp_err_t inspiration_recorder_init(void);
void inspiration_recorder_toggle(void);
void inspiration_recorder_stop(void);
void inspiration_recorder_set_wifi_ready(bool ready);
void inspiration_recorder_clear_stop_indicator(void);
void inspiration_recorder_snapshot(inspiration_state_t *state_out, uint16_t *peak_out);
void inspiration_recorder_waveform(uint8_t levels_out[12]);
bool inspiration_recorder_play_chunk(uint32_t sequence);
void inspiration_recorder_stop_playback(void);
bool inspiration_recorder_is_playing(void);
