#include <assert.h>
#include <kernel/io/co.h>
#include <kernel/lib/bst.h>
#include <kernel/panic.h>
#include <stdint.h>
#include <string.h>

//------------------------------- Configuration -------------------------------

// Check tree integrity after tree operation?
static bool const CONFIG_CHECK_TREE = true;

//-----------------------------------------------------------------------------

#define CHECK_FLAG_NO_HEIGHT    (1U << 0)
#define CHECK_FLAG_NO_BF        (1U << 1)

// NOLINTNEXTLINE(misc-no-recursion)
static int32_t height_of_subtree(struct bst_node *subtreeroot) {
    int32_t lheight = 0;
    int32_t rheight = 0;
    if (subtreeroot->children[BST_DIR_LEFT] != NULL) {
        lheight = height_of_subtree(
            subtreeroot->children[BST_DIR_LEFT]) + 1;
    }
    if (subtreeroot->children[BST_DIR_RIGHT] != NULL) {
        rheight = height_of_subtree(
            subtreeroot->children[BST_DIR_RIGHT]) + 1;
    }
    return (lheight < rheight) ? rheight : lheight;
}

static int32_t balence_factor(struct bst_node *subtreeroot) {
    int32_t lheight = 0;
    int32_t rheight = 0;
    if (subtreeroot->children[BST_DIR_LEFT]) {
        lheight = height_of_subtree(
            subtreeroot->children[BST_DIR_LEFT]) + 1;
    }
    if (subtreeroot->children[BST_DIR_RIGHT]) {
        rheight = height_of_subtree(
            subtreeroot->children[BST_DIR_RIGHT]) + 1;
    }
    return lheight - rheight;
}

// NOLINTNEXTLINE(misc-no-recursion)
static void check_subtree(
    struct bst_node *root, struct bst_node *parent, bool preaction,
    uint8_t flags)
{
    if (!CONFIG_CHECK_TREE) {
        return;
    }
    if (root == NULL) {
        return;
    }
    bool failed = false;
    if (root->parent != parent) {
        if (parent != NULL) {
            if (root->parent != NULL) {
                co_printf(
                    "[%#lx] expected parent %#lx, got %#lx\n",
                    root->key, parent->key, root->parent->key);
            } else {
                co_printf(
                    "[%#lx] expected parent %#lx, got no parent\n",
                    root->key, parent->key);
            }
        } else {
            if (parent != NULL) {
                co_printf(
                    "[%#lx] expected no parent, got %#lx\n",
                    root->key, root->parent->key);
            } else {
                assert(!"huh?");
            }
        }
        failed = true;
    }
    if (!(flags & CHECK_FLAG_NO_HEIGHT)) {
        int32_t expectedheight = height_of_subtree(root);
        if (root->height != expectedheight) {
            co_printf("expectedheight %d\n", expectedheight);
            co_printf("  root->height %d\n", root->height);
            co_printf(
                "[%#lx] expected height %d, got %d\n",
                root->key, expectedheight, root->height);
            failed = true;
        }
    }

    if (!(flags & CHECK_FLAG_NO_BF)) {
        int32_t expectedbf = balence_factor(root);
        if (root->bf != expectedbf) {
            co_printf(
                "[%#lx] expected BF %d, got %d\n", root->key, expectedbf,
                root->bf);
            failed = true;
        }
    }
    if (failed) {
        if (preaction) {
            panic("tree pre-check failed");
        } else {
            panic("tree post-check failed");
        }
    }

    check_subtree(
        root->children[BST_DIR_LEFT], root, preaction, flags);
    check_subtree(
        root->children[BST_DIR_RIGHT], root, preaction, flags);

}

static void check_tree(struct bst *self, bool preaction, uint8_t flags) {
    if (!CONFIG_CHECK_TREE) {
        return;
    }
    check_subtree(self->root, NULL, preaction, flags);
}

void bst_insert_node_unbalenced(
    struct bst *self, struct bst_node *node, intmax_t key, void *data)
{
    check_tree(self, true, 0);
    // Find where to insert
    node->children[BST_DIR_LEFT] = NULL;
    node->children[BST_DIR_RIGHT] = NULL;
    node->bf = 0;
    node->data = data;
    node->key = key;
    struct bst_node *current = self->root;
    if (current == NULL) {
        // Tree is empty
        self->root = node;
        node->parent = NULL;
        check_tree(self, false, 0);
        return;
    }
    struct bst_node *insertparent;
    enum bst_dir childindex;
    while(current != NULL) {
        insertparent = current;
        if (node->key < current->key) {
            childindex = BST_DIR_LEFT;
        } else if (node->key > current->key) {
            childindex = BST_DIR_RIGHT;
        } else {
            panic("bst: duplicate tree key found");
        }
        current = current->children[childindex];
    }
    // Insert the node
    insertparent->children[childindex] = node;
    node->parent = insertparent;
    if (node->parent != NULL) {
        bst_recalculate_height(node->parent);
        bst_recalculate_bf(node->parent);
    }
    check_tree(self, false, 0);
}

