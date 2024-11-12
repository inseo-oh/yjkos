#include "psf.h"
#include "fbtty.h"
#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/lib/string_ext.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/vmm.h>
#include <kernel/raster/fb.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

//------------------------------- Configuration -------------------------------

// Draws damage region before drawing the content.
//
// WARNING: This can cause a LOT of flickering colors! YOU'VE BEEN WARNED.
static bool const CONFIG_SHOW_DAMAGE = false; 

//-----------------------------------------------------------------------------

fb_color makecolor(uint8_t red, uint8_t green, uint8_t blue) {
    return (((uint32_t)(red >> 3)) << 10) |
           (((uint32_t)(green >> 3)) << 5) |
           ((fb_color)(blue >> 3));
}

fb_color black(void) {
    return makecolor(0, 0, 0);
}

fb_color white(void) {
    return makecolor(255, 255, 255);
}

static int s_width;
static int s_height;
static int s_damagefirsty = -1;
static int s_damagelasty = -1;

static fb_color *s_backbuffer;

static void include_damage(int from, int to) {
    if ((s_damagefirsty == -1) || (from < s_damagefirsty)) {
        s_damagefirsty = from;
    }
    if ((s_damagelasty == -1) || (s_damagelasty < to)) {
        s_damagelasty = to;
    }
}

////////////////////////////////////////////////////////////////////////////////

void fb_drawpixel(int x, int y, fb_color color) {
    if (s_backbuffer == NULL) {
        return;
    }
    size_t baseoffset = (y * s_width) + x;
    (s_backbuffer)[baseoffset] = color;
    include_damage(y, y);
}

void fb_drawimage(
    fb_color *image, int width, int height, int pixelsperline, int destx, int desty)
{
    size_t baseoffset = (desty * s_width) + destx;
    fb_color *srcline = image;
    fb_color *destline = &s_backbuffer[baseoffset];
    for (int srcy = 0; srcy < height; srcy++) {
        memcpy(destline, srcline, sizeof(*destline) * width);
        srcline += pixelsperline;
        destline += s_width;
    }
    include_damage(desty, desty + height - 1);
}

void fb_drawrect(
    int width, int height, int destx, int desty,
    fb_color color)
{
    size_t baseoffset = (desty * s_width) + destx;
    fb_color *destline = &s_backbuffer[baseoffset];
    for (int srcy = 0; srcy < height; srcy++) {
        for (int srcx = 0; srcx < width; srcx++) {
            destline[srcx] = color;
        }
        destline += s_width;
    }
    include_damage(desty, desty + height - 1);
}

