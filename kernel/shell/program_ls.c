#include "shell.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/tty.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/mem/heap.h>

// https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html

struct opts {
    //--------------------------------------------------------------------------
    // Filtering options
    //--------------------------------------------------------------------------
    bool all          : 1; // -a
    bool all_alt      : 1; // -A

    //--------------------------------------------------------------------------
    // Output options
    //--------------------------------------------------------------------------
    // I don't know why it's called stream output format in POSIX
    bool streamformat : 1; // -m
};

static WARN_UNUSED_RESULT bool getopts(
    struct opts *out, int argc, char *argv[])
{
    bool ok = true;
    int c;
    memset(out, 0, sizeof(*out));
    while (1) {
        c = getopt(argc, argv, "ACFHLRSacdfghiklmnopqrstux1");
        if (c == -1) {
            break;
        }
        switch(c) {
            case 'a':
                out->all = true;
                break;
            case 'A':
                out->all_alt = true;
                break;
            case 'm':
                out->streamformat = true;
                break;
            case '?':
            case ':':
                ok = false;
                break;
            default:
                tty_printf("NOT IMPLEMENTED: %c flag\n", c);
        }
    }
    return ok;
}

enum {
    /*
     * XXX: Query this from current stdout, after we implement support for
     * that.
     */
    COLUMNS = 80,
};

struct entry {
    char *name;
};

static int format(
    char *buf, size_t size, struct entry const *ent, struct opts const *opts,
    bool islastentry)
{
    if (opts->streamformat) {
        if (islastentry) {
            return snprintf(buf, size, "%s", ent->name);
        } else {
            return snprintf(buf, size, "%s, ", ent->name);
        }
    }
    return snprintf(buf, size, "%s ", ent->name);
}

static void showdir(
    char const *progname, char const *path, struct opts const *opts)
{
    DIR *dir;
    int ret = vfs_opendir(&dir, path);
    if (ret < 0) {
        tty_printf(
            "%s: failed to open directory %s (error %d)\n",
            progname, path, ret);
        return;
    }
    struct entry *entries = NULL;
    size_t entrieslen = 0;
    while (1) {
        struct dirent ent;
        ret = vfs_readdir(&ent, dir);
        if (ret == -ENOENT) {
            break;
        } else if (ret != 0) {
            tty_printf(
                "%s: failed to read directory %s (error %d)\n",
                progname, path, ret);
            break;
        }
        bool shouldhide = false;
        if (!opts->all || opts->all_alt) {
            shouldhide |= strcmp(ent.d_name, ".") == 0 ||
                          strcmp(ent.d_name, "..") == 0;
        }
        if (!opts->all && !opts->all_alt) {
            shouldhide |= ent.d_name[0] == '.';
        }
        if (shouldhide) {
            continue;
        }
        if ((SIZE_MAX - 1) < entrieslen) {
            goto oom;
        }
        void *newentries = heap_reallocarray(
            entries, sizeof(*entries),
            entrieslen + 1, 0);
        if (newentries == NULL) {
            goto oom;
        }
        entries = newentries;
        entrieslen++;
        struct entry *dest = &entries[entrieslen - 1];
        memset(dest, 0, sizeof(*dest));
        dest->name = strdup(ent.d_name);
        if (dest->name == NULL) {
            goto oom;
        }
    }
    goto cont;
oom:
    vfs_closedir(dir);
    tty_printf("%s: not enough memory to allocate list\n", progname);
    goto out;
cont:
    vfs_closedir(dir);
    int linelen = 0;
    char buf[COLUMNS + 1];
    for (size_t i = 0; i < entrieslen; i++) {
        struct entry const *ent = &entries[i];
        bool islastentry = i == (entrieslen - 1);
        int len = format(buf, sizeof(buf), ent, opts, islastentry);
        bool ishorizontal = opts->streamformat;
        if ((ishorizontal && (COLUMNS - linelen) < len) ||
            (!ishorizontal && i != 0))
        {
            tty_printf("\n");
            linelen = 0;
        }
        tty_printf("%s", buf);
        linelen += len;
    }
    tty_printf("\n");
out:
    for (size_t i = 0; i < entrieslen; i++) {
        heap_free(entries[i].name);
    }
    heap_free(entries);
    return;
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    if (!getopts(&opts, argc, argv)) {
        return 1;
    }
    if (argc <= optind) {
        showdir(argv[0], ".", &opts);
        return 0;
    }
    for (int i = optind; i < argc; i++) {
        if (optind + 1 != argc) {
            tty_printf("%s:\n", argv[i]);
        }
        showdir(argv[0], argv[i], &opts);
    }
    return 0;
}

struct shell_program g_shell_program_ls = {
    .name = "ls",
    .main = program_main,
};