static void remove_terminal_node(struct bst *self, struct bst_node *node) {
    if (node->parent != NULL) {
        node->parent->children[bst_dir_in_parent(node)] = NULL;
    } else {
        assert(self->root == node);
        self->root = NULL;
    }
}

static void remove_node_with_left_child(
    struct bst *self, struct bst_node *node)
{
    if (node->parent != NULL) {
        node->parent->children[bst_dir_in_parent(node)] =
            node->children[BST_DIR_LEFT];
        node->children[BST_DIR_LEFT]->parent = node->parent;
    } else {
        node->children[BST_DIR_LEFT]->parent = NULL;
        assert(self->root == node);
        self->root = node->children[BST_DIR_LEFT];
    }
}

static void remove_node_with_right_child(
    struct bst *self, struct bst_node *node)
{
    if (node->parent != NULL) {
        node->parent->children[bst_dir_in_parent(node)] =
            node->children[BST_DIR_RIGHT];
        node->children[BST_DIR_RIGHT]->parent = node->parent;
    } else {
        node->children[BST_DIR_RIGHT]->parent = NULL;
        assert(self->root == node);
        self->root = node->children[BST_DIR_RIGHT];
    }
}

static void remove_node_with_both_children(
    struct bst *self, struct bst_node *node)
{
    struct bst_node *replacement =
        bst_max_of(node->children[BST_DIR_LEFT]);
    assert(replacement);
    assert(replacement->parent);
    struct bst_node *old_parent = replacement->parent;
    replacement->parent->children[bst_dir_in_parent(replacement)] = NULL;
    if (node->parent != NULL) {
        node->parent->children[bst_dir_in_parent(node)] = replacement;
    } else {
        self->root = replacement;
    }
    replacement->parent = node->parent;
    replacement->children[BST_DIR_LEFT] = node->children[BST_DIR_LEFT];
    replacement->children[BST_DIR_RIGHT] = node->children[BST_DIR_RIGHT];

    if (replacement->children[BST_DIR_LEFT] != NULL) {
        replacement->children[BST_DIR_LEFT]->parent = replacement;
    }
    if (replacement->children[BST_DIR_RIGHT] != NULL) {
        replacement->children[BST_DIR_RIGHT]->parent = replacement;
    }
    /*
     * We have to update height of the replacement node's old parent(since it 
     * is no longer a child of that parent), but there is also a case where the 
     * replacement was direct child of the node we removed. So we have to check 
     * for that one.
     */
    if (old_parent != node) {
        /*
         * height of `replacement` will also be calculated as part of below 
         * recalculation.
         */
        bst_recalculate_height(old_parent);
    } else {
        bst_recalculate_height(replacement);
    }
    bst_recalculate_bf(replacement);
}

void bst_remove_node_unbalenced(struct bst *self, struct bst_node *node) {
    check_tree(self, true, 0);
    struct bst_node *parent_node = node->parent;
    if (
        (node->children[BST_DIR_LEFT] == NULL) &&
        (node->children[BST_DIR_RIGHT] == NULL))
    {
        remove_terminal_node(self, node);
    } else if (
        (node->children[BST_DIR_LEFT] != NULL) &&
        node->children[BST_DIR_RIGHT] == NULL)
    {
        remove_node_with_left_child(self, node);
    } else if (
        (node->children[BST_DIR_LEFT] == NULL) &&
        (node->children[BST_DIR_RIGHT] != NULL))
    {
        remove_node_with_right_child(self, node);
    } else {
        remove_node_with_both_children(self, node);
    }
    if (parent_node != NULL) {
        bst_recalculate_height(parent_node);
        bst_recalculate_bf(parent_node);
    }
    check_tree(self, false, 0);
}

void bst_init(struct bst *self) {
    memset(self, 0, sizeof(*self));
}

void bst_insert_node(
    struct bst *self, struct bst_node *node, intmax_t key, void *data)
{
    check_tree(self, true, 0);
    bst_insert_node_unbalenced(self, node, key, data);
    if (node->parent != NULL) {
        bst_check_and_rebalence(self, node->parent);
    }
    check_tree(self, false, 0);
}

