#pragma once

#include <stdbool.h>
#include "bsp_button.h"

void inspiration_ui_start(void);
void inspiration_ui_open_library(void);
bool inspiration_ui_handle_key(bsp_btn_t button, bsp_btn_ev_t event);
