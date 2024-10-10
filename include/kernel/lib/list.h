#pragma once

typedef struct list_node list_node_t;
struct list_node {
    list_node_t *next;
    list_node_t *prev;
    void *data;
};

typedef struct list list_t;
struct list {
    list_node_t *front;
    list_node_t *back;
};

// This is not necessary if it's static variable(which is initialized by zero).
void list_init(list_t *self);
void list_insertfront(list_t *list, list_node_t *node, void *data);
void list_insertback(list_t *list, list_node_t *node, void *data);
void list_insertafter(list_t *list, list_node_t *after, list_node_t *node, void *data);
void list_insertbefore(list_t *list, list_node_t *before, list_node_t *node, void *data);
list_node_t *list_removefront(list_t *list);
list_node_t *list_removeback(list_t *list);
void list_removenode(list_t *list, list_node_t *node);
