#include "psf.h"
#include <kernel/mem/heap.h>
#include <kernel/status.h>
#include <kernel/io/tty.h>
#include <kernel/lib/miscmath.h>
#include <kernel/panic.h>
#include <stddef.h>
#include <stdint.h>

static uint16_t const PSF1_MAGIC = 0x0436; 
static uint32_t const PSF2_MAGIC = 0x864ab572;

static uint16_t *s_unicodetable = NULL;
static uint8_t *s_glyphs = NULL;
static uint32_t s_fontwidth;
static uint32_t s_fontheight;
static size_t s_bytesperglyph;
static size_t s_glyphcount;

// https://scripts.sil.org/cms/scripts/page.php?id=iws-appendixa&site_id=nrsi
static FAILABLE_FUNCTION utf8_getbyteslen(size_t *out, uint8_t leadingbyte) {
FAILABLE_PROLOGUE
    size_t bytelen;
    if (leadingbyte < 128) {
        bytelen = 1;
    } else if ((leadingbyte & 0xe0) == 0xc0) {
        bytelen = 2;
    } else if ((leadingbyte & 0xf0) == 0xe0) {
        bytelen = 3;
    } else if ((leadingbyte & 0xf8) == 0xf0) {
        bytelen = 4;
    } else {
        THROW(ERR_INVAL);
    }
    *out = bytelen;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
} 

// WARNING: Be sure to get how much bytes are needed before calling this function,
//          and make sure this function will not cause index out-of-bounds error.
// https://scripts.sil.org/cms/scripts/page.php?id=iws-appendixa&site_id=nrsi
static FAILABLE_FUNCTION utf8_tocodepoint(uint32_t *out, uint8_t *buf) {
FAILABLE_PROLOGUE
    if (buf[0] < 128) {
        *out = buf[0];
    } else if ((buf[0] & 0xe0) == 0xc0) {
        if ((buf[1] & 0xc0) != 0x80) {
            THROW(ERR_INVAL);
        }
        *out = ((uint32_t)(buf[0] & 0x1f) << 6) |
               (uint32_t)(buf[1] & 0x3f);
    } else if ((buf[0] & 0xf0) == 0xe0) {
        if (((buf[1] & 0xc0) != 0x80) || ((buf[2] & 0xc0) != 0x80)) {
            THROW(ERR_INVAL);
        }
        *out = ((uint32_t)(buf[0] & 0x0f) << 12) |
               ((uint32_t)(buf[1] & 0x3f) << 6) |
               (uint32_t)(buf[2] & 0x3f);
    } else if ((buf[0] & 0xf8) == 0xf0) {
        if (((buf[1] & 0xc0) != 0x80) || ((buf[2] & 0xc0) != 0x80) || ((buf[3] & 0xc0) != 0x80)) {
            THROW(ERR_INVAL);
        }
        *out = ((uint32_t)(buf[0] & 0x07) << 18) |
               ((uint32_t)(buf[1] & 0x3f) << 12) |
               ((uint32_t)(buf[2] & 0x3f) << 6) |
               (uint32_t)(buf[3] & 0x3f);
    } else {
        THROW(ERR_INVAL);
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

void psf_init(void) {
    uint8_t *data = _binary_kernelfont_psf_start;
    uint16_t magic16 = uint16leat(data);
    if (magic16 == PSF1_MAGIC) {
        panic("PSF1 fonts are not supported");
    }
    uint32_t magic32 = uint32leat(data);
    if (magic32 != PSF2_MAGIC) {
        panic("Invalid PSF2 magic");
    }
    uint32_t version       = uint32leat(data + 4);
    if (version != 0) {
        tty_printf("psf: font version is not 0(got %u) - Not guranteed to work!\n", version);
    }
    uint32_t headersize    = uint32leat(data + 8);
    uint32_t flags         = uint32leat(data + 12);
    uint32_t numglyph      = uint32leat(data + 16);
    uint32_t bytesperglyph = uint32leat(data + 20);
    uint32_t height        = uint32leat(data + 24);
    uint32_t width         = uint32leat(data + 28);
    tty_printf("psf: font %ux%u headersize %u, flags %u, numglyph %u bytesperglyph %u\n", width, height, headersize, flags, numglyph, bytesperglyph);
    s_glyphs = data + headersize;
    s_fontwidth = width;
    s_fontheight = height;
    s_bytesperglyph = bytesperglyph;
    s_glyphcount = numglyph;
    if (!(flags & 0x1)) {
        goto unicodetabledone;
    }
    // Read unicode translation table
    if ((SIZE_MAX / bytesperglyph) < numglyph) {
        tty_printf("psf: glyph table is too large - cannot locate unicode translation table\n");
        goto unicodetabledone;
    }
    s_unicodetable = heap_calloc(sizeof(*s_unicodetable), UINT16_MAX, HEAP_FLAG_ZEROMEMORY);
    if (s_unicodetable == NULL) {
        tty_printf("psf: not enough memory to have unicode translation table\n");
        goto unicodetabledone;
    }
    // Read each UTF-8 entry
    uint32_t glyph = 0;
    uint8_t *nextchr = s_glyphs + (bytesperglyph * numglyph);
    while ((nextchr < _binary_kernelfont_psf_end) && (glyph < numglyph)) {
        uint8_t b = *nextchr;
        if (b == 0xff) {
            // EOL
            nextchr++;
            glyph++;
            continue;
        } 
        size_t byteslen;
        status_t status = utf8_getbyteslen(&byteslen, b);
        uint32_t unicode;
        if (status != OK) {
            tty_printf("psf: utf8_getbyteslen %02x\n", b);
            goto unicodebad;
        }
        // Make sure we don't get EOL or EOF.
        for (size_t i = 1; i < byteslen; i++) {
            if (_binary_kernelfont_psf_end <= &nextchr[i]) {
                tty_printf("psf: &_binary_kernelfont_psf_end <= &nextchr[i]\n");
                goto unicodebad;
            }
            if (nextchr[i] == 0xff) {
                tty_printf("psf: nextchr[i] == 0xff\n");
                goto unicodebad;
            }
        }
        status = utf8_tocodepoint(&unicode, nextchr);
        if (status != OK) {
            tty_printf("psf: utf8_tocodepoint\n");
            goto unicodebad;
        }
        nextchr += byteslen;
        goto unicodedone;
    unicodebad:
        tty_printf("psf: unicode table entry #%zu - illegal utf-8 sequence\n", glyph);
        unicode = b;
        nextchr++;
    unicodedone:
        if (0xffff < unicode) {
            continue;
        }
        s_unicodetable[unicode] = glyph;
    }
unicodetabledone:
    return;
}

size_t psf_getwidth(void) {
    return s_fontwidth;
}

size_t psf_getheight(void) {
    return s_fontheight;
}

size_t psf_getbytesperline(void) {
    return (s_fontwidth + 7) / 8;
}

uint8_t *psf_getglyph(uint32_t chr) {
    if (0xffff < chr) {
        return psf_getglyph('?');
    }
    size_t glyphindex;
    if (s_unicodetable) {
        glyphindex = s_unicodetable[chr];
    } else {
        glyphindex = chr;
    }
    // Make sure we don't access outside of the font.
    glyphindex %= s_glyphcount;
    return &s_glyphs[s_bytesperglyph * glyphindex];
}
