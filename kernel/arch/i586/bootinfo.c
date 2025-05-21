#include "bootinfo.h"
#include "sections.h"
#include "thirdparty/multiboot.h"
#include "vgatty.h"
#include <assert.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/co.h>
#include <kernel/lib/pstring.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/panic.h>
#include <kernel/raster/fb.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

struct mem_region {
    PHYSPTR base;
    size_t len;
};

static void exclude_region(struct mem_region *before_out, struct mem_region *after_out, PHYSPTR addr, size_t len, PHYSPTR exclude_addr, size_t exclude_len) {
    PHYSPTR start = addr;
    PHYSPTR end = start + len;
    PHYSPTR exclude_start = exclude_addr;
    PHYSPTR exclude_end = exclude_start + exclude_len;
    if ((end <= exclude_start) || (exclude_end <= start)) {
        before_out->base = start;
        before_out->len = len;
        after_out->base = 0;
        after_out->len = 0;
        return;
    }
    if (UINTPTR_MAX - start < len) {
        end = UINTPTR_MAX;
    }
    if (UINTPTR_MAX - exclude_len < exclude_start) {
        exclude_end = UINTPTR_MAX;
    }

    PHYSPTR before_start = start;
    PHYSPTR before_end = exclude_start;
    PHYSPTR after_start = exclude_end;
    PHYSPTR after_end = end;

    vmemset(before_out, 0, sizeof(*before_out));
    vmemset(after_out, 0, sizeof(*after_out));
    if (before_start < before_end) {
        before_out->base = before_start;
        before_out->len = before_end - before_start;
    }
    if (after_start < after_end) {
        after_out->base = after_start;
        after_out->len = after_end - after_start;
    }
}

static struct mem_region const REGIONS_TO_EXCLUDE[] = {
    {
        .base = 0x0,
        .len = 0x100000,
    },
    {
        .base = (uintptr_t)ARCHI586_KERNEL_PHYSICAL_ADDRESS_BEGIN,
        .len = (uintptr_t)ARCHI586_KERNEL_PHYSICAL_ADDRESS_END,
    },
};
#define EXCLUDE_COUNT (sizeof(REGIONS_TO_EXCLUDE) / sizeof(*REGIONS_TO_EXCLUDE))

typedef enum {
    ADDRLIMIT_RESULT_IGNORE,
    ADDRLIMIT_RESULT_WARN,
    ADDRLIMIT_RESULT_OK,
} ADDRLIMIT_RESULT;

static ADDRLIMIT_RESULT limit_to_32bit_addr(PHYSPTR *addr_out, size_t *len_out, uint64_t addr, uint64_t len) {
    uint64_t first_addr = addr;
    uint64_t last_addr = addr + len - 1;
    bool outside_32bit = false;
    if (UINT32_MAX < first_addr) {
        return ADDRLIMIT_RESULT_IGNORE;
    }
    if (UINT32_MAX < last_addr) {
        outside_32bit = true;
        last_addr = UINT32_MAX;
    }
    *addr_out = first_addr;
    *len_out = last_addr - first_addr + 1;
    if (outside_32bit) {
        return ADDRLIMIT_RESULT_WARN;
    }
    return ADDRLIMIT_RESULT_OK;
}

static void human_readable_len(size_t *len_out, char const **unit_out, size_t len) {
    static char const *const UNITS[] = {"B", "K", "M", "G", "T"};
    enum {
        UNITS_COUNT = sizeof(UNITS) / sizeof(*UNITS)
    };
    size_t resultlen = len;
    size_t idx = 0;
    while (1024 <= resultlen) {
        if (((UNITS_COUNT - 1) <= idx)) {
            break;
        }
        idx++;
        resultlen /= 1024;
    }
    *len_out = resultlen;
    *unit_out = UNITS[idx];
}

