#pragma once
#include <kernel/io/iodev.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t diskblkptr;

struct pdisk;
struct pdisk_ops {
    FAILABLE_FUNCTION (*write)(struct pdisk *self, void const *buf, diskblkptr blockaddr, size_t blockcount);
    FAILABLE_FUNCTION (*read)(struct pdisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
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

FAILABLE_FUNCTION ldisk_read(struct ldisk *self, void *buf, diskblkptr blockaddr, size_t *blockcount_inout);
FAILABLE_FUNCTION ldisk_write(struct ldisk *self, void *buf, diskblkptr blockaddr, size_t *blockcount_inout);
FAILABLE_FUNCTION ldisk_read_exact(struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
FAILABLE_FUNCTION ldisk_write_exact(struct ldisk *self, void *buf, diskblkptr blockaddr, size_t blockcount);
FAILABLE_FUNCTION pdisk_register(struct pdisk *disk_out, size_t blocksize, struct pdisk_ops const *ops, void *data);
void ldisk_discover(void);
