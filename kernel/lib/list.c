#include <assert.h>
#include <kernel/lib/list.h>
#include <stddef.h>
#include <string.h>

void List_Init(struct List *self) {
    memset(self, 0, sizeof(*self));
}

void List_InsertFront(struct List *list, struct List_Node *node, void *data) {
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

void List_InsertBack(struct List *list, struct List_Node *node, void *data) {
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

void List_InsertAfter(struct List *list, struct List_Node *after, struct List_Node *node, void *data) {
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

void List_InsertBefore(struct List *list, struct List_Node *before, struct List_Node *node, void *data) {
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

struct List_Node *List_RemoveFront(struct List *list) {
    struct List_Node *removed = list->front;
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

struct List_Node *List_RemoveBack(struct List *list) {
    struct List_Node *removed = list->back;
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

void List_RemoveNode(struct List *list, struct List_Node *node) {
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

void *List_GetDataOrNull(struct List_Node *node) {
    if (node == NULL) {
        return NULL;
    }
    assert(node->data);
    return node->data;
}