void fb_drawtext(char *text, int destx, int desty, fb_color color) {
    size_t baseoffset = (desty * s_width) + destx;
    for (char *nextchar = text; *nextchar != '\0'; nextchar++) {
        // TODO: Decode UTF-8
        uint8_t const *glyph = psf_getglyph(*nextchar);
        uint8_t const *srcline = glyph;
        fb_color *destline = &s_backbuffer[baseoffset];
        for (int l = 0; l < psf_getheight(); l++) {
            uint8_t const *srcpixel = srcline;
            fb_color *destpixel = destline;
            uint8_t mask = 0x80;
            for (
                int c = 0; c < psf_getwidth();
                c++, mask >>= 1U, destpixel++)
            {
                if (*srcpixel == 0) {
                    mask = 0x7f;
                    srcpixel++;
                    c += (8 - (c % 8)) - 1;
                    continue;
                }
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
    include_damage(desty, desty + psf_getheight() - 1);
}

////////////////////////////////////////////////////////////////////////////////

static union {
    struct {
        int redfieldpos,  greenfieldpos,  bluefieldpos;
        int redmasksize,  greenmasksize,  bluemasksize;
    } rgb;
    struct {
        uint8_t *palette;
        int colorcount;
    } indexed;
} s_colorinfo;

static void *s_fbbase;
static int s_fbpitch;


static uint32_t makenativecolor_rgb(fb_color rgb) {
    uint32_t red   = ((((uint32_t)rgb >> 10U) & 0x1fU) * 255) / 0x1f;
    uint32_t green = ((((uint32_t)rgb >> 5U) & 0x1fU) * 255) / 0x1f;
    uint32_t blue  = (((uint32_t)rgb & 0x1fU) * 255) / 0x1f;
    uint32_t redshiftcount = 8 - s_colorinfo.rgb.redmasksize;
    uint32_t greenshiftcount = 8 - s_colorinfo.rgb.greenmasksize;
    uint32_t blueshiftcount = 8 - s_colorinfo.rgb.bluemasksize;
    uint8_t redfieldpos = s_colorinfo.rgb.redfieldpos;
    uint8_t greenfieldpos = s_colorinfo.rgb.greenfieldpos;
    uint8_t bluefieldpos = s_colorinfo.rgb.bluefieldpos;
    return ((red)   >> redshiftcount)   << redfieldpos |
           ((green) >> greenshiftcount) << greenfieldpos |
           ((blue)  >> blueshiftcount)  << bluefieldpos;
}

static uint32_t makenativecolor_indexed(fb_color rgb) {
    static int s_lastavg = -1;
    static int s_lastcolor = -1;

    uint red   = (((uint32_t)rgb >> 10U) & 0x1fU) << 3U;
    uint green = (((uint32_t)rgb >> 5) & 0x1fU) << 3U;
    uint blue  = (rgb & 0x1fU) << 3U;
    int avgcolor = (int)((red + green + blue) / 3);
    if (s_lastavg == avgcolor) {
        return s_lastcolor;
    }
    int mindiff = 0x7fff;
    int chosencolor = 0;
    for (int i = 0; i < s_colorinfo.indexed.colorcount; i++) {
        size_t coloroffset = i * 3;
        int pred = s_colorinfo.indexed.palette[coloroffset];
        int pgreen = s_colorinfo.indexed.palette[coloroffset + 1];
        int pblue = s_colorinfo.indexed.palette[coloroffset + 2];
        int pavg = (pred + pgreen + pblue) / 3;
        int diff = pavg - avgcolor;
        if (diff < 0) {
            diff = -diff;
        }
        if (diff < mindiff) {
            mindiff = diff;
            chosencolor = i;
        }
        if (mindiff == 0) {
            break;
        }
    }
    s_lastavg = avgcolor;
    s_lastcolor = chosencolor;
    return chosencolor;
}

static void update_null(void) {}

void (*fb_update)(void) = update_null;

static void update32(void) {
    if ((s_backbuffer == NULL) || (s_damagefirsty < 0)) {
        return;
    }
    if (CONFIG_SHOW_DAMAGE) {
        uint32_t *destline = &((uint32_t *)s_fbbase)[
            s_damagefirsty * (s_fbpitch / 4)];
        for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
            uint32_t *destpixel = destline;
            for (int srcx = 0; srcx < s_width; srcx++, destpixel++) {
                *destpixel = 0x7f7f7f;
            }
            destline += s_fbpitch / 4;
        }
    }
    fb_color *srcpixel = &s_backbuffer[s_damagefirsty * s_width];
    uint32_t *destline = &((uint32_t *)s_fbbase)[
        s_damagefirsty * (s_fbpitch / 4)];
    int lastcolor = -1;
    uint32_t lastnativecolor = -1;
    for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
        uint32_t *destpixel = destline;
        for (int srcx = 0; srcx < s_width; srcx++, srcpixel++, destpixel++) {
            int color = *srcpixel;
            if (color == lastcolor) {
                *destpixel = lastnativecolor;
            } else {
                lastnativecolor =  makenativecolor_rgb(color);
                lastcolor = color;
                *destpixel = lastnativecolor;
            }
        }
        destline += s_fbpitch / 4;
    }
    s_damagefirsty = -1;
    s_damagelasty = -1;
}

static void update24(void) {
    if ((s_backbuffer == NULL) || (s_damagefirsty < 0)) {
        return;
    }
    if (CONFIG_SHOW_DAMAGE) {
        uint8_t *destline = &((uint8_t *)s_fbbase)[s_damagefirsty * s_fbpitch];
        for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
            uint8_t *destpixel = destline;
            for (
                int srcx = 0; srcx < s_width;
                srcx++, destpixel += 3)
            {
                destpixel[0] = 127;
                destpixel[1] = 127;
                destpixel[2] = 127;
            }
            destline += s_fbpitch;
        }
    }

    fb_color *srcpixel = &s_backbuffer[s_damagefirsty * s_width];
    uint8_t *destline = &((uint8_t *)s_fbbase)[s_damagefirsty * s_fbpitch];
    uint32_t lastcolor = -1;
    uint32_t lastnativecolor = -1;

    for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
        uint8_t *destpixel = destline;
        for (
            int srcx = 0; srcx < s_width;
            srcx++, srcpixel++, destpixel += 3)
        {
            uint32_t color = *srcpixel;
            if (color == lastcolor) {
                destpixel[0] = lastnativecolor;
                destpixel[1] = lastnativecolor >> 8U;
                destpixel[2] = lastnativecolor >> 16U;
            } else {
                lastnativecolor = makenativecolor_rgb(color);
                lastcolor = color;
                destpixel[0] = lastnativecolor;
                destpixel[1] = lastnativecolor >> 8U;
                destpixel[2] = lastnativecolor >> 16U;
            }
        }
        destline += s_fbpitch;
    }
    s_damagefirsty = -1;
    s_damagelasty = -1;
}

