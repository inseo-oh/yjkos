#pragma once

struct list_node {
    struct list_node *next;
    struct list_node *prev;
    void *data;
};
struct list {
    struct list_node *front;
    struct list_node *back;
};

/* This is not necessary if it's static variable(which is initialized by zero). */
void list_init(struct list *self);
void list_insert_front(struct list *list, struct list_node *node, void *data);
void list_insert_back(struct list *list, struct list_node *node, void *data);
void list_insert_after(struct list *list, struct list_node *after, struct list_node *node, void *data);
void list_insert_before(struct list *list, struct list_node *before, struct list_node *node, void *data);
struct list_node *list_remove_front(struct list *list);
struct list_node *list_remove_back(struct list *list);
void list_remove_node(struct list *list, struct list_node *node);
void *list_get_data_or_null(struct list_node *node);

#define LIST_FOREACH(_list, _varname) for (                             \
    struct list_node * (_varname) = (_list)->front; (_varname) != NULL; \
    (_varname) = (_varname)->next)
