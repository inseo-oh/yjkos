#pragma once
#include <sys/types.h>
#include <limits.h>

// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/dirent.h.html

typedef struct DIR DIR; // Implementation of DIR is in the each filesystem driver.

struct dirent {
    ino_t d_ino;
    char d_name[NAME_MAX + 1];
};


