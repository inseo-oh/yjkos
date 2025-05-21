#pragma once
#include <stddef.h>

struct kobject;

struct kobject_ops {
    void (*deinit)(struct kobject *obj);
};

struct kobject *kobject_create( char const *name, size_t data_size, struct kobject_ops const *ops);
void kobject_set_parent(struct kobject *obj, struct kobject *parent);
void kobject_ref(struct kobject *obj);
void kobject_unref(struct kobject *obj);
void *kobject_get_data(struct kobject *obj);
char const *kobject_get_name(struct kobject *obj);
void kobject_print_tree(struct kobject *obj);
