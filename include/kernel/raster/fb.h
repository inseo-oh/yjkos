#pragma once
#include <kernel/types.h>
#include <stdint.h>

typedef uint16_t fb_color_t;

fb_color_t makecolor(uint8_t red, uint8_t green, uint8_t blue);
fb_color_t black(void);
fb_color_t white(void);

void fb_drawpixel(int32_t x, int32_t y, fb_color_t color);
void fb_drawimage(fb_color_t *image, int32_t width, int32_t height, int32_t pixelsperline, int32_t destx, int32_t desty);
void fb_drawrect(int32_t width, int32_t height, int32_t destx, int32_t desty, fb_color_t color);
void fb_drawtext(char *text, int32_t destx, int32_t desty, fb_color_t color);
extern void (*fb_update)(void);

int32_t fb_getwidth(void);
int32_t fb_getheight(void);

void fb_init(
    int8_t redfieldpos,
    int8_t redmasksize,
    int8_t greenfieldpos,
    int8_t greenmasksize,
    int8_t bluefieldpos,
    int8_t bluemasksize,
    physptr_t framebufferbase,
    int32_t width,
    int32_t height,
    int32_t pitch,
    int8_t bpp
);
