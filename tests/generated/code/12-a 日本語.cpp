#include <stdint.h>

const int image_12_a_width = 3;
const int image_12_a_height = 2;
static const uint32_t image_12_a_palette[16] = {
    0x000000, 0x030201, 0x060402, 0x090603,
    0x0C0804, 0x0F0A05, 0x120C06, 0x150E07,
    0x181008, 0x1B1209, 0x1E140A, 0x21160B,
    0x24180C, 0x271A0D, 0x2A1C0E, 0x2D1E0F,
};

// Top-down, 4bpp: high nibble first, each row byte-aligned.
static uint8_t image_12_a_data[] = {
    0x01, 0x20, 0x34, 0x50, 
};
