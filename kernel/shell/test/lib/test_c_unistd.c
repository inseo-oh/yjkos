#include "../test.h"
#include <kernel/lib/strutil.h>
#include <unistd.h>

static bool do_getopt(void) {
    opterr = 0;
    char const *options = "+:iroha:";
    char *argv[7] = {"<if you see me, something went wrong>"};
    optind = 1;
    TEST_EXPECT(getopt(1, argv, options) == -1);
    TEST_EXPECT(optind == 1);
    argv[1] = "-i";
    TEST_EXPECT(getopt(2, argv, options) == 'i');
    TEST_EXPECT(optind == 2);
    argv[2] = "-hi";
    TEST_EXPECT(getopt(3, argv, options) == 'h');
    TEST_EXPECT(optind == 2);
    TEST_EXPECT(getopt(3, argv, options) == 'i');
    TEST_EXPECT(optind == 3);
    argv[3] = "-a";
    TEST_EXPECT(getopt(4, argv, options) == ':');
    TEST_EXPECT(optind == 4);
    TEST_EXPECT(optopt == 'a');
    optind = 3;
    argv[3] = "-alove";
    TEST_EXPECT(getopt(4, argv, options) == 'a');
    TEST_EXPECT(optind == 4);
    TEST_EXPECT(kstrcmp(optarg, "love") == 0);
    argv[4] = "-a";
    argv[5] = "daisuki";
    TEST_EXPECT(getopt(6, argv, options) == 'a');
    TEST_EXPECT(optind == 6);
    TEST_EXPECT(kstrcmp(optarg, "daisuki") == 0);
    argv[6] = "-z";
    TEST_EXPECT(getopt(7, argv, options) == '?');
    TEST_EXPECT(optind == 7);
    TEST_EXPECT(optopt == 'z');
    TEST_EXPECT(getopt(7, argv, options) == -1);

    return true;
}

static bool do_getopt_stderr(void) {
    opterr = 1;
    char const *options = "+iroha:";
    char *argv[7] = {"<please ignore this error>"};
    optind = 1;
    TEST_EXPECT(getopt(1, argv, options) == -1);
    TEST_EXPECT(optind == 1);
    argv[1] = "-i";
    TEST_EXPECT(getopt(2, argv, options) == 'i');
    TEST_EXPECT(optind == 2);
    argv[2] = "-hi";
    TEST_EXPECT(getopt(3, argv, options) == 'h');
    TEST_EXPECT(optind == 2);
    TEST_EXPECT(getopt(3, argv, options) == 'i');
    TEST_EXPECT(optind == 3);
    argv[3] = "-a";
    TEST_EXPECT(getopt(4, argv, options) == '?');
    TEST_EXPECT(optind == 4);
    TEST_EXPECT(optopt == 'a');
    optind = 3;
    argv[3] = "-alove";
    TEST_EXPECT(getopt(4, argv, options) == 'a');
    TEST_EXPECT(optind == 4);
    TEST_EXPECT(kstrcmp(optarg, "love") == 0);
    argv[4] = "-a";
    argv[5] = "daisuki";
    TEST_EXPECT(getopt(6, argv, options) == 'a');
    TEST_EXPECT(optind == 6);
    TEST_EXPECT(kstrcmp(optarg, "daisuki") == 0);
    argv[6] = "-z";
    TEST_EXPECT(getopt(7, argv, options) == '?');
    TEST_EXPECT(optind == 7);
    TEST_EXPECT(optopt == 'z');
    TEST_EXPECT(getopt(7, argv, options) == -1);

    return true;
}

static bool do_getopt_nonflag(void) {
    opterr = 1;
    char const *options = "+:s:g:";
    char *argv[] = {
        "<if you see me, something went wrong>",
        "-s",
        "kokona",
        "ibuki",
        "--",
        "-cherino",
    };
    enum {
        ARGC = sizeof(argv) / sizeof(*argv)
    };
    optind = 1;
    TEST_EXPECT(getopt(ARGC, argv, options) == 's');
    TEST_EXPECT(kstrcmp(optarg, "kokona") == 0);
    TEST_EXPECT(getopt(ARGC, argv, options) == -1);
    TEST_EXPECT(optind == 3);
    optind++;
    TEST_EXPECT(getopt(ARGC, argv, options) == -1);
    TEST_EXPECT(optind == 5);

    return true;
}

static struct test const TESTS[] = {
    {.name = "getopt", .fn = do_getopt},
    {.name = "getopt(with stderr)", .fn = do_getopt_stderr},
    {.name = "getopt(non-flag options)", .fn = do_getopt_nonflag},
};

const struct test_group TESTGROUP_C_UNISTD = {
    .name = "c_unistd",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
