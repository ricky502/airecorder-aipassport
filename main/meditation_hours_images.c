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
#include "assets/meditation_hours/hour_12_pixels.generated"
#include "assets/meditation_hours/hour_13_pixels.generated"
#include "assets/meditation_hours/hour_14_pixels.generated"
#include "assets/meditation_hours/hour_15_pixels.generated"
#include "assets/meditation_hours/hour_16_pixels.generated"
#include "assets/meditation_hours/hour_17_pixels.generated"
#include "assets/meditation_hours/hour_18_pixels.generated"
#include "assets/meditation_hours/hour_19_pixels.generated"
#include "assets/meditation_hours/hour_20_pixels.generated"
#include "assets/meditation_hours/hour_21_pixels.generated"
#include "assets/meditation_hours/hour_22_pixels.generated"
#include "assets/meditation_hours/hour_23_pixels.generated"

#define HOUR_IMAGE(N) { \
    .header = {.cf = LV_COLOR_FORMAT_RGB565, .w = 144, .h = 145}, \
    .data_size = sizeof(meditation_hour_ ## N ## _pixels), \
    .data = meditation_hour_ ## N ## _pixels, \
}

const lv_image_dsc_t meditation_hour_images[MEDITATION_HOUR_FRAME_COUNT] = {
    HOUR_IMAGE(0), HOUR_IMAGE(1), HOUR_IMAGE(2), HOUR_IMAGE(3),
    HOUR_IMAGE(4), HOUR_IMAGE(5), HOUR_IMAGE(6), HOUR_IMAGE(7),
    HOUR_IMAGE(8), HOUR_IMAGE(9), HOUR_IMAGE(10), HOUR_IMAGE(11),
    HOUR_IMAGE(12), HOUR_IMAGE(13), HOUR_IMAGE(14), HOUR_IMAGE(15),
    HOUR_IMAGE(16), HOUR_IMAGE(17), HOUR_IMAGE(18), HOUR_IMAGE(19),
    HOUR_IMAGE(20), HOUR_IMAGE(21), HOUR_IMAGE(22), HOUR_IMAGE(23),
};
