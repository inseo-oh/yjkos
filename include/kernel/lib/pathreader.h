#pragma once
#include <kernel/lib/diagnostics.h>
#include <limits.h>

struct PathReader {
    char const *remaining_path;
    char name_buf[NAME_MAX + 1];
};

void PathReader_Init(struct PathReader *out, char const *path);
[[nodiscard]] int PathReader_Next(char const **name_out, struct PathReader *self);
