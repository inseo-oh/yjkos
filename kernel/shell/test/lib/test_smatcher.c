#include "../test.h"
#include <kernel/lib/smatcher.h>
#include <string.h>

static bool do_slice(void) {
    struct SMatcher smatcher;
    struct SMatcher newsmatcher;
    Smatcher_Init(&smatcher, "hello world people");
    Smatcher_Slice(&newsmatcher, &smatcher, 6, 10);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&newsmatcher, "world") == true);
    TEST_EXPECT(newsmatcher.currentindex == 5);

    return true;
}

static bool do_consume_string_if_match(void) {
    struct SMatcher smatcher;
    Smatcher_InitWithLen(&smatcher, "hello world people", 11);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "hello1") == false);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "world") == false);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "hello") == true);
    TEST_EXPECT(smatcher.currentindex == 5);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "hello") == false);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "world") == false);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, " world") == true);
    TEST_EXPECT(smatcher.currentindex == 11);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, " people") == false);

    return true;
}

static bool do_consume_word_if_match(void) {
    struct SMatcher smatcher;
    Smatcher_InitWithLen(&smatcher, "hello worldpeopleguy", 17);
    TEST_EXPECT(Smatcher_ConsumeWordIfMatch(&smatcher, "world") == false);
    TEST_EXPECT(Smatcher_ConsumeWordIfMatch(&smatcher, "hello") == true);
    TEST_EXPECT(smatcher.currentindex == 5);
    TEST_EXPECT(Smatcher_ConsumeWordIfMatch(&smatcher, "hello") == false);
    TEST_EXPECT(Smatcher_ConsumeWordIfMatch(&smatcher, " world") == false);
    TEST_EXPECT(Smatcher_ConsumeWordIfMatch(&smatcher, " worldpeople") == true);
    TEST_EXPECT(smatcher.currentindex == 17);

    return true;
}

static bool do_skip_whitespaces(void) {
    struct SMatcher smatcher;
    Smatcher_InitWithLen(&smatcher, "hello    worldpeople", 14);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "hello") == true);
    Smatcher_SkipWhitespaces(&smatcher);
    TEST_EXPECT(smatcher.currentindex == 9);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "world") == true);
    TEST_EXPECT(smatcher.currentindex == 14);
    TEST_EXPECT(Smatcher_ConsumeStrIfMatch(&smatcher, "people") == false);

    return true;
}

static bool do_consume_word(void) {
    char const *str = NULL;
    size_t len = 0;
    struct SMatcher smatcher;
    Smatcher_InitWithLen(&smatcher, "hello worldpeopleguy", 17);
    TEST_EXPECT(Smatcher_ConsumeWord(&str, &len, &smatcher) == true);
    TEST_EXPECT(strncmp(str, "hello", len) == 0);
    TEST_EXPECT(Smatcher_ConsumeWord(&str, &len, &smatcher) == false);
    Smatcher_SkipWhitespaces(&smatcher);
    TEST_EXPECT(Smatcher_ConsumeWord(&str, &len, &smatcher) == true);
    TEST_EXPECT(strncmp(str, "worldpeople", len) == 0);
    TEST_EXPECT(Smatcher_ConsumeWord(&str, &len, &smatcher) == false);

    return true;
}

static struct Test const TESTS[] = {
    {.name = "slice", .fn = do_slice},
    {.name = "consume_string_if_match", .fn = do_consume_string_if_match},
    {.name = "consume_word_if_match", .fn = do_consume_word_if_match},
    {.name = "skip_whitespaces", .fn = do_skip_whitespaces},
    {.name = "consume_word", .fn = do_consume_word},
};

const struct TestGroup TESTGROUP_SMATCHER = {
    .name = "smatcher",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
