#ifndef _JPEG_DECODE_H_
#define _JPEG_DECODE

#include <stdio.h>

void jpeg_decode_init(uint8_t *jpeg_dma_buffer);

void jpeg_decode_process(uint8_t *src_buffer);

#endif // _JPEG_DECODE_H_