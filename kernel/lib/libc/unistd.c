
#include <kernel/lib/strutil.h>
#include <unistd.h>
#ifdef YJKERNEL_SOURCE
#include <kernel/io/co.h>
#else
#error TODO
#endif

char *optarg;
int opterr = 1, optind = 1, optopt;
static int s_next_char_idx = 1;

static void getopt_nextarg(void) {
    optind++;
    s_next_char_idx = 1;
}

static bool getopt_get_arg(int argc, char *const argv[], char *argopt, int opt_char) {
    if (argopt[1] == '\0') {
        if ((argc - optind) < 2) {
            goto fail;
        }
        optarg = argv[optind + 1];
        getopt_nextarg();
        getopt_nextarg();
    } else {
        optarg = &argopt[1];
        if (optarg[0] == '\0') {
            goto fail;
        }
        getopt_nextarg();
    }
    return true;
fail:
    getopt_nextarg();
    optopt = opt_char;
    return false;
}

/*
 * Returns nonzero(skip length) if current option needs to be skipped.
 * Otherwise it returns 0.
 */
static int getopt_opt_char_skip_len(const char *argopt, const char *next_opt_char) {
    char opt_char = *next_opt_char;
    if ((opt_char == '+') || (opt_char == ':') || (opt_char == '?')) {
        return 1;
    }
    bool has_args = next_opt_char[1] == ':';
    if (*argopt != opt_char) {
        if (has_args) {
            return 2;
        }
        return 1;
    }
    return 0;
}

/* https://pubs.opengroup.org/onlinepubs/9799919799/functions/getopt.html */
int getopt(int argc, char *const argv[], const char *optstring) {
    char const *errmsg = NULL;
    if (argc <= optind) {
        return -1;
    }
    char const *next_opt_char = optstring;
    if (*next_opt_char == '+') {
        next_opt_char++;
    }
    bool print_err = *next_opt_char != ':';
    if (!print_err) {
        next_opt_char++;
    }
    if (opterr == 0) {
        print_err = false;
    }
    int result_opt = '?';
    char *arg = argv[optind];
    if ((arg == NULL) || (*arg != '-') || (strcmp(arg, "-") == 0)) {
        return -1;
    }
    if (strcmp(arg, "--") == 0) {
        getopt_nextarg();
        return -1;
    }
    char *argopt = &arg[s_next_char_idx];
    while ((*next_opt_char != '\0') && (optind < argc)) {
        int skip_len = getopt_opt_char_skip_len(argopt, next_opt_char);
        if (skip_len != 0) {
            next_opt_char += skip_len;
            continue;
        }
        char opt_char = *next_opt_char;
        bool has_args = next_opt_char[1] == ':';
        result_opt = (int)opt_char;
        if (has_args) {
            if (!getopt_get_arg(argc, argv, argopt, opt_char)) {
                errmsg = "option requires an argument";
                result_opt = ':';
                goto error;
            }
            break;
        }
        s_next_char_idx++;
        if (argopt[1] == '\0') {
            getopt_nextarg();
        }
        break;
    }
    if (result_opt != '?') {
        goto out;
    }
    if (optind == argc) {
        return -1;
    }
    errmsg = "invalid option";
    optopt = (unsigned char)*argopt;
    s_next_char_idx++;
    if (argopt[1] == '\0') {
        getopt_nextarg();
    }
    goto error;
error:
    if (print_err) {
        co_printf("%s: %s -- '%c'\n", argv[0], errmsg, optopt);
        result_opt = '?';
    }
out:
    return result_opt;
}