static void update16(void) {
    if ((s_backbuffer == NULL) || (s_damagefirsty < 0)) {
        return;
    }
    if (CONFIG_SHOW_DAMAGE) {
        uint16_t *destline = &((uint16_t *)s_fbbase)[
            s_damagefirsty * (s_fbpitch / 2)];
        for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
            uint16_t *destpixel = destline;
            for (
                int srcx = 0; srcx < s_width;
                srcx++, destpixel++)
            {
                destpixel[0] = 0x7f7f;
            }
            destline += s_fbpitch;
        }
    }

    fb_color *srcpixel = &s_backbuffer[s_damagefirsty * s_width];
    uint16_t *destline = &((uint16_t *)s_fbbase)[
        s_damagefirsty * (s_fbpitch / 2)];
    int lastcolor = -1;
    uint32_t lastnativecolor = -1;

    for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
        uint16_t *destpixel = destline;
        for (
            int srcx = 0; srcx < s_width;
            srcx++, srcpixel++, destpixel++)
        {
            int color = *srcpixel;
            if (color == lastcolor) {
                *destpixel = lastnativecolor;
            } else {
                lastnativecolor = makenativecolor_rgb(color);
                lastcolor = color;
                *destpixel = lastnativecolor;
            }
        }
        destline += s_fbpitch / 2;
    }
    s_damagefirsty = -1;
    s_damagelasty = -1;
}

static void update8(void) {
    if ((s_backbuffer == NULL) || (s_damagefirsty < 0)) {
        return;
    }
    fb_color *srcpixel = &s_backbuffer[s_damagefirsty * s_width];
    uint8_t *destline = &((uint8_t *)s_fbbase)[s_damagefirsty * s_fbpitch];
    int lastcolor = -1;
    uint32_t lastnativecolor = -1;

    if (CONFIG_SHOW_DAMAGE) {
        uint8_t *destline = &((uint8_t *)s_fbbase)[s_damagefirsty * s_fbpitch];
        for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
            for (int x = 0; x < s_width; x++) {
                destline[x] = 7;
            }
            destline += s_fbpitch;
        }
    }

    for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
        for (int x = 0; x < s_width; x++, srcpixel++) {
            int color = *srcpixel;
            if (color == lastcolor) {
                destline[x] = lastnativecolor;
            } else {
                lastnativecolor = makenativecolor_indexed(color);
                lastcolor = color;
                destline[x] = lastnativecolor;
            }
        }
        destline += s_fbpitch;
    }
    s_damagefirsty = -1;
    s_damagelasty = -1;
}

static void update1(void) {
    if ((s_backbuffer == NULL) || (s_damagefirsty < 0)) {
        return;
    }
    fb_color *srcpixel = &s_backbuffer[s_damagefirsty * s_width];
    uint8_t *destline = &((uint8_t *)s_fbbase)[s_damagefirsty * s_fbpitch];
    uint32_t lastcolor = -1;
    uint32_t lastnativecolor = -1;

    if (CONFIG_SHOW_DAMAGE) {
        uint8_t *destline = &((uint8_t *)s_fbbase)[s_damagefirsty * s_fbpitch];
        for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
            for (int x = 0; x < s_width; x += 8) {
                destline[x / 8] = 0xff;
            }
            destline += s_fbpitch;
        }
    }

    for (int srcy = s_damagefirsty; srcy <= s_damagelasty; srcy++) {
        uint8_t temppixeldata = 0;
        for (int x = 0; x < s_width; x++, srcpixel++) {
            uint32_t color = *srcpixel;
            uint32_t resultcolor;
            if (color == lastcolor) {
                resultcolor = lastnativecolor;
            } else {
                uint red   = ((color >> 10U) & 0x1fU) << 3U;
                uint green = ((color >> 5U) & 0x1fU) << 3U;
                uint blue  = (color & 0x1fU) << 3U;
                uint avg = (red + green + blue) / 3;
                lastnativecolor = 128 <= avg;
                lastcolor = color;
                resultcolor = lastnativecolor;
            }
            temppixeldata <<= 1U;
            if (resultcolor) {
                temppixeldata |= 1U;
            }
            if ((x + 1) % 8 == 0) {
                destline[x / 8] = temppixeldata;
                temppixeldata = 0;
            }
        }
        destline += s_fbpitch;
    }
    s_damagefirsty = -1;
    s_damagelasty = -1;
}

