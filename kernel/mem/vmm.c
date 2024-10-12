#include <assert.h>
#include <kernel/arch/interrupts.h> 
#include <kernel/arch/mmu.h> 
#include <kernel/arch/stacktrace.h> 
#include <kernel/io/tty.h> 
#include <kernel/lib/bitmap.h> 
#include <kernel/lib/bst.h> 
#include <kernel/lib/list.h> 
#include <kernel/lib/miscmath.h> 
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h> 
#include <kernel/mem/vmm.h>
#include <kernel/panic.h> 
#include <kernel/status.h> 
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------ Configuration ------------------------------

// Print when page fault occurs?
static bool const CONFIG_PRINT_PAGE_FAULTS = false;

// ---------------------------------------------------------------------------

struct objectgroup {
    struct bst_node node;
    struct list objectlist; // Holds vmobject
};

struct uncommitedobject {
    struct list_node node;
    struct vmobject *object;
    struct bitmap bitmap;
    bitword_t bitmapdata[];
};

static struct vmobject *takeobject(struct addressspace *self, struct objectgroup *group, struct vmobject *object) {
    list_removenode(&group->objectlist, &object->node);
    if (group->objectlist.front == NULL) {
        // list is empty -> Remove the node
        bst_removenode(&self->objectgrouptree, &group->node);
        heap_free(group);
    }
    return object;
}

