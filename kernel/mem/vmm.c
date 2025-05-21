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
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Configuration **************************************************************/

/* Print when page fault occurs? */
static bool const CONFIG_PRINT_PAGE_FAULTS = false;

/******************************************************************************/

struct uncommited_object {
    struct list_node node;
    struct vmm_object *object;
    struct bitmap bitmap;
    UINT bitmap_data[];
};

#ifdef NEW_VMM

static struct vmm_object *take_object(struct vmm_address_space *self, struct vmm_object *object) {
    list_remove_node(&self->object_list, &object->node);
    return object;
}

#else
struct objectgroup {
    struct bst_node node;
    struct list objectlist; /* Holds vm_object */
};

static struct vm_object *take_object(struct address_space *self, struct objectgroup *group, struct vm_object *object) {
    list_remove_node(&group->objectlist, &object->node);
    if (group->objectlist.front == NULL) {
        /* list is empty -> Remove the node */
        bst_remove_node(&self->object_group_tree, &group->node);
        heap_free(group);
    }
    return object;
}

#endif

/*
 * Finds the first VM object with given minium size.
 */
#ifdef NEW_VMM
static struct vmm_object *take_object_with_min_size(struct vmm_address_space *self, size_t page_count) {
    assert(page_count != 0);
    assert(self->object_list.front != NULL);
    struct vmm_object *result = NULL;
    LIST_FOREACH(&self->object_list, object_node) {
        assert(object_node->data);
        struct vmm_object *object = object_node->data;
        size_t object_page_count = size_to_blocks(vmm_get_object_size(object), ARCH_PAGESIZE);
        if (object_page_count < page_count) {
            /* Not enough pages */
            continue;
        }
        result = take_object(self, object_node->data);
        break;
    }

    return result;
}
/*
 * Finds the first VM object that includes given address.
 * (Size between two are adjusted to nearest page size)
 */
static struct vmm_object *take_object_including(struct vmm_address_space *self, void *start, void *end) {
    size_t page_count = size_to_blocks((uintptr_t)end - (uintptr_t)start + 1, ARCH_PAGESIZE);
    assert(page_count != 0);
    struct vmm_object *result = NULL;
    if ((UINTPTR_MAX - (uintptr_t)start) < (page_count * ARCH_PAGESIZE)) {
        /* Too large */
        goto out;
    }
    end = (char *)start + (page_count * ARCH_PAGESIZE - 1);
    LIST_FOREACH(&self->object_list, object_node) {
        struct vmm_object *object = object_node->data;
        assert(object);
        if (((uintptr_t)start < (uintptr_t)object->start) || ((uintptr_t)object->end < (uintptr_t)start) || ((uintptr_t)end < (uintptr_t)object->start) || ((uintptr_t)object->end < (uintptr_t)end)) {
            /* Address is out of range */
            continue;
        }
        result = object;
        break;
    }
out:
    return result;
}
[[nodiscard]] static bool add_object_to_address_space(struct vmm_address_space *self, struct vmm_object *object) {
    assert(is_aligned(vmm_get_object_size(object), ARCH_PAGESIZE));

    /* Find where we want to insert to ****************************************/
    struct vmm_object *insert_after = NULL;
    LIST_FOREACH(&self->object_list, object_node) {
        struct vmm_object *old_object = list_get_data_or_null(object_node->data);
        if ((uintptr_t)old_object->end < (uintptr_t)object->start) {
            insert_after = old_object;
        }
    }
    /* Insert the object *****************************************************/
    if (insert_after == NULL) {
        list_insert_back(&self->object_list, &object->node, object);
    } else {
        struct vmm_object *next_object = list_get_data_or_null(insert_after->node.next);
        if (next_object != NULL) {
            if (next_object->start < object->end) {
                co_printf(
                    "Bad VM object insertion! Attempted to insert [%p, %p] between [%p, %p] and [%p, %p]",
                    object->start, object->end,
                    insert_after->start, insert_after->end,
                    next_object->start, next_object->end);
                return true;
            }
        }
        list_insert_after(&self->object_list, &insert_after->node, &object->node, object);
    }
    return true;
}
#else
/*
 * Finds the first VM object with given minium size.
 */
