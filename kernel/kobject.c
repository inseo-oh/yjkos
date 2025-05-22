#include "kernel/io/co.h"
#include <errno.h>
#include <kernel/kobject.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <kernel/panic.h>
#include <stddef.h>
#include <stdint.h>

struct kobject {
    struct kobject *parent;
    struct kobject_ops const *ops;
    struct list child_list;
    char *id;
    size_t ref_count;
    struct list_node list_node;
    max_align_t data[];
};

const struct kobject_ops KOBJECT_OPS_EMPTY;

int kobject_create(struct kobject **obj_out, char const *id, size_t data_size, struct kobject_ops const *ops) {
    struct kobject *obj = heap_alloc(sizeof(*obj) + data_size, HEAP_FLAG_ZEROMEMORY);
    if (obj == nullptr) {
        goto fail;
    }
    if (id != nullptr) {
        /* Use provided ID. In this case, ID cannot start with 0(reserved for numeric IDs) */
        obj->id = strdup(id);
        if (obj->id == nullptr) {
            goto fail;
        }
        if ((obj->id[0] == '\0') || (('0' <= obj->id[0]) && (obj->id[0] <= '9'))) {
            goto fail;
        }
    } else {
        /*
         * Generate numeric ID internally - by that I mean just using the address.
         * Probably bad for security, but who cares?
         */
        uint32_t id = (uintptr_t)obj;
        obj->id = heap_calloc(sizeof(char), 11, HEAP_FLAG_ZEROMEMORY);
        if (obj->id == nullptr) {
            goto fail;
        }
        obj->id[0] = '0' + ((id / 1'000'000'000) % 10);
        obj->id[1] = '0' + ((id / 100'000'000) % 10);
        obj->id[2] = '0' + ((id / 10'000'000) % 10);
        obj->id[3] = '0' + ((id / 1'000'000) % 10);
        obj->id[4] = '0' + ((id / 100'000) % 10);
        obj->id[5] = '0' + ((id / 10'000) % 10);
        obj->id[6] = '0' + ((id / 1'000) % 10);
        obj->id[7] = '0' + ((id / 100) % 10);
        obj->id[8] = '0' + ((id / 10) % 10);
        obj->id[9] = '0' + ((id / 1) % 10);
    }
    list_init(&obj->child_list);
    obj->ops = ops;
    obj->ref_count = 1;
    goto out;
fail:
    if (obj != nullptr) {
        heap_free(obj->id);
    }
    heap_free(obj);
out:
    *obj_out = obj;
    return 0;
}
struct kobject *kobject_find_direct_child(struct kobject *obj, char const *id) {
    if (obj == nullptr) {
        return nullptr;
    }
    LIST_FOREACH(&obj->child_list, child_node) {
        if (child_node == &obj->list_node) {
            struct kobject *child = list_get_data_or_null(child_node);
            if (kstrcmp(child->id, id) == 0) {
                return child;
            }
        }
    };
    return nullptr;
}
int kobject_set_parent(struct kobject *obj, struct kobject *parent) {
    if ((obj == nullptr) || (obj->parent == parent)) {
        return 0;
    }
    /* Remove from old parent *************************************************/
    if (obj->parent != nullptr) {
        LIST_FOREACH(&obj->parent->child_list, child_node) {
            if (child_node == &obj->list_node) {
                list_remove_node(&obj->parent->child_list, &obj->list_node);
            }
        };
    }
    if (parent == nullptr) {
        obj->parent = nullptr;
        return 0;
    }
    /* Make sure the ID is unique within the parent ***************************/
    if (kobject_find_direct_child(parent, obj->id) != nullptr) {
        return -EEXIST;
    }
    /* Check if parent is child of the object *********************************/
    {
        struct kobject *curr_parent = parent->parent;
        while (curr_parent != nullptr) {
            if (curr_parent == obj) {
                return -EEXIST;
            }
            curr_parent = curr_parent->parent;
        }
    }
    /* Set parent *************************************************************/
    obj->parent = parent;
    kobject_ref(parent);
    list_insert_back(&parent->child_list, &obj->list_node, obj);
    return 0;
}
struct kobject *kobject_get_parent(struct kobject *obj) {
    return obj->parent;
}
void kobject_ref(struct kobject *obj) {
    if (obj == nullptr) {
        return;
    }
    obj->ref_count++;
}
void kobject_unref(struct kobject *obj) {
    [[maybe_unused]] int ret_unused;

    if (obj == nullptr) {
        return;
    }
    obj->ref_count--;
    if (obj->ref_count != 0) {
        return;
    }
    /* Destroy object *********************************************************/
    LIST_FOREACH(&obj->child_list, child_node) {
        struct kobject *child = list_get_data_or_null(child_node);
        kobject_unref(child);
    }
    ret_unused = kobject_set_parent(obj, nullptr);
    if (obj->ops->deinit != nullptr) {
        obj->ops->deinit(obj);
    }

    heap_free(obj);
}
void *kobject_get_data(struct kobject *obj) {
    return obj->data;
}
char const *kobject_get_id(struct kobject *obj) {
    return obj->id;
}

static void print_tree(struct kobject *obj, int indent) {
    if (obj == nullptr) {
        return;
    }
    for (int i = 0; i < indent; i++) {
        co_put_char(' ');
    }
    co_printf("%s:", obj->id);
    if (obj->child_list.front == nullptr) {
        co_printf(" No children objects", obj->id);
    }
    co_printf("\n");
    LIST_FOREACH(&obj->child_list, child_node) {
        struct kobject *obj = list_get_data_or_null(child_node);
        print_tree(obj, indent + 4);
    };
}
void kobject_print_tree(struct kobject *obj) {
    print_tree(obj, 0);
}
