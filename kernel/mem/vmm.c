#include <assert.h>
#include <kernel/arch/interrupts.h> 
#include <kernel/arch/mmu.h> 
#include <kernel/arch/stacktrace.h> 
#include <kernel/io/co.h> 
#include <kernel/lib/bitmap.h> 
#include <kernel/lib/bst.h> 
#include <kernel/lib/diagnostics.h> 
#include <kernel/lib/list.h> 
#include <kernel/lib/miscmath.h> 
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h> 
#include <kernel/mem/vmm.h>
#include <kernel/panic.h> 
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
    uint bitmapdata[];
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

/*
 * Finds the first VM object with given minium size.
 */
static struct vmobject *takeobject_with_minsize(
    struct addressspace *self, size_t pagecount
) {
    assert(pagecount != 0);
    struct vmobject *result = NULL;
    if (self->objectgrouptree.root == NULL) {
        // Tree is empty
        goto out;
    }
    struct bst_node *nextnode = NULL;
    for (
        struct bst_node *currentnode =
            bst_minof_tree(&self->objectgrouptree);
            currentnode != NULL; currentnode = nextnode)
    {
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
out:
    return result;
}

/*
 * Finds the first VM object that includes given address.
 * (Size between two are adjusted to nearest page size)
 */
static struct vmobject *take_object_including(
    struct addressspace *self, uintptr_t startaddress, uintptr_t endaddress
) {
    size_t pagecount = sizetoblocks(endaddress - startaddress + 1, ARCH_PAGESIZE);
    assert(pagecount != 0);
    struct vmobject *result = NULL;
    if ((UINTPTR_MAX - startaddress) < (pagecount * ARCH_PAGESIZE)) {
        // Too large
        goto out;
    }
    endaddress = startaddress + (pagecount * ARCH_PAGESIZE - 1);
    if (self->objectgrouptree.root == NULL) {
        // Tree is empty
        goto out;
    }
    struct bst_node *nextnode = NULL;
    for (
        struct bst_node *currentnode =
            bst_minof_tree(&self->objectgrouptree);
        currentnode != NULL; currentnode = nextnode)
    {
        nextnode = bst_successor(currentnode);
        struct objectgroup *currentgroup = currentnode->data;
        assert(currentgroup);

        size_t currentpagecount = currentgroup->node.key;
        if (currentpagecount < pagecount) {
            // Not enough pages
            continue;
        }

        struct vmobject *resultobject = NULL;
        LIST_FOREACH(&currentgroup->objectlist, objectnode) {
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
out:
    return result;
}

/*
 * Returns NULL on allocation failure.
 */
static struct vmobject * create_object(
    struct addressspace *self, uintptr_t startaddress, uintptr_t endaddress,
    physptr physicalbase, uint8_t mapflags
) {
    struct vmobject *object = heap_alloc(
        sizeof(*object), HEAP_FLAG_ZEROMEMORY);
    if (object == NULL) {
        return NULL;
    }
    object->startaddress = startaddress;
    object->endaddress = endaddress;
    object->addressspace = self;
    object->mapflags = mapflags;
    object->physicalbase = physicalbase;
    return object;
}

/*
 * `object` is only used to store pointer to the uncommitedobject, so it
 * doesn't have to be initialized.
 *
 * Returns NULL on allocation failure.
 */
static struct uncommitedobject *create_uncommitedobject(
    struct vmobject *object, size_t pagecount
) {
    size_t wordcount = bitmap_neededwordcount(pagecount);
    size_t size = sizeof(struct uncommitedobject) + (wordcount * sizeof(uint));
    struct uncommitedobject *uobject = heap_alloc(size, HEAP_FLAG_ZEROMEMORY);
    if (uobject == NULL) {
        return NULL;
    }
    uobject->object = object;
    uobject->bitmap.words = uobject->bitmapdata;
    uobject->bitmap.wordcount = wordcount;
    bitmap_setbits(&uobject->bitmap, 0, pagecount);
    return uobject;
}

static WARN_UNUSED_RESULT bool add_object_to_tree(
    struct addressspace *self, struct vmobject *object)
{
    while(1) {
        assert(isaligned(
            object->endaddress - object->startaddress + 1,
            ARCH_PAGESIZE));
        size_t pagecount = (object->endaddress - object->startaddress + 1)
            / ARCH_PAGESIZE;
        struct bst_node *groupnode = bst_findnode(&self->objectgrouptree, pagecount);
        if (groupnode == NULL) {
            // Create a new object group
            struct objectgroup *group = heap_alloc(
                sizeof(*group), HEAP_FLAG_ZEROMEMORY);
            if (group == NULL) {
                goto oom;
            }
            list_init(&group->objectlist);
            groupnode = &group->node;
            bst_insertnode(&self->objectgrouptree, &group->node, pagecount, group);
        }
        struct objectgroup *group = groupnode->data;

        /*
         * Insert the object into object group. Note that we keep the list
         * sorted by address.
         */
        struct list_node *insertafter = NULL;
        LIST_FOREACH(&group->objectlist, currentnode) {
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
        /*
         * Because the list is sorted, we can easily figure out previous and/or
         * next region is actually contiguous.
         * If so, remove that region, and make the current object larger to
         * include it.
         */
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
        /*
         * If the size was changed(i.e. Object was cominbed with nearby ones),
         * it no longer belongs to current group.
         * Remove the object from group and go back to beginning.
        */
        list_removenode(&group->objectlist, &object->node);
        if (group->objectlist.front == NULL) {
            // list is empty -> Remove the group.
            bst_removenode(&self->objectgrouptree, &group->node);
            heap_free(group);
        }
    }
    return true;
oom:
    return false;
}

/*
 * Returns NULL if not found.
 */
static struct uncommitedobject *find_object_in_uncommited(struct addressspace *self, uintptr_t addr) {
    uintptr_t page_base = aligndown(addr, ARCH_PAGESIZE);
    LIST_FOREACH(&self->uncommitedobjects, objectnode) {
        struct uncommitedobject *uobject = objectnode->data;
        assert(uobject != NULL);
        if ((uobject->object->startaddress <= page_base) && (page_base <= uobject->object->endaddress)) {
            return uobject;
        }
    }
    return NULL;
}

WARN_UNUSED_RESULT bool vmm_init_addressspace(struct addressspace *out, uintptr_t startaddress, uintptr_t endaddress, bool is_user) {
    memset(out, 0, sizeof(*out));
    out->is_user = is_user;
    struct vmobject *object = create_object(
        out, startaddress, endaddress, VMM_PHYSADDR_NOMAP,
        0);
    if (object == NULL) {
        goto fail_oom;
    }
    if (!add_object_to_tree(out, object)) {
        goto fail_oom;
    }
    return true;
fail_oom:
    heap_free(object);
    return false;
}



void vmm_deinit_addressspace(struct addressspace *self) {
    for (
        struct bst_node *currentnode =
            bst_minof_tree(&self->objectgrouptree); currentnode != NULL;
            currentnode = bst_successor(currentnode))
    {
        struct objectgroup *group = currentnode->data;
        while (1) {
            struct list_node *objectnode =
                list_removefront(&group->objectlist);
            if (objectnode == NULL) {
                break;
            }
            heap_free(objectnode->data);
        } 
    }
    heap_free(self);
}

WARN_UNUSED_RESULT struct vmobject *vmm_alloc_object(
    struct addressspace *self, physptr physicalbase, size_t size,
    uint8_t mapflags
) {
    struct vmobject *oldobject = NULL;

    size_t pagecount = sizetoblocks(size, ARCH_PAGESIZE);
    if (physicalbase != VMM_PHYSADDR_NOMAP) {
        assert(isaligned(physicalbase, ARCH_PAGESIZE));
    }

    /*
     * We create objects first with dummy values, so that we just have to free
     * those objects on failure.
     * (It's a good idea to avoid undoing takeobject~, because that undo
     * operation can technically also fail)
     */
    struct vmobject *newobject = create_object(
        self, 0, 0, physicalbase, mapflags);
    struct uncommitedobject *uobject = create_uncommitedobject(
        newobject, pagecount);
    oldobject = takeobject_with_minsize(self, pagecount);
    if ((newobject == NULL) || (oldobject == NULL) || (oldobject == NULL)) {
        goto fail_oom;
    }
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
        int ret = add_object_to_tree(self, oldobject);
        if (ret < 0) {
            co_printf(
                "vmm: could not add modified vm object back to vmm(error %d)\n",
                ret);
        }
    }
    oldobject = NULL;
    list_insertback(&self->uncommitedobjects, &uobject->node, uobject);
    goto out;
fail_oom:
    heap_free(uobject);
    heap_free(oldobject);
    heap_free(newobject);
out:
    return newobject;
}

WARN_UNUSED_RESULT struct vmobject *vmm_alloc_object_at(
    struct addressspace *self, uintptr_t virtualbase, physptr physicalbase,
    size_t size, uint8_t mapflags)
{
    bool previnterrupts = arch_interrupts_disable();
    assert(virtualbase);
    size_t pagecount = sizetoblocks(size, ARCH_PAGESIZE);
    if (physicalbase != VMM_PHYSADDR_NOMAP) {
        assert(isaligned(physicalbase, ARCH_PAGESIZE));
    }
    assert(!isaligned(virtualbase, ARCH_PAGESIZE));

    if ((SIZE_MAX / ARCH_PAGESIZE) < pagecount) {
        goto fail_badsize;
    }
    size_t actualsize = ARCH_PAGESIZE * pagecount;
    if ((UINTPTR_MAX - virtualbase) < actualsize) {
        goto fail_badsize;
    }
    uintptr_t endaddress = virtualbase + actualsize - 1;

    /*
     * We create objects first with dummy values, so that we just have to free
     * those objects on failure.
     * (It's a good idea to avoid undoing takeobject~, because that undo
     * operation can technically also fail)
     */
    struct vmobject *leftobject = NULL;
    struct vmobject *newobject = create_object(
        self, 0, 0, physicalbase, mapflags);
    struct vmobject *rightobject = create_object(
        self, 0, 0, VMM_PHYSADDR_NOMAP,
        0);
    struct uncommitedobject *uobject = create_uncommitedobject(
        newobject, pagecount);
    if ((newobject == NULL) || (rightobject == NULL) || (uobject == NULL)) {
        goto fail_oom;
    } 

    leftobject = take_object_including(
        self, virtualbase, endaddress);
    if (leftobject == NULL) {
        goto fail_oom;
    }
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
        int ret = add_object_to_tree(self, leftobject);
        if (ret < 0) {
            co_printf(
                "vmm: could not add modified vm object back to vmm(error %d)\n",
                ret);
        }
    }
    leftobject = NULL;
    if (rightobject->endaddress < rightobject->startaddress) {
        heap_free(rightobject);
    } else {
        // Add modified object back to the tree.
        int ret = add_object_to_tree(self, rightobject);
        if (ret < 0) {
            co_printf(
                "vmm: could not add modified vm object back to vmm(error %d)\n",
                ret);
        }
    }
    rightobject = NULL;
    list_insertback(&self->uncommitedobjects, &uobject->node, uobject);
    goto out;
fail_oom:
    heap_free(uobject);
    heap_free(newobject);
    heap_free(leftobject);
    heap_free(rightobject);
fail_badsize:
    newobject = NULL;
out:
    interrupts_restore(previnterrupts);
    return newobject;
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
        physptr physaddr;
        if (arch_mmu_virttophys(&physaddr, addr) == 0) {
            if (object->physicalbase == VMM_PHYSADDR_NOMAP) {
                pmm_free(physaddr, 1);
            }
            int ret = arch_mmu_unmap(addr, 1);
            if (ret < 0) {
                co_printf(
                    "vmm: commited page %#lx doesn't exist?\n", addr);
                panic("failed to unmap commited pages");
            }
        }
    }
    // Return object back to the tree
    int ret = add_object_to_tree(object->addressspace, object);
    if (ret < 0) {
        co_printf(
            "vmm: could not register returned virtual memory(error %d). this may decrease usable virtual memory.\n", ret);
    }
    interrupts_restore(previnterrupts);
}

WARN_UNUSED_RESULT struct vmobject *vmm_alloc(
    struct addressspace *self, size_t size, uint8_t mapflags)
{
    return vmm_alloc_object(self, VMM_PHYSADDR_NOMAP, size, mapflags);
}
WARN_UNUSED_RESULT struct vmobject *vmm_alloc_at(struct addressspace *self, uintptr_t virtualbase, size_t size, uint8_t mapflags) {
    return vmm_alloc_object_at(self, virtualbase, VMM_PHYSADDR_NOMAP, size, mapflags);
}
WARN_UNUSED_RESULT struct vmobject *vmm_map(
    struct addressspace *self, uintptr_t physicalbase, size_t size, uint8_t mapflags)
{
    assert(physicalbase != VMM_PHYSADDR_NOMAP);
    return vmm_alloc_object(self, physicalbase, size, mapflags);
}
WARN_UNUSED_RESULT struct vmobject *vmm_map_at(struct addressspace *self, uintptr_t virtualbase, uintptr_t physicalbase, size_t size, uint8_t mapflags) {
    assert(physicalbase != VMM_PHYSADDR_NOMAP);
    return vmm_alloc_object_at(self, virtualbase, physicalbase, size, mapflags);
}

void *vmm_ezmap(physptr base, size_t size) {
    size_t offset = base % ARCH_PAGESIZE;
    physptr pagebase = base - offset;
    size_t actualsize = size + offset;
    struct vmobject *object = vmm_map(
        vmm_get_kernel_addressspace(), pagebase,
        actualsize, MAP_PROT_READ | MAP_PROT_WRITE);
    if (object == NULL) {
        panic("vmm: failed to ezmap virtual memory");
    }
    return (void *)(object->startaddress + offset);
}

struct addressspace *vmm_get_kernel_addressspace(void) {
    static struct addressspace addressspace;
    static bool initialized = false;

    if (!initialized) {
        if (!vmm_init_addressspace(
            &addressspace, ARCH_KERNEL_VM_START,
            ARCH_KERNEL_VM_END,false
        )) {
            panic("not enough memory to initialize kernel addessspace");
        }
        initialized = true;
    }
    return &addressspace;
}

struct addressspace *vmm_addressspace_for(uintptr_t addr) {
    if ((ARCH_KERNEL_VM_START <= addr) && (addr <= ARCH_KERNEL_VM_END)) {
        // Kernel VM
        return vmm_get_kernel_addressspace();
    }
    if (addr < ARCH_KERNEL_SPACE_BASE) {
        // Userspace address
        co_printf(
            "vmm_addressspace_for: userspace addresses are not supported yet\n");
        return NULL;
    }
    // Kernel image, scratch page, etc...
    return NULL;
}

void vmm_pagefault(
    uintptr_t addr, bool was_present, bool was_write, bool was_user,
    void *trapframe) {
    if (CONFIG_PRINT_PAGE_FAULTS) {
        co_printf(
            "[PF] addr=%#lx, was_present=%d, was_write=%d, was_user=%d\n",
            addr, was_present, was_write, was_user);
    }
    uintptr_t page_base = aligndown(addr, ARCH_PAGESIZE);
    physptr physaddr = 0;
    int ret = arch_mmu_emulate(
        &physaddr, addr,
        MAP_PROT_READ | (was_write ? MAP_PROT_WRITE : 0U),
        was_user);
    if (ret == 0) {
        // It's a valid access, but TLB was out of sync.
        arch_mmu_flushtlb_for((void *)addr);
        return;
    }
    if (was_present) {
        co_printf(
            "privilege violation: attempted to %s on page at %#lx\n",
            was_write ? "read" : "write", addr);
        goto realfault;
    }
    if (page_base == 0) {
        co_printf(
            "NULL dereference: attempted to %s on page at %#lx\n",
            was_write ? "read" : "write", addr);
        goto realfault;
    }
    // See if it's uncommited object.
    struct addressspace *addressspace = vmm_addressspace_for(page_base);
    if (addressspace == NULL) {
        // We don't know what this address is.
        goto nonpresent;
    }
    struct uncommitedobject *uobject =
        find_object_in_uncommited(addressspace, addr);
    if (uobject == NULL) {
        // Not part of an uncommited object.
        goto nonpresent;
    }
    long pageindex =
        (long)((page_base - uobject->object->startaddress) / ARCH_PAGESIZE);
    if (!bitmap_isbitset(&uobject->bitmap, pageindex)) {
        // Already commited page...?
        co_printf("non-present page %#lx(base: %#lx) but it's already commited. WTF?\n", addr, page_base);
        panic("non-present page on already commited page");
    }
    // Uncommited page
    bitmap_clearbit(&uobject->bitmap, pageindex);
    size_t pagecount = 1;
    if (uobject->object->physicalbase != VMM_PHYSADDR_NOMAP) {
        physaddr = uobject->object->physicalbase + (pageindex * ARCH_PAGESIZE);
    } else {
        physaddr = pmm_alloc(&pagecount);
        if (physaddr == PHYSICALPTR_NULL) {
            // TODO: Run the OOM killer
            panic("ran out of memory while trying to commit the page");
        }
    }
    ret = arch_mmu_map(page_base, physaddr, 1, uobject->object->mapflags, uobject->object->addressspace->is_user);
    if (ret < 0) {
        co_printf("arch_mmu_map failed (error %d)\n", ret);
        panic("failed to map allocated memory");
    }
    if (bitmap_findfirstsetbit(&uobject->bitmap, 0) < 0) {
        list_removenode(&uobject->object->addressspace->uncommitedobjects, &uobject->node);
        heap_free(uobject);
    }
    return;
nonpresent:
    co_printf("attempted to %s on non-present page at %#lx\n", was_write ? "read" : "write", addr);
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
            allocobjects[i] = vmm_alloc(
                vmm_get_kernel_addressspace(), allocsizes[i],
                MAP_PROT_READ | MAP_PROT_WRITE);
            if (allocobjects[i] != NULL) {
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
                co_printf(
                    "value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n",
                    &ptr[j], i, ptr, j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        vmm_free(allocobjects[i]);
    }
    return true;
testfail:
    co_printf("vmm: random test failed\n");
    return false;
}