static struct vm_object *take_object_with_min_size(struct address_space *self, size_t page_count) {
    assert(page_count != 0);
    struct vm_object *result = NULL;
    if (self->object_group_tree.root == NULL) {
        /* Tree is empty */
        goto out;
    }
    struct bst_node *nextnode = NULL;
    for (struct bst_node *currentnode = bst_min_of_tree(&self->object_group_tree); currentnode != NULL; currentnode = nextnode) {
        nextnode = bst_successor(currentnode);
        struct objectgroup *currentgroup = currentnode->data;
        assert(currentgroup);

        size_t currentpagecount = currentgroup->node.key;
        if (currentpagecount < page_count) {
            /* Not enough pages */
            continue;
        }
        struct list_node *object_node = currentgroup->objectlist.front;
        assert(object_node); /* BST nodes with empty list should NOT exist */
        assert(object_node->data);
        result = take_object(self, currentgroup, object_node->data);
        break;
    }
out:
    return result;
}
/*
 * Finds the first VM object that includes given address.
 * (Size between two are adjusted to nearest page size)
 */
static struct vm_object *take_object_including(struct address_space *self, void *start, void *end) {
    size_t page_count = size_to_blocks((uintptr_t)end - (uintptr_t)start + 1, ARCH_PAGESIZE);
    assert(page_count != 0);
    struct vm_object *result = NULL;
    if ((UINTPTR_MAX - (uintptr_t)start) < (page_count * ARCH_PAGESIZE)) {
        /* Too large */
        goto out;
    }
    end = (char *)start + (page_count * ARCH_PAGESIZE - 1);
    if (self->object_group_tree.root == NULL) {
        /* Tree is empty */
        goto out;
    }
    struct bst_node *nextnode = NULL;
    for (struct bst_node *currentnode = bst_min_of_tree(&self->object_group_tree); currentnode != NULL; currentnode = nextnode) {
        nextnode = bst_successor(currentnode);
        struct objectgroup *currentgroup = currentnode->data;
        assert(currentgroup);

        size_t currentpagecount = currentgroup->node.key;
        if (currentpagecount < page_count) {
            /* Not enough pages */
            continue;
        }

        struct vm_object *resultobject = NULL;
        LIST_FOREACH(&currentgroup->objectlist, object_node) {
            struct vm_object *object = object_node->data;
            assert(object);
            if (((uintptr_t)start < (uintptr_t)object->start) || ((uintptr_t)object->end < (uintptr_t)start) || ((uintptr_t)end < (uintptr_t)object->start) || ((uintptr_t)object->end < (uintptr_t)end)) {
                /* Address is out of range */
                continue;
            }
            resultobject = object;
            break;
        }
        if (resultobject == NULL) {
            continue;
        }
        result = take_object(self, currentgroup, resultobject);
        break;
    }
out:
    return result;
}
[[nodiscard]] static bool add_object_to_address_space(struct address_space *self, struct vm_object *object) {
    while (1) {
        assert(is_aligned(vmm_get_object_size(object), ARCH_PAGESIZE));
        size_t page_count = vmm_get_object_size(object) / ARCH_PAGESIZE;
        struct bst_node *groupnode = bst_find_node(&self->object_group_tree, page_count);
        if (groupnode == NULL) {
            /* Create a new object group */
            struct objectgroup *group = heap_alloc(sizeof(*group), HEAP_FLAG_ZEROMEMORY);
            if (group == NULL) {
                goto oom;
            }
            list_init(&group->objectlist);
            groupnode = &group->node;
            bst_insert_node(&self->object_group_tree, &group->node, page_count, group);
        }
        struct objectgroup *group = groupnode->data;

        /*
         * Insert the object into object group. Note that we keep the list sorted by address.
         */
        struct list_node *insertafter = NULL;
        LIST_FOREACH(&group->objectlist, currentnode) {
            struct vm_object *currentobject = currentnode->data;
            if (currentobject->end < object->start) {
                insertafter = currentnode;
            } else {
                break;
            }
        }
        if (insertafter == NULL) {
            list_insert_front(&group->objectlist, &object->node, object);
        } else {
            list_insert_after(&group->objectlist, insertafter, &object->node, object);
        }
        /*
         * Because the list is sorted, we can easily figure out previous and/or next region is actually contiguous.
         * If so, remove that region, and make the current object larger to include it.
         */
        bool sizechanged = false;
        if (object->node.prev != NULL) {
            struct vm_object *prevobject = object->node.prev->data;
            if (((uintptr_t)prevobject->end + 1) == (uintptr_t)object->start) {
                object->start = prevobject->start;
                list_remove_node(&group->objectlist, &prevobject->node);
                heap_free(prevobject);
                sizechanged = true;
            }
        }
        if (object->node.next != NULL) {
            struct vm_object *nextobject = object->node.next->data;
            assert(nextobject->start != 0);
            if (((uintptr_t)nextobject->start - 1) == (uintptr_t)object->end) {
                object->end = nextobject->end;
                list_remove_node(&group->objectlist, &nextobject->node);
                heap_free(nextobject);
                sizechanged = true;
            }
        }
        if (!sizechanged) {
            break;
        }
        /*
         * If the size was changed(i.e. Object was cominbed with nearby ones), it no longer belongs to current group.
         * Remove the object from group and go back to beginning.
         */
        list_remove_node(&group->objectlist, &object->node);
        if (group->objectlist.front == NULL) {
            /* list is empty -> Remove the group. */
            bst_remove_node(&self->object_group_tree, &group->node);
            heap_free(group);
        }
    }
    return true;
oom:
    return false;
}
#endif

