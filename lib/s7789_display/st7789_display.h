#ifndef _ST7789_DISPLAY_H_
#define _ST7789_DISPLAY_H_

#include <stdio.h>
#include <stdint.h>

/**
 * @brief Number of destination rows rendered and sent to the panel per transfer.
 *
 * The album art is decoded at its native (small) resolution and upscaled to the
 * screen size while it is transmitted, so this driver only needs
 * width * ST7789_DISPLAY_BAND_ROWS * 2 bytes of buffer instead of a full
 * width * height framebuffer.
 */
#define ST7789_DISPLAY_BAND_ROWS 8

typedef struct {

    int spi_host_id;
    int sclk_gpio;
    int mosi_gpio;
    int res_gpio;
    int dc_gpio;
    int blk_gpio;
    int width;
    int height;
    int px_count;
    int offset_x;
    int offset_y;
    int display_clock_hz;
    uint8_t *band_buffer;   /** @brief Holds band_rows upscaled rows, width * band_rows * 2 bytes */
    int band_rows;          /** @brief Rows sent per transfer, MIN(ST7789_DISPLAY_BAND_ROWS, height) */

} St7789_Display;

void st7789_display_init(
    int spi_host_id,
    int sclk_gpio,
    int mosi_gpio,
    int res_gpio,
    int dc_gpio,
    int blk_gpio,
    int width,
    int height,
    int px_count,
    int offset_x,
    int offset_y,
    int display_clock_hz
);

void st7789_display_begin();

/**
 * @brief Upscales src_rgb565[src_width x src_height] to the display resolution
 *        using nearest neighbour sampling and sends it out in horizontal bands.
 *
 * The source buffer is only read, never modified, and it can be much smaller
 * than the screen: the scaling happens here while the pixels are transmitted,
 * so no screen sized buffer is required anywhere in the pipeline.
 *
 * @param src_rgb565  RGB565 source pixels, src_width * src_height * 2 bytes.
 * @param src_width   Width of the source image in pixels.
 * @param src_height  Height of the source image in pixels.
 */
void st7789_display_push_scaled(const uint8_t *src_rgb565, int src_width, int src_height);

#endif // _ST7789_DISPLAY_H_