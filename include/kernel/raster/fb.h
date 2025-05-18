#pragma once
#include <kernel/types.h>
#include <stdint.h>

typedef uint16_t FB_COLOR;

FB_COLOR MakeColor(uint8_t red, uint8_t green, uint8_t blue);
FB_COLOR Black(void);
FB_COLOR White(void);

void Fb_DrawPixel(int x, int y, FB_COLOR color);
void Fb_DrawImage(FB_COLOR *image, int width, int height, int pixelsperline, int destx, int desty);
void Fb_DrawRect(int width, int height, int destx, int desty, FB_COLOR color);
void Fb_DrawText(char *text, int destx, int desty, FB_COLOR color);
extern void (*fb_update)(void);
void Fb_Scroll(int scrolllen);

int Fb_GetWidth(void);
int Fb_GetHeight(void);

void Fb_InitRgb(
    int redfieldpos, int redmasksize,
    int greenfieldpos, int greenmasksize,
    int bluefieldpos, int bluemasksize,
    PHYSPTR framebufferbase,
    int width, int height, int pitch, int bpp);
void Fb_initIndexed(
    uint8_t *palette, int colorcount,
    PHYSPTR framebufferbase, int width, int height, int pitch, int bpp);