/*
 * Returns NULL on allocation failure.
 */
static struct vmm_object *create_object(struct vmm_address_space *self, void *start, void *end, PHYSPTR phys_base, uint8_t mapflags) {
    struct vmm_object *object = heap_alloc(sizeof(*object), HEAP_FLAG_ZEROMEMORY);
    if (object == NULL) {
        return NULL;
    }
    object->start = start;
    object->end = end;
    object->address_space = self;
    object->mapflags = mapflags;
    object->phys_base = phys_base;
    return object;
}

/*
 * `object` is only used to store pointer to the uncommitedobject, so it doesn't have to be initialized.
 *
 * Returns NULL on allocation failure.
 */
static struct uncommited_object *create_uncommited_object(struct vmm_object *object, size_t page_count) {
    size_t wordcount = bitmap_needed_word_count(page_count);
    size_t size = sizeof(struct uncommited_object) + (wordcount * sizeof(UINT));
    struct uncommited_object *uobject = heap_alloc(size, HEAP_FLAG_ZEROMEMORY);
    if (uobject == NULL) {
        return NULL;
    }
    uobject->object = object;
    uobject->bitmap.words = uobject->bitmap_data;
    uobject->bitmap.word_count = wordcount;
    bitmap_set_bits(&uobject->bitmap, 0, page_count);
    return uobject;
}

/*
 * Returns NULL if not found.
 */
static struct uncommited_object *find_object_in_uncommited(struct vmm_address_space *self, void *ptr) {
    void *page_base = align_ptr_down(ptr, ARCH_PAGESIZE);
    LIST_FOREACH(&self->uncommited_objects, object_node) {
        struct uncommited_object *uobject = object_node->data;
        assert(uobject != NULL);
        if (((uintptr_t)uobject->object->start <= (uintptr_t)page_base) &&
            ((uintptr_t)page_base <= (uintptr_t)uobject->object->end)) {
            return uobject;
        }
    }
    return NULL;
}

