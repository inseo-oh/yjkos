#pragma once
#include <stddef.h>
#include <stdint.h>

#define KOBJECT_MAKE_TYPE_CODE(_a, _b, _c, _d) ((_a << 24) | (_b << 16) | (_c << 8) | (_d))
#define KOBJECT_TYPE_GENERIC KOBJECT_MAKE_TYPE_CODE('g', 'e', 'n', 'r')

struct kobject;

struct kobject_ops {
    void (*deinit)(struct kobject *obj);
};

extern const struct kobject_ops KOBJECT_OPS_EMPTY;
[[nodiscard]] int kobject_create(struct kobject **obj_out, uint32_t type, char const *id, size_t data_size, struct kobject_ops const *ops);
struct kobject *kobject_find_direct_child(struct kobject *obj, char const *id);
[[nodiscard]] int kobject_set_parent(struct kobject *obj, struct kobject *parent);
void kobject_ref(struct kobject *obj);
void kobject_unref(struct kobject *obj);
void *kobject_get_data(struct kobject *obj);
char const *kobject_get_id(struct kobject const *obj);
struct kobject *kobject_get_parent(struct kobject const *obj);
bool kobject_check_type(struct kobject const *obj, uint32_t type);

void kobject_print_tree(struct kobject const *obj);
