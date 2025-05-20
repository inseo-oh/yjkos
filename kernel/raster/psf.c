#include "psf.h"
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <stddef.h>
#include <stdint.h>

static uint16_t const PSF1_MAGIC = 0x0436;
static uint32_t const PSF2_MAGIC = 0x864ab572;

static uint16_t *s_unicodetable = NULL;
static uint8_t *s_glyphs = NULL;
static int s_fontwidth;
static int32_t s_fontheight;
static size_t s_bytesperglyph;
static size_t s_glyphcount;

/*
 * Returns -1 if if input is invalid.
 */
[[nodiscard]] int utf8_get_bytes_len(uint8_t leading_byte) {
    /* https://scripts.sil.org/cms/scripts/page.php?id=iws-appendixa&site_id=nrsi */
    if (leading_byte < 128) {
        return 1;
    }
    if ((leading_byte & 0xe0) == 0xc0) {
        return 2;
    }
    if ((leading_byte & 0xf0) == 0xe0) {
        return 3;
    }
    if ((leading_byte & 0xf8) == 0xf0) {
        return 4;
    }
    return -1;
}

/*
 * WARNING: Be sure to get how much bytes are needed before calling this function,
 *          and make sure this function will not cause index out-of-bounds error.
 *
 * Returns -1 if input is invalid.
 */
static int32_t utf8_to_codepoint(uint8_t const *buf) {
    /* https://scripts.sil.org/cms/scripts/page.php?id=iws-appendixa&site_id=nrsi */
    uint32_t result = 0;

    if (buf[0] < 128) {
        result = buf[0];
    } else if ((buf[0] & 0xe0) == 0xc0) {
        if ((buf[1] & 0xc0) != 0x80) {
            goto badcode;
        }
        result = ((uint32_t)(buf[0] & 0x1fU) << 6) |
                 (uint32_t)(buf[1] & 0x3fU);
    } else if ((buf[0] & 0xf0) == 0xe0) {
        if (((buf[1] & 0xc0) != 0x80) || ((buf[2] & 0xc0) != 0x80)) {
            goto badcode;
        }
        result = ((uint32_t)(buf[0] & 0x0fU) << 12) |
                 ((uint32_t)(buf[1] & 0x3fU) << 6) |
                 (uint32_t)(buf[2] & 0x3fU);
    } else if ((buf[0] & 0xf8) == 0xf0) {
        if (((buf[1] & 0xc0) != 0x80) || ((buf[2] & 0xc0) != 0x80) || ((buf[3] & 0xc0) != 0x80)) {
            goto badcode;
        }
        result = ((uint32_t)(buf[0] & 0x07) << 18) |
                 ((uint32_t)(buf[1] & 0x3f) << 12) |
                 ((uint32_t)(buf[2] & 0x3f) << 6) |
                 (uint32_t)(buf[3] & 0x3fU);
    } else {
        goto badcode;
    }
    return (int32_t)result;
badcode:
    return -1;
}

static void read_psf1_utf16_entries(bool *had_multi_chars_out, uint32_t bytes_per_glyph, uint32_t num_glyph) {
    uint32_t glyph = 0;
    uint8_t *nextchr = s_glyphs + (bytes_per_glyph * num_glyph);
    bool had_multi_chars = false;
    bool gotchar = false;
    while ((nextchr < _binary_kernelfont_psf_end) && (glyph < num_glyph)) {
        uint16_t unicode = u16le_at(nextchr);
        if (unicode == 0xffff) {
            /* EOL */
            nextchr += 2;
            glyph++;
            gotchar = false;
            continue;
        }
        if (unicode == 0xfffe) {
            /* Mapping to unicode character sequences is not supported */
            nextchr += 2;
            continue;
        }
        nextchr += 2;
        goto unicodedone;
        nextchr++;
    unicodedone:
        if (gotchar) {
            had_multi_chars = true;
        }
        gotchar = true;
        s_unicodetable[unicode] = glyph;
    }
    *had_multi_chars_out = had_multi_chars;
}

static void init_psf1(void) {
    uint8_t *data = _binary_kernelfont_psf_start;
    uint8_t mode = data[2];
    uint32_t width = 8;
    uint32_t height = data[3];
    uint32_t num_glyph = 256;
    uint32_t headersize = 4;
    if ((mode & 0x01U)) {
        num_glyph = 512;
    }
    co_printf("psf: v1 font %ux%u mode %u, num_glyph %u\n", width, height, mode, num_glyph);
    uint32_t bytes_per_glyph = height;
    if (!(mode & (0x02U | 0x04U))) {
        goto unicodetabledone;
    }
    if ((SIZE_MAX / bytes_per_glyph) < num_glyph) {
        co_printf("psf: glyph table is too large - cannot locate unicode translation table\n");
        goto unicodetabledone;
    }
    s_unicodetable = heap_calloc(sizeof(*s_unicodetable), UINT16_MAX, HEAP_FLAG_ZEROMEMORY);
    s_glyphs = data + headersize;
    assert(width < 32767);
    assert(height < 32767);
    s_fontwidth = (int)width;
    s_fontheight = (int)height;
    s_bytesperglyph = bytes_per_glyph;
    s_glyphcount = num_glyph;
    if (s_unicodetable == NULL) {
        co_printf("psf: not enough memory to have unicode translation table\n");
        goto unicodetabledone;
    }
    /* Read each UTF-16 entry **************************************************/
    bool had_multi_chars;
    read_psf1_utf16_entries(&had_multi_chars, bytes_per_glyph, num_glyph);
    if (had_multi_chars) {
        co_printf("psf: mapping to unicode character sequences is not supported\n");
    }
unicodetabledone:
    return;
}

