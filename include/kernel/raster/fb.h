#pragma once
#include <kernel/types.h>
#include <stdint.h>

typedef uint16_t fb_color;

fb_color makecolor(uint8_t red, uint8_t green, uint8_t blue);
fb_color black(void);
fb_color white(void);

void fb_drawpixel(int32_t x, int32_t y, fb_color color);
void fb_drawimage(fb_color *image, int32_t width, int32_t height, int32_t pixelsperline, int32_t destx, int32_t desty);
void fb_drawrect(int32_t width, int32_t height, int32_t destx, int32_t desty, fb_color color);
void fb_drawtext(char *text, int32_t destx, int32_t desty, fb_color color);
extern void (*fb_update)(void);
void fb_scroll(int scrolllen);

int32_t fb_getwidth(void);
int32_t fb_getheight(void);

void fb_init_rgb(
    int8_t redfieldpos,
    int8_t redmasksize,
    int8_t greenfieldpos,
    int8_t greenmasksize,
    int8_t bluefieldpos,
    int8_t bluemasksize,
    physptr framebufferbase,
    int32_t width,
    int32_t height,
    int32_t pitch,
    int8_t bpp
);
void fb_init_indexed(
    uint8_t *palette,
    int colorcount,
    physptr framebufferbase,
    int32_t width,
    int32_t height,
    int32_t pitch,
    int8_t bpp
);
