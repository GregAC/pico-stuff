#ifndef __PIO_VGA_SPRITE_H__
#define __PIO_VGA_SPRITE_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t* data_ptr;
    unsigned int height;
    int x;
    int y;
    bool enabled;
} sprite_info_t;

#define NUM_SPRITES 128
#define MAX_SPRITES_PER_LINE 8
#define SPRITE_WIDTH 16

#endif
