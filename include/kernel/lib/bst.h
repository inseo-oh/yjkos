#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct bst_node bst_node_t;
struct bst_node {
    bst_node_t *parent;
    bst_node_t *children[2];
    void *data;
    intmax_t key;
    int32_t bf;
    int32_t height;
};

typedef struct bst bst_t;
struct bst {
    bst_node_t *root;
};

typedef enum {
    BST_DIR_LEFT,
    BST_DIR_RIGHT,
} bst_dir_t;

// This is not necessary if it's static variable(which is initialized by zero).
void bst_init(bst_t *self);
void bst_insertnode(bst_t *self, bst_node_t *node, intmax_t key, void *data);
void bst_removenode(bst_t *self, bst_node_t *node);
bst_node_t *bst_findnode(bst_t *self, intmax_t key);
bst_node_t *bst_minof_tree(bst_t *self);
bst_node_t *bst_maxof_tree(bst_t *self);
bst_node_t *bst_minof(bst_node_t *subtreeroot);
bst_node_t *bst_maxof(bst_node_t *subtreeroot);
bst_node_t *bst_successor(bst_node_t *node);
bst_node_t *bst_predecessor(bst_node_t *node);
bst_dir_t bst_dirinparent(bst_node_t *node);
void bst_rotate(bst_t *self, bst_node_t *subtreeroot, bst_dir_t dir);
void bst_recalculateheight(bst_node_t *subtreeroot);
void bst_recalculatebf_tree(bst_t *self);
void bst_recalculatebf(bst_node_t *subtreeroot);
void bst_checkandrebalence(bst_t *self, bst_node_t *startNode);

// Unbalenced operations will break tree balence, but it *is* still valid bst_t.
// It will be just slower to search than balenced tree.
// (For balenced operations, use the ones without ~Unbalenced suffix)
void bst_insertnode_unbalenced(bst_t *self, bst_node_t *node, intmax_t key, void *data);
void bst_removenode_unbalenced(bst_t *self, bst_node_t *node);
