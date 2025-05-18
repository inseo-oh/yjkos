#include "../test.h"
#include <kernel/lib/bitmap.h>
#include <kernel/types.h>

static bool do_make_bitmask(void) {
    TEST_EXPECT(MakeBitmask(0, 0) == 0);
    TEST_EXPECT(MakeBitmask(1, 0) == 0);
    TEST_EXPECT(MakeBitmask(1, 1) == (0x1U << 1U));
    TEST_EXPECT(MakeBitmask(2, 2) == (0x3U << 2U));
    TEST_EXPECT(MakeBitmask(12, 3) == (0x7U << 12U));
    TEST_EXPECT(MakeBitmask(29, 3) == (0x7UL << 29U));
    return true;
}

static bool do_find_first_set_bit(void) {
    struct Bitmap bmp;
    UINT words[] = {
        0xe0ddf00d, /* 11100000110111011111000000001101 */
        0x10abcdef, /* 00010000101010111100110111101111 */
        0xcafefeed, /* 11001010111111101111111011101101 */
    };
    bmp.words = words;
    bmp.word_count = sizeof(words) / sizeof(*words);

    TEST_EXPECT(Bitmap_FindFirstSetBit(&bmp, 0) == 0);
    TEST_EXPECT(Bitmap_FindFirstSetBit(&bmp, 1) == 2);
    TEST_EXPECT(Bitmap_FindFirstSetBit(&bmp, 24) == 29);
    TEST_EXPECT(Bitmap_FindFirstSetBit(&bmp, 61) == 64);
    TEST_EXPECT(Bitmap_FindFirstSetBit(&bmp, -1) == -1);
    TEST_EXPECT(Bitmap_FindFirstSetBit(&bmp, 0xfffff) == -1);
    return true;
}

static bool do_find_last_contiguous_bit(void) {
    struct Bitmap bmp;
    UINT words[] = {
        0xe0ddf00d, /* 11100000110111011111000000001101 */
        0x90abcdef, /* 10010000101010111100110111101111 */
        0xcafefeed, /* 11001010111111101111111011101101 */
        0xf6543210, /* 11110110010101000011001000010000 */
    };
    bmp.words = words;
    bmp.word_count = sizeof(words) / sizeof(*words);

    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 0) == 0);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 1) == -1);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 29) == 35);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 63) == 64);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 64) == 64);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 124) == 127);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, -1) == -1);
    TEST_EXPECT(Bitmap_FindLastContiguousBit(&bmp, 0xfffff) == -1);
    return true;
}

static bool do_are_bits_set(void) {
    struct Bitmap bmp;
    UINT words[] = {
        0xe0ddf00d, /* 11100000110111011111000000001101 */
        0x90abcdef, /* 10010000101010111100110111101111 */
        0xffffffff, /* 11111111111111111111111111111111 */
        0xffffffff, /* 11111111111111111111111111111111 */
    };
    bmp.words = words;
    bmp.word_count = sizeof(words) / sizeof(*words);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 0, 1) == true);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 1, 1) == false);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 2, 1) == true);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 2, 2) == true);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 2, 3) == false);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 29, 7) == true);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 29, 8) == false);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 64, 64) == true);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 64, 65) == false);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 0x7fffff, 6) == false);
    TEST_EXPECT(Bitmap_AreBitsSet(&bmp, 31, 0x7fffff) == false);
    return true;
}

static bool do_set_bits(void) {
    struct Bitmap bmp;
    UINT words[] = {
        0,
        0,
        0,
    };
    bmp.words = words;
    bmp.word_count = sizeof(words) / sizeof(*words);
    Bitmap_SetBits(&bmp, 0, 1);
    TEST_EXPECT(words[0] == 0x00000001);
    TEST_EXPECT(words[1] == 0x00000000);
    TEST_EXPECT(words[2] == 0x00000000);
    Bitmap_SetBits(&bmp, 4, 4);
    TEST_EXPECT(words[0] == 0x000000F1);
    TEST_EXPECT(words[1] == 0x00000000);
    TEST_EXPECT(words[2] == 0x00000000);
    Bitmap_SetBits(&bmp, 28, 4);
    TEST_EXPECT(words[0] == 0xf00000f1);
    TEST_EXPECT(words[1] == 0x00000000);
    TEST_EXPECT(words[2] == 0x00000000);
    words[0] = 0;
    Bitmap_SetBits(&bmp, 1, 64);
    TEST_EXPECT(words[0] == 0xfffffffe);
    TEST_EXPECT(words[1] == 0xffffffff);
    TEST_EXPECT(words[2] == 0x00000001);
    return true;
}

static bool do_clear_bits(void) {
    struct Bitmap bmp;
    UINT words[] = {
        0xffffffff,
        0xffffffff,
        0xffffffff,
    };
    bmp.words = words;
    bmp.word_count = sizeof(words) / sizeof(*words);
    Bitmap_ClearBits(&bmp, 0, 1);
    TEST_EXPECT(words[0] == 0xfffffffe);
    TEST_EXPECT(words[1] == 0xffffffff);
    TEST_EXPECT(words[2] == 0xffffffff);
    Bitmap_ClearBits(&bmp, 4, 4);
    TEST_EXPECT(words[0] == 0xffffff0e);
    TEST_EXPECT(words[1] == 0xffffffff);
    TEST_EXPECT(words[2] == 0xffffffff);
    Bitmap_ClearBits(&bmp, 28, 4);
    TEST_EXPECT(words[0] == 0x0FFFFF0E);
    TEST_EXPECT(words[1] == 0xffffffff);
    TEST_EXPECT(words[2] == 0xffffffff);
    words[0] = 0xffffffff;
    Bitmap_ClearBits(&bmp, 1, 64);
    TEST_EXPECT(words[0] == 0x00000001);
    TEST_EXPECT(words[1] == 0x00000000);
    TEST_EXPECT(words[2] == 0xfffffffe);
    return true;
}

static bool do_find_set_bits(void) {
    struct Bitmap bmp;
    UINT words[] = {
        0xe0ddf00d, /* 11100000110111011111000000001101 */
        0x10abcdef, /* 00010000101010111100110111101111 */
        0xffffffff, /* 11111111111111111111111111111111 */
        0xffffffff, /* 11111111111111111111111111111111 */
    };
    bmp.words = words;
    bmp.word_count = sizeof(words) / sizeof(*words);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 0, 1) == 0);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 1, 2) == 2);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 13, 6) == 29);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 45, 63) == 64);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 45, 64) == 64);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 45, 65) == -1);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 0x7fffff, 6) == -1);
    TEST_EXPECT(Bitmap_FindSetBits(&bmp, 31, 0x7fffff) == -1);
    return true;
}

static struct Test const TESTS[] = {
    {.name = "make_bitmask", .fn = do_make_bitmask},
    {.name = "find_first_set_bit", .fn = do_find_first_set_bit},
    {.name = "find_last_contiguous_bit", .fn = do_find_last_contiguous_bit},
    {.name = "find_set_bits", .fn = do_find_set_bits},
    {.name = "are_bits_set", .fn = do_are_bits_set},
    {.name = "set_bits", .fn = do_set_bits},
    {.name = "clear_bits", .fn = do_clear_bits},
};

const struct TestGroup TESTGROUP_BITMAP = {
    .name = "bitmap",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
