#include "inspiration_storage.h"

#include <errno.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "esp_spiffs.h"
#include "inspiration_config.h"

#define INSPIRATION_STORAGE_BASE_PATH "/voice"
#define INSPIRATION_STORAGE_MAX_FILES 16

static bool s_initialized;

static int chunk_path(char *path, size_t path_size, uint32_t sequence)
{
    return snprintf(path, path_size, INSPIRATION_STORAGE_BASE_PATH "/%06" PRIu32 ".adpcm",
                    sequence);
}

esp_err_t inspiration_storage_init(void)
{
    if (s_initialized) return ESP_OK;
    const esp_vfs_spiffs_conf_t config = {
        .base_path = INSPIRATION_STORAGE_BASE_PATH,
        .partition_label = INSPIRATION_VOICEFS_LABEL,
        .max_files = INSPIRATION_STORAGE_MAX_FILES,
        // On corruption this formats voicefs only, never cardid or bundled assets.
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err == ESP_OK) s_initialized = true;
    return err;
}

esp_err_t inspiration_storage_open_chunk(uint32_t sequence, FILE **file_out)
{
    if (!s_initialized || !file_out) return ESP_ERR_INVALID_STATE;
    char path[32];
    if (chunk_path(path, sizeof(path), sequence) < 0) return ESP_FAIL;
    FILE *file = fopen(path, "wb");
    if (!file) return errno == ENOSPC ? ESP_ERR_NO_MEM : ESP_FAIL;
    *file_out = file;
    return ESP_OK;
}

esp_err_t inspiration_storage_open_chunk_read(uint32_t sequence, FILE **file_out)
{
    if (!s_initialized || !file_out) return ESP_ERR_INVALID_STATE;
    char path[32];
    if (chunk_path(path, sizeof(path), sequence) < 0) return ESP_FAIL;
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    *file_out = file;
    return ESP_OK;
}

esp_err_t inspiration_storage_delete_chunk(uint32_t sequence)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    char path[32];
    if (chunk_path(path, sizeof(path), sequence) < 0) return ESP_FAIL;
    return remove(path) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t inspiration_storage_info(size_t *total_bytes, size_t *used_bytes)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return esp_spiffs_info(INSPIRATION_VOICEFS_LABEL, total_bytes, used_bytes);
}

esp_err_t inspiration_storage_for_each_chunk(inspiration_storage_chunk_cb_t callback, void *user)
{
    if (!s_initialized || !callback) return ESP_ERR_INVALID_STATE;
    DIR *dir = opendir(INSPIRATION_STORAGE_BASE_PATH);
    if (!dir) return ESP_FAIL;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        uint32_t sequence = 0;
        if (sscanf(entry->d_name, "%" SCNu32 ".adpcm", &sequence) != 1) continue;
        char path[32];
        if (chunk_path(path, sizeof(path), sequence) < 0) continue;
        FILE *file = fopen(path, "rb");
        if (!file) continue;
        if (fseek(file, 0, SEEK_END) != 0) { fclose(file); continue; }
        long bytes = ftell(file);
        fclose(file);
        if (bytes <= 0 || !callback(sequence, (uint32_t)bytes, user)) break;
    }
    closedir(dir);
    return ESP_OK;
}
