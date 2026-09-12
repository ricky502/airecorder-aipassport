#include "inspiration_adpcm.h"

#include <limits.h>

static const int s_step_table[89] = {
     7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int8_t s_index_delta[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

static uint8_t encode_nibble(int16_t sample, int *predictor, int *index)
{
    int step = s_step_table[*index];
    int delta = (int)sample - *predictor;
    uint8_t nibble = delta < 0 ? 8U : 0U;
    if (delta < 0) delta = -delta;

    int diff = step >> 3;
    if (delta >= step) { nibble |= 4U; delta -= step; diff += step; }
    step >>= 1;
    if (delta >= step) { nibble |= 2U; delta -= step; diff += step; }
    step >>= 1;
    if (delta >= step) { nibble |= 1U; diff += step; }

    *predictor += (nibble & 8U) ? -diff : diff;
    *predictor = clamp_int(*predictor, INT16_MIN, INT16_MAX);
    *index = clamp_int(*index + s_index_delta[nibble], 0, 88);
    return nibble;
}

static int16_t decode_nibble(uint8_t nibble, int *predictor, int *index)
{
    int step = s_step_table[*index];
    int diff = step >> 3;
    if (nibble & 4U) diff += step;
    if (nibble & 2U) diff += step >> 1;
    if (nibble & 1U) diff += step >> 2;
    *predictor += (nibble & 8U) ? -diff : diff;
    *predictor = clamp_int(*predictor, INT16_MIN, INT16_MAX);
    *index = clamp_int(*index + s_index_delta[nibble & 0x0fU], 0, 88);
    return (int16_t)*predictor;
}

size_t inspiration_adpcm_encoded_size(size_t sample_count)
{
    if (!sample_count) return 0;
    return INSPIRATION_ADPCM_HEADER_BYTES + sample_count / 2U;
}

bool inspiration_adpcm_encode_packet(const int16_t *samples, size_t sample_count,
                                     uint8_t *packet, size_t packet_capacity,
                                     size_t *packet_bytes)
{
    size_t needed = inspiration_adpcm_encoded_size(sample_count);
    if (!samples || !packet || !packet_bytes || !sample_count || packet_capacity < needed ||
        sample_count > UINT32_MAX) return false;

    int predictor = samples[0];
    int index = 0;
    packet[0] = (uint8_t)(predictor & 0xff);
    packet[1] = (uint8_t)((uint16_t)predictor >> 8);
    packet[2] = (uint8_t)index;
    packet[3] = 0;
    packet[4] = (uint8_t)(sample_count & 0xffU);
    packet[5] = (uint8_t)((sample_count >> 8U) & 0xffU);
    packet[6] = (uint8_t)((sample_count >> 16U) & 0xffU);
    packet[7] = (uint8_t)((sample_count >> 24U) & 0xffU);

    size_t out = INSPIRATION_ADPCM_HEADER_BYTES;
    bool low_nibble = true;
    for (size_t i = 1; i < sample_count; i++) {
        uint8_t nibble = encode_nibble(samples[i], &predictor, &index);
        if (low_nibble) {
            packet[out] = nibble;
            low_nibble = false;
        } else {
            packet[out++] |= (uint8_t)(nibble << 4U);
            low_nibble = true;
        }
    }
    if (!low_nibble) out++;
    *packet_bytes = out;
    return out == needed;
}

bool inspiration_adpcm_decode_packet(const uint8_t *packet, size_t packet_bytes,
                                     int16_t *samples, size_t sample_capacity,
                                     size_t *sample_count)
{
    if (!packet || !samples || !sample_count || packet_bytes < INSPIRATION_ADPCM_HEADER_BYTES) return false;
    uint32_t declared = (uint32_t)packet[4] | ((uint32_t)packet[5] << 8U) |
                        ((uint32_t)packet[6] << 16U) | ((uint32_t)packet[7] << 24U);
    size_t needed = inspiration_adpcm_encoded_size(declared);
    if (!declared || declared > sample_capacity || needed != packet_bytes) return false;

    int predictor = (int16_t)((uint16_t)packet[0] | ((uint16_t)packet[1] << 8U));
    int index = packet[2];
    if (index > 88) return false;
    samples[0] = (int16_t)predictor;
    for (size_t i = 1; i < declared; i++) {
        uint8_t packed = packet[INSPIRATION_ADPCM_HEADER_BYTES + (i - 1U) / 2U];
        uint8_t nibble = ((i - 1U) & 1U) ? (packed >> 4U) : (packed & 0x0fU);
        samples[i] = decode_nibble(nibble, &predictor, &index);
    }
    *sample_count = declared;
    return true;
}
