#include "meditation_clock_image.h"

#include "assets/meditation_clock_pixels.generated"

const lv_image_dsc_t meditation_clock_image = {
    .header = {.cf = LV_COLOR_FORMAT_RGB565, .w = 216, .h = 217},
    .data_size = sizeof(meditation_clock_pixels),
    .data = meditation_clock_pixels,
};
