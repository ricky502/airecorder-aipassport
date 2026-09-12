#include <assert.h>

#include "inspiration_chunk_queue.h"

int main(void)
{
    inspiration_chunk_queue_t queue;
    inspiration_chunk_queue_init(&queue);
    assert(queue.count == 0);
    assert(inspiration_chunk_queue_enqueue(&queue, 7, 120));
    assert(inspiration_chunk_queue_enqueue(&queue, 8, 240));

    inspiration_chunk_t *chunk = inspiration_chunk_queue_next_ready(&queue);
    assert(chunk && chunk->sequence == 7);
    assert(!inspiration_chunk_queue_acknowledge(&queue, 8));
    assert(inspiration_chunk_queue_retry(&queue, 7));
    chunk = inspiration_chunk_queue_next_ready(&queue);
    assert(chunk && chunk->sequence == 7);
    assert(inspiration_chunk_queue_acknowledge(&queue, 7));
    assert(queue.count == 1);
    chunk = inspiration_chunk_queue_next_ready(&queue);
    assert(chunk && chunk->sequence == 8);
    assert(inspiration_chunk_queue_acknowledge(&queue, 8));
    assert(queue.count == 0);
    return 0;
}