[[nodiscard]] size_t vmm_get_object_size(struct vmm_object *object) {
    return (uintptr_t)object->end - (uintptr_t)object->start + 1;
}

[[nodiscard]] bool vmm_init_address_space(struct vmm_address_space *out, void *start, void *end, bool is_user) {
    memset(out, 0, sizeof(*out));
    out->is_user = is_user;
    struct vmm_object *object = create_object(out, start, end, VMM_PHYSADDR_NOMAP, 0);
    if (object == NULL) {
        goto fail_oom;
    }
    if (!add_object_to_address_space(out, object)) {
        goto fail_oom;
    }
    return true;
fail_oom:
    heap_free(object);
    return false;
}

#ifdef NEW_VMM
void vmm_deinit_address_space(struct vmm_address_space *self) {
    while (1) {
        struct list_node *object_node = list_remove_front(&self->object_list);
        if (object_node == NULL) {
            break;
        }
        heap_free(object_node->data);
    }
}
#else
void vmm_deinit_address_space(struct address_space *self) {
    for (struct bst_node *currentnode = bst_min_of_tree(&self->object_group_tree); currentnode != NULL; currentnode = bst_successor(currentnode)) {
        struct objectgroup *group = currentnode->data;
        while (1) {
            struct list_node *object_node = list_remove_front(&group->objectlist);
            if (object_node == NULL) {
                break;
            }
            heap_free(object_node->data);
        }
    }
    heap_free(self);
}
#endif

[[nodiscard]] struct vmm_object *vmm_alloc_object(struct vmm_address_space *self, PHYSPTR phys_base, size_t size, uint8_t mapflags) {
    struct vmm_object *oldobject = NULL;

    size_t page_count = size_to_blocks(size, ARCH_PAGESIZE);
    if (phys_base != VMM_PHYSADDR_NOMAP) {
        assert(is_aligned(phys_base, ARCH_PAGESIZE));
    }

    /*
     * We create objects first with dummy values, so that we just have to free those objects on failure.
     * (It's a good idea to avoid undoing take_object~, because that undo operation can technically also fail)
     */
    struct vmm_object *newobject = create_object(self, 0, 0, phys_base, mapflags);
    struct uncommited_object *uobject = create_uncommited_object(newobject, page_count);
    oldobject = take_object_with_min_size(self, page_count);
    if ((newobject == NULL) || (oldobject == NULL)) {
        goto fail_oom;
    }
    size_t newsize = page_count * ARCH_PAGESIZE;
    newobject->start = oldobject->start;
    newobject->end = (char *)newobject->start + newsize - 1;
    /* Shrink the existing object */
    oldobject->start = (char *)oldobject->start + newsize;
    if (oldobject->end < oldobject->start) {
        /* The object is no longer valid. Remove it. */
        heap_free(oldobject);
    } else {
        /* Add modified object back to the tree. */
        int ret = add_object_to_address_space(self, oldobject);
        if (ret < 0) {
            co_printf("vmm: could not add modified vm object back to vmm(error %d)\n", ret);
        }
    }
    oldobject = NULL;
    list_insert_back(&self->uncommited_objects, &uobject->node, uobject);
    goto out;
fail_oom:
    heap_free(uobject);
    heap_free(oldobject);
    heap_free(newobject);
out:
    return newobject;
}

