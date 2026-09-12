// SPIFFS-backed persistence for recorder chunks. This mounts voicefs only.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

esp_err_t inspiration_storage_init(void);
esp_err_t inspiration_storage_open_chunk(uint32_t sequence, FILE **file_out);
esp_err_t inspiration_storage_open_chunk_read(uint32_t sequence, FILE **file_out);
esp_err_t inspiration_storage_delete_chunk(uint32_t sequence);
esp_err_t inspiration_storage_info(size_t *total_bytes, size_t *used_bytes);