// Finds the first VM object with given minium size.
static FAILABLE_FUNCTION takeobject_with_minsize(struct vmobject **out, struct addressspace *self, size_t pagecount) {
FAILABLE_PROLOGUE
    assert(pagecount != 0);
    if (self->objectgrouptree.root == NULL) {
        // Tree is empty
        THROW(ERR_NOMEM);
    }
    struct bst_node *nextnode = NULL;
    struct vmobject *result = NULL;
    for (struct bst_node *currentnode = bst_minof_tree(&self->objectgrouptree); currentnode != NULL; currentnode = nextnode) {
        nextnode = bst_successor(currentnode);
        struct objectgroup *currentgroup = currentnode->data;
        assert(currentgroup);

        size_t currentpagecount = currentgroup->node.key;
        if (currentpagecount < pagecount) {
            // Not enough pages
            continue;
        }
        struct list_node *objectnode = currentgroup->objectlist.front;
        assert(objectnode); // BST nodes with empty list should NOT exist
        assert(objectnode->data);
        result = takeobject(self, currentgroup, objectnode->data);
        break;
    }
    if (result == NULL) {
        THROW(ERR_NOMEM);
    }
    *out = result;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

// Finds the first VM object that includes given address.
// (Size between two are adjusted to nearest page size)
static FAILABLE_FUNCTION take_object_including(struct vmobject **out, struct addressspace *self, uintptr_t startaddress, uintptr_t endaddress) {
FAILABLE_PROLOGUE
    size_t pagecount = sizetoblocks(endaddress - startaddress + 1, ARCH_PAGESIZE);
    assert(pagecount != 0);
    if ((UINTPTR_MAX - startaddress) < (pagecount * ARCH_PAGESIZE)) {
        // Too large
        THROW(ERR_NOMEM);
    }
    endaddress = startaddress + (pagecount * ARCH_PAGESIZE - 1);

    if (self->objectgrouptree.root == NULL) {
        // Tree is empty
        THROW(ERR_NOMEM);
    }

    struct bst_node *nextnode = NULL;
    struct vmobject *result = NULL;
    for (struct bst_node *currentnode = bst_minof_tree(&self->objectgrouptree); currentnode != NULL; currentnode = nextnode) {
        nextnode = bst_successor(currentnode);
        struct objectgroup *currentgroup = currentnode->data;
        assert(currentgroup);

        size_t currentpagecount = currentgroup->node.key;
        if (currentpagecount < pagecount) {
            // Not enough pages
            continue;
        }

        struct vmobject *resultobject = NULL;
        for (struct list_node *objectnode = currentgroup->objectlist.front; objectnode != NULL; objectnode = objectnode->next) {
            struct vmobject *object = objectnode->data;
            assert(object);
            if (
                (startaddress < object->startaddress) ||
                (object->endaddress < startaddress) ||
                (endaddress < object->startaddress) ||
                (object->endaddress < endaddress)
            ) {
                // Address is out of range
                continue;
            }
            resultobject = object;
            break;
        }
        if (resultobject == NULL) {
            continue;
        }
        result = takeobject(self, currentgroup, resultobject);
        break;
    }
    if (result == NULL) {
        THROW(ERR_NOMEM);
    }
    *out = result;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION create_object(struct vmobject **out, struct addressspace *self, uintptr_t startaddress, uintptr_t endaddress, physptr_t physicalbase, memmapflags_t mapflags) {
FAILABLE_PROLOGUE
    struct vmobject *object = heap_alloc(sizeof(*object), HEAP_FLAG_ZEROMEMORY);
    if (object == NULL) {
        THROW(ERR_NOMEM);
    }
    object->startaddress = startaddress;
    object->endaddress = endaddress;
    object->addressspace = self;
    object->mapflags = mapflags;
    object->physicalbase = physicalbase;
    *out = object;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

// `object` is only used to store pointer to the uncommitedobject, so it doesn't have to be initialized. 
static FAILABLE_FUNCTION create_uncommitedobject(struct uncommitedobject **out, struct vmobject *object, size_t pagecount) {
FAILABLE_PROLOGUE
    size_t wordcount = bitmap_neededwordcount(pagecount);
    size_t size = sizeof(struct uncommitedobject) + (wordcount * sizeof(bitword_t));
    struct uncommitedobject *uobject = heap_alloc(size, HEAP_FLAG_ZEROMEMORY);
    if (uobject == NULL) {
        THROW(ERR_NOMEM);
    }
    uobject->object = object;
    uobject->bitmap.words = uobject->bitmapdata;
    uobject->bitmap.wordcount = wordcount;
    bitmap_setbits(&uobject->bitmap, 0, pagecount);
    *out = uobject;
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

static FAILABLE_FUNCTION add_object_to_tree(struct addressspace *self, struct vmobject *object) {
FAILABLE_PROLOGUE
    while(1) {
        assert(isaligned(object->endaddress - object->startaddress + 1, ARCH_PAGESIZE));
        size_t pagecount = (object->endaddress - object->startaddress + 1) / ARCH_PAGESIZE;
        struct bst_node *groupnode = bst_findnode(&self->objectgrouptree, pagecount);
        if (groupnode == NULL) {
            // Create a new object group
            struct objectgroup *group = heap_alloc(sizeof(*group), HEAP_FLAG_ZEROMEMORY);
            if (group == NULL) {
                THROW(ERR_NOMEM);
            }
            list_init(&group->objectlist);
            groupnode = &group->node;
            bst_insertnode(&self->objectgrouptree, &group->node, pagecount, group);
        }
        struct objectgroup *group = groupnode->data;

        // Insert the object into object group. Note that we keep the list sorted by address.
        struct list_node *insertafter = NULL;
        for (struct list_node *currentnode = group->objectlist.front; currentnode != NULL; currentnode = currentnode->next) {
            struct vmobject *currentobject = currentnode->data;
            if (currentobject->endaddress < object->startaddress) {
                insertafter = currentnode;
            } else {
                break;
            }
        }
        if (insertafter == NULL) {
            list_insertfront(&group->objectlist, &object->node, object);
        } else {
            list_insertafter(&group->objectlist, insertafter, &object->node, object);
        }
        // Because the list is sorted, we can easily figure out previous and/or next region is actually contiguous.
        // If so, remove that region, and make the current object larger to include it.
        bool sizechanged = false;
        if (object->node.prev != NULL) {
            struct vmobject *prevobject = object->node.prev->data;
            if ((prevobject->endaddress + 1) == object->startaddress) {
                object->startaddress = prevobject->startaddress;
                list_removenode(&group->objectlist, &prevobject->node);
                heap_free(prevobject);
                sizechanged = true;
            }
        }
        if (object->node.next != NULL) {
            struct vmobject *nextobject = object->node.next->data;
            assert(nextobject->startaddress != 0);
            if ((nextobject->startaddress - 1) == object->endaddress) {
                object->endaddress = nextobject->endaddress;
                list_removenode(&group->objectlist, &nextobject->node);
                heap_free(nextobject);
                sizechanged = true;
            }
        }
        if (!sizechanged) {
            break;
        }
        // If the size was changed(i.e. Object was cominbed with nearby ones), it no longer belongs to current group.
        // Remove from the group and go back to beginning.
        list_removenode(&group->objectlist, &object->node);
        if (group->objectlist.front == NULL) {
            // list is empty -> Remove the group.
            bst_removenode(&self->objectgrouptree, &group->node);
            heap_free(group);
        }
    }
FAILABLE_EPILOGUE_BEGIN
FAILABLE_EPILOGUE_END
}

// Returns NULL if not found.
static struct uncommitedobject *find_object_in_uncommited(struct addressspace *self, uintptr_t addr) {
    uintptr_t page_base = aligndown(addr, ARCH_PAGESIZE);
    for (struct list_node *objectnode = self->uncommitedobjects.front; objectnode != NULL; objectnode = objectnode->next) {
        struct uncommitedobject *uobject = objectnode->data;
        assert(uobject != NULL);
        if ((uobject->object->startaddress <= page_base) && (page_base <= uobject->object->endaddress)) {
            return uobject;
        }
    }
    return NULL;
}

FAILABLE_FUNCTION vmm_init_addressspace(struct addressspace *out, uintptr_t startaddress, uintptr_t endaddress, bool is_user) {
FAILABLE_PROLOGUE
    memset(out, 0, sizeof(*out));
    out->is_user = is_user;
    struct vmobject *object = NULL;
    TRY(create_object(&object, out, startaddress, endaddress, VMM_PHYSADDR_NOMAP, 0));
    TRY(add_object_to_tree(out, object));
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(object);
    }
FAILABLE_EPILOGUE_END
}



void vmm_deinit_addressspace(struct addressspace *self) {
    for (struct bst_node *currentnode = bst_minof_tree(&self->objectgrouptree); currentnode != NULL; currentnode = bst_successor(currentnode)) {
        struct objectgroup *group = currentnode->data;
        while (1) {
            struct list_node *objectnode = list_removefront(&group->objectlist);
            if (objectnode == NULL) {
                break;
            }
            heap_free(objectnode->data);
        } 
    }
    heap_free(self);
}

FAILABLE_FUNCTION vmm_alloc_object(struct vmobject **out, struct addressspace *self, physptr_t physicalbase, size_t size, memmapflags_t mapflags) {
FAILABLE_PROLOGUE
    struct uncommitedobject *uobject = NULL;
    struct vmobject *newobject = NULL;
    struct vmobject *oldobject = NULL;

    size_t pagecount = sizetoblocks(size, ARCH_PAGESIZE);
    if (physicalbase != VMM_PHYSADDR_NOMAP) {
        if (!isaligned(physicalbase, ARCH_PAGESIZE)) {
            THROW(ERR_FAULT);
        }
    }

    // We create objects first with dummy values, so that we just have to free those objects on failure.
    // (It's a good idea to avoid undoing takeobject~, because that undo operation can technically
    // also fail)
    TRY(create_object(&newobject, self, 0, 0, physicalbase, mapflags));
    TRY(create_uncommitedobject(&uobject, newobject, pagecount));
    TRY(takeobject_with_minsize(&oldobject, self, pagecount));
    size_t newsize = pagecount * ARCH_PAGESIZE;
    newobject->startaddress = oldobject->startaddress;
    newobject->endaddress = newobject->startaddress + newsize - 1;
    // Shrink the existing object
    oldobject->startaddress += newsize;
    if (oldobject->endaddress < oldobject->startaddress) {
        // The object is no longer valid. Remove it.
        heap_free(oldobject);
    } else {
        // Add modified object back to the tree.
        status_t status = add_object_to_tree(self, oldobject);
        if (status != OK) {
            tty_printf("vmm: could not add modified vm object back to vmm(error %d). this may decrease usable virtual memory.\n", status);
        }
    }
    oldobject = NULL;
    list_insertback(&self->uncommitedobjects, &uobject->node, uobject);
    *out = newobject;
FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(uobject);
        heap_free(oldobject);
        heap_free(newobject);
    }
FAILABLE_EPILOGUE_END
}

FAILABLE_FUNCTION vmm_alloc_object_at(struct vmobject **out, struct addressspace *self, uintptr_t virtualbase, physptr_t physicalbase, size_t size, memmapflags_t mapflags) {
FAILABLE_PROLOGUE
    bool previnterrupts = arch_interrupts_disable();
    assert(virtualbase);
    size_t pagecount = sizetoblocks(size, ARCH_PAGESIZE);
    if (physicalbase != VMM_PHYSADDR_NOMAP) {
        if (!isaligned(physicalbase, ARCH_PAGESIZE)) {
            THROW(ERR_FAULT);
        }
    }
    if (!isaligned(virtualbase, ARCH_PAGESIZE)) {
        THROW(ERR_FAULT);
    }

    if ((SIZE_MAX / ARCH_PAGESIZE) < pagecount) {
        THROW(ERR_NOMEM);
    }
    size_t actualsize = ARCH_PAGESIZE * pagecount;
    if ((UINTPTR_MAX - virtualbase) < actualsize) {
        THROW(ERR_NOMEM);
    }
    uintptr_t endaddress = virtualbase + actualsize - 1;

    // We create objects first with dummy values, so that we just have to free those objects on failure.
    // (It's a good idea to avoid undoing takeobject~, because that undo operation can technically also fail)
    struct vmobject *newobject = NULL, *rightobject = NULL;
    TRY(create_object(&newobject, self, 0, 0, physicalbase, mapflags));
    TRY(create_object(&rightobject, self, 0, 0, VMM_PHYSADDR_NOMAP, 0));
    struct uncommitedobject *uobject = NULL;
    TRY(create_uncommitedobject(&uobject, newobject, pagecount));

    struct vmobject *leftobject = NULL;
    TRY(take_object_including(&leftobject, self, virtualbase, endaddress));
    uintptr_t oldendaddr = leftobject->endaddress;

    size_t newsize = pagecount * ARCH_PAGESIZE;
    newobject->startaddress = virtualbase;
    newobject->endaddress = newobject->startaddress + newsize - 1;

    // Split into two objects
    leftobject->endaddress = virtualbase - 1;
    rightobject->startaddress = virtualbase + (pagecount * ARCH_PAGESIZE);
    rightobject->endaddress = oldendaddr;

    if (leftobject->endaddress < leftobject->startaddress) {
        heap_free(leftobject);
    } else {
        // Add modified object back to the tree.
        status_t status = add_object_to_tree(self, leftobject);
        if (status != OK) {
            tty_printf("vmm: could not add modified vm object back to vmm(error %d). this may decrease usable virtual memory.\n", status);
        }
    }
    leftobject = NULL;
    if (rightobject->endaddress < rightobject->startaddress) {
        heap_free(rightobject);
    } else {
        // Add modified object back to the tree.
        status_t status = add_object_to_tree(self, rightobject);
        if (status != OK) {
            tty_printf("vmm: could not add modified vm object back to vmm(error %d). this may decrease usable virtual memory.\n", status);
        }
    }
    rightobject = NULL;
    list_insertback(&self->uncommitedobjects, &uobject->node, uobject);
    *out = newobject;

FAILABLE_EPILOGUE_BEGIN
    if (DID_FAIL) {
        heap_free(uobject);
        heap_free(newobject);
        heap_free(leftobject);
        heap_free(rightobject);
    }
    interrupts_restore(previnterrupts);
FAILABLE_EPILOGUE_END
}

void vmm_free(struct vmobject *object) {
    bool previnterrupts = arch_interrupts_disable();
    // Remove from uncommited memory list
    struct uncommitedobject *uobject = find_object_in_uncommited(object->addressspace, object->startaddress);
    if (uobject != NULL) {
        list_removenode(&object->addressspace->uncommitedobjects, &uobject->node);
        heap_free(uobject);

    }
    // Free commited physical pages and unmap it.
    for (uintptr_t addr = object->startaddress; addr <= object->endaddress; addr += ARCH_PAGESIZE) {
        physptr_t physaddr;
        status_t status = arch_mmu_virttophys(&physaddr, addr);
        if (status == OK) {
            if (object->physicalbase == VMM_PHYSADDR_NOMAP) {
                pmm_free(physaddr, 1);
            }
            status = arch_mmu_unmap(addr, 1);
            if (status != OK) {
                tty_printf("vmm: failed to unmap commited page at %#lx (error %d)\n", addr, status);
                panic("failed to unmap commited pages");
            }
        }
    }
    // Return object back to the tree
    status_t status = add_object_to_tree(object->addressspace, object);
    if (status != OK) {
        tty_printf("vmm: could not add returned vm object back to vmm(error %d). this may decrease usable virtual memory.\n", status);
    }
    interrupts_restore(previnterrupts);
}

FAILABLE_FUNCTION vmm_alloc(struct vmobject **out, struct addressspace *self, size_t size, memmapflags_t mapflags) {
    return vmm_alloc_object(out, self, VMM_PHYSADDR_NOMAP, size, mapflags);
}
FAILABLE_FUNCTION vmm_alloc_at(struct vmobject **out, struct addressspace *self, uintptr_t virtualbase, size_t size, memmapflags_t mapflags) {
    return vmm_alloc_object_at(out, self, virtualbase, VMM_PHYSADDR_NOMAP, size, mapflags);
}
FAILABLE_FUNCTION vmm_map(struct vmobject **out, struct addressspace *self, uintptr_t physicalbase, size_t size, memmapflags_t mapflags) {
    assert(physicalbase != VMM_PHYSADDR_NOMAP);
    return vmm_alloc_object(out, self, physicalbase, size, mapflags);
}
FAILABLE_FUNCTION vmm_map_at(struct vmobject **out, struct addressspace *self, uintptr_t virtualbase, uintptr_t physicalbase, size_t size, memmapflags_t mapflags) {
    assert(physicalbase != VMM_PHYSADDR_NOMAP);
    return vmm_alloc_object_at(out, self, virtualbase, physicalbase, size, mapflags);
}

// "Easy" version of vmm_map/vmm_alloc_object. If it succeeds, it returns pointer to mapped memory
// (Not vmobject!). If it fails, it just panics. Allocated memory will be R+W permission.
// The purpose of this function is to simplify mapping hardware prepherals:
// ```
// char *vmem = vmm_ezmap(0xb80000, 4000); // So easy :D
// ```
//
// In addition to being easy wrapper for vmm_map, it also supports mapping addresses that are not
// at page boundary: The actual mapping will be done at page boundary, but then it adds appropriate offset
// and returns that pointer.
//
// vmm_ezmap has one caveat: There's no support for remap/unmapping. This is because the underlying vmobject
// is not returned for simplicity. 
void *vmm_ezmap(physptr_t base, size_t size) {
    size_t offset = base % ARCH_PAGESIZE;
    physptr_t page_base = base - offset;
    size_t actualsize = size + offset;
    struct vmobject *object;
    status_t status = vmm_map(&object, vmm_get_kernel_addressspace(), page_base, actualsize, MAP_PROT_READ | MAP_PROT_WRITE);
    if (status != OK) {
        tty_printf("vmm: could not map memory at %#lx(size: %zu) to vm (error %d)\n", base, size, status);
        panic("failed to map to virtual memory");
    }
    return (void *)(object->startaddress + offset);
}


struct addressspace *vmm_get_kernel_addressspace(void) {
    static struct addressspace addressspace;
    static bool initialized = false;

    if (!initialized) {
        status_t status = vmm_init_addressspace(&addressspace, ARCH_KERNEL_VM_START, ARCH_KERNEL_VM_END, false);
        if (status != OK) {
            tty_printf("vmm_init_addressspace failed(error %d)\n", status);
            panic("could not initialize kernel addessspace");
        }
        initialized = true;
    }
    return &addressspace;
}

struct addressspace *vmm_addressspace_for(uintptr_t addr) {
    if ((ARCH_KERNEL_VM_START <= addr) && (addr <= ARCH_KERNEL_VM_END)) {
        // Kernel VM
        return vmm_get_kernel_addressspace();
    } else if (addr < ARCH_KERNEL_SPACE_BASE) {
        // Userspace address
        tty_printf("vmm_addressspace_for: userspace addresses are not supported yet\n");
        return NULL;
    } else {
        // Kernel image, scratch page, etc...
        return NULL;
    }
}

void vmm_pagefault(uintptr_t addr, bool was_present, bool was_write, bool was_user, void *trapframe) {
    if (CONFIG_PRINT_PAGE_FAULTS) {
        tty_printf("[PF] addr=%#lx, was_present=%d, was_write=%d, was_user=%d\n", addr, was_present, was_write, was_user);
    }
    uintptr_t page_base = aligndown(addr, ARCH_PAGESIZE);
    // Maybe it just needs TLB flushing?
    physptr_t physaddr;
    status_t status = arch_mmu_emulate(&physaddr, addr, MAP_PROT_READ | (was_write ? MAP_PROT_WRITE : 0), was_user);
    if (status == OK) {
        arch_mmu_flushtlb_for((void *)addr);
        return;
    }
    // If the page was present, we have privilege violation. 
    if (was_present) {
        tty_printf("privilege violation: attempted to %s on page at %#lx\n", was_write ? "read" : "write", addr);
        goto realfault;
    }
    if (page_base == 0) {
        tty_printf("NULL dereference: attempted to %s on page at %#lx\n", was_write ? "read" : "write", addr);
        goto realfault;
    }
    // See if it's uncommited object.
    struct addressspace *addressspace = vmm_addressspace_for(page_base);
    if (addressspace == NULL) {
        // We don't know what this address is.
        goto nonpresent;
    }
    struct uncommitedobject *uobject = find_object_in_uncommited(addressspace, addr);
    if (uobject == NULL) {
        // Not part of an uncommited object.
        goto nonpresent;
    }
    size_t pageindex = (page_base - uobject->object->startaddress) / ARCH_PAGESIZE;
    if (!bitmap_isbitset(&uobject->bitmap, pageindex)) {
        // Already commited page...?
        tty_printf("non-present page %#lx(base: %#lx) but it's already commited. WTF?\n", addr, page_base);
        panic("non-present page on already commited page");
    }
    // Uncommited page
    bitmap_clearbit(&uobject->bitmap, pageindex);
    size_t pagecount = 1;
    if (uobject->object->physicalbase != VMM_PHYSADDR_NOMAP) {
        physaddr = uobject->object->physicalbase + (pageindex * ARCH_PAGESIZE);
    } else {
        status_t status = pmm_alloc(&physaddr, &pagecount);
        if (status != OK) {
            // TODO: Run the OOM killer
            panic("ran out of memory while trying to commit the page");
        }
    }
    status = arch_mmu_map(page_base, physaddr, 1, uobject->object->mapflags, uobject->object->addressspace->is_user);
    if (status != OK) {
        tty_printf("arch_mmu_map failed (error %d)\n", status);
        panic("failed to map allocated memory");
    }
    if (bitmap_findfirstsetbit(&uobject->bitmap, 0) < 0) {
        list_removenode(&uobject->object->addressspace->uncommitedobjects, &uobject->node);
        heap_free(uobject);
    }
    return;
nonpresent:
    tty_printf("attempted to %s on non-present page at %#lx\n", was_write ? "read" : "write", addr);
realfault:
    arch_stacktrace_for_trapframe(trapframe);
    panic("fatal memory access fault");
}

enum {
    RAND_TEST_ALLOC_COUNT = 100,
    MAX_ALLOC_SIZE        = (1024 * 1024) / RAND_TEST_ALLOC_COUNT,
};

bool vmm_random_test(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    struct vmobject *allocobjects[RAND_TEST_ALLOC_COUNT];

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while(1) {
            allocsizes[i] = (size_t)rand() % MAX_ALLOC_SIZE;
            allocsizes[i] = MAX_ALLOC_SIZE;
            status_t status = vmm_alloc(&allocobjects[i], vmm_get_kernel_addressspace(), allocsizes[i], MAP_PROT_READ | MAP_PROT_WRITE);
            if (status == OK) {
                break;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptrcount = allocsizes[i] / sizeof(void *); 
        void **ptr = (void *)allocobjects[i]->startaddress;
        for (size_t j = 0; j < ptrcount; j++) {
            ptr[j] = &ptr[j];
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptrcount = allocsizes[i] / sizeof(void *); 
        void **ptr = (void *)allocobjects[i]->startaddress;
        for (size_t j = 0; j < ptrcount; j++) {
            void *expectedvalue = &ptr[j];
            void *gotvalue = ptr[j];
            if (ptr[j] != gotvalue) {
                tty_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", &ptr[j], i, ptr, j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        vmm_free(allocobjects[i]);
    }
    return true;
testfail:
    tty_printf("vmm: random test failed\n");
    return false;
}
