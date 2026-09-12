#include <assert.h>
#include <stdint.h>

#include "inspiration_adpcm.h"

int main(void)
{
    static const int16_t source[] = {0, 1200, -1200, 7000, -7000, 16000, -16000, 0, 300, -300, 0};
    uint8_t packet[64];
    int16_t decoded[16];
    size_t packet_bytes = 0;
    size_t sample_count = 0;

    assert(inspiration_adpcm_encoded_size(0) == 0);
    assert(inspiration_adpcm_encoded_size(1) == INSPIRATION_ADPCM_HEADER_BYTES);
    assert(inspiration_adpcm_encode_packet(source, sizeof(source) / sizeof(source[0]),
                                           packet, sizeof(packet), &packet_bytes));
    assert(packet_bytes == inspiration_adpcm_encoded_size(sizeof(source) / sizeof(source[0])));
    assert(inspiration_adpcm_decode_packet(packet, packet_bytes, decoded,
                                           sizeof(decoded) / sizeof(decoded[0]), &sample_count));
    assert(sample_count == sizeof(source) / sizeof(source[0]));
    assert(decoded[0] == source[0]);
    assert(decoded[4] != 0);
    assert(decoded[5] != 0);
    assert(!inspiration_adpcm_decode_packet(packet, packet_bytes - 1U, decoded,
                                            sizeof(decoded) / sizeof(decoded[0]), &sample_count));
    return 0;
}
