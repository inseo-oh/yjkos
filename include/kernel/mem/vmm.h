#pragma once
#include <kernel/types.h>
#include <kernel/lib/bst.h> 
#include <kernel/lib/list.h> 
#include <kernel/status.h> 
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

FAILABLE_FUNCTION vmm_init_addressspace(struct addressspace *out, uintptr_t startaddress, uintptr_t endaddress, bool is_user);
void vmm_deinit_addressspace(struct addressspace *self);

// XXX: Instead of accepting addressspace, figure out addressspace itself.

FAILABLE_FUNCTION vmm_alloc_object(struct vmobject **out, struct addressspace *self, physptr physicalbase, size_t size, uint8_t mapflags);
FAILABLE_FUNCTION vmm_alloc_object_at(struct vmobject **out, struct addressspace *self, uintptr_t virtualbase, physptr physicalbase, size_t size, uint8_t mapflags);
FAILABLE_FUNCTION vmm_alloc(struct vmobject **out, struct addressspace *self, size_t size, uint8_t mapflags);
FAILABLE_FUNCTION vmm_alloc_at(struct vmobject **out, struct addressspace *self, uintptr_t virtualbase, size_t size, uint8_t mapflags);
FAILABLE_FUNCTION vmm_map(struct vmobject **out, struct addressspace *self, uintptr_t physicalbase, size_t size, uint8_t mapflags);
FAILABLE_FUNCTION vmm_map_at(struct vmobject **out, struct addressspace *self, uintptr_t virtualbase, uintptr_t physicalbase, size_t size, uint8_t mapflags);
void *vmm_ezmap(physptr base, size_t size);
void vmm_free(struct vmobject *object);
struct addressspace *vmm_get_kernel_addressspace(void);
// Note that this will return NULL if it points to kernel area, but outside of kernel VM,
// since those areas are never meant to be touched by VM code.
struct addressspace *vmm_addressspace_for(uintptr_t addr);
void vmm_pagefault(uintptr_t addr, bool was_present, bool was_write, bool was_user, void *trapframe);
bool vmm_random_test(void);
