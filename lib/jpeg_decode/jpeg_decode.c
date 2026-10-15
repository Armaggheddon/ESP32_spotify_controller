#include "jpeg_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "rom/tjpgd.h"

#define rgb565(r, g, b) ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

static const char *TAG = "jpeg_decode";

static JDEC jd;
static JRESULT rc;

static uint8_t tjpgd_work[3096]; //tjpg buffer. min size=3096bytes

static const uint8_t *_src_buff = NULL;
static int _src_len = 0;
static int _buffer_pos = 0;

static uint8_t *_dst_buff = NULL;
static int _dst_width = 0;
static int _dst_height = 0;


void jpeg_decode_init(uint8_t *jpeg_dma_buffer, int dst_width, int dst_height){

    _dst_buff = jpeg_dma_buffer;
    _dst_width = dst_width;
    _dst_height = dst_height;
}

static UINT feed_buffer(JDEC *jd, BYTE *buff, UINT nbyte){

    UINT count = 0;
    UINT available = (_buffer_pos < _src_len) ? (UINT)(_src_len - _buffer_pos) : 0;

    //Never read past the end of the source buffer, returning less than the
    //requested amount tells tjpgd that the data is over
    if(nbyte > available) nbyte = available;

    while(count < nbyte){
        if(buff != NULL){
            *buff++ = _src_buff[_buffer_pos];
        }
        count++;
        _buffer_pos++;
    }

    return count;
}

static UINT tjd_output(JDEC *jd, void *bitmap, JRECT *rect){

    const uint8_t *bitmap_ptr = (const uint8_t *)bitmap;

    if(_dst_buff == NULL){
        return 0; //abort the decoding
    }

    uint32_t w = rect->right - rect->left + 1;
    uint32_t h = rect->bottom - rect->top + 1;

    for(uint32_t y = 0; y<h; y++){

        int dst_y = rect->top + y;

        for(uint32_t x = 0; x<w; x++){

            uint8_t r = *(bitmap_ptr++);
            uint8_t g = *(bitmap_ptr++);
            uint8_t b = *(bitmap_ptr++);

            int dst_x = rect->left + x;

            //ignore everything that falls outside of the destination buffer
            if(dst_x < 0 || dst_x >= _dst_width || dst_y < 0 || dst_y >= _dst_height){
                continue;
            }

            uint32_t offset = ((uint32_t)dst_y * _dst_width + dst_x) * 2;
            uint16_t color = rgb565(r, g, b);

            _dst_buff[offset] = (color >> 8) & 0xFF;
            _dst_buff[offset + 1] = color & 0xFF;
        }
    }

    return 1;
}

int jpeg_decode_process(const uint8_t *src_buffer, int src_len){

    if(_dst_buff == NULL){
        ESP_LOGE(TAG, "Destination buffer is NULL, call jpeg_decode_init first");
        return -1;
    }

    if(src_buffer == NULL || src_len <= 0){
        ESP_LOGE(TAG, "Invalid source buffer [%p, %d bytes]", src_buffer, src_len);
        return -1;
    }

    _src_buff = src_buffer;
    _src_len = src_len;
    _buffer_pos = 0;

    rc = jd_prepare(&jd, feed_buffer, tjpgd_work, sizeof(tjpgd_work), NULL);
    if(rc != JDR_OK){
        ESP_LOGE(TAG, "Failed to prepare the jpeg image, error %d", rc);
        return -(int)rc;
    }

    if(jd.width > _dst_width || jd.height > _dst_height){
        ESP_LOGW(TAG, "Image [%dx%d] is bigger than the destination [%dx%d], it will be clipped",
            jd.width, jd.height, _dst_width, _dst_height);
    }

    rc = jd_decomp(&jd, tjd_output, 0);
    if(rc != JDR_OK){
        ESP_LOGE(TAG, "Failed to decode the jpeg image, error %d", rc);
        return -(int)rc;
    }

    return 0;
}
