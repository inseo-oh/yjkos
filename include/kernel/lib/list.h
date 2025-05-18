#pragma once

struct List_Node {
    struct List_Node *next;
    struct List_Node *prev;
    void *data;
};
struct List {
    struct List_Node *front;
    struct List_Node *back;
};

/* This is not necessary if it's static variable(which is initialized by zero). */
void List_Init(struct List *self);
void List_InsertFront(struct List *list, struct List_Node *node, void *data);
void List_InsertBack(struct List *list, struct List_Node *node, void *data);
void List_InsertAfter(struct List *list, struct List_Node *after, struct List_Node *node, void *data);
void List_InsertBefore(struct List *list, struct List_Node *before, struct List_Node *node, void *data);
struct List_Node *List_RemoveFront(struct List *list);
struct List_Node *List_RemoveBack(struct List *list);
void List_RemoveNode(struct List *list, struct List_Node *node);
void *List_GetDataOrNull(struct List_Node *node);

#define LIST_FOREACH(_list, _varname) for (                             \
    struct List_Node * (_varname) = (_list)->front; (_varname) != NULL; \
    (_varname) = (_varname)->next)
