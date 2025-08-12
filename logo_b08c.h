#pragma once
#include <lvgl.h>

// Déclaration du buffer image généré dans MonLogo64.c
extern const uint8_t MonLogo64_map[];

static const lv_img_dsc_t logo_b08c_img = {
  .header = {
    .cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .always_zero = 0,
    .reserved = 0,
    .w = 64,
    .h = 64,
  },
  .data_size = 4096 * LV_IMG_PX_SIZE_ALPHA_BYTE,
  .data = MonLogo64_map,
};
