#include "kernel/io/co.h"
#include <kernel/kobject.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>
#include <kernel/mem/heap.h>
#include <stddef.h>

struct kobject {
    struct kobject *parent;
    struct kobject_ops const *ops;
    struct list child_list;
    char *name;
    size_t ref_count;
    struct list_node list_node;
    max_align_t data[];
};

struct kobject *kobject_create(char const *name, size_t data_size, struct kobject_ops const *ops) {
    struct kobject *obj = heap_alloc(sizeof(*obj) + data_size, HEAP_FLAG_ZEROMEMORY);
    if (obj == NULL) {
        goto fail;
    }
    obj->name = strdup(name);
    if (obj->name == NULL) {
        goto fail;
    }
    list_init(&obj->child_list);
    obj->ops = ops;
    obj->ref_count = 1;
    goto out;
fail:
    if (obj != NULL) {
        heap_free(obj->name);
    }
    heap_free(obj);
out:
    return obj;
}
void kobject_set_parent(struct kobject *obj, struct kobject *parent) {
    if ((obj == NULL) || (obj->parent == parent)) {
        return;
    }
    if (obj->parent != NULL) {
        /* Remove from old parent */
        LIST_FOREACH(&obj->parent->child_list, child_node) {
            if (child_node == &obj->list_node) {
                list_remove_node(&obj->parent->child_list, &obj->list_node);
            }
        };
    }
    obj->parent = parent;
    if (parent == NULL) {
        return;
    }
    list_insert_back(&parent->child_list, &obj->list_node, obj);
}
void kobject_ref(struct kobject *obj) {
    if (obj == NULL) {
        return;
    }
    obj->ref_count++;
}
void kobject_unref(struct kobject *obj) {
    if (obj == NULL) {
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
    kobject_set_parent(obj, NULL);
    if (obj->ops->deinit != NULL) {
        obj->ops->deinit(obj);
    }

    heap_free(obj);
}
void *kobject_get_data(struct kobject *obj) {
    return obj->data;
}
char const *kobject_get_name(struct kobject *obj) {
    return obj->name;
}

static void print_tree(struct kobject *obj, int indent) {
    if (obj == NULL) {
        return;
    }
    for (int i = 0; i < indent; i++) {
        co_put_char(' ');
    }
    co_printf("%s:", obj->name);
    if (obj->child_list.front == NULL) {
        co_printf(" No children objects", obj->name);
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
