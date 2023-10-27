#ifndef _ST7789_DISPLAY_H_
#define _ST7789_DISPLAY_H_

#include <stdio.h>

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
    uint8_t *frame_buffer;
    uint8_t *jpeg_blob_buffer;

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

uint8_t *st7789_display_get_frame_buffer();

void st7789_display_push();
    

#endif // _ST7789_DISPLAY_H_