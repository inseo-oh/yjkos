#include "../test.h"
#include <kernel/lib/strutil.h>
#include <stddef.h>

static bool do_slice(void) {
    struct smatcher smatcher;
    struct smatcher newsmatcher;
    smatcher_init(&smatcher, "hello world people");
    smatcher_slice(&newsmatcher, &smatcher, 6, 10);
    TEST_EXPECT(smatcher_consume_str_if_match(&newsmatcher, "world") == true);
    TEST_EXPECT(newsmatcher.currentindex == 5);

    return true;
}

static bool do_consume_string_if_match(void) {
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello world people", 11);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "hello1") == false);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "world") == false);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "hello") == true);
    TEST_EXPECT(smatcher.currentindex == 5);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "hello") == false);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "world") == false);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, " world") == true);
    TEST_EXPECT(smatcher.currentindex == 11);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, " people") == false);

    return true;
}

static bool do_consume_word_if_match(void) {
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello worldpeopleguy", 17);
    TEST_EXPECT(smatcher_consume_word_if_match(&smatcher, "world") == false);
    TEST_EXPECT(smatcher_consume_word_if_match(&smatcher, "hello") == true);
    TEST_EXPECT(smatcher.currentindex == 5);
    TEST_EXPECT(smatcher_consume_word_if_match(&smatcher, "hello") == false);
    TEST_EXPECT(smatcher_consume_word_if_match(&smatcher, " world") == false);
    TEST_EXPECT(smatcher_consume_word_if_match(&smatcher, " worldpeople") == true);
    TEST_EXPECT(smatcher.currentindex == 17);

    return true;
}

static bool do_skip_whitespaces(void) {
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello    worldpeople", 14);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "hello") == true);
    smatcher_skip_whitespaces(&smatcher);
    TEST_EXPECT(smatcher.currentindex == 9);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "world") == true);
    TEST_EXPECT(smatcher.currentindex == 14);
    TEST_EXPECT(smatcher_consume_str_if_match(&smatcher, "people") == false);

    return true;
}

static bool do_consume_word(void) {
    char const *str = NULL;
    size_t len = 0;
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello worldpeopleguy", 17);
    TEST_EXPECT(smatcher_consume_word(&str, &len, &smatcher) == true);
    TEST_EXPECT(kstrncmp(str, "hello", len) == 0);
    TEST_EXPECT(smatcher_consume_word(&str, &len, &smatcher) == false);
    smatcher_skip_whitespaces(&smatcher);
    TEST_EXPECT(smatcher_consume_word(&str, &len, &smatcher) == true);
    TEST_EXPECT(kstrncmp(str, "worldpeople", len) == 0);
    TEST_EXPECT(smatcher_consume_word(&str, &len, &smatcher) == false);

    return true;
}

static struct test const TESTS[] = {
    {.name = "slice", .fn = do_slice},
    {.name = "consume_string_if_match", .fn = do_consume_string_if_match},
    {.name = "consume_word_if_match", .fn = do_consume_word_if_match},
    {.name = "skip_whitespaces", .fn = do_skip_whitespaces},
    {.name = "consume_word", .fn = do_consume_word},
};

const struct test_group TESTGROUP_SMATCHER = {
    .name = "smatcher",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
