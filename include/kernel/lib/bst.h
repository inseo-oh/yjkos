#pragma once
#include <stddef.h>
#include <stdint.h>

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

typedef enum {
    BST_DIR_LEFT,
    BST_DIR_RIGHT,
} BST_DIR;

// This is not necessary if it's static variable(which is initialized by zero).
void bst_init(struct bst *self);
void bst_insert_node(struct bst *self, struct bst_node *node, intmax_t key, void *data);
void bst_remove_node(struct bst *self, struct bst_node *node);
struct bst_node *bst_find_node(struct bst *self, intmax_t key);
struct bst_node *bst_min_of_tree(struct bst *self);
struct bst_node *bst_max_of_tree(struct bst *self);
struct bst_node *bst_min_of(struct bst_node *subtreeroot);
struct bst_node *bst_max_of(struct bst_node *subtreeroot);
struct bst_node *bst_successor(struct bst_node *node);
struct bst_node *bst_predecessor(struct bst_node *node);
BST_DIR bst_dir_in_parent(struct bst_node *node);
void bst_rotate(struct bst *self, struct bst_node *subtreeroot, BST_DIR dir);
void bst_recalculate_height(struct bst_node *subtreeroot);
void bst_recalculate_bf_tree(struct bst *self);
void bst_recalculate_bf(struct bst_node *subtreeroot);
void bst_check_and_rebalence(struct bst *self, struct bst_node *startNode);

// Unbalenced operations will break tree balence, but it *is* still valid BST.
// It will be just slower to search than balenced tree.
// (For balenced operations, use the ones without ~unbalenced suffix)
void bst_insert_node_unbalenced(struct bst *self, struct bst_node *node, intmax_t key, void *data);
void bst_remove_node_unbalenced(struct bst *self, struct bst_node *node);
