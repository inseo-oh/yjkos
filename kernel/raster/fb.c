#include "psf.h"
#include "fbtty.h"
#include <kernel/io/tty.h>
#include <kernel/lib/bitmap.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/vmm.h>
#include <kernel/panic.h>
#include <kernel/raster/fb.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

fb_color_t makecolor(uint8_t red, uint8_t green, uint8_t blue) {
    return (((fb_color_t)(red >> 3)) << 10) | (((fb_color_t)(green >> 3)) << 5) | ((fb_color_t)(blue >> 3));
}

fb_color_t black(void) {
    return makecolor(0, 0, 0);
}
fb_color_t white(void) {
    return makecolor(255, 255, 255);
}

static int32_t s_width;
static int32_t s_height;

static fb_color_t *s_backbuffer;

////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// fb_drawpixel
//------------------------------------------------------------------------------

void fb_drawpixel(int32_t x, int32_t y, fb_color_t color) {
    if (s_backbuffer == NULL) {
        return;
    }
    size_t baseoffset = (y * s_width) + x;
    ((fb_color_t *)s_backbuffer)[baseoffset] = color;
}

void fb_drawimage(fb_color_t *image, int32_t width, int32_t height, int32_t pixelsperline, int32_t destx, int32_t desty) {
    size_t baseoffset = (desty * s_width) + destx;
    fb_color_t *srcline = image;
    fb_color_t *destline = &s_backbuffer[baseoffset];
    for (int32_t srcy = 0; srcy < height; srcy++) {
        memcpy(destline, srcline, sizeof(*destline) * width);
        srcline += pixelsperline;
        destline += s_width;
    }
}

void fb_drawrect(int32_t width, int32_t height, int32_t destx, int32_t desty, fb_color_t color) {
    size_t baseoffset = (desty * s_width) + destx;
    fb_color_t *destline = &s_backbuffer[baseoffset];
    for (int32_t srcy = 0; srcy < height; srcy++) {
        for (int32_t srcx = 0; srcx < width; srcx++) {
            destline[srcx] = color;
        }
        destline += s_width;
    }
}


void fb_drawtext(char *text, int32_t destx, int32_t desty, fb_color_t color) {
    size_t baseoffset = (desty * s_width) + destx;
    for (char *nextchar = text; *nextchar != '\0'; nextchar++) {
        // TODO: Decode UTF-8
        uint8_t const *glyph = psf_getglyph(*nextchar);
        uint8_t const *srcline = glyph;
        fb_color_t *destline = &s_backbuffer[baseoffset];
        for (size_t l = 0; l < psf_getheight(); l++) {
            uint8_t const *srcpixel = srcline;
            fb_color_t *destpixel = destline;
            uint8_t mask = 0x80;
            for (size_t c = 0; c < psf_getwidth(); c++, mask >>= 1, destpixel++) {
                if (mask == 0) {
                    mask = 0x80;
                    srcpixel++;
                }
                if (*srcpixel & mask) {
                    *destpixel = color;
                }
            }
            srcline += psf_getbytesperline();
            destline += s_width;
        }
        baseoffset += psf_getwidth();
    }
}

////////////////////////////////////////////////////////////////////////////////

static int8_t   s_redfieldpos,  s_greenfieldpos,  s_bluefieldpos;
static uint32_t s_redinputmask, s_greeninputmask, s_blueinputmask;
static void *s_fbbase;
static int32_t s_fbpitch;

static uint32_t makenativecolor(fb_color_t rgb) {
    uint32_t red   = ((rgb >> 10) & 0x1f) << 3;
    uint32_t green = ((rgb >> 5) & 0x1f) << 3;
    uint32_t blue  = (rgb & 0x1f) << 3;
    return (((uint32_t)red)   & s_redinputmask)   << s_redfieldpos |
           (((uint32_t)green) & s_greeninputmask) << s_greenfieldpos |
           (((uint32_t)blue)  & s_blueinputmask)  << s_bluefieldpos;
}

static void update_null(void) {}

void (*fb_update)(void) = update_null;

static void update32(void) {
    if (s_backbuffer == NULL) {
        return;
    }
    fb_color_t *srcline = s_backbuffer;
    uint32_t *destline = s_fbbase;
    for (int32_t srcy = 0; srcy < s_height; srcy++) {
        fb_color_t *srcpixel = srcline;
        uint32_t *destpixel = destline;
        for (int32_t srcx = 0; srcx < s_width; srcx++, srcpixel++, destpixel++) {
            *destpixel = makenativecolor(*srcpixel);
        }
        srcline += s_width;
        destline += s_fbpitch / 4;
    }
}

static void update24(void) {
    if (s_backbuffer == NULL) {
        return;
    }
    fb_color_t *srcline = s_backbuffer;
    uint8_t *destline = s_fbbase;
    for (int32_t srcy = 0; srcy < s_height; srcy++) {
        fb_color_t *srcpixel = srcline;
        uint8_t *destpixel = destline;
        for (int32_t srcx = 0; srcx < s_width; srcx++, srcpixel++, destpixel += 3) {
            bitword_t nativecolor = makenativecolor(*srcpixel);
            destpixel[0] = nativecolor;
            destpixel[1] = nativecolor >> 8;
            destpixel[2] = nativecolor >> 16;
        }
        srcline += s_width;
        destline += s_fbpitch;
    }
}

int32_t fb_getwidth(void) {
    return s_width;
}

int32_t fb_getheight(void) {
    return s_height;
}

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
) {
    s_redfieldpos = redfieldpos;
    s_greenfieldpos = greenfieldpos;
    s_bluefieldpos = bluefieldpos;
    s_redinputmask = makebitmask(0, redmasksize);
    s_greeninputmask = makebitmask(0, greenmasksize);
    s_blueinputmask = makebitmask(0, bluemasksize);
    s_width = width;
    s_height = height;
    s_fbpitch = pitch;
    s_backbuffer = heap_alloc(width * height * sizeof(*s_backbuffer), 0);
    if (s_backbuffer == NULL) {
        tty_printf("fb: not enough memory to allocate buffer\n");
        goto fail;
    }
    switch (bpp) {
        case 32:
            fb_update = update32;
            break;
        case 24:
            fb_update = update24;
            break;
        default:
            tty_printf("fb: unsupported bpp %dbpp\n", bpp);
            goto fail;
    }
    s_fbbase = vmm_ezmap(framebufferbase, pitch * height);
    tty_printf("fb: rgb %dx%d %dbpp video\n", width, height, bpp);
    fb_update();
    psf_init();
    fbtty_init();
    return;
fail:
    heap_free(s_backbuffer);
}
