#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "inspiration_state.h"

esp_err_t inspiration_recorder_init(void);
void inspiration_recorder_toggle(void);
void inspiration_recorder_stop(void);
void inspiration_recorder_set_wifi_ready(bool ready);
bool inspiration_recorder_upload_active(void);
void inspiration_recorder_clear_stop_indicator(void);
void inspiration_recorder_snapshot(inspiration_state_t *state_out, uint16_t *peak_out);
void inspiration_recorder_waveform(uint8_t levels_out[12]);
bool inspiration_recorder_play_chunk(uint32_t sequence);
void inspiration_recorder_stop_playback(void);
bool inspiration_recorder_is_playing(void);
uint8_t inspiration_recorder_adjust_playback_volume(int delta);
uint8_t inspiration_recorder_playback_volume(void);
void inspiration_recorder_playback_progress(uint32_t *elapsed_ms_out, uint32_t *duration_ms_out);
bool inspiration_recorder_delete_chunk(uint32_t sequence);

// Pause background transfers while the local library is open, so a selected
// clip cannot be claimed by the uploader halfway through deletion.
void inspiration_recorder_set_library_active(bool active);