void bst_remove_node(struct bst *self, struct bst_node *node) {
    check_tree(self, true, 0);
    bst_remove_node_unbalenced(self, node);
    if (node->parent != NULL) {
        bst_check_and_rebalence(self, node->parent);
    }
    check_tree(self, false, 0);
}

struct bst_node *bst_findnode(struct bst *self, intmax_t key) {
    check_tree(self, true, 0);
    struct bst_node *current = self->root;
    while (current != NULL) {
        if (key < current->key) {
            current = current->children[BST_DIR_LEFT];
        } else if (key > current->key) {
            current = current->children[BST_DIR_RIGHT];
        } else {
            check_tree(self, false, 0);
            return current;
        }
    }
    check_tree(self, false, 0);
    return NULL;
}

struct bst_node *bst_min_of_tree(struct bst *self) {
    return bst_min_of(self->root);
}

struct bst_node *bst_max_of_tree(struct bst *self) {
    return bst_max_of(self->root);
}

struct bst_node *bst_min_of(struct bst_node *subtreeroot) {
    struct bst_node *result = NULL;
    struct bst_node *current = subtreeroot;
    while (current != NULL) {
        result = current;
        current = current->children[BST_DIR_LEFT];
    }
    return result;
}

struct bst_node *bst_max_of(struct bst_node *subtreeroot) {
    struct bst_node *result = NULL;
    struct bst_node *current = subtreeroot;
    while (current != NULL) {
        result = current;
        current = current->children[BST_DIR_RIGHT];
    }
    return result;
}

enum bst_dir bst_dir_in_parent(struct bst_node *node) {
    struct bst_node *parent = node->parent;
    if (parent == NULL) {
        panic("bst: attempted to child index on a node without parent");
    }
    if (parent->children[BST_DIR_LEFT] == node) {
        return BST_DIR_LEFT;
    }
    if (parent->children[BST_DIR_RIGHT] == node) {
        return BST_DIR_RIGHT;
    }
    panic(
        "bst: attempted to child index, but parent doesn't have the node as child");
}

struct bst_node *bst_successor(struct bst_node *node) {
    struct bst_node *right_subtree = node->children[BST_DIR_RIGHT];
    if (right_subtree == NULL) {
        struct bst_node *current = node->parent;
        while (current != NULL) {
            if (node->key < current->key) {
                check_subtree(
                    node, node->parent, false,
                    0);
                return current;
            }
            current = current->parent;
        }
        check_subtree(
            node, node->parent, false, 0);
        return NULL;
    }
    return bst_min_of(right_subtree);
}

struct bst_node *bst_predecessor(struct bst_node *node) {
    struct bst_node *left_subtree = node->children[BST_DIR_LEFT];
    if (left_subtree == NULL) {
        struct bst_node *current = node->parent;
        while (current != NULL) {
            if (node->key > current->key) {
                check_subtree(node, node->parent, false, 0);
                return current;
            }
            current = current->parent;
        }
    }
    return bst_max_of(left_subtree);
}

void bst_rotate(
    struct bst *self, struct bst_node *subtreeroot, enum bst_dir dir)
{
    check_tree(self, true, 0);
    enum bst_dir oppositedir = 1 - dir;
    /*
     * Tree rotation example(Left rotation):
     *     [P]
     *      |
     *     [A]   <--- Subtree root
     *     / \        
     * [...] [B] <--- A's <oppositedir> child 
     *       / \    
     *     [C] [...]
     *     ^
     *     +---------- B's <dir> child
     */
    struct bst_node *nodea = subtreeroot;
    struct bst_node *nodeb = nodea->children[oppositedir];
    if (nodeb == NULL) {
        // Node B needs to go to where Node A is currently at, but of course we
        // can't do anything if nothing is there.
        panic("bst: the subtree cannot be rotated");
    }
    /*
     * Note that Node C and P doesn't need to exist, but if they do, we have to 
     * relink them to right nodes.
     */
    struct bst_node *nodec = nodeb->children[dir];
    struct bst_node *nodep = nodea->parent;
    
    /*
     * Perform rotation. This is where Node C loses its place.
     *         [P]
     *          |
     *         [B]       [C] <-- Still thinks B is parent, but this poor child
     *         / \               was thrown out by B :(
     *      [A]    [...]
     *     /
     *  [...]
     */
    if (nodep != NULL) {
        nodep->children[bst_dir_in_parent(nodea)] = nodeb;
        nodeb->parent = nodep;
    } else {
        nodeb->parent = NULL;
        self->root = nodeb;
    }
    nodeb->children[dir] = nodea;
    nodea->parent = nodeb;
    /*
     * Give Node C a new parent.
     * - Since Node C was left child of B, C's key is less than B.
     * - And Node B was right child of A, B's key is greater than A.
     * So it is A < B and C < B, and that means A's right child is perfect 
     * place for it.
     * (Of course this is assuming it is right rotation)
     *         [P]
     *          |
     *         [B]
     *         / \
     *      [A]    [...]
     *     /   \
     *  [...]  [C]
     */
    nodea->children[oppositedir] = nodec;
    if (nodec != NULL) {
        nodec->parent = nodea;
    }
    // This will calculate of its parents as well(including nodeb)
    bst_recalculate_height(nodea);
    bst_recalculate_bf(nodeb);
    check_tree(self, false, 0);
}

