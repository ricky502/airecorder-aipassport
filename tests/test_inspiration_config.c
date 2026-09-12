#include <assert.h>

#include "inspiration_config.h"

int main(void)
{
    assert(INSPIRATION_SAMPLE_RATE_HZ == 8000U);
    assert(INSPIRATION_PCM_BITS == 16U);
    assert(INSPIRATION_CHUNK_SECONDS == 60U);
    assert(INSPIRATION_OFFLINE_TARGET_SECONDS >= 600U);
    return 0;
}
