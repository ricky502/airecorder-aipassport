// Hardware-independent state machine for the Inspiration Recorder.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INSPIRATION_IDLE = 0,
    INSPIRATION_RECORDING,
    INSPIRATION_PAUSED,
    INSPIRATION_FINALIZING,
    INSPIRATION_UPLOADING,
    INSPIRATION_ERROR,
} inspiration_phase_t;

typedef struct {
    inspiration_phase_t phase;
    uint32_t elapsed_ms;
    uint32_t pending_chunks;
    bool wifi_ready;
    bool stop_indicator;
} inspiration_state_t;

void inspiration_state_init(inspiration_state_t *state);
void inspiration_state_toggle_recording(inspiration_state_t *state);
void inspiration_state_stop(inspiration_state_t *state);
void inspiration_state_upload_started(inspiration_state_t *state);
void inspiration_state_upload_finished(inspiration_state_t *state, bool success);
void inspiration_state_tick(inspiration_state_t *state, uint32_t elapsed_ms);
bool inspiration_state_audio_meter_visible(const inspiration_state_t *state);
