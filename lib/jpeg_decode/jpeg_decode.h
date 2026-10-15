#ifndef _JPEG_DECODE_H_
#define _JPEG_DECODE_H_

#include <stdio.h>

//Initializes the decoder, jpeg_dma_buffer is the buffer the decoded rgb565 image
//is written into, its size (in pixels) is dst_width * dst_height
void jpeg_decode_init(uint8_t *jpeg_dma_buffer, int dst_width, int dst_height);

//Decodes src_buffer[src_len] into the buffer passed to jpeg_decode_init.
//Returns 0 on success, a negative value on failure
int jpeg_decode_process(const uint8_t *src_buffer, int src_len);

#endif // _JPEG_DECODE_H_
