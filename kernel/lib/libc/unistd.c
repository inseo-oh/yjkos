
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#ifdef YJKERNEL_SOURCE
#include <kernel/io/tty.h>
#else
#error TODO
#endif

char *optarg;
int opterr = 1, optind = 1, optopt;
static int s_nextcharidx = 1;

static void getopt_nextarg(void) {
    optind++;
    s_nextcharidx = 1;
}

// https://pubs.opengroup.org/onlinepubs/9799919799/functions/getopt.html
int getopt(int argc, char * const argv[], const char *optstring) {
    char const *errmsg = NULL;
    if (argc <= optind) {
        return -1;
    }
    char const *nextoptchar = optstring;
    if (*nextoptchar == '+') {
        nextoptchar++;
    }
    bool printerr = *nextoptchar != ':';
    if (!printerr) {
        nextoptchar++;
    }
    if (opterr == 0) {
        printerr = false;
    }
    int resultopt = '?';
    char *arg = argv[optind];
    if ((arg == NULL) || (*arg != '-') || (strcmp(arg, "-") == 0)) {
        return -1;
    }
    if (strcmp(arg, "--") == 0) {
        getopt_nextarg();
        return -1;
    }
    char *argopt = &arg[s_nextcharidx];
    while ((*nextoptchar != '\0') && (optind < argc)) {
        char optchar = *nextoptchar;
        if ((optchar == '+') || (optchar == ':') || (optchar == '?')) {
            nextoptchar++;
            continue;
        }
        bool hasargs = nextoptchar[1] == ':';
        if (*argopt != optchar) {
            if (hasargs) {
                nextoptchar += 2;
            } else {
                nextoptchar++;
            }
            continue;
        }
        resultopt = optchar;
        if (hasargs) {
            if (argopt[1] == '\0') {
                if ((argc - optind) < 2) {
                    getopt_nextarg();
                    resultopt = ':';
                    errmsg = "option requires an argument";
                    optopt = optchar;
                    goto error;
                }
                optarg = argv[optind + 1];
                getopt_nextarg();
                getopt_nextarg();
            } else {
                optarg = &argopt[1];
                if (optarg[0] == '\0') {
                    getopt_nextarg();
                    resultopt = ':';
                    errmsg = "option requires an argument";
                    optopt = optchar;
                    goto error;
                }
                getopt_nextarg();
            }
            break;
        } else {
            s_nextcharidx++;
            if (argopt[1] == '\0') {
                getopt_nextarg();
            }
            break;
        }
    }
    if (resultopt == '?') {
        if (optind == argc) {
            return -1;
        }
        errmsg = "invalid option";
        optopt = *argopt;
        s_nextcharidx++;
        if (argopt[1] == '\0') {
            getopt_nextarg();
        }
        goto error;
    }
    goto out;
error:
    if (printerr) {
        tty_printf("%s: %s -- '%c'\n", argv[0], errmsg, optopt);
        resultopt = '?';
    }
out:
    return resultopt;
}
