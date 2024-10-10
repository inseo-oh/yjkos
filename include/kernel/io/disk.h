#pragma once
#include <kernel/io/iodev.h>
#include <kernel/status.h>
#include <kernel/types.h>
#include <stddef.h>

typedef struct pdisk pdisk_t;
typedef struct pdisk_ops pdisk_ops_t;

typedef size_t diskblkptr_t;

struct pdisk_ops {
    FAILABLE_FUNCTION (*write)(pdisk_t *self, void const *buf, diskblkptr_t blockaddr, size_t blockcount);
    FAILABLE_FUNCTION (*read)(pdisk_t *self, void *buf, diskblkptr_t blockaddr, size_t blockcount);
};

struct pdisk {
    iodev_t iodev;
    pdisk_ops_t const *ops;
    size_t blocksize;
    void *data;
};

typedef struct ldisk ldisk_t;
struct ldisk {
    iodev_t iodev;
    pdisk_t *physdisk;
    diskblkptr_t startblockaddr;
    size_t blockcount;
};

FAILABLE_FUNCTION ldisk_read(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t *blockCountInOut);
FAILABLE_FUNCTION ldisk_write(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t *blockCountInOut);
FAILABLE_FUNCTION ldisk_read_exact(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t blockcount);
FAILABLE_FUNCTION ldisk_write_exact(ldisk_t *self, void *buf, diskblkptr_t blockaddr, size_t blockcount);
FAILABLE_FUNCTION pdisk_register(pdisk_t *disk_out, size_t blocksize, pdisk_ops_t const *ops, void *data);
void ldisk_discover(void);
