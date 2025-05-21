#include "../test.h"
#include <errno.h>
#include <kernel/lib/pathreader.h>
#include <kernel/lib/strutil.h>

static bool do_simple(void) {
    char const *str = NULL;
    struct path_reader reader;

    pathreader_init(&reader, "hello/world");
    TEST_EXPECT(pathreader_next(&str, &reader) == 0);
    TEST_EXPECT(str_cmp(str, "hello") == 0);
    TEST_EXPECT(pathreader_next(&str, &reader) == 0);
    TEST_EXPECT(str_cmp(str, "world") == 0);
    TEST_EXPECT(pathreader_next(&str, &reader) == -ENOENT);

    return true;
}

static bool do_empty(void) {
    char const *str = NULL;
    struct path_reader reader;

    pathreader_init(&reader, "");
    TEST_EXPECT(pathreader_next(&str, &reader) == -ENOENT);

    return true;
}

static bool do_empty_segments(void) {
    char const *str = NULL;
    struct path_reader reader;

    pathreader_init(&reader, "hello//world");
    TEST_EXPECT(pathreader_next(&str, &reader) == 0);
    TEST_EXPECT(str_cmp(str, "hello") == 0);
    TEST_EXPECT(pathreader_next(&str, &reader) == 0);
    TEST_EXPECT(str_cmp(str, "world") == 0);
    TEST_EXPECT(pathreader_next(&str, &reader) == -ENOENT);

    return true;
}

static bool do_trailing_slash(void) {
    char const *str = NULL;
    struct path_reader reader;

    pathreader_init(&reader, "hello/world/");
    TEST_EXPECT(pathreader_next(&str, &reader) == 0);
    TEST_EXPECT(str_cmp(str, "hello") == 0);
    TEST_EXPECT(pathreader_next(&str, &reader) == 0);
    TEST_EXPECT(str_cmp(str, "world") == 0);
    TEST_EXPECT(pathreader_next(&str, &reader) == -ENOENT);

    return true;
}

static struct test const TESTS[] = {
    {.name = "simple", .fn = do_simple},
    {.name = "empty", .fn = do_empty},
    {.name = "with empty segments", .fn = do_empty_segments},
    {.name = "with trailing slash", .fn = do_trailing_slash},
};

const struct test_group TESTGROUP_PATHREADER = {
    .name = "pathreader",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
