#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "inspiration_chunk_queue.h"

// Endpoint is persisted separately by the eventual phone/BLUFI setup flow.
// Until configured, offline chunks remain safely queued.
esp_err_t inspiration_upload_load_config(void);
bool inspiration_upload_configured(void);
esp_err_t inspiration_upload_chunk(const inspiration_chunk_t *chunk);
