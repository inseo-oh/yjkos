#include <kernel/lib/diagnostics.h>
#include <kernel/lib/pathreader.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

void PathReader_Init(struct PathReader *out, char const *path) {
    out->remaining_path = path;
}

[[nodiscard]] int PathReader_Next(char const **name_out, struct PathReader *self) {
    while (*self->remaining_path != '\0') {
        char *nextslash = strchr(self->remaining_path, '/');
        char const *name;
        char const *new_remaining_path;
        if (nextslash == NULL) {
            name = self->remaining_path;
            new_remaining_path = strchr(self->remaining_path, '\0');
        } else {
            size_t len = nextslash - self->remaining_path;
            if (NAME_MAX < len) {
                return -ENAMETOOLONG;
            }
            memcpy(self->name_buf, self->remaining_path, len);
            name = self->name_buf;
            self->name_buf[len] = '\0';
            new_remaining_path = &nextslash[1];
        }
        self->remaining_path = new_remaining_path;
        if (name[0] == '\0') {
            continue;
        }
        *name_out = name;
        return 0;
    }
    return -ENOENT;
}
