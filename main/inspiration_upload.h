#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "inspiration_chunk_queue.h"

// Endpoint and the active recording ID live in NVS.  The recording ID remains
// stable across a reboot so offline chunks cannot be mixed into another memo.
esp_err_t inspiration_upload_load_config(void);
esp_err_t inspiration_upload_set_endpoint(const char *endpoint);
bool inspiration_upload_configured(void);
esp_err_t inspiration_upload_begin_session(void);
bool inspiration_upload_session_active(void);
esp_err_t inspiration_upload_chunk(const inspiration_chunk_t *chunk);
esp_err_t inspiration_upload_complete(void);
esp_err_t inspiration_upload_clear_session(void);
