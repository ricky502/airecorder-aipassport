#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Starts STA using credentials already persisted by BLUFI/original firmware.
esp_err_t inspiration_wifi_init(void);
bool inspiration_wifi_ready(void);

