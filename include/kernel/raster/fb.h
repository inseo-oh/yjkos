#pragma once
#include <kernel/types.h>
#include <stdint.h>

typedef uint16_t FB_COLOR;

FB_COLOR make_color(uint8_t red, uint8_t green, uint8_t blue);
FB_COLOR black(void);
FB_COLOR white(void);

void fb_draw_pixel(int x, int y, FB_COLOR color);
void fb_draw_image(FB_COLOR *image, int width, int height, int pixelsperline, int destx, int desty);
void fb_draw_rect(int width, int height, int destx, int desty, FB_COLOR color);
void fb_draw_text(char *text, int destx, int desty, FB_COLOR color);
extern void (*fb_update)(void);
void fb_scroll(int scrolllen);

int fb_get_width(void);
int fb_get_height(void);

void fb_init_rgb(
    int redfieldpos, int redmasksize,
    int greenfieldpos, int greenmasksize,
    int bluefieldpos, int bluemasksize,
    PHYSPTR framebufferbase,
    int width, int height, int pitch, int bpp);
void fb_init_indexed(
    uint8_t *palette, int colorcount,
    PHYSPTR framebufferbase, int width, int height, int pitch, int bpp);
