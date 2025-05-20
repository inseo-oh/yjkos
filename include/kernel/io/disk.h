#pragma once
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t DISK_BLOCK_ADDR;

struct pdisk;
struct pdisk_ops {
    int (*write)(struct pdisk *self, void const *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
    int (*read)(struct pdisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
};

struct pdisk {
    struct iodev iodev;
    struct pdisk_ops const *ops;
    size_t block_size;
    void *data;
};

struct ldisk {
    struct iodev iodev;
    struct pdisk *physdisk;
    DISK_BLOCK_ADDR startblockaddr;
    size_t block_count;
};

[[nodiscard]] ssize_t ldisk_read(struct ldisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] ssize_t ldisk_write(struct ldisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] int ldisk_read_exact(struct ldisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] int ldisk_write_exact(struct ldisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] int pdisk_register(struct pdisk *disk_out, size_t blocksize, struct pdisk_ops const *ops, void *data);
void ldisk_discover(void);
