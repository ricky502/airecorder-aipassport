// Small, allocation-free IMA-ADPCM packet codec for 16 kHz mono recorder chunks.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Packet: initial predictor (LE int16), step index, reserved, sample count (LE uint32), nibbles.
#define INSPIRATION_ADPCM_HEADER_BYTES 8U

size_t inspiration_adpcm_encoded_size(size_t sample_count);
bool inspiration_adpcm_encode_packet(const int16_t *samples, size_t sample_count,
                                     uint8_t *packet, size_t packet_capacity,
                                     size_t *packet_bytes);
bool inspiration_adpcm_decode_packet(const uint8_t *packet, size_t packet_bytes,
                                     int16_t *samples, size_t sample_capacity,
                                     size_t *sample_count);
