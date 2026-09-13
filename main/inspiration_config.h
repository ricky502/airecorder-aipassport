// Recorder capacity is intentionally based on the verified, unused voicefs tail.
#pragma once

#define INSPIRATION_SAMPLE_RATE_HZ 8000U
#define INSPIRATION_PCM_BITS 16U
#define INSPIRATION_CHUNK_SECONDS 60U
#define INSPIRATION_VOICEFS_LABEL "voicefs"

// 8 kHz IMA-ADPCM is about 4 KB/s.  The 0x306000-byte voicefs therefore gives
// roughly 12 minutes after filesystem overhead, while retaining useful ASR speech.
#define INSPIRATION_OFFLINE_TARGET_SECONDS 600U

// A freshly flashed device stays safely offline until its owner enters a
// private receiver address on the local provisioning page.  Never put a
// personal LAN address, webhook, or credentials into public firmware source.
#ifndef INSPIRATION_DEFAULT_BACKEND_ENDPOINT
#define INSPIRATION_DEFAULT_BACKEND_ENDPOINT ""
#endif
