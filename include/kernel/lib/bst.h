#pragma once
#include <stdint.h>
#include <stddef.h>

struct bst_node {
    struct bst_node *parent;
    struct bst_node *children[2];
    void *data;
    intmax_t key;
    int32_t bf;
    int32_t height;
};

struct bst {
    struct bst_node *root;
};

enum bst_dir {
    BST_DIR_LEFT,
    BST_DIR_RIGHT,
};

// This is not necessary if it's static variable(which is initialized by zero).
void bst_init(struct bst *self);
void bst_insertnode(struct bst *self, struct bst_node *node, intmax_t key, void *data);
void bst_removenode(struct bst *self, struct bst_node *node);
struct bst_node *bst_findnode(struct bst *self, intmax_t key);
struct bst_node *bst_minof_tree(struct bst *self);
struct bst_node *bst_maxof_tree(struct bst *self);
struct bst_node *bst_minof(struct bst_node *subtreeroot);
struct bst_node *bst_maxof(struct bst_node *subtreeroot);
struct bst_node *bst_successor(struct bst_node *node);
struct bst_node *bst_predecessor(struct bst_node *node);
enum bst_dir bst_dirinparent(struct bst_node *node);
void bst_rotate(struct bst *self, struct bst_node *subtreeroot, enum bst_dir dir);
void bst_recalculateheight(struct bst_node *subtreeroot);
void bst_recalculatebf_tree(struct bst *self);
void bst_recalculatebf(struct bst_node *subtreeroot);
void bst_checkandrebalence(struct bst *self, struct bst_node *startNode);

// Unbalenced operations will break tree balence, but it *is* still valid BST.
// It will be just slower to search than balenced tree.
// (For balenced operations, use the ones without ~Unbalenced suffix)
void bst_insertnode_unbalenced(struct bst *self, struct bst_node *node, intmax_t key, void *data);
void bst_removenode_unbalenced(struct bst *self, struct bst_node *node);
