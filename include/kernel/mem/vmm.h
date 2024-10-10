#pragma once
#include <kernel/types.h>
#include <kernel/lib/bst.h> 
#include <kernel/lib/list.h> 
#include <kernel/status.h> 
#include <stdint.h>
#include <stdbool.h>

typedef uint8_t memmapflags_t;
static memmapflags_t const MAP_PROT_READ  = 1 << 0;
static memmapflags_t const MAP_PROT_WRITE = 1 << 1;
static memmapflags_t const MAP_PROT_EXEC  = 1 << 2;
static memmapflags_t const MAP_PROT_NOCACHE  = 1 << 3;

typedef struct addressspace addressspace_t; 
struct addressspace {
    bst_t objectgrouptree;     // objectgroup_t items, BST node key = Size of the page.
    list_t uncommitedobjects; // uncommitedobject_t items
    bool is_user;
};

typedef struct vmobject vmobject_t;
struct vmobject {
    list_node_t node;
    struct addressspace *addressspace;
    uintptr_t startaddress;
    uintptr_t endaddress;
    memmapflags_t mapflags;
    physptr_t physicalbase;  // VMM_PHYSADDR_NOMAP means it allocates pages instead of mapping existing pages.
};

static physptr_t const VMM_PHYSADDR_NOMAP = ~0;

FAILABLE_FUNCTION vmm_init_addressspace(struct addressspace *out, uintptr_t startaddress, uintptr_t endaddress, bool is_user);
void vmm_deinit_addressspace(struct addressspace *self);

// XXX: Instead of accepting addressspace_t, figure out addressspace_t itself.

FAILABLE_FUNCTION vmm_alloc_object(vmobject_t **out, struct addressspace *self, physptr_t physicalbase, size_t size, memmapflags_t mapflags);
FAILABLE_FUNCTION vmm_alloc_object_at(vmobject_t **out, struct addressspace *self, uintptr_t virtualbase, physptr_t physicalbase, size_t size, memmapflags_t mapflags);
FAILABLE_FUNCTION vmm_alloc(vmobject_t **out, struct addressspace *self, size_t size, memmapflags_t mapflags);
FAILABLE_FUNCTION vmm_alloc_at(vmobject_t **out, struct addressspace *self, uintptr_t virtualbase, size_t size, memmapflags_t mapflags);
FAILABLE_FUNCTION vmm_map(vmobject_t **out, struct addressspace *self, uintptr_t physicalbase, size_t size, memmapflags_t mapflags);
FAILABLE_FUNCTION vmm_map_at(vmobject_t **out, struct addressspace *self, uintptr_t virtualbase, uintptr_t physicalbase, size_t size, memmapflags_t mapflags);
void *vmm_ezmap(physptr_t base, size_t size);
void vmm_free(vmobject_t *object);
addressspace_t *vmm_get_kernel_addressspace(void);
// Note that this will return NULL if it points to kernel area, but outside of kernel VM,
// since those areas are never meant to be touched by VM code.
addressspace_t *vmm_addressspace_for(uintptr_t addr);
void vmm_pagefault(uintptr_t addr, bool was_present, bool was_write, bool was_user, void *trapframe);
bool vmm_random_test(void);
