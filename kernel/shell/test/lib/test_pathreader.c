#include "../test.h"
#include <kernel/lib/pathreader.h>

#include <errno.h>
#include <string.h>

static bool do_simple(void) {
    char const *str = NULL;
    struct PathReader reader;

    PathReader_Init(&reader, "hello/world");
    TEST_EXPECT(PathReader_Next(&str, &reader) == 0);
    TEST_EXPECT(strcmp(str, "hello") == 0);
    TEST_EXPECT(PathReader_Next(&str, &reader) == 0);
    TEST_EXPECT(strcmp(str, "world") == 0);
    TEST_EXPECT(PathReader_Next(&str, &reader) == -ENOENT);

    return true;
}

static bool do_empty(void) {
    char const *str = NULL;
    struct PathReader reader;

    PathReader_Init(&reader, "");
    TEST_EXPECT(PathReader_Next(&str, &reader) == -ENOENT);

    return true;
}

static bool do_empty_segments(void) {
    char const *str = NULL;
    struct PathReader reader;

    PathReader_Init(&reader, "hello//world");
    TEST_EXPECT(PathReader_Next(&str, &reader) == 0);
    TEST_EXPECT(strcmp(str, "hello") == 0);
    TEST_EXPECT(PathReader_Next(&str, &reader) == 0);
    TEST_EXPECT(strcmp(str, "world") == 0);
    TEST_EXPECT(PathReader_Next(&str, &reader) == -ENOENT);

    return true;
}

static bool do_trailing_slash(void) {
    char const *str = NULL;
    struct PathReader reader;

    PathReader_Init(&reader, "hello/world/");
    TEST_EXPECT(PathReader_Next(&str, &reader) == 0);
    TEST_EXPECT(strcmp(str, "hello") == 0);
    TEST_EXPECT(PathReader_Next(&str, &reader) == 0);
    TEST_EXPECT(strcmp(str, "world") == 0);
    TEST_EXPECT(PathReader_Next(&str, &reader) == -ENOENT);

    return true;
}

static struct Test const TESTS[] = {
    { .name = "simple",              .fn = do_simple         },
    { .name = "empty",               .fn = do_empty          },
    { .name = "with empty segments", .fn = do_empty_segments },
    { .name = "with trailing slash", .fn = do_trailing_slash },
};

const struct TestGroup TESTGROUP_PATHREADER = {
    .name = "pathreader",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
