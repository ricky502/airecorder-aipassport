#include "inspiration_chunk_queue.h"

#include <stddef.h>

void inspiration_chunk_queue_init(inspiration_chunk_queue_t *queue)
{
    queue->count = 0;
}

bool inspiration_chunk_queue_enqueue(inspiration_chunk_queue_t *queue,
                                     uint32_t sequence, uint32_t bytes)
{
    if (!queue || !bytes || queue->count >= INSPIRATION_CHUNK_QUEUE_CAPACITY) return false;
    inspiration_chunk_t *item = &queue->items[queue->count++];
    *item = (inspiration_chunk_t){
        .sequence = sequence,
        .bytes = bytes,
        .status = INSPIRATION_CHUNK_READY,
    };
    return true;
}

inspiration_chunk_t *inspiration_chunk_queue_next_ready(inspiration_chunk_queue_t *queue)
{
    if (!queue) return NULL;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (queue->items[i].status == INSPIRATION_CHUNK_READY) {
            queue->items[i].status = INSPIRATION_CHUNK_UPLOADING;
            return &queue->items[i];
        }
    }
    return NULL;
}

bool inspiration_chunk_queue_acknowledge(inspiration_chunk_queue_t *queue, uint32_t sequence)
{
    if (!queue) return false;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (queue->items[i].sequence != sequence ||
            queue->items[i].status != INSPIRATION_CHUNK_UPLOADING) continue;
        for (uint8_t j = i + 1; j < queue->count; j++) queue->items[j - 1] = queue->items[j];
        queue->count--;
        return true;
    }
    return false;
}

bool inspiration_chunk_queue_retry(inspiration_chunk_queue_t *queue, uint32_t sequence)
{
    if (!queue) return false;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (queue->items[i].sequence == sequence &&
            queue->items[i].status == INSPIRATION_CHUNK_UPLOADING) {
            queue->items[i].status = INSPIRATION_CHUNK_READY;
            return true;
        }
    }
    return false;
}

bool inspiration_chunk_queue_is_ready(const inspiration_chunk_queue_t *queue, uint32_t sequence)
{
    if (!queue) return false;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (queue->items[i].sequence == sequence && queue->items[i].status == INSPIRATION_CHUNK_READY) {
            return true;
        }
    }
    return false;
}

bool inspiration_chunk_queue_remove_ready(inspiration_chunk_queue_t *queue, uint32_t sequence)
{
    if (!queue) return false;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (queue->items[i].sequence != sequence || queue->items[i].status != INSPIRATION_CHUNK_READY) continue;
        for (uint8_t j = i + 1; j < queue->count; j++) queue->items[j - 1] = queue->items[j];
        queue->count--;
        return true;
    }
    return false;
}
