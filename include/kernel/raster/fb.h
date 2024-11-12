#pragma once
#include <kernel/types.h>
#include <stdint.h>

typedef uint16_t fb_color;

fb_color makecolor(uint8_t red, uint8_t green, uint8_t blue);
fb_color black(void);
fb_color white(void);

void fb_drawpixel(int x, int y, fb_color color);
void fb_drawimage(fb_color *image, int width, int height, int pixelsperline, int destx, int desty);
void fb_drawrect(int width, int height, int destx, int desty, fb_color color);
void fb_drawtext(char *text, int destx, int desty, fb_color color);
extern void (*fb_update)(void);
void fb_scroll(int scrolllen);

int fb_getwidth(void);
int fb_getheight(void);

void fb_init_rgb(
    int redfieldpos,
    int redmasksize,
    int greenfieldpos,
    int greenmasksize,
    int bluefieldpos,
    int bluemasksize,
    physptr framebufferbase,
    int width,
    int height,
    int pitch,
    int bpp
);
void fb_init_indexed(
    uint8_t *palette,
    int colorcount,
    physptr framebufferbase,
    int width,
    int height,
    int pitch,
    int8_t bpp
);
