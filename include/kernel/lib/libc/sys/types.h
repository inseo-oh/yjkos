#pragma once
#include <stdint.h>

#define __YJK_USE_INTERNAL_TYPES_H
#include <_internal/types.h>

// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_types.h.html

typedef __YJK_SIZE_TYPE  size_t;
typedef __YJK_SSIZE_TYPE ssize_t;

typedef size_t        blkcnt_t;
typedef ssize_t       blksize_t;
typedef uint16_t      gid_t;
typedef size_t        ino_t;
typedef int64_t       off_t;
typedef uint64_t      time_t;
typedef uint16_t      uid_t;