[[nodiscard]] struct vmm_object *vmm_alloc_object_at(struct vmm_address_space *self, void *virt_base, PHYSPTR phys_base, size_t size, uint8_t mapflags) {
    bool prev_interrupts = arch_irq_disable();
    assert(virt_base);
    size_t page_count = size_to_blocks(size, ARCH_PAGESIZE);
    if (phys_base != VMM_PHYSADDR_NOMAP) {
        assert(is_aligned(phys_base, ARCH_PAGESIZE));
    }
    assert(!is_aligned((uintptr_t)virt_base, ARCH_PAGESIZE));

    if ((SIZE_MAX / ARCH_PAGESIZE) < page_count) {
        goto fail_badsize;
    }
    size_t actualsize = ARCH_PAGESIZE * page_count;
    if ((UINTPTR_MAX - (uintptr_t)virt_base) < actualsize) {
        goto fail_badsize;
    }
    void *end = (char *)virt_base + actualsize - 1;

    /*
     * We create objects first with dummy values, so that we just have to free those objects on failure.
     * (It's a good idea to avoid undoing takeobject~, because that undo operation can technically also fail)
     */
    struct vmm_object *lobject = NULL;
    struct vmm_object *newobject = create_object(self, 0, 0, phys_base, mapflags);
    struct vmm_object *rightobject = create_object(self, 0, 0, VMM_PHYSADDR_NOMAP, 0);
    struct uncommited_object *uobject = create_uncommited_object(newobject, page_count);
    if ((newobject == NULL) || (rightobject == NULL) || (uobject == NULL)) {
        goto fail_oom;
    }

    lobject = take_object_including(self, virt_base, end);
    if (lobject == NULL) {
        goto fail_oom;
    }
    void *old_end = lobject->end;

    size_t newsize = page_count * ARCH_PAGESIZE;
    newobject->start = virt_base;
    newobject->end = (char *)newobject->start + newsize - 1;

    /* Split into two objects */
    lobject->end = (char *)virt_base - 1;
    rightobject->start = (char *)virt_base + (page_count * ARCH_PAGESIZE);
    rightobject->end = old_end;

    if (lobject->end < lobject->start) {
        heap_free(lobject);
    } else {
        /* Add modified object back to the tree. */
        int ret = add_object_to_address_space(self, lobject);
        if (ret < 0) {
            co_printf("vmm: could not add modified vm object back to vmm(error %d)\n", ret);
        }
    }
    lobject = NULL;
    if (rightobject->end < rightobject->start) {
        heap_free(rightobject);
    } else {
        /* Add modified object back to the tree. */
        int ret = add_object_to_address_space(self, rightobject);
        if (ret < 0) {
            co_printf("vmm: could not add modified vm object back to vmm(error %d)\n", ret);
        }
    }
    rightobject = NULL;
    list_insert_back(&self->uncommited_objects, &uobject->node, uobject);
    goto out;
fail_oom:
    heap_free(uobject);
    heap_free(newobject);
    heap_free(lobject);
    heap_free(rightobject);
fail_badsize:
    newobject = NULL;
out:
    arch_irq_restore(prev_interrupts);
    return newobject;
}

void vmm_free(struct vmm_object *object) {
    bool prev_interrupts = arch_irq_disable();
    /* Remove from uncommited memory list *************************************/
    struct uncommited_object *uobject = find_object_in_uncommited(object->address_space, object->start);
    if (uobject != NULL) {
        list_remove_node(&object->address_space->uncommited_objects, &uobject->node);
        heap_free(uobject);
    }
    /* Free commited physical pages and unmap it. *****************************/
    for (char *ptr = object->start; (uintptr_t)ptr <= (uintptr_t)object->end; ptr += ARCH_PAGESIZE) {
        PHYSPTR physaddr;
        if (arch_mmu_virtual_to_physical(&physaddr, ptr) == 0) {
            if (object->phys_base == VMM_PHYSADDR_NOMAP) {
                pmm_free(physaddr, 1);
            }
            int ret = arch_mmu_unmap(ptr, 1);
            if (ret < 0) {
                co_printf("vmm: commited page %p doesn't exist?\n", ptr);
                panic("failed to unmap commited pages");
            }
        }
    }
    /* Return object back to the tree */
    int ret = add_object_to_address_space(object->address_space, object);
    if (ret < 0) {
        co_printf("vmm: could not register returned virtual memory(error %d). this may decrease usable virtual memory.\n", ret);
    }
    arch_irq_restore(prev_interrupts);
}

