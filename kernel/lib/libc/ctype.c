#include "ctype.h"
#include <stdint.h>

typedef uint8_t CHARCLASS_TYPE;

static CHARCLASS_TYPE const CCLASS_FLAG_SPACE = 1U << 0U;
static CHARCLASS_TYPE const CCLASS_FLAG_DIGIT = 1U << 1U;

static CHARCLASS_TYPE const CHARS[128] = {
    ['0'] = CCLASS_FLAG_DIGIT,
    ['1'] = CCLASS_FLAG_DIGIT,
    ['2'] = CCLASS_FLAG_DIGIT,
    ['3'] = CCLASS_FLAG_DIGIT,
    ['4'] = CCLASS_FLAG_DIGIT,
    ['5'] = CCLASS_FLAG_DIGIT,
    ['6'] = CCLASS_FLAG_DIGIT,
    ['7'] = CCLASS_FLAG_DIGIT,
    ['8'] = CCLASS_FLAG_DIGIT,
    ['9'] = CCLASS_FLAG_DIGIT,
    ['\t'] = CCLASS_FLAG_SPACE,
    [' '] = CCLASS_FLAG_SPACE,
};

static int const CCLASS_MAX_CHAR = (sizeof(CHARS) / sizeof(*CHARS)) - 1;

int isspace(int c) {
    if ((c < 0) || (CCLASS_MAX_CHAR <= c)) {
        return 0;
    }
    return (CHARS[c] & CCLASS_FLAG_SPACE) != 0;
}

int isdigit(int c) {
    if ((c < 0) || (CCLASS_MAX_CHAR <= c)) {
        return 0;
    }
    return (CHARS[c] & CCLASS_FLAG_DIGIT) != 0;
}