void bst_recalculate_height(struct bst_node *subtreeroot) {
    check_subtree(
        subtreeroot, subtreeroot->parent, true,
        CHECK_FLAG_NO_HEIGHT | CHECK_FLAG_NO_BF);
    struct bst_node *current = subtreeroot;

    while (current != NULL) {
        int32_t lheight = 0;
        int32_t rheight = 0;
        if (current->children[BST_DIR_LEFT] != NULL) {
            lheight = current->children[BST_DIR_LEFT]->height + 1;
        }
        if (current->children[BST_DIR_RIGHT] != NULL) {
            rheight = current->children[BST_DIR_RIGHT]->height + 1;
        }
        current->height = (lheight < rheight) ? rheight : lheight;
        current = current->parent;
    }
    check_subtree(
        subtreeroot, subtreeroot->parent, false,
        CHECK_FLAG_NO_BF);
}

void bst_recalculate_bf_tree(struct bst *self) {
    check_tree(self, true, CHECK_FLAG_NO_BF);
    struct bst_node *current = bst_min_of_tree(self);
    while (current) {
        int32_t lheight = 0;
        int32_t rheight = 0;
        if (current->children[BST_DIR_LEFT]) {
            lheight = current->children[BST_DIR_LEFT]->height + 1;
        }
        if (current->children[BST_DIR_RIGHT]) {
            rheight = current->children[BST_DIR_RIGHT]->height + 1;
        }
        current->bf = lheight - rheight;
        current = bst_successor(current);
    }
    check_tree(self, false, 0);
}

void bst_recalculate_bf(struct bst_node *subtreeroot) {
    check_subtree(
        subtreeroot, subtreeroot->parent, true, 
        CHECK_FLAG_NO_BF);
    // Recalculate BF of current subtree
    struct bst_node *current = bst_min_of(subtreeroot);
    struct bst_node *last = bst_max_of(subtreeroot);
    while (1) {
        int32_t lheight = 0;
        int32_t rheight = 0;
        if (current->children[BST_DIR_LEFT]) {
            lheight = current->children[BST_DIR_LEFT]->height + 1;
        }
        if (current->children[BST_DIR_RIGHT]) {
            rheight = current->children[BST_DIR_RIGHT]->height + 1;
        }
        current->bf = lheight - rheight;
        if (current == last) {
            break;
        }
        current = bst_successor(current);
        assert(current);
    }
    // Recalculate BF of parents
    current = subtreeroot->parent;
    while (current != NULL) {
        int32_t lheight = 0;
        int32_t rheight = 0;
        if (current->children[BST_DIR_LEFT]) {
            lheight = current->children[BST_DIR_LEFT]->height + 1;
        }
        if (current->children[BST_DIR_RIGHT]) {
            rheight = current->children[BST_DIR_RIGHT]->height + 1;
        }
        current->bf = lheight - rheight;
        current = current->parent;
    }
    check_subtree(
        subtreeroot, subtreeroot->parent, false,
        0);
}

void bst_check_and_rebalence(struct bst *self, struct bst_node *startnode) {
    check_tree(self, true, 0);
    struct bst_node *current= startnode;
    while (current != NULL) {
        struct bst_node *oldparent = current->parent;
        if (current->bf > 1) {
            // Left heavy
            struct bst_node *child = current->children[BST_DIR_LEFT];
            assert(child);
            if (child->bf < 0) {
                // Left-right heavy
                bst_rotate(self, child, BST_DIR_LEFT);
            }
            bst_rotate(self, current, BST_DIR_RIGHT);
        } else if (current->bf < -1) {
            // Right heavy
            struct bst_node *child = current->children[BST_DIR_RIGHT];
            assert(child);
            if (child->bf > 0) {
                // Right-left heavy
                bst_rotate(self, child, BST_DIR_RIGHT);
            }
            bst_rotate(self, current, BST_DIR_LEFT);
        }
        assert(current != oldparent);
        current = oldparent;
    }
    check_tree(self, false, 0);
}
