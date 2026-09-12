#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "inspiration_state.h"

esp_err_t inspiration_recorder_init(void);
void inspiration_recorder_toggle(void);
void inspiration_recorder_stop(void);
void inspiration_recorder_snapshot(inspiration_state_t *state_out, uint16_t *peak_out);