void fb_scroll(int scrolllen) {
    assert(0 < scrolllen);
    if (s_backbuffer == NULL) {
        return;
    }
    {
        uint8_t *srcline  = &((uint8_t *)s_fbbase)[s_fbpitch * scrolllen];
        uint8_t *destline = s_fbbase;
        for (
            int y = 0; y < s_height - scrolllen;
            y++, destline += s_fbpitch, srcline += s_fbpitch)
        {
            memcpy32(destline, srcline, s_fbpitch / 4);
        }
    }
    {
        uint16_t *srcline = &s_backbuffer[s_width * scrolllen];
        uint16_t *destline = s_backbuffer;
        for (
            int y = 0; y < s_height - scrolllen;
            y++, destline += s_width, srcline += s_width)
        {
            memcpy32(
                destline, srcline,
                (s_width * sizeof(*destline)) / 4);
        }
    }
}

int fb_getwidth(void) {
    return s_width;
}

int fb_getheight(void) {
    return s_height;
}

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
) {
    s_colorinfo.rgb.redfieldpos = redfieldpos;
    s_colorinfo.rgb.greenfieldpos = greenfieldpos;
    s_colorinfo.rgb.bluefieldpos = bluefieldpos;
    s_colorinfo.rgb.redmasksize = redmasksize;
    s_colorinfo.rgb.greenmasksize = greenmasksize;
    s_colorinfo.rgb.bluemasksize = bluemasksize;
    s_width = width;
    s_height = height;
    s_fbpitch = pitch;
    s_backbuffer = heap_alloc(
        width * height * sizeof(*s_backbuffer), 0);
    if (s_backbuffer == NULL) {
        co_printf("fb: not enough memory to allocate buffer\n");
        goto fail;
    }
    switch (bpp) {
        case 32:
            fb_update = update32;
            break;
        case 24:
            fb_update = update24;
            break;
        case 16:
        case 15:
            fb_update = update16;
            break;
        default:
            co_printf("fb: unsupported rgb bpp %dbpp\n", bpp);
            goto fail;
    }
    s_fbbase = vmm_ezmap(framebufferbase, pitch * height);
    co_printf("fb: rgb %dx%d %dbpp video\n", width, height, bpp);
    fb_update();
    psf_init();
    fbtty_init();
    return;
fail:
    heap_free(s_backbuffer);
}

void fb_init_indexed(
    uint8_t *palette,
    int colorcount,
    physptr framebufferbase,
    int width,
    int height,
    int pitch,
    int8_t bpp
) {
    s_colorinfo.indexed.palette = palette;
    s_colorinfo.indexed.colorcount = colorcount;
    s_width = width;
    s_height = height;
    s_fbpitch = pitch;
    s_backbuffer = heap_alloc(
        width * height * sizeof(*s_backbuffer), 0);
    if (s_backbuffer == NULL) {
        co_printf("fb: not enough memory to allocate buffer\n");
        goto fail;
    }
    switch (bpp) {
        case 8:
            fb_update = update8;
            break;
        case 1:
            assert((width % 8) == 0);
            fb_update = update1;
            break;
        default:
            co_printf("fb: unsupported indexed bpp %dbpp\n", bpp);
            goto fail;
    }
    s_fbbase = vmm_ezmap(framebufferbase, pitch * height);
    co_printf(
        "fb: %u-color indexed %dx%d %dbpp video\n",
        colorcount, width, height, bpp);

    // Make sure we can safely use memcpy32
    assert(s_fbpitch % 4 == 0);
    assert((s_width * sizeof(*s_backbuffer)) % 4 == 0);

    fb_update();
    psf_init();
    fbtty_init();
    return;
fail:
    heap_free(s_backbuffer);
}
