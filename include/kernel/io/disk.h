#pragma once
#include <kernel/io/iodev.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint32_t DISK_BLOCK_ADDR;

struct PDisk;
struct PdiskOps {
    int (*write)(struct PDisk *self, void const *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
    int (*read)(struct PDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
};

struct PDisk {
    struct IoDev iodev;
    struct PdiskOps const *ops;
    size_t block_size;
    void *data;
};

struct LDisk {
    struct IoDev iodev;
    struct PDisk *physdisk;
    DISK_BLOCK_ADDR startblockaddr;
    size_t block_count;
};

[[nodiscard]] ssize_t Ldisk_Read(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] ssize_t LDisk_Write(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] int Ldisk_ReadExact(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] int Ldisk_WriteExact(struct LDisk *self, void *buf, DISK_BLOCK_ADDR block_addr, size_t block_count);
[[nodiscard]] int Pdisk_Register(struct PDisk *disk_out, size_t blocksize, struct PdiskOps const *ops, void *data);
void Ldisk_Discover(void);
