#include <assert.h>
#include <kernel/lib/list.h>
#include <stddef.h>
#include <string.h>

void list_init(struct list *self) {
    memset(self, 0, sizeof(*self));
}

void list_insertfront(struct list *list, struct list_node *node, void *data) {
    if (list->front) {
        list->front->prev = node;
    }
    node->prev = NULL;
    node->next = list->front;
    node->data = data;
    list->front = node;
    if (!list->back) {
        list->back = node;
    }
}

void list_insertback(struct list *list, struct list_node *node, void *data) {
    if (list->back) {
        list->back->next = node;
    }
    node->next = NULL;
    node->prev = list->back;
    node->data = data;
    list->back = node;
    if (!list->front) {
        list->front = node;
    }
}

void list_insertafter(
    struct list *list, struct list_node *after, struct list_node *node,
    void *data)
{
    node->prev = after;
    node->next = after->next;
    node->data = data;
    if (node->next) {
        node->next->prev = node;
    }
    after->next = node;
    if (list->back == after) {
        list->back = node;
    }
}

void list_insertbefore(
    struct list *list, struct list_node *before, struct list_node *node,
    void *data)
{
    node->next = before;
    node->prev = before->prev;
    node->data = data;
    if (node->prev) {
        node->prev->next = node;
    }
    before->prev = node;
    if (list->front == before) {
        list->front = node;
    }
}

struct list_node *list_removefront(struct list *list) {
    struct list_node *removed = list->front;
    if (!removed) {
        return NULL;
    }
    list->front = removed->next;
    if (list->front) {
        list->front->prev = NULL;
    } else {
        list->back = NULL;
    }
    return removed;
}

struct list_node *list_removeback(struct list *list) {
    struct list_node *removed = list->back;
    if (!removed) {
        return NULL;
    }
    list->back = removed->prev;
    if (list->back) {
        list->back->next = NULL;
    } else {
        list->front = NULL;
    }
    return removed;
}

void list_removenode(struct list *list, struct list_node *node) {
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    if (node == list->front) {
        list->front = node->next;
    }
    if (node == list->back) {
        list->back = node->prev;
    }
    if ((list->front == NULL) || (list->back == NULL)) {
        list->front = NULL;
        list->back = NULL;
    }
}

void *list_data_or_null(struct list_node *node) {
    if (node == NULL) {
        return NULL;
    }
    assert(node->data);
    return node->data;
}