[[nodiscard]] struct vmm_object *vmm_alloc(struct vmm_address_space *self, size_t size, uint8_t mapflags) {
    return vmm_alloc_object(self, VMM_PHYSADDR_NOMAP, size, mapflags);
}
[[nodiscard]] struct vmm_object *vmm_alloc_at(struct vmm_address_space *self, void *virt_base, size_t size, uint8_t mapflags) {
    return vmm_alloc_object_at(self, virt_base, VMM_PHYSADDR_NOMAP, size, mapflags);
}
[[nodiscard]] struct vmm_object *vmm_map_mem(struct vmm_address_space *self, PHYSPTR phys_base, size_t size, uint8_t mapflags) {
    assert(phys_base != VMM_PHYSADDR_NOMAP);
    return vmm_alloc_object(self, phys_base, size, mapflags);
}
[[nodiscard]] struct vmm_object *vmm_map_memory_at(struct vmm_address_space *self, void *virt_base, PHYSPTR phys_base, size_t size, uint8_t mapflags) {
    assert(phys_base != VMM_PHYSADDR_NOMAP);
    return vmm_alloc_object_at(self, virt_base, phys_base, size, mapflags);
}

void *vmm_ezmap(PHYSPTR base, size_t size) {
    size_t offset = base % ARCH_PAGESIZE;
    PHYSPTR pagebase = base - offset;
    size_t actualsize = size + offset;
    struct vmm_object *object = vmm_map_mem(vmm_get_kernel_address_space(), pagebase, actualsize, MAP_PROT_READ | MAP_PROT_WRITE);
    if (object == NULL) {
        panic("vmm: failed to ezmap virtual memory");
    }
    return (char *)object->start + offset;
}

struct vmm_address_space *vmm_get_kernel_address_space(void) {
    static struct vmm_address_space address_space;
    static bool initialized = false;

    if (!initialized) {
        if (!vmm_init_address_space(&address_space, ARCH_KERNEL_VM_START, ARCH_KERNEL_VM_END, false)) {
            panic("not enough memory to initialize kernel addessspace");
        }
        initialized = true;
    }
    return &address_space;
}

struct vmm_address_space *vmm_get_address_space_of(void *ptr) {
    if (((uintptr_t)ARCH_KERNEL_VM_START <= (uintptr_t)ptr) && ((uintptr_t)ptr <= (uintptr_t)ARCH_KERNEL_VM_END)) {
        /* Kernel VM */
        return vmm_get_kernel_address_space();
    }
    if ((uintptr_t)ptr < (uintptr_t)ARCH_KERNEL_SPACE_BASE) {
        /* Userspace address */
        co_printf("vmm_get_address_space_of: userspace addresses are not supported yet\n");
        return NULL;
    }
    /* Kernel image, scratch page, etc... */
    return NULL;
}

