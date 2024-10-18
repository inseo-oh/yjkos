#ifndef _DIRENT_H
#define _DIRENT_H   1

#include <sys/types.h>
#include <limits.h>

// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/dirent.h.html

typedef struct DIR DIR;

struct dirent {
    ino_t d_ino;
    char d_name[NAME_MAX + 1];
};

#endif
