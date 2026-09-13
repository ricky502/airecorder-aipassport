#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Starts STA using credentials already persisted by BLUFI/original firmware.
esp_err_t inspiration_wifi_init(void);
esp_err_t inspiration_wifi_begin_upload_window(void);
void inspiration_wifi_end_upload_window(void);
bool inspiration_wifi_ready(void);
// Long-press UP opens a temporary AP setup page; it never clears Passport data.
void inspiration_wifi_begin_setup(void);
