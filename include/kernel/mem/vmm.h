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

struct Vmm_AddressSpace {
#ifdef NEW_VMM
    struct List object_list; /* uncommited_object items */
#else
    struct bst object_group_tree; /* objectgroup_t items, BST node key = Size of the page. */
#endif
    struct List uncommited_objects; /* uncommited_object items */
    bool is_user;
};

struct Vmm_Object {
    struct List_Node node;
    struct Vmm_AddressSpace *address_space;
    void *start;
    void *end;
    PHYSPTR phys_base; /* VMM_PHYSADDR_NOMAP means it allocates pages instead of mapping existing pages. */
    uint8_t mapflags;
};

static PHYSPTR const VMM_PHYSADDR_NOMAP = ~0;

[[nodiscard]] size_t Vmm_GetObjectSize(struct Vmm_Object *object);

/*
 * Returns NULL on allocation failure
 */
[[nodiscard]] bool Vmm_InitAddressSpace(struct Vmm_AddressSpace *out, void *start, void *end, bool is_user);
void Vmm_DeinitAddressSpace(struct Vmm_AddressSpace *self);

/* TODO: Instead of accepting Vmm_AddressSpace, figure out addressspace itself. */

[[nodiscard]] struct Vmm_Object *Vmm_AllocObject(struct Vmm_AddressSpace *self, PHYSPTR physicalbase, size_t size, uint8_t mapflags);
[[nodiscard]] struct Vmm_Object *Vmm_AllocObjectAt(struct Vmm_AddressSpace *self, void *virtualbase, PHYSPTR physicalbase, size_t size, uint8_t mapflags);
[[nodiscard]] struct Vmm_Object *Vmm_Alloc(struct Vmm_AddressSpace *self, size_t size, uint8_t mapflags);
[[nodiscard]] struct Vmm_Object *Vmm_AllocAt(struct Vmm_AddressSpace *self, void *virt_base, size_t size, uint8_t mapflags);
[[nodiscard]] struct Vmm_Object *Vmm_MapMemory(struct Vmm_AddressSpace *self, PHYSPTR phys_base, size_t size, uint8_t mapflags);
[[nodiscard]] struct Vmm_Object *Vmm_MapMemoryAt(struct Vmm_AddressSpace *self, void *virt_base, PHYSPTR phys_base, size_t size, uint8_t mapflags);

/*
 * "Easy" version of Vmm_MapMemory/Vmm_AllocObject. If it succeeds, it returns pointer to mapped memory (Not vmobject!).
 * If it fails, it just panics, and allocated memory will be R+W permission.
 * The purpose of this function is to simplify mapping hardware prepherals:
 * ```
 * char *vmem = Vmm_EzMap(0xb80000, 4000);
 * ```
 *
 * In addition to being easy wrapper for Vmm_MapMemory, it also supports mapping
 * addresses that are not at page boundary: The actual mapping will be done at
 * page boundary, but then it adds appropriate offset and returns that pointer.
 *
 * `Vmm_EzMap` has one caveat: There's no support for remap/unmapping. This is
 * because the underlying vmobject is not returned for simplicity.
 */
void *Vmm_EzMap(PHYSPTR base, size_t size);
void Vmm_Free(struct Vmm_Object *object);
struct Vmm_AddressSpace *Vmm_GetKernelAddressSpace(void);
/*
 * Note that this will return NULL if it points to kernel area but outside of kernel VM.
 * Those areas include kernel .text and .data, since those areas are never meant to be touched by VM code.
 */
struct Vmm_AddressSpace *Vmm_GetAddressSpaceOf(void *ptr);
void Vmm_PageFault(void *ptr, bool was_present, bool was_write, bool was_user, void *trapframe);
bool Vmm_RandomTest(void);
