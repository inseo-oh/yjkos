#pragma once
#include <kernel/lib/bst.h>
#include <kernel/lib/diagnostics.h>
#include <kernel/lib/list.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>

#define NEW_VMM

/* Memory mapping flags */
#define MAP_PROT_READ (1U << 0)
#define MAP_PROT_WRITE (1U << 1)
#define MAP_PROT_EXEC (1U << 2)
#define MAP_PROT_NOCACHE (1U << 3)

struct vmm_address_space {
#ifdef NEW_VMM
    struct list object_list; /* uncommited_object items */
#else
    struct bst object_group_tree; /* objectgroup_t items, BST node key = Size of the page. */
#endif
    struct list uncommited_objects; /* uncommited_object items */
    bool is_user;
};

struct vmm_object {
    struct list_node node;
    struct vmm_address_space *address_space;
    void *start;
    void *end;
    PHYSPTR phys_base; /* VMM_PHYSADDR_NOMAP means it allocates pages instead of mapping existing pages. */
    uint8_t mapflags;
};

static PHYSPTR const VMM_PHYSADDR_NOMAP = ~0;

[[nodiscard]] size_t vmm_get_object_size(struct vmm_object *object);

/*
 * Returns nullptr on allocation failure
 */
[[nodiscard]] bool vmm_init_address_space(struct vmm_address_space *out, void *start, void *end, bool is_user);
void vmm_deinit_address_space(struct vmm_address_space *self);

/* TODO: Instead of accepting vmm_address_space, figure out addressspace itself. */

[[nodiscard]] struct vmm_object *vmm_alloc_object(struct vmm_address_space *self, PHYSPTR physicalbase, size_t size, uint8_t mapflags);
[[nodiscard]] struct vmm_object *vmm_alloc_object_at(struct vmm_address_space *self, void *virtualbase, PHYSPTR physicalbase, size_t size, uint8_t mapflags);
[[nodiscard]] struct vmm_object *vmm_alloc(struct vmm_address_space *self, size_t size, uint8_t mapflags);
[[nodiscard]] struct vmm_object *vmm_alloc_at(struct vmm_address_space *self, void *virt_base, size_t size, uint8_t mapflags);
[[nodiscard]] struct vmm_object *vmm_map_mem(struct vmm_address_space *self, PHYSPTR phys_base, size_t size, uint8_t mapflags);
[[nodiscard]] struct vmm_object *vmm_map_memory_at(struct vmm_address_space *self, void *virt_base, PHYSPTR phys_base, size_t size, uint8_t mapflags);

/*
 * "Easy" version of vmm_map_mem/vmm_alloc_object. If it succeeds, it returns pointer to mapped memory (Not vmobject!).
 * If it fails, it just panics, and allocated memory will be R+W permission.
 * The purpose of this function is to simplify mapping hardware prepherals:
 * ```
 * char *vmem = vmm_ezmap(0xb80000, 4000);
 * ```
 *
 * In addition to being easy wrapper for vmm_map_mem, it also supports mapping
 * addresses that are not at page boundary: The actual mapping will be done at
 * page boundary, but then it adds appropriate offset and returns that pointer.
 *
 * `vmm_ezmap` has one caveat: There's no support for remap/unmapping. This is
 * because the underlying vmobject is not returned for simplicity.
 */
void *vmm_ezmap(PHYSPTR base, size_t size);
void vmm_free(struct vmm_object *object);
struct vmm_address_space *vmm_get_kernel_address_space(void);
/*
 * Note that this will return nullptr if it points to kernel area but outside of kernel VM.
 * Those areas include kernel .text and .data, since those areas are never meant to be touched by VM code.
 */
struct vmm_address_space *vmm_get_address_space_of(void *ptr);
void vmm_page_fault(void *ptr, bool was_present, bool was_write, bool was_user, void *trapframe);
bool vmm_random_test(void);