static void print_mem_map(struct multiboot_info const *info) {
    multiboot_uint32_t mmaplen = info->mmap_length;
    multiboot_uint32_t mmapaddr = info->mmap_addr;
    size_t readlen;
    co_printf("----------------- Memory map -----------------\n");
    co_printf("fromaddr  toaddr   length  type\n");
    size_t totalsize;
    bool warn_too_much_mem = false;
    PHYSPTR entryaddr = mmapaddr;
    for (readlen = 0; readlen < mmaplen; entryaddr += totalsize) {
        struct multiboot_mmap_entry entry;
        pmemcpy_in(&entry, entryaddr, sizeof(entry), false);
        static char const *const MEMMAP_TYPES[] = {
            "other",
            "available",
            "reserved",
            "ACPI(reclaimable)",
            "reserved",
            "bad ram",
        };
        totalsize = sizeof(entry.size) + entry.size;
        readlen += totalsize;

        PHYSPTR addr;
        size_t len;
        uint32_t type = entry.type;
        switch (limit_to_32bit_addr(&addr, &len, entry.addr, entry.len)) {
        case ADDRLIMIT_RESULT_IGNORE:
            if (type == MULTIBOOT_MEMORY_AVAILABLE) {
                warn_too_much_mem = true;
            }
            continue;
        case ADDRLIMIT_RESULT_WARN:
            if (type == MULTIBOOT_MEMORY_AVAILABLE) {
                warn_too_much_mem = true;
            }
            break;
        case ADDRLIMIT_RESULT_OK:
            break;
        }
        char const *type_str;
        if (sizeof(MEMMAP_TYPES) / sizeof(*MEMMAP_TYPES) <= type) {
            type_str = MEMMAP_TYPES[0];
        } else {
            type_str = MEMMAP_TYPES[type];
        }
        char const *lenunit;
        size_t displaylen;
        human_readable_len(&displaylen, &lenunit, len);

        co_printf("%08X  %08X  %4zu%s  %s(%u)\n", addr, addr + len - 1, displaylen, lenunit, type_str, type);
    }
    co_printf("----------------------------------------------\n");
    if (warn_too_much_mem) {
        co_printf("the system has more memory, but ignored due to being outside of 32-bit address space.\n");
    }
}

/*
 * Excluding each region can result 0 or 1 additional entries.
 * So after processing one exclusion entry, the list can grow by 2x and that list becomes input when processing next exclusion entry.
 * So the maximum size for resulting list would be 2^EXCLUDE_COUNT.
 */
#define MAX_RESULT_REGIONS (1 << EXCLUDE_COUNT)

static size_t exclude_regions(struct mem_region *result_regions, PHYSPTR addr, size_t len) {
    size_t result_regions_count = 1;
    result_regions[0].base = addr;
    result_regions[0].len = len;
    for (size_t i = 0; i < EXCLUDE_COUNT; i++) {
        size_t old_regions_count = result_regions_count;
        for (size_t j = 0; j < old_regions_count; j++) {
            struct mem_region r1;
            struct mem_region r2;
            if (result_regions[j].len == 0) {
                continue;
            }
            exclude_region(&r1, &r2, result_regions[j].base, result_regions[j].len, REGIONS_TO_EXCLUDE[i].base, REGIONS_TO_EXCLUDE[i].len);
            if ((r1.len != 0) && (r2.len != 0)) {
                /* Both r1 and r2 exists -> Overwrite original region with r1, copy r2 to new region entry. */
                result_regions[j].base = r1.base;
                result_regions[j].len = r1.len;
                assert(result_regions_count < MAX_RESULT_REGIONS);
                result_regions[result_regions_count].base = r2.base;
                result_regions[result_regions_count].len = r2.len;
                result_regions_count++;
            } else if (r1.len != 0) {
                /* Only r1 exists -> Overwrite original region with r1 */
                result_regions[j].base = r1.base;
                result_regions[j].len = r1.len;
            } else if (r2.len != 0) {
                /* Only r2 exists -> Overwrite original region with r2 */
                result_regions[j].base = r2.base;
                result_regions[j].len = r2.len;
            } else {
                /* No region */
                result_regions[j].base = 0;
                result_regions[j].len = 0;
            }
        }
    }
    return result_regions_count;
}

static void register_region(PHYSPTR addr, size_t len) {
    if ((addr % ARCH_PAGESIZE) != 0) {
        size_t incr = ARCH_PAGESIZE - (addr % ARCH_PAGESIZE);
        addr += incr;
        len -= incr;
    }
    size_t page_count = len / ARCH_PAGESIZE;
    if (page_count == 0) {
        return;
    }
    co_printf("register memory: %08x ~ %08x (%u pages)\n", addr, addr + len - 1, page_count);
    pmm_register_mem(addr, page_count);
}

