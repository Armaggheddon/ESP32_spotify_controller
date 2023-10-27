#include "st7789_display.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_heap_caps.h"

static St7789_Display _display;
static esp_lcd_panel_io_handle_t _io_handle = NULL;
static esp_lcd_panel_handle_t _panel_handle = NULL;

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
){

    _display.spi_host_id = spi_host_id;
    _display.sclk_gpio = sclk_gpio;
    _display.mosi_gpio = mosi_gpio;
    _display.res_gpio = res_gpio;
    _display.dc_gpio = dc_gpio;
    _display.blk_gpio = blk_gpio;
    _display.width = width;
    _display.height = height;
    _display.px_count = px_count;
    _display.offset_x = offset_x;
    _display.offset_y = offset_y;
    _display.display_clock_hz = display_clock_hz;

    _display.frame_buffer = heap_caps_malloc(width * height * 2, MALLOC_CAP_DMA);
    if(_display.frame_buffer == NULL){
        printf("Error allocating frame buffer\n");
    }
    //_display.jpeg_blob_buffer = NULL;

}

void st7789_display_begin(){

    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << _display.blk_gpio
    };

    gpio_config(&bk_gpio_config);
    gpio_set_level(_display.blk_gpio, 0);

    spi_bus_config_t buscfg = {
        .sclk_io_num = _display.sclk_gpio,
        .mosi_io_num = _display.mosi_gpio,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0
    };
    spi_bus_initialize(_display.spi_host_id, &buscfg, SPI_DMA_CH_AUTO);

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = _display.dc_gpio,
        .cs_gpio_num = -1,
        .pclk_hz = _display.display_clock_hz,
        .spi_mode = 2,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
    };
    
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) _display.spi_host_id, &io_config, &_io_handle);

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = _display.res_gpio,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16
    };

    esp_lcd_new_panel_st7789(_io_handle, &panel_config, &_panel_handle);

    esp_lcd_panel_reset(_panel_handle);
    esp_lcd_panel_init(_panel_handle);
    esp_lcd_panel_invert_color(_panel_handle, true);
    esp_lcd_panel_disp_on_off(_panel_handle, true);
}

uint8_t *st7789_display_get_frame_buffer(){

    return _display.frame_buffer;

}

void st7789_display_push(){

    esp_lcd_panel_draw_bitmap(_panel_handle, 0, 0, _display.width, _display.height, _display.frame_buffer);

}