#include <assert.h>

#include "inspiration_state.h"

int main(void)
{
    inspiration_state_t state;
    inspiration_state_init(&state);
    assert(state.phase == INSPIRATION_IDLE);
    assert(!inspiration_state_audio_meter_visible(&state));

    inspiration_state_toggle_recording(&state);
    assert(state.phase == INSPIRATION_RECORDING);
    inspiration_state_chunk_queued(&state);
    inspiration_state_chunk_queued(&state);
    assert(state.pending_chunks == 2);
    inspiration_state_chunk_acknowledged(&state);
    inspiration_state_chunk_acknowledged(&state);
    inspiration_state_chunk_acknowledged(&state);
    assert(state.pending_chunks == 0);
    inspiration_state_tick(&state, 1250);
    assert(state.elapsed_ms == 1250);
    assert(inspiration_state_audio_meter_visible(&state));

    inspiration_state_toggle_recording(&state);
    assert(state.phase == INSPIRATION_PAUSED);
    inspiration_state_tick(&state, 1000);
    assert(state.elapsed_ms == 1250);

    inspiration_state_toggle_recording(&state);
    inspiration_state_stop(&state);
    assert(state.phase == INSPIRATION_FINALIZING);
    assert(state.stop_indicator);
    inspiration_state_upload_started(&state);
    assert(state.phase == INSPIRATION_UPLOADING);
    inspiration_state_upload_finished(&state, true);
    assert(state.phase == INSPIRATION_IDLE);

    inspiration_state_toggle_recording(&state);
    inspiration_state_stop(&state);
    inspiration_state_upload_started(&state);
    inspiration_state_upload_finished(&state, false);
    assert(state.phase == INSPIRATION_ERROR);
    return 0;
}
