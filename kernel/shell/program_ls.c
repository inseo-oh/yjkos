#include "shell.h"
#include <dirent.h>
#include <errno.h>
#include <kernel/fs/vfs.h>
#include <kernel/io/co.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/miscmath.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

/* https://pubs.opengroup.org/onlinepubs/9799919799/utilities/ls.html */

struct opts {
    /***************************************************************************
     * Filtering options
     **************************************************************************/
    bool all : 1;     /* -a */
    bool all_alt : 1; /* -A */

    /**************************************************************************
     * Output options
     **************************************************************************/
    /* I don't know why it's called stream output format in POSIX */
    bool stream_format : 1; /* -m */
};

[[nodiscard]] static bool getopts(struct opts *out, int argc, char *argv[]) {
    bool ok = true;
    int c;
    memset(out, 0, sizeof(*out));
    while (1) {
        c = getopt(argc, argv, "ACFHLRSacdfghiklmnopqrstux1");
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'a':
            out->all = true;
            break;
        case 'A':
            out->all_alt = true;
            break;
        case 'm':
            out->stream_format = true;
            break;
        case '?':
        case ':':
            ok = false;
            break;
        default:
            co_printf("NOT IMPLEMENTED: %c flag\n", c);
        }
    }
    return ok;
}

/* XXX: Query this from current stdout, after we implement support for that. */
#define COLUMNS 80

struct entry {
    char *name;
};

static int format(char *buf, size_t size, struct entry const *ent, struct opts const *opts, bool is_last_entry) {
    if (opts->stream_format) {
        if (is_last_entry) {
            return snprintf(buf, size, "%s", ent->name);
        }
        return snprintf(buf, size, "%s, ", ent->name);
    }
    return snprintf(buf, size, "%s ", ent->name);
}

static bool should_hide_dirent(struct dirent *ent, struct opts const *opts) {
    if (!opts->all || opts->all_alt) {
        if ((strcmp(ent->d_name, ".") == 0) ||
            strcmp(ent->d_name, "..") == 0) {
            return true;
        }
    }
    if (!opts->all && !opts->all_alt) {
        if (ent->d_name[0] == '.') {
            return true;
        }
    }
    return false;
}

static int collect_entries(struct entry **entries_out, size_t *entries_len_out, char const *path, struct opts const *opts) {
    DIR *dir = NULL;
    int ret = vfs_open_directory(&dir, path);
    if (ret < 0) {
        return ret;
    }
    struct entry *entries = NULL;
    size_t entries_len = 0;
    while (1) {
        struct dirent ent;
        ret = vfs_read_directory(&ent, dir);
        if (ret < 0) {
            break;
        }
        if (should_hide_dirent(&ent, opts)) {
            continue;
        }
        if (WILL_ADD_OVERFLOW(entries_len, 1, SIZE_MAX)) {
            goto oom;
        }
        void *new_entries = heap_realloc_array(entries, sizeof(*entries), entries_len + 1, 0);
        if (new_entries == NULL) {
            goto oom;
        }
        entries = new_entries;
        entries_len++;
        struct entry *dest = &entries[entries_len - 1];
        memset(dest, 0, sizeof(*dest));
        dest->name = strdup(ent.d_name);
        if (dest->name == NULL) {
            goto oom;
        }
    }
    ret = 0;
    goto out;
oom:
    ret = -ENOMEM;
    for (size_t i = 0; i < entries_len; i++) {
        heap_free(entries[i].name);
    }
    heap_free(entries);
    goto out;
out:
    vfs_close_directory(dir);
    *entries_out = entries;
    *entries_len_out = entries_len;
    return ret;
}

static void show_dir(char const *progname, char const *path, struct opts const *opts) {
    struct entry *entries = NULL;
    size_t entries_len = 0;
    int ret = collect_entries(&entries, &entries_len, path, opts);
    if (ret < 0) {
        co_printf("%s: failed to read directory %s (error %d)\n", progname, path, ret);
        goto out;
    }
    int line_len = 0;
    char buf[COLUMNS + 1];
    for (size_t i = 0; i < entries_len; i++) {
        struct entry const *ent = &entries[i];
        bool is_last_entry = i == (entries_len - 1);
        int len = format(buf, sizeof(buf), ent, opts, is_last_entry);
        bool ishorizontal = opts->stream_format;
        if ((ishorizontal && (COLUMNS - line_len) < len) ||
            (!ishorizontal && i != 0)) {
            co_printf("\n");
            line_len = 0;
        }
        co_printf("%s", buf);
        line_len += len;
    }
    co_printf("\n");
out:
    for (size_t i = 0; i < entries_len; i++) {
        heap_free(entries[i].name);
    }
    heap_free(entries);
}

static int program_main(int argc, char *argv[]) {
    struct opts opts;
    if (!getopts(&opts, argc, argv)) {
        return 1;
    }
    if (argc <= optind) {
        show_dir(argv[0], ".", &opts);
        return 0;
    }
    for (int i = optind; i < argc; i++) {
        if (optind + 1 != argc) {
            co_printf("%s:\n", argv[i]);
        }
        show_dir(argv[0], argv[i], &opts);
    }
    return 0;
}

struct shell_program g_shell_program_ls = {
    .name = "ls",
    .main = program_main,
};
