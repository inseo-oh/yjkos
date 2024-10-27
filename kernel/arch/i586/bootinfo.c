#include "bootinfo.h"
#include "sections.h"
#include "thirdparty/multiboot.h"
#include "vgatty.h"
#include <assert.h>
#include <kernel/arch/mmu.h>
#include <kernel/io/co.h>
#include <kernel/lib/bitmap.h>
#include <kernel/lib/pstring.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/panic.h>
#include <kernel/raster/fb.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

struct memregion {
    physptr base;
    size_t len;
};

static void excluderegion(struct memregion *before_out, struct memregion *after_out, physptr addr, size_t len, physptr excludeaddr, size_t excludelen) {
    physptr start = addr;
    physptr end = start + len;
    physptr excludestart = excludeaddr;
    physptr excludeend = excludestart + excludelen;
    if ((end <= excludestart) || (excludeend <= start)) {
        before_out->base = start;
        before_out->len = len;
        after_out->base = 0;
        after_out->len = 0;
        return;
    }
    if (UINTPTR_MAX - start < len) {
        end = UINTPTR_MAX;
    }
    if (UINTPTR_MAX - excludelen < excludestart) {
        excludeend = UINTPTR_MAX;
    }

    physptr beforestart = start;
    physptr beforeend = excludestart;
    physptr afterstart = excludeend;
    physptr afterend = end;

    memset(before_out, 0, sizeof(*before_out));
    memset(after_out, 0, sizeof(*after_out));
    if (beforestart < beforeend) {
        before_out->base = beforestart;
        before_out->len = beforeend - beforestart;
    }
    if (afterstart < afterend) {
        after_out->base = afterstart;
        after_out->len = afterend - afterstart;
    }
}

static struct memregion const REGIONS_TO_EXCLUDE[] = {
    {.base = 0x0,                                   .len = 0x100000                           },
    {.base = ARCHI586_KERNEL_PHYSICAL_ADDRESS_BEGIN, .len = ARCHI586_KERNEL_PHYSICAL_ADDRESS_END},
};

enum addrlimitresult {
    ADDRLIMIT_IGNORE,
    ADDRLIMIT_WARN,
    ADDRLIMIT_OK,
};

static enum addrlimitresult limitto32bitaddr(physptr *addr_out, size_t *len_out, uint64_t addr, uint64_t len) {
    uint64_t firstaddr = addr;
    uint64_t lastaddr = addr + len - 1;
    bool outside32bit = false;
    if (UINT32_MAX < firstaddr) {
        return ADDRLIMIT_IGNORE;
    }
    if (UINT32_MAX < lastaddr) {
        outside32bit = true;
        lastaddr = UINT32_MAX;
    }
    *addr_out = firstaddr;
    *len_out = lastaddr - firstaddr + 1;
    if (outside32bit) {
        return ADDRLIMIT_WARN;
    }
    return ADDRLIMIT_OK;
}