void vmm_page_fault(void *ptr, bool was_present, bool was_write, bool was_user, void *trapframe) {
    if (CONFIG_PRINT_PAGE_FAULTS) {
        co_printf("[PF] addr=%p, was_present=%d, was_write=%d, was_user=%d\n", ptr, was_present, was_write, was_user);
    }
    void *page_base = align_ptr_down(ptr, ARCH_PAGESIZE);
    PHYSPTR physaddr = 0;

    /* Is it valid address? ***************************************************/
    int ret = arch_mmu_emulate(&physaddr, ptr, MAP_PROT_READ | (was_write ? MAP_PROT_WRITE : 0U), was_user);
    if (ret == 0) {
        /* It's a valid access, but TLB was out of sync. */
        arch_mmu_flush_tlb_for(ptr);
        return;
    }
    if (was_present) {
        co_printf("privilege violation: attempted to %s on page at %p\n", was_write ? "read" : "write", ptr);
        goto realfault;
    }
    if (page_base == 0) {
        co_printf("NULL dereference: attempted to %s on page at %p\n", was_write ? "read" : "write", ptr);
        goto realfault;
    }

    /* See if it's uncommited object *****************************************/
    struct vmm_address_space *address_space = vmm_get_address_space_of(page_base);
    if (address_space == NULL) {
        /* We don't know what this address is. */
        goto nonpresent;
    }

    struct uncommited_object *uobject = find_object_in_uncommited(address_space, ptr);
    if (uobject == NULL) {
        /* Not part of an uncommited object. */
        goto nonpresent;
    }

    long page_index = (long)(((uintptr_t)page_base - (uintptr_t)uobject->object->start) / ARCH_PAGESIZE);
    if (!bitmap_is_bit_set(&uobject->bitmap, page_index)) {
        /* Already commited page...? */
        co_printf("non-present page %p(base: %p) but it's already commited. WTF?\n", ptr, page_base);
        panic("non-present page on already commited page");
    }

    /* It is uncommited object *************************************************/
    bitmap_clear_bit(&uobject->bitmap, page_index);
    size_t page_count = 1;
    if (uobject->object->phys_base != VMM_PHYSADDR_NOMAP) {
        physaddr = uobject->object->phys_base + (page_index * ARCH_PAGESIZE);
    } else {
        physaddr = pmm_alloc(&page_count);
        if (physaddr == PHYSICALPTR_NULL) {
            /* TODO: Run the OOM killer */
            panic("ran out of memory while trying to commit the page");
        }
    }
    ret = arch_mmu_map(page_base, physaddr, 1, uobject->object->mapflags, uobject->object->address_space->is_user);
    if (ret < 0) {
        co_printf("arch_mmu_map failed (error %d)\n", ret);
        panic("failed to map allocated memory");
    }
    if (bitmap_find_first_set_bit(&uobject->bitmap, 0) < 0) {
        list_remove_node(&uobject->object->address_space->uncommited_objects, &uobject->node);
        heap_free(uobject);
    }
    return;
nonpresent:
    co_printf("attempted to %s on non-present page at %p\n", was_write ? "read" : "write", ptr);
realfault:
    arch_stacktrace_for_trapframe(trapframe);
    panic("fatal memory access fault");
}

#define RAND_TEST_ALLOC_COUNT 1000
#define MAX_ALLOC_SIZE ((1024 * 1024) / RAND_TEST_ALLOC_COUNT)

bool vmm_random_test(void) {
    size_t allocsizes[RAND_TEST_ALLOC_COUNT];
    struct vmm_object *alloc_objects[RAND_TEST_ALLOC_COUNT];

    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        while (1) {
            allocsizes[i] = (size_t)rand() % MAX_ALLOC_SIZE;
            allocsizes[i] = MAX_ALLOC_SIZE;
            alloc_objects[i] = vmm_alloc(vmm_get_kernel_address_space(), allocsizes[i], MAP_PROT_READ | MAP_PROT_WRITE);
            if (alloc_objects[i] != NULL) {
                break;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptrcount = allocsizes[i] / sizeof(void *);
        void **ptr = alloc_objects[i]->start;
        for (size_t j = 0; j < ptrcount; j++) {
            ptr[j] = &ptr[j];
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        size_t ptrcount = allocsizes[i] / sizeof(void *);
        void **ptr = alloc_objects[i]->start;
        for (size_t j = 0; j < ptrcount; j++) {
            void *expectedvalue = &ptr[j];
            void *gotvalue = ptr[j];
            if (ptr[j] != gotvalue) {
                co_printf("value mismatch at %p(allocation %zu, base %p, offset %zu): expected %p, got %p\n", &ptr[j], i, ptr, j, expectedvalue, gotvalue);
                goto testfail;
            }
        }
    }
    for (size_t i = 0; i < RAND_TEST_ALLOC_COUNT; i++) {
        vmm_free(alloc_objects[i]);
    }
    co_printf("vmm: test OK\n");
    return true;
testfail:
    co_printf("vmm: random test failed\n");
    return false;
}