static bool psf2_will_eol_or_eof(uint8_t const *base, int len) {
    for (int i = 1; i < len; i++) {
        if (_binary_kernelfont_psf_end <= &base[i]) {
            return true;
        }
        if (base[i] == 0xff) {
            return true;
        }
    }
    return false;
}

static void read_psf2_utf8_entries(bool *had_multi_chars_out, uint32_t bytes_per_glyph, uint32_t num_glyph) {
    uint32_t glyph = 0;
    uint8_t *next_chr = s_glyphs + (bytes_per_glyph * num_glyph);
    bool had_multi_chars = false;
    bool gotchar = false;
    while ((next_chr < _binary_kernelfont_psf_end) && (glyph < num_glyph)) {
        uint8_t byt = *next_chr;
        if (byt == 0xff) {
            /* EOL */
            next_chr++;
            glyph++;
            gotchar = false;
            continue;
        }
        if (byt == 0xfe) {
            /* Mapping to unicode character sequences is not supported */
            next_chr++;
            continue;
        }
        int bytes_len = utf8_get_bytes_len(byt);
        int32_t unicode = 0;
        if (bytes_len < 0) {
            goto badcode;
        }
        /* Make sure we don't get EOL or EOF. */
        if (psf2_will_eol_or_eof(next_chr, bytes_len)) {
            goto badcode;
        }
        unicode = utf8_to_codepoint(next_chr);
        if (unicode < 0) {
            goto badcode;
        }
        next_chr += bytes_len;
        goto unicodedone;
    badcode:
        co_printf("psf: unicode table entry #%zu - illegal utf-8 sequence\n", glyph);
        unicode = byt;
        next_chr++;
    unicodedone:
        if (0xffff < unicode) {
            continue;
        }
        if (gotchar) {
            had_multi_chars = true;
        }
        gotchar = true;
        s_unicodetable[unicode] = glyph;
    }
    *had_multi_chars_out = had_multi_chars;
}

static void init_psf2(void) {
    uint8_t *data = _binary_kernelfont_psf_start;
    uint32_t version = u32le_at(data + 4);
    if (version != 0) {
        co_printf("psf: font version is not 0(got %u) - Not guranteed to work!\n", version);
    }
    uint32_t headersize = u32le_at(data + 8);
    uint32_t flags = u32le_at(data + 12);
    uint32_t num_glyph = u32le_at(data + 16);
    uint32_t bytes_per_glyph = u32le_at(data + 20);
    uint32_t height = u32le_at(data + 24);
    uint32_t width = u32le_at(data + 28);
    co_printf("psf: v2 font %ux%u headersize %u, flags %u, num_glyph %u\n", width, height, headersize, flags, num_glyph);
    s_glyphs = data + headersize;
    assert(width < 32767);
    assert(height < 32767);
    s_fontwidth = (int)width;
    s_fontheight = (int)height;
    s_bytesperglyph = bytes_per_glyph;
    s_glyphcount = num_glyph;
    if (!(flags & 0x1U)) {
        goto unicode_table_done;
    }
    /* Read unicode translation table *****************************************/
    if ((SIZE_MAX / bytes_per_glyph) < num_glyph) {
        co_printf("psf: glyph table is too large - cannot locate unicode translation table\n");
        goto unicode_table_done;
    }
    s_unicodetable = heap_calloc(sizeof(*s_unicodetable), UINT16_MAX, HEAP_FLAG_ZEROMEMORY);
    if (s_unicodetable == NULL) {
        co_printf("psf: not enough memory to have unicode translation table\n");
        goto unicode_table_done;
    }
    /* Read each UTF-8 entry **************************************************/
    bool had_multi_chars = false;
    read_psf2_utf8_entries(&had_multi_chars, bytes_per_glyph, num_glyph);
    if (had_multi_chars) {
        co_printf("psf: mapping to unicode character sequences is not supported\n");
    }
unicode_table_done:
    return;
}

void psf_init(void) {
    uint8_t *data = _binary_kernelfont_psf_start;
    uint16_t magic16 = u16le_at(data);
    if (magic16 == PSF1_MAGIC) {
        init_psf1();
        return;
    }
    uint32_t magic32 = u32le_at(data);
    if (magic32 == PSF2_MAGIC) {
        init_psf2();
        return;
    }
    panic("Invalid PSF magic");
}

int psf_getwidth(void) {
    return s_fontwidth;
}

int psf_getheight(void) {
    return s_fontheight;
}

size_t psf_getbytesperline(void) {
    return (s_fontwidth + 7) / 8;
}

uint8_t *psf_getglyph(uint32_t chr) {
    if (0xffff < chr) {
        chr = '?';
    }
    size_t glyphindex = chr;
    if (s_unicodetable != NULL) {
        glyphindex = s_unicodetable[chr];
    }
    /* Make sure we don't access outside of the font. */
    glyphindex %= s_glyphcount;
    return &s_glyphs[s_bytesperglyph * glyphindex];
}
