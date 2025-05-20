#pragma once
#include <kernel/lib/diagnostics.h>
#include <limits.h>

struct path_reader {
    char const *remaining_path;
    char name_buf[NAME_MAX + 1];
};

void pathreader_init(struct path_reader *out, char const *path);
[[nodiscard]] int pathreader_next(char const **name_out, struct path_reader *self);
