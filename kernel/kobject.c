#include <kernel/lib/list.h>
#include <kernel/mem/heap.h>
#include <stddef.h>
#include <string.h>

struct kobject;

struct kobject_ops {
    void (*deinit)(struct kobject *obj);
};

struct kobject {
    struct kobject *parent;
    struct kobject_ops const *ops;
    struct list child_list;
    char *name;
    size_t ref_count;
    max_align_t data[];
};

struct kobject *kobject_create_with_parent(struct kobject *parent, char const *name, size_t data_size, struct kobject_ops const *ops) {
    struct kobject *obj = heap_alloc(sizeof(*obj) + data_size, HEAP_FLAG_ZEROMEMORY);
    if (obj == NULL) {
        goto fail;
    }
    obj->name = strdup(name);
    if (obj->name == NULL) {
        goto fail;
    }
    list_init(&obj->child_list);
    obj->parent = parent;
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
struct kobject *kobject_create(size_t data_size, char const *name, struct kobject_ops const *ops) {
    return kobject_create_with_parent(NULL, name, data_size, ops);
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
    if (obj->ops->deinit != NULL) {
        obj->ops->deinit(obj);
    }
    heap_free(obj);
}
