#pragma once
#include <kernel/types.h>
#include <kernel/lib/bst.h> 
#include <kernel/lib/diagnostics.h> 
#include <kernel/lib/list.h> 
#include <stdint.h>
#include <stdbool.h>

// Memory mapping flags
static uint8_t const MAP_PROT_READ    = 1 << 0;
static uint8_t const MAP_PROT_WRITE   = 1 << 1;
static uint8_t const MAP_PROT_EXEC    = 1 << 2;
static uint8_t const MAP_PROT_NOCACHE = 1 << 3;

struct addressspace {
    struct bst objectgrouptree;     // objectgroup_t items, BST node key = Size of the page.
    struct list uncommitedobjects;  // uncommitedobject_t items
    bool is_user;
};

struct vmobject {
    struct list_node node;
    struct addressspace *addressspace;
    uintptr_t startaddress;
    uintptr_t endaddress;
    physptr physicalbase;  // VMM_PHYSADDR_NOMAP means it allocates pages instead of mapping existing pages.
    uint8_t mapflags;
};

static physptr const VMM_PHYSADDR_NOMAP = ~0;

/*
 * Returns NULL on allocation failure
 */
WARN_UNUSED_RESULT bool vmm_init_addressspace(struct addressspace *out, uintptr_t startaddress, uintptr_t endaddress, bool is_user);
void vmm_deinit_addressspace(struct addressspace *self);

// XXX: Instead of accepting addressspace, figure out addressspace itself.

WARN_UNUSED_RESULT struct vmobject *vmm_alloc_object(
    struct addressspace *self, physptr physicalbase, size_t size,
    uint8_t mapflags
);
WARN_UNUSED_RESULT struct vmobject *vmm_alloc_object_at(struct addressspace *self, uintptr_t virtualbase, physptr physicalbase, size_t size, uint8_t mapflags);
WARN_UNUSED_RESULT struct vmobject *vmm_alloc(struct addressspace *self, size_t size, uint8_t mapflags);
WARN_UNUSED_RESULT struct vmobject *vmm_alloc_at(struct addressspace *self, uintptr_t virtualbase, size_t size, uint8_t mapflags);
WARN_UNUSED_RESULT struct vmobject *vmm_map(struct addressspace *self, uintptr_t physicalbase, size_t size, uint8_t mapflags);
WARN_UNUSED_RESULT struct vmobject *vmm_map_at(struct addressspace *self, uintptr_t virtualbase, uintptr_t physicalbase, size_t size, uint8_t mapflags);
/*
 * "Easy" version of vmm_map/vmm_alloc_object. If it succeeds, it returns
 * pointer to mapped memory (Not vmobject!). If it fails, it just panics.
 * Allocated memory will be R+W permission.
 * The purpose of this function is to simplify mapping hardware prepherals:
 * ```
 * char *vmem = vmm_ezmap(0xb80000, 4000); // So easy :D
 * ```
 *
 * In addition to being easy wrapper for vmm_map, it also supports mapping
 * addresses that are not at page boundary: The actual mapping will be done at
 * page boundary, but then it adds appropriate offset and returns that pointer.
 *
 * `vmm_ezmap` has one caveat: There's no support for remap/unmapping. This is
 * because the underlying vmobject is not returned for simplicity. 
 */
void *vmm_ezmap(physptr base, size_t size);
void vmm_free(struct vmobject *object);
struct addressspace *vmm_get_kernel_addressspace(void);
// Note that this will return NULL if it points to kernel area, but outside of kernel VM,
// since those areas are never meant to be touched by VM code.
struct addressspace *vmm_addressspace_for(uintptr_t addr);
void vmm_pagefault(uintptr_t addr, bool was_present, bool was_write, bool was_user, void *trapframe);
bool vmm_random_test(void);
