#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Starts STA using credentials already persisted by BLUFI/original firmware.
esp_err_t inspiration_wifi_init(void);
esp_err_t inspiration_wifi_begin_upload_window(void);
void inspiration_wifi_end_upload_window(void);
bool inspiration_wifi_ready(void);
// Power-on time sync: opens one short upload window so SNTP can correct the
// clock restored from NVS, regardless of whether any recording is pending.
void inspiration_wifi_sync_time_at_boot(void);
// Long-press UP opens a temporary AP setup page; it never clears Passport data.
void inspiration_wifi_begin_setup(void);
// Leaves the setup AP (long-press UP again, or the 3-minute timeout) so the
// recorder can resume uploading. Calling this with no setup running is a no-op.
void inspiration_wifi_end_setup(void);
// Drives the setup auto-timeout; safe to call from the UI tick.
void inspiration_wifi_setup_poll(void);
bool inspiration_wifi_setup_active(void);
const char *inspiration_wifi_setup_ssid(void);
const char *inspiration_wifi_setup_password(void);