static void humanreadablelen(size_t *len_out, char const **unit_out, size_t len) {
    static char const * const UNITS[] = { "B", "K", "M", "G", "T" };
    enum {
        UNITS_COUNT = sizeof(UNITS)/sizeof(*UNITS)
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

void archi586_bootinfo_process(physptr infoaddr) {
    struct multiboot_info info;
    pmemcpy_in(&info, infoaddr, sizeof(info), false);
    if (info.flags & MULTIBOOT_INFO_MEM_MAP) {
        multiboot_uint32_t mmaplen = info.mmap_length;
        multiboot_uint32_t mmapaddr = info.mmap_addr;
        size_t readlen;
        co_printf("----------------- Memory map -----------------\n");
        co_printf("fromaddr  toaddr   length  type\n");
        size_t totalsize;
        bool warntoomuchmem = false;
        uintptr_t entryaddr = mmapaddr;
        for(readlen = 0; readlen < mmaplen; entryaddr += totalsize) {
            struct multiboot_mmap_entry entry;
            pmemcpy_in(&entry, entryaddr, sizeof(entry), false);

            static char const * const MEMMAP_TYPES[] = {
                "other",
                "available",
                "reserved",
                "ACPI(reclaimable)",
                "reserved",
                "bad ram",
            };
            totalsize = sizeof(entry.size) + entry.size;
            readlen += totalsize;
    
            physptr addr;
            size_t len;
            uint32_t type = entry.type;
            switch(limitto32bitaddr(&addr, &len, entry.addr, entry.len)) {
                case ADDRLIMIT_IGNORE:
                    if (type == MULTIBOOT_MEMORY_AVAILABLE) {
                        warntoomuchmem = true;
                    }
                    continue;
                case ADDRLIMIT_WARN:
                    if (type == MULTIBOOT_MEMORY_AVAILABLE) {
                        warntoomuchmem = true;
                    }
                    break;
                case ADDRLIMIT_OK:
                    break;
            }
            char const *typestr;
            if (sizeof(MEMMAP_TYPES)/sizeof(*MEMMAP_TYPES) <= type) {
                typestr = MEMMAP_TYPES[0];
            } else {
                typestr = MEMMAP_TYPES[type];
            }
            char const *lenunit;
            size_t displaylen;
            humanreadablelen(&displaylen, &lenunit, len);

            co_printf("%08X  %08X  %4zu%s  %s(%u)\n", addr, addr + len - 1, displaylen, lenunit, typestr, type);
        }
        co_printf("----------------------------------------------\n");
        if (warntoomuchmem) {
            co_printf("the system has more memory, but ignored due to being outside of 32-bit address space.\n");
        }
        entryaddr = mmapaddr;
        for(readlen = 0; readlen < mmaplen; entryaddr += totalsize) {
            struct multiboot_mmap_entry entry;
            pmemcpy_in(&entry, entryaddr, sizeof(entry), false);

            totalsize = sizeof(entry.size) + entry.size;
            readlen += totalsize;

            physptr addr;
            size_t len;
            switch(limitto32bitaddr(&addr, &len, entry.addr, entry.len)) {
                case ADDRLIMIT_IGNORE:
                    continue;
                case ADDRLIMIT_WARN:
                case ADDRLIMIT_OK:
                    break;
            }
            uint32_t type = entry.type;

            if (type != MULTIBOOT_MEMORY_AVAILABLE) {
                continue;
            }
            enum {
                EXCLUDE_COUNT = sizeof(REGIONS_TO_EXCLUDE)/sizeof(*REGIONS_TO_EXCLUDE)
            };

            // Now we filter out the regions we should exclude.
            // Excluding each region can result 0 or 1 additional entries, meaning after processing one exclusion entry,
            // the list can grow by 2x, and that list becomes input when processing next exclusion entry.
            // So the maximum size for resulting list would be 2^EXCLUDE_COUNT.
            struct memregion resultregions[1 << EXCLUDE_COUNT];
            size_t resultregionscount = 1;
            resultregions[0].base = addr;
            resultregions[0].len = len;
            for (size_t i = 0; i < EXCLUDE_COUNT; i++) {
                size_t oldregionscount = resultregionscount;
                for (size_t j = 0; j < oldregionscount; j++) {
                    struct memregion r1, r2;
                    if (resultregions[j].len == 0) {
                        continue;
                    }
                    excluderegion(&r1, &r2, resultregions[j].base, resultregions[j].len, REGIONS_TO_EXCLUDE[i].base, REGIONS_TO_EXCLUDE[i].len);
                    if ((r1.len != 0) && (r2.len != 0)) {
                        // Both r1 and r2 exists -> Overwrite original region with r1, copy r2 to new region entry.
                        resultregions[j].base = r1.base;
                        resultregions[j].len = r1.len;
                        assert(resultregionscount < (sizeof(resultregions)/sizeof(*resultregions)));
                        resultregions[resultregionscount].base = r2.base;
                        resultregions[resultregionscount].len = r2.len;
                        resultregionscount++;
                    } else if (r1.len != 0) {
                        // Only r1 exists -> Overwrite original region with r1
                        resultregions[j].base = r1.base;
                        resultregions[j].len = r1.len;
                    } else if (r2.len != 0) {
                        // Only r2 exists -> Overwrite original region with r2
                        resultregions[j].base = r2.base;
                        resultregions[j].len = r2.len;
                    } else {
                        // No region
                        resultregions[j].base = 0;
                        resultregions[j].len = 0;
                    }
                }
            }
            for (size_t i = 0; i < resultregionscount; i++) {
                physptr addr = resultregions[i].base;
                size_t len = resultregions[i].len;

                // Align address to page boundary
                if ((addr % ARCH_PAGESIZE) != 0) {
                    size_t incr = ARCH_PAGESIZE - (addr % ARCH_PAGESIZE);
                    addr += incr;
                    len -= incr;
                }
                size_t pagecount = len / ARCH_PAGESIZE;
                if (pagecount == 0) {
                    continue;
                }
                co_printf(
                    "register memory: %08x ~ %08x (%u pages)\n",
                    addr, addr + len - 1, pagecount);
                pmm_register(addr, pagecount);
            }
        }
    }
    if (info.flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) {
        co_printf("framebuffer address is %p\n", info.framebuffer_addr);
        if (info.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {
            uint8_t *colors = heap_alloc(
                info.framebuffer_palette_num_colors * 3, 0);
            if (colors == NULL) {
                panic("not enough memory to store palette");
            }
            for (int i = 0; i < info.framebuffer_palette_num_colors; i++) {
                struct multiboot_color color;
                pmemcpy_in(
                    &color,
                    info.framebuffer_palette_addr + (sizeof(color) * i),
                    sizeof(color), true);
                colors[i * 3 + 0] = color.red;
                colors[i * 3 + 1] = color.green;
                colors[i * 3 + 2] = color.blue;
            }
            fb_init_indexed(
                colors,
                info.framebuffer_palette_num_colors,
                info.framebuffer_addr,
                info.framebuffer_width,
                info.framebuffer_height,
                info.framebuffer_pitch,
                info.framebuffer_bpp
            );
            
        } else if (info.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
            fb_init_rgb(
                info.framebuffer_red_field_position,
                info.framebuffer_red_mask_size,
                info.framebuffer_green_field_position,
                info.framebuffer_green_mask_size,
                info.framebuffer_blue_field_position,
                info.framebuffer_blue_mask_size,
                info.framebuffer_addr,
                info.framebuffer_width,
                info.framebuffer_height,
                info.framebuffer_pitch,
                info.framebuffer_bpp
            );
        } else if (
            info.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT)
        {
            co_printf("text mode %dx%d\n", info.framebuffer_width, info.framebuffer_height);
            assert(info.framebuffer_bpp == 16);
            archi586_vgatty_init(
                info.framebuffer_addr,
                info.framebuffer_width,
                info.framebuffer_height,
                info.framebuffer_pitch);
            co_printf("initialized text mode console\n");
        } else {
            co_printf("unknown framebuffer type %d with size %dx%d\n", info.framebuffer_type, info.framebuffer_width, info.framebuffer_height);
        }
    } else {
        co_printf("no framebuffer info! not initializing video\n");
    }
}
