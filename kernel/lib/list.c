#include <assert.h>
#include <kernel/lib/list.h>
#include <kernel/lib/strutil.h>

void list_init(struct list *self) {
    vmemset(self, 0, sizeof(*self));
}

void list_insert_front(struct list *list, struct list_node *node, void *data) {
    if (list->front) {
        list->front->prev = node;
    }
    node->prev = nullptr;
    node->next = list->front;
    node->data = data;
    list->front = node;
    if (!list->back) {
        list->back = node;
    }
}

void list_insert_back(struct list *list, struct list_node *node, void *data) {
    if (list->back) {
        list->back->next = node;
    }
    node->next = nullptr;
    node->prev = list->back;
    node->data = data;
    list->back = node;
    if (!list->front) {
        list->front = node;
    }
}

void list_insert_after(struct list *list, struct list_node *after, struct list_node *node, void *data) {
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

void list_insert_before(struct list *list, struct list_node *before, struct list_node *node, void *data) {
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

struct list_node *list_remove_front(struct list *list) {
    struct list_node *removed = list->front;
    if (!removed) {
        return nullptr;
    }
    list->front = removed->next;
    if (list->front) {
        list->front->prev = nullptr;
    } else {
        list->back = nullptr;
    }
    return removed;
}

struct list_node *list_remove_back(struct list *list) {
    struct list_node *removed = list->back;
    if (!removed) {
        return nullptr;
    }
    list->back = removed->prev;
    if (list->back) {
        list->back->next = nullptr;
    } else {
        list->front = nullptr;
    }
    return removed;
}

void list_remove_node(struct list *list, struct list_node *node) {
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
    if ((list->front == nullptr) || (list->back == nullptr)) {
        list->front = nullptr;
        list->back = nullptr;
    }
}

void *list_get_data_or_null(struct list_node *node) {
    if (node == nullptr) {
        return nullptr;
    }
    assert(node->data);
    return node->data;
}
