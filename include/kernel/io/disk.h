#pragma once
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t diskblkptr;

struct pdisk;
struct pdisk_ops {
    WARN_UNUSED_RESULT int (*write)(
        struct pdisk *self, void const *buf, diskblkptr blockaddr,
        size_t blockcount);
    WARN_UNUSED_RESULT int (*read)(
        struct pdisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
};

struct pdisk {
    struct iodev iodev;
    struct pdisk_ops const *ops;
    size_t blocksize;
    void *data;
};

struct ldisk {
    struct iodev iodev;
    struct pdisk *physdisk;
    diskblkptr startblockaddr;
    size_t blockcount;
};

WARN_UNUSED_RESULT ssize_t ldisk_read(
    struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
WARN_UNUSED_RESULT ssize_t ldisk_write(
    struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
WARN_UNUSED_RESULT int ldisk_read_exact(
    struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
WARN_UNUSED_RESULT int ldisk_write_exact(
    struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
WARN_UNUSED_RESULT int pdisk_register(
    struct pdisk *disk_out, size_t blocksize, struct pdisk_ops const *ops,
    void *data);
void ldisk_discover(void);
