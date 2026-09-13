// Recorder capacity is intentionally based on the verified, unused voicefs tail.
#pragma once

#define INSPIRATION_SAMPLE_RATE_HZ 8000U
#define INSPIRATION_PCM_BITS 16U
#define INSPIRATION_CHUNK_SECONDS 60U
#define INSPIRATION_VOICEFS_LABEL "voicefs"

// 8 kHz IMA-ADPCM is about 4 KB/s.  The 0x306000-byte voicefs therefore gives
// roughly 12 minutes after filesystem overhead, while retaining useful ASR speech.
#define INSPIRATION_OFFLINE_TARGET_SECONDS 600U

// Match the existing Cardputer recorder backend.  A future provisioning flow
// may store an NVS override, but a freshly flashed Passport is useful on the
// same LAN without requiring a second companion application.
#define INSPIRATION_DEFAULT_BACKEND_ENDPOINT ""
