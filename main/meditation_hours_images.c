#include "meditation_hours_images.h"

#include "assets/meditation_hours/hour_0_pixels.generated"
#include "assets/meditation_hours/hour_1_pixels.generated"
#include "assets/meditation_hours/hour_2_pixels.generated"
#include "assets/meditation_hours/hour_3_pixels.generated"
#include "assets/meditation_hours/hour_4_pixels.generated"
#include "assets/meditation_hours/hour_5_pixels.generated"
#include "assets/meditation_hours/hour_6_pixels.generated"
#include "assets/meditation_hours/hour_7_pixels.generated"
#include "assets/meditation_hours/hour_8_pixels.generated"
#include "assets/meditation_hours/hour_9_pixels.generated"
#include "assets/meditation_hours/hour_10_pixels.generated"
#include "assets/meditation_hours/hour_11_pixels.generated"

#define HOUR_IMAGE(N) { \
    .header = {.cf = LV_COLOR_FORMAT_RGB565, .w = 216, .h = 235}, \
    .data_size = sizeof(meditation_hour_ ## N ## _pixels), \
    .data = meditation_hour_ ## N ## _pixels, \
}

const lv_image_dsc_t meditation_hour_images[MEDITATION_HOUR_FRAME_COUNT] = {
    HOUR_IMAGE(0), HOUR_IMAGE(1), HOUR_IMAGE(2), HOUR_IMAGE(3),
    HOUR_IMAGE(4), HOUR_IMAGE(5), HOUR_IMAGE(6), HOUR_IMAGE(7),
    HOUR_IMAGE(8), HOUR_IMAGE(9), HOUR_IMAGE(10), HOUR_IMAGE(11),
};
