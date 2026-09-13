// Bounded, allocation-free queue for completed recorder chunks.
// Files are deleted only after the server acknowledges their sequence number.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INSPIRATION_CHUNK_QUEUE_CAPACITY 16U

typedef enum {
    INSPIRATION_CHUNK_READY = 0,
    INSPIRATION_CHUNK_UPLOADING,
} inspiration_chunk_status_t;

typedef struct {
    uint32_t sequence;
    uint32_t bytes;
    inspiration_chunk_status_t status;
} inspiration_chunk_t;

typedef struct {
    inspiration_chunk_t items[INSPIRATION_CHUNK_QUEUE_CAPACITY];
    uint8_t count;
} inspiration_chunk_queue_t;

void inspiration_chunk_queue_init(inspiration_chunk_queue_t *queue);
bool inspiration_chunk_queue_enqueue(inspiration_chunk_queue_t *queue,
                                     uint32_t sequence, uint32_t bytes);
inspiration_chunk_t *inspiration_chunk_queue_next_ready(inspiration_chunk_queue_t *queue);
bool inspiration_chunk_queue_acknowledge(inspiration_chunk_queue_t *queue, uint32_t sequence);
bool inspiration_chunk_queue_retry(inspiration_chunk_queue_t *queue, uint32_t sequence);
bool inspiration_chunk_queue_is_ready(const inspiration_chunk_queue_t *queue, uint32_t sequence);
bool inspiration_chunk_queue_remove_ready(inspiration_chunk_queue_t *queue, uint32_t sequence);
// Remove a locally deleted clip regardless of whether the uploader has claimed it.
bool inspiration_chunk_queue_remove_any(inspiration_chunk_queue_t *queue, uint32_t sequence);