static void process_mem_map(struct multiboot_info const *info) {
    multiboot_uint32_t mmaplen = info->mmap_length;
    multiboot_uint32_t mmapaddr = info->mmap_addr;
    size_t readlen;
    size_t totalsize;
    PHYSPTR entryaddr = mmapaddr;
    for (readlen = 0; readlen < mmaplen; entryaddr += totalsize) {
        struct multiboot_mmap_entry entry;
        pmemcpy_in(&entry, entryaddr, sizeof(entry), false);

        totalsize = sizeof(entry.size) + entry.size;
        readlen += totalsize;

        PHYSPTR addr;
        size_t len;
        switch (limit_to_32bit_addr(&addr, &len, entry.addr, entry.len)) {
        case ADDRLIMIT_RESULT_IGNORE:
            continue;
        case ADDRLIMIT_RESULT_WARN:
        case ADDRLIMIT_RESULT_OK:
            break;
        }
        uint32_t type = entry.type;

        if (type != MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }

        struct mem_region result_regions[MAX_RESULT_REGIONS];
        size_t result_regions_count = exclude_regions(result_regions, addr, len);
        for (size_t i = 0; i < result_regions_count; i++) {
            PHYSPTR addr = result_regions[i].base;
            size_t len = result_regions[i].len;
            register_region(addr, len);
        }
    }
}

static void process_framebuffer_info(struct multiboot_info const *info) {
    co_printf("framebuffer address is %p\n", info->framebuffer_addr);
    if (info->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {
        uint8_t *colors = heap_alloc(info->framebuffer_palette_num_colors * 3, 0);
        if (colors == NULL) {
            panic("not enough memory to store palette");
        }
        for (int i = 0; i < info->framebuffer_palette_num_colors; i++) {
            struct multiboot_color color;
            pmemcpy_in(&color, info->framebuffer_palette_addr + (sizeof(color) * i), sizeof(color), true);
            colors[i * 3 + 0] = color.red;
            colors[i * 3 + 1] = color.green;
            colors[i * 3 + 2] = color.blue;
        }
        fb_init_indexed(
            colors,
            info->framebuffer_palette_num_colors,
            info->framebuffer_addr,
            (int)info->framebuffer_width,
            (int)info->framebuffer_height,
            (int)info->framebuffer_pitch,
            (int)info->framebuffer_bpp);
    } else if (info->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        fb_init_rgb(
            info->framebuffer_red_field_position,
            info->framebuffer_red_mask_size,
            info->framebuffer_green_field_position,
            info->framebuffer_green_mask_size,
            info->framebuffer_blue_field_position,
            info->framebuffer_blue_mask_size,
            info->framebuffer_addr,
            (int)info->framebuffer_width,
            (int)info->framebuffer_height,
            (int)info->framebuffer_pitch,
            info->framebuffer_bpp);
    } else if (info->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
        co_printf("text mode %dx%d\n", info->framebuffer_width, info->framebuffer_height);
        assert(info->framebuffer_bpp == 16);
        archi586_vgatty_init(info->framebuffer_addr, info->framebuffer_width, info->framebuffer_height, info->framebuffer_pitch);
        co_printf("initialized text mode console\n");
    } else {
        co_printf("unknown framebuffer type %d with size %dx%d\n", info->framebuffer_type, info->framebuffer_width, info->framebuffer_height);
    }
}

void archi586_bootinfo_process(PHYSPTR infoaddr) {
    struct multiboot_info info;
    pmemcpy_in(&info, infoaddr, sizeof(info), false);
    if (info.flags & (uint32_t)MULTIBOOT_INFO_MEM_MAP) {
        print_mem_map(&info);
        process_mem_map(&info);
    } else {
        co_printf("no memory map! no memory will be registered\n");
    }
    if (info.flags & (uint32_t)MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        process_framebuffer_info(&info);
    } else {
        co_printf("no framebuffer info! not initializing video\n");
    }
}
