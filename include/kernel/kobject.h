#pragma once
#include <stddef.h>



struct kobject;

struct kobject_ops {
    void (*deinit)(struct kobject *obj);
};

extern const struct kobject_ops KOBJECT_OPS_EMPTY;
[[nodiscard]] int kobject_create(struct kobject **obj_out, char const *id, size_t data_size, struct kobject_ops const *ops);
struct kobject *kobject_find_direct_child(struct kobject *obj, char const *id);
[[nodiscard]] int kobject_set_parent(struct kobject *obj, struct kobject *parent);
struct kobject *kobject_get_parent(struct kobject *obj);
void kobject_ref(struct kobject *obj);
void kobject_unref(struct kobject *obj);
void *kobject_get_data(struct kobject *obj);
char const *kobject_get_id(struct kobject *obj);
void kobject_print_tree(struct kobject *obj);
