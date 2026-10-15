#include "st7789_display.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "st7789";

#define ST7789_MIN(a,b) (((a)<(b))?(a):(b))

static St7789_Display _display;
static esp_lcd_panel_io_handle_t _io_handle = NULL;
static esp_lcd_panel_handle_t _panel_handle = NULL;

//Given by st7789_on_color_trans_done() once the panel has finished transmitting a
//band. esp_lcd_panel_draw_bitmap() is asynchronous (the color data is read by the
//DMA in the background), so the band buffer must not be reused before this fires.
static SemaphoreHandle_t _trans_done_sem = NULL;
static const TickType_t _trans_done_timeout = pdMS_TO_TICKS(2000);


static bool st7789_on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *evt, void *user_ctx){

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if(_trans_done_sem != NULL){
        xSemaphoreGiveFromISR(_trans_done_sem, &xHigherPriorityTaskWoken);
    }

    return xHigherPriorityTaskWoken == pdTRUE;
}

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

    //Only a few destination rows are buffered at a time. The art is upscaled here
    //while it is sent, so the driver never needs a full screen framebuffer
    _display.band_rows = ST7789_MIN(ST7789_DISPLAY_BAND_ROWS, height);
    if(_display.band_rows <= 0) _display.band_rows = 1;

    _display.band_buffer = heap_caps_malloc(width * _display.band_rows * 2, MALLOC_CAP_DMA);
    if(_display.band_buffer == NULL){
        ESP_LOGE(TAG, "Failed allocating the %d bytes band buffer from DMA capable memory",
            width * _display.band_rows * 2);
        ESP_LOGE(TAG, "\tfree internal %u bytes, free DMA capable %u bytes, largest DMA capable block %u bytes",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }

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

    //The band buffer is only recycled once the previous transfer completed, the
    //semaphore starts available so the first band can be sent right away
    if(_trans_done_sem == NULL){
        _trans_done_sem = xSemaphoreCreateBinary();
        if(_trans_done_sem != NULL) xSemaphoreGive(_trans_done_sem);
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = _display.dc_gpio,
        .cs_gpio_num = -1,
        .pclk_hz = _display.display_clock_hz,
        .spi_mode = 2,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = st7789_on_color_trans_done,
        .user_ctx = NULL,
    };
    
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) _display.spi_host_id, &io_config, &_io_handle);

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = _display.res_gpio,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16
    };

    esp_lcd_new_panel_st7789(_io_handle, &panel_config, &_panel_handle);

    esp_lcd_panel_reset(_panel_handle);
    esp_lcd_panel_init(_panel_handle);
    esp_lcd_panel_invert_color(_panel_handle, true);
    esp_lcd_panel_disp_on_off(_panel_handle, true);
}

void st7789_display_push_scaled(const uint8_t *src_rgb565, int src_width, int src_height){

    if(_panel_handle == NULL || _display.band_buffer == NULL || src_rgb565 == NULL){
        return;
    }

    if(src_width <= 0 || src_height <= 0){
        return;
    }

    const int dst_width = _display.width;
    const int dst_height = _display.height;

    for(int y0 = 0; y0 < dst_height; y0 += _display.band_rows){

        const int rows = ST7789_MIN(_display.band_rows, dst_height - y0);
        uint8_t *band = _display.band_buffer;

        //Do not overwrite the band buffer while the previous band is still being
        //transmitted, the semaphore is given by st7789_on_color_trans_done()
        if(_trans_done_sem != NULL){
            if(xSemaphoreTake(_trans_done_sem, _trans_done_timeout) != pdTRUE){
                ESP_LOGW(TAG, "Timed out waiting for the previous band to be transmitted");
            }
        }

        for(int r = 0; r<rows; r++){

            //nearest neighbour: take the source row/column closest to the destination one
            const int src_y = (int)(((uint32_t)(y0 + r) * src_height) / dst_height);
            const uint8_t *src_row = src_rgb565 + ((uint32_t)src_y * src_width) * 2;
            uint8_t *dst_row = band + (uint32_t)r * dst_width * 2;

            for(int x = 0; x<dst_width; x++){
                const int src_x = (int)(((uint32_t)x * src_width) / dst_width);
                const uint8_t *src_px = src_row + ((uint32_t)src_x) * 2;

                dst_row[x * 2] = src_px[0];
                dst_row[x * 2 + 1] = src_px[1];
            }
        }

        esp_lcd_panel_draw_bitmap(_panel_handle, 0, y0, dst_width, y0 + rows, band);
    }
}