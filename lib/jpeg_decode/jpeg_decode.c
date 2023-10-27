#include "jpeg_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "rom/tjpgd.h"

#define rgb565(r, g, b) ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

JDEC jd;
JRESULT rc;

static uint8_t tjpgd_work[3096]; //tjpg buffer. min size=3096bytes
static uint8_t *_src_buff = NULL;
static uint8_t *_jpeg_dma_buffer = NULL;
static int _buffer_pos = 0;
static int _jpeg_dma_buffer_pos = 0;


void jpeg_decode_init(uint8_t *jpeg_dma_buffer){

    //_src_buff = src_buffer;
    _jpeg_dma_buffer = jpeg_dma_buffer;
    _buffer_pos = 0;
    _jpeg_dma_buffer_pos = 0;
    
}

static uint32_t feed_buffer(JDEC *jd, uint8_t *buff, uint32_t nbyte){
    
    uint32_t count = 0;

    while(count < nbyte){
        if(buff != NULL){
            *buff++ = _src_buff[_buffer_pos];
        }
        count++;
        _buffer_pos++;
    }

    return count;
}

static uint32_t tjd_output(JDEC *jd, void *bitmap, JRECT *rect){

    uint32_t w = rect->right - rect->left + 1;
    uint32_t h = rect->bottom - rect->top + 1;
    uint32_t width = jd->width;
    uint8_t *bitmap_ptr = (uint8_t *)bitmap;

    if(_jpeg_dma_buffer == NULL){
        
        _jpeg_dma_buffer = heap_caps_malloc(w*h*2, MALLOC_CAP_DMA);
    }

    

    for(uint32_t i = 0; i< w*h; i++){
        uint8_t r = *(bitmap_ptr++);
        uint8_t g = *(bitmap_ptr++);
        uint8_t b = *(bitmap_ptr++);

        //uint32_t val = (r*38 + g*75 + b*15) >> 7;

        int xx = rect->left + (i % w);
        if(xx < 0 || xx > width){
            continue;
        }

        int yy = rect->top + (i / w);
        if(yy < 0 || yy > jd->height){
            continue;
        }

        uint16_t color = rgb565(r, g, b);
        _jpeg_dma_buffer[_jpeg_dma_buffer_pos++] = (color >> 8) & 0xFF;
        _jpeg_dma_buffer[_jpeg_dma_buffer_pos++] = color & 0xFF;
        
    }

    return 1;

}

void jpeg_decode_process(uint8_t *src_buff){

    _src_buff = &src_buff;
    _buffer_pos = 0;
    rc = jd_prepare(&jd, feed_buffer, tjpgd_work, sizeof(tjpgd_work), &src_buff);

    rc = jd_decomp(&jd, tjd_output, 0);

    // here the jpeg image has been fully decoded
    // jpeg_dma_buffer contains the rgb565 image ready to be pushed to the display
}

