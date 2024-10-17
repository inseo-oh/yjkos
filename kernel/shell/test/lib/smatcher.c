#include "../../shell.h"
#include "../test.h"
#include <kernel/lib/smatcher.h>
#include <string.h>

static bool do_slice(void) {
    struct smatcher smatcher, newsmatcher;
    smatcher_init(&smatcher, "hello world people");
    smatcher_slice(&newsmatcher, &smatcher, 6, 10);
    TEST_EXPECT(smatcher_consumestringifmatch(&newsmatcher, "world")  == true);
    TEST_EXPECT(newsmatcher.currentindex == 5);

    return true;
}

static bool do_consumestringifmatch(void) {
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello world people", 11);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "hello1") == false);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "world")  == false);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "hello")  == true);
    TEST_EXPECT(smatcher.currentindex == 5);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "hello")  == false);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "world")  == false);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, " world") == true);
    TEST_EXPECT(smatcher.currentindex == 11);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, " people") == false);

    return true;
}

static bool do_consumewordifmatch(void) {
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello worldpeopleguy", 17);
    TEST_EXPECT(smatcher_consumewordifmatch(&smatcher, "world")  == false);
    TEST_EXPECT(smatcher_consumewordifmatch(&smatcher, "hello")  == true);
    TEST_EXPECT(smatcher.currentindex == 5);
    TEST_EXPECT(smatcher_consumewordifmatch(&smatcher, "hello")  == false);
    TEST_EXPECT(smatcher_consumewordifmatch(&smatcher, " world")  == false);
    TEST_EXPECT(smatcher_consumewordifmatch(&smatcher, " worldpeople") == true);
    TEST_EXPECT(smatcher.currentindex == 17);

    return true;
}

static bool do_skipwhitespaces(void) {
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello    worldpeople", 14);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "hello")  == true);
    smatcher_skipwhitespaces(&smatcher);
    TEST_EXPECT(smatcher.currentindex == 9);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "world")  == true);
    TEST_EXPECT(smatcher.currentindex == 14);
    TEST_EXPECT(smatcher_consumestringifmatch(&smatcher, "people")  == false);

    return true;
}

static bool do_consumeword(void) {
    char const *str;
    size_t len;
    struct smatcher smatcher;
    smatcher_init_with_len(&smatcher, "hello worldpeopleguy", 17);
    TEST_EXPECT(smatcher_consumeword(&str, &len, &smatcher) == true);
    TEST_EXPECT(strncmp(str, "hello", len) == 0);
    TEST_EXPECT(smatcher_consumeword(&str, &len, &smatcher) == false);
    smatcher_skipwhitespaces(&smatcher);
    TEST_EXPECT(smatcher_consumeword(&str, &len, &smatcher) == true);
    TEST_EXPECT(strncmp(str, "worldpeople", len) == 0);
    TEST_EXPECT(smatcher_consumeword(&str, &len, &smatcher) == false);

    return true;
}

static struct test const TESTS[] = {
    { .name = "slice",                .fn = do_slice                 },
    { .name = "consumestringifmatch", .fn = do_consumestringifmatch  },
    { .name = "consumewordifmatch",   .fn = do_consumewordifmatch    },
    { .name = "skipwhitespaces",      .fn = do_skipwhitespaces       },
    { .name = "consumeword",          .fn = do_consumeword           },
};

const struct testgroup TESTGROUP_SMATCHER = {
    .name = "smatcher",
    .tests = TESTS,
    .testslen = sizeof(TESTS)/sizeof(*TESTS),
};
