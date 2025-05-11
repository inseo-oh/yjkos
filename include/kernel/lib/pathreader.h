#pragma once
#include <kernel/lib/diagnostics.h>
#include <limits.h>

struct pathreader {
    char const *remaining_path;
    char name_buf[NAME_MAX + 1];
};

void pathreader_init(struct pathreader *out, char const *path);
NODISCARD int pathreader_next(char const **name_out, struct pathreader *self);
