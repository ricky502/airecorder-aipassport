#include "inspiration_state.h"

void inspiration_state_init(inspiration_state_t *state)
{
    *state = (inspiration_state_t){.phase = INSPIRATION_IDLE};
}

void inspiration_state_toggle_recording(inspiration_state_t *state)
{
    switch (state->phase) {
    case INSPIRATION_IDLE:
        state->elapsed_ms = 0;
        state->pending_chunks = 0;
        state->stop_indicator = false;
        state->phase = INSPIRATION_RECORDING;
        break;
    case INSPIRATION_RECORDING:
        state->phase = INSPIRATION_PAUSED;
        break;
    case INSPIRATION_PAUSED:
        state->phase = INSPIRATION_RECORDING;
        break;
    default:
        break;
    }
}

void inspiration_state_stop(inspiration_state_t *state)
{
    if (state->phase != INSPIRATION_RECORDING && state->phase != INSPIRATION_PAUSED) return;
    state->stop_indicator = true;
    state->phase = INSPIRATION_FINALIZING;
}

void inspiration_state_upload_started(inspiration_state_t *state)
{
    if (state->phase != INSPIRATION_FINALIZING) return;
    state->stop_indicator = false;
    state->phase = INSPIRATION_UPLOADING;
}

void inspiration_state_upload_finished(inspiration_state_t *state, bool success)
{
    if (state->phase != INSPIRATION_UPLOADING) return;
    state->phase = success ? INSPIRATION_IDLE : INSPIRATION_ERROR;
}

void inspiration_state_tick(inspiration_state_t *state, uint32_t elapsed_ms)
{
    if (state->phase == INSPIRATION_RECORDING) state->elapsed_ms += elapsed_ms;
}

bool inspiration_state_audio_meter_visible(const inspiration_state_t *state)
{
    return state->phase == INSPIRATION_RECORDING || state->phase == INSPIRATION_PAUSED;
}
