#include "../test.h"
#include <kernel/io/co.h>
#include <kernel/lib/bst.h>
#include <kernel/lib/strutil.h>
#include <kernel/panic.h>

static struct bst_node *assertnonnullnode(struct bst_node *node, char const *assertion, char const *function, char const *file, int line) {
    if (node == nullptr) {
        co_printf("non-null assertion failed at %s(%s:%d): %s\n", function, file, line, assertion);
        panic(nullptr);
    }
    return node;
}

#define ASSERT_NONNULL_BSTNODE(_x) assertnonnullnode(_x, #_x, __func__, __FILE__, __LINE__)

static bool do_insertnode_unbalenced(void) {
    struct bst bst;
    bst_init(&bst);

    struct bst_node nodes[5];
    vmemset(nodes, 0, sizeof(nodes));
    // Insert root node
    bst_insert_node_unbalenced(&bst, &nodes[0], 1000, nullptr);
    TEST_EXPECT(bst.root == &nodes[0]);
    TEST_EXPECT(bst.root->parent == nullptr);
    TEST_EXPECT(bst.root->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(bst.root->children[BST_DIR_RIGHT] == nullptr);

    /*
     *   1000
     *   /
     * 500
     */
    bst_insert_node_unbalenced(&bst, &nodes[1], 500, nullptr);
    TEST_EXPECT(bst.root == &nodes[0]);
    TEST_EXPECT(bst.root->parent == nullptr);
    TEST_EXPECT(bst.root->children[BST_DIR_LEFT] == &nodes[1]);
    TEST_EXPECT(bst.root->children[BST_DIR_RIGHT] == nullptr);

    TEST_EXPECT(nodes[1].parent == &nodes[0]);
    TEST_EXPECT(nodes[1].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[1].children[BST_DIR_RIGHT] == nullptr);

    /*
     *   1000
     *      \
     *      1500
     */
    bst.root->children[BST_DIR_LEFT] = nullptr;
    bst.root->height = 0;
    bst.root->bf = 0;
    bst_insert_node_unbalenced(&bst, &nodes[2], 1500, nullptr);
    TEST_EXPECT(bst.root == &nodes[0]);
    TEST_EXPECT(bst.root->parent == nullptr);
    TEST_EXPECT(bst.root->children[BST_DIR_LEFT] == nullptr)
    TEST_EXPECT(bst.root->children[BST_DIR_RIGHT] == &nodes[2]);

    TEST_EXPECT(nodes[2].parent == &nodes[0]);
    TEST_EXPECT(nodes[2].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[2].children[BST_DIR_RIGHT] == nullptr);

    /*
     *   1000
     *    / \
     *  500  1500
     */
    bst.root->children[BST_DIR_LEFT] = nullptr;
    bst.root->children[BST_DIR_RIGHT] = nullptr;
    nodes[1].parent = nullptr;
    nodes[2].parent = nullptr;
    bst.root->height = 0;
    bst.root->bf = 0;
    bst_insert_node_unbalenced(&bst, &nodes[1], nodes[1].key, nullptr);
    bst_insert_node_unbalenced(&bst, &nodes[2], nodes[2].key, nullptr);
    TEST_EXPECT(bst.root == &nodes[0]);
    TEST_EXPECT(bst.root->parent == nullptr);
    TEST_EXPECT(bst.root->children[BST_DIR_LEFT] == &nodes[1]);
    TEST_EXPECT(bst.root->children[BST_DIR_RIGHT] == &nodes[2]);

    TEST_EXPECT(nodes[1].parent == &nodes[0]);
    TEST_EXPECT(nodes[1].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[1].children[BST_DIR_RIGHT] == nullptr);

    TEST_EXPECT(nodes[2].parent == &nodes[0]);
    TEST_EXPECT(nodes[2].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[2].children[BST_DIR_RIGHT] == nullptr);

    /*
     *   1000
     *   /  \
     *  /    \
     * 500    1500
     *   \     /
     *   600  1400
     */
    bst_insert_node_unbalenced(&bst, &nodes[3], 600, nullptr);
    bst_insert_node_unbalenced(&bst, &nodes[4], 1400, nullptr);
    TEST_EXPECT(bst.root == &nodes[0]);
    TEST_EXPECT(bst.root->parent == nullptr);
    TEST_EXPECT(bst.root->children[BST_DIR_LEFT] == &nodes[1]);
    TEST_EXPECT(bst.root->children[BST_DIR_RIGHT] == &nodes[2]);

    TEST_EXPECT(nodes[1].parent == &nodes[0]);
    TEST_EXPECT(nodes[1].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[1].children[BST_DIR_RIGHT] == &nodes[3]);

    TEST_EXPECT(nodes[2].parent == &nodes[0]);
    TEST_EXPECT(nodes[2].children[BST_DIR_LEFT] == &nodes[4]);
    TEST_EXPECT(nodes[2].children[BST_DIR_RIGHT] == nullptr);

    TEST_EXPECT(nodes[3].parent == &nodes[1]);
    TEST_EXPECT(nodes[3].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[3].children[BST_DIR_RIGHT] == nullptr);

    TEST_EXPECT(nodes[4].parent == &nodes[2]);
    TEST_EXPECT(nodes[4].children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(nodes[4].children[BST_DIR_RIGHT] == nullptr);

    return true;
}

// NOTE: There is no separate "Remove & Balencing" test, because
//       1) I'm tired :(
//       2) Balencing works the same when removing, and we test all four AVL balencing
//          cases in this one test.

static bool do_balencing(void) {
    struct bst bst;
    struct bst_node nodes[12];

    bst_init(&bst);
    vmemset(nodes, 0, sizeof(nodes));

    //--------------------------------------------------------------------------
    // Left-left case
    //--------------------------------------------------------------------------
    /*
     *        1000 <BF=2>          900
     *       /                    /   \
     *    900 <BF=1>     ----> 800    1000
     *    /
     * 800                     * BF is all 0
     */
    struct bst_node *node1000 = &nodes[0];
    struct bst_node *node900 = &nodes[1];
    struct bst_node *node800 = &nodes[2];
    bst_insert_node(&bst, node1000, 1000, nullptr);
    bst_insert_node(&bst, node900, 900, nullptr);
    bst_insert_node(&bst, node800, 800, nullptr);

    TEST_EXPECT(bst.root == node900);
    TEST_EXPECT(node900->parent == nullptr);
    TEST_EXPECT(node900->children[BST_DIR_LEFT] == node800);
    TEST_EXPECT(node900->children[BST_DIR_RIGHT] == node1000);
    TEST_EXPECT(node900->bf == 0);

    TEST_EXPECT(node800->parent == node900);
    TEST_EXPECT(node800->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node800->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node800->bf == 0);

    TEST_EXPECT(node1000->parent == node900);
    TEST_EXPECT(node1000->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1000->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1000->bf == 0);
    /*
     *          900 <BF=2>                    900 <BF=1>
     *         /    \                        /   \
     *      800      1000                 700     1000
     *      /  <BF=2>             -->    /   \
     *   700 <BF=1>                    600    800
     *   /
     * 600
     */
    struct bst_node *node700 = &nodes[3];
    struct bst_node *node600 = &nodes[4];
    bst_insert_node(&bst, node700, 700, nullptr);
    bst_insert_node(&bst, node600, 600, nullptr);

    TEST_EXPECT(bst.root == node900);
    TEST_EXPECT(node900->parent == nullptr);
    TEST_EXPECT(node900->children[BST_DIR_LEFT] == node700);
    TEST_EXPECT(node900->children[BST_DIR_RIGHT] == node1000);
    TEST_EXPECT(node900->bf == 1);

    TEST_EXPECT(node700->parent == node900);
    TEST_EXPECT(node700->children[BST_DIR_LEFT] == node600);
    TEST_EXPECT(node700->children[BST_DIR_RIGHT] == node800);
    TEST_EXPECT(node700->bf == 0);

    TEST_EXPECT(node1000->parent == node900);
    TEST_EXPECT(node1000->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1000->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1000->bf == 0);

    TEST_EXPECT(node600->parent == node700);
    TEST_EXPECT(node600->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node600->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node600->bf == 0);

    TEST_EXPECT(node800->parent == node700);
    TEST_EXPECT(node800->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node800->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node800->bf == 0);

    //--------------------------------------------------------------------------
    // Left-right case
    //--------------------------------------------------------------------------
    /*
     *              900 <BF=3>                <BF=2> 900                      700
     *             /   \                            /   \                   /     \
     *   <BF=2> 700     1000               <BF=1> 700    1000            550       900
     *         /   \                             /   \                  /  \       /  \
     * <BF=2> 600   800             -->        550    800        -->  500   600  800  1000
     *       /                                /   \
     *    500 <BF=-1>                      500    600
     *       \
     *        550
     */
    struct bst_node *node500 = &nodes[5];
    struct bst_node *node550 = &nodes[6];
    bst_insert_node(&bst, node500, 500, nullptr);
    bst_insert_node(&bst, node550, 550, nullptr);

    TEST_EXPECT(bst.root == node700);
    TEST_EXPECT(node700->parent == nullptr);
    TEST_EXPECT(node700->children[BST_DIR_LEFT] == node550);
    TEST_EXPECT(node700->children[BST_DIR_RIGHT] == node900);
    TEST_EXPECT(node700->bf == 0);

    TEST_EXPECT(node550->parent == node700);
    TEST_EXPECT(node550->children[BST_DIR_LEFT] == node500);
    TEST_EXPECT(node550->children[BST_DIR_RIGHT] == node600);
    TEST_EXPECT(node550->bf == 0);

    TEST_EXPECT(node900->parent == node700);
    TEST_EXPECT(node900->children[BST_DIR_LEFT] == node800);
    TEST_EXPECT(node900->children[BST_DIR_RIGHT] == node1000);
    TEST_EXPECT(node900->bf == 0);

    TEST_EXPECT(node500->parent == node550);
    TEST_EXPECT(node500->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node500->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node500->bf == 0);

    TEST_EXPECT(node600->parent == node550);
    TEST_EXPECT(node600->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node600->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node600->bf == 0);

    TEST_EXPECT(node800->parent == node900);
    TEST_EXPECT(node800->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node800->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node800->bf == 0);

    TEST_EXPECT(node1000->parent == node900);
    TEST_EXPECT(node1000->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1000->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1000->bf == 0);

    //--------------------------------------------------------------------------
    // Right-right case
    //--------------------------------------------------------------------------
    /*
     *         700 <BF=-2>                          700 <BF=-1>
     *       /     \                              /     \
     *    550       900 <BF=-2>                 550      900 <BF=-1>
     *   /  \       /  \                       /  \      /  \
     * 500   600  800  1000 <BF=-2>   -->    500  600  800  1100
     *                    \                                /   \
     *                    1100 <BF=-1>                  1000   1200
     *                      \
     *                     1200
     */
    struct bst_node *node1100 = &nodes[7];
    struct bst_node *node1200 = &nodes[8];
    bst_insert_node(&bst, node1100, 1100, nullptr);
    bst_insert_node(&bst, node1200, 1200, nullptr);

    TEST_EXPECT(bst.root == node700);
    TEST_EXPECT(node700->parent == nullptr);
    TEST_EXPECT(node700->children[BST_DIR_LEFT] == node550);
    TEST_EXPECT(node700->children[BST_DIR_RIGHT] == node900);
    TEST_EXPECT(node700->bf == -1);

    TEST_EXPECT(node900->parent == node700);
    TEST_EXPECT(node900->children[BST_DIR_LEFT] == node800);
    TEST_EXPECT(node900->children[BST_DIR_RIGHT] == node1100);
    TEST_EXPECT(node900->bf == -1);

    TEST_EXPECT(node800->parent == node900);
    TEST_EXPECT(node800->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node800->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node800->bf == 0);

    TEST_EXPECT(node1100->parent == node900);
    TEST_EXPECT(node1100->children[BST_DIR_LEFT] == node1000);
    TEST_EXPECT(node1100->children[BST_DIR_RIGHT] == node1200);
    TEST_EXPECT(node1100->bf == 0);

    TEST_EXPECT(node1000->parent == node1100);
    TEST_EXPECT(node1000->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1000->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1000->bf == 0);

    TEST_EXPECT(node1200->parent == node1100);
    TEST_EXPECT(node1200->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1200->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1200->bf == 0);

    //--------------------------------------------------------------------------
    // Right-left case
    //--------------------------------------------------------------------------
    /*
     *         700 <BF=-3>                          700 <BF=-2>                     700  <BF=-1>
     *       /     \                              /     \                         /      \
     *     550      900 <BF=-3>                 550      900 <BF=-2>            550         1100
     *    /  \      /  \                       /  \     /   \                  /   \      /     \
     *  500  600  800  1100 <BF=-2>      --> 500  600  800  1100 <BF=-1> --> 500   600  900       1290
     *                /   \                           /        \                        / \      /    \
     *             1000   1200 <BF=-2>             BF=1        1290                   800 1000  1200  1300
     *                       \                                /    \
     *                       1300 <BF=1>                   1200    1300
     *                       /
     *                    1290
     */
    struct bst_node *node1300 = &nodes[9];
    struct bst_node *node1290 = &nodes[10];
    bst_insert_node(&bst, node1300, 1300, nullptr);
    bst_insert_node(&bst, node1290, 1290, nullptr);

    TEST_EXPECT(bst.root == node700);
    TEST_EXPECT(node700->parent == nullptr);
    TEST_EXPECT(node700->children[BST_DIR_LEFT] == node550);
    TEST_EXPECT(node700->children[BST_DIR_RIGHT] == node1100);
    TEST_EXPECT(node700->bf == -1);

    TEST_EXPECT(node1100->parent == node700);
    TEST_EXPECT(node1100->children[BST_DIR_LEFT] == node900);
    TEST_EXPECT(node1100->children[BST_DIR_RIGHT] == node1290);
    TEST_EXPECT(node1100->bf == 0);

    TEST_EXPECT(node900->parent == node1100);
    TEST_EXPECT(node900->children[BST_DIR_LEFT] == node800);
    TEST_EXPECT(node900->children[BST_DIR_RIGHT] == node1000);
    TEST_EXPECT(node900->bf == 0);

    TEST_EXPECT(node1290->parent == node1100);
    TEST_EXPECT(node1290->children[BST_DIR_LEFT] == node1200);
    TEST_EXPECT(node1290->children[BST_DIR_RIGHT] == node1300);
    TEST_EXPECT(node1290->bf == 0);

    TEST_EXPECT(node800->parent == node900);
    TEST_EXPECT(node800->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node800->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node800->bf == 0);

    TEST_EXPECT(node1000->parent == node900);
    TEST_EXPECT(node1000->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1000->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1000->bf == 0);

    TEST_EXPECT(node1200->parent == node1290);
    TEST_EXPECT(node1200->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1200->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1200->bf == 0);

    TEST_EXPECT(node1300->parent == node1290);
    TEST_EXPECT(node1300->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node1300->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node1300->bf == 0);

    //--------------------------------------------------------------------------
    // As the last test, we have to see tree root changing by rotating left, right?
    //--------------------------------------------------------------------------
    /*
     *        700  <BF=-2>                                        1100 <BF=0>
     *      /      \                                            /       \
     *    550         1100 <BF=-1>                           700         1290 <BF=-1>
     *   /   \      /     \                                /     \       /   \
     * 500   600  900       1290 <BF=-1>     -->         550     900  1200   1300 <BF=-1>
     *            / \      /    \                       /  \    /   \           \
     *          800 1000  1200  1300 <BF=-1>          500  600 800  1000        1400
     *                             \
     *                             1400
     */
    struct bst_node *node1400 = &nodes[11];
    bst_insert_node(&bst, node1400, 1400, nullptr);

    TEST_EXPECT(bst.root == node1100);
    TEST_EXPECT(node1100->parent == nullptr);
    TEST_EXPECT(node1100->children[BST_DIR_LEFT] == node700);
    TEST_EXPECT(node1100->children[BST_DIR_RIGHT] == node1290);
    TEST_EXPECT(node1100->bf == 0);

    TEST_EXPECT(node700->parent == node1100);
    TEST_EXPECT(node700->children[BST_DIR_LEFT] == node550);
    TEST_EXPECT(node700->children[BST_DIR_RIGHT] == node900);
    TEST_EXPECT(node700->bf == 0);

    TEST_EXPECT(node1290->parent == node1100);
    TEST_EXPECT(node1290->children[BST_DIR_LEFT] == node1200);
    TEST_EXPECT(node1290->children[BST_DIR_RIGHT] == node1300);
    TEST_EXPECT(node1290->bf == -1);

    return true;
}

struct testtree {
    struct bst bst;
    struct bst_node nodes[7];
};

/*
 * ------ Keys ------  ------ Node indices ------
 *        50                         0
 *      /    \                    /    \
 *     /      \                  /      \
 *    25      75                1       2
 *   /  \     /                / \     /
 *  12  37  63                3   4   5
 *            \                        \
 *             69                       6
 */
static void inittesttree(struct testtree *out) {
    vmemset(out, 0, sizeof(*out));

    out->nodes[0].key = 50;
    out->bst.root = &out->nodes[0];

    out->nodes[1].key = 25;
    out->nodes[1].parent = &out->nodes[0];
    out->nodes[0].children[BST_DIR_LEFT] = &out->nodes[1];
    out->nodes[2].key = 75;
    out->nodes[2].parent = &out->nodes[0];
    out->nodes[0].children[BST_DIR_RIGHT] = &out->nodes[2];

    out->nodes[3].key = 12;
    out->nodes[3].parent = &out->nodes[1];
    out->nodes[1].children[BST_DIR_LEFT] = &out->nodes[3];
    out->nodes[4].key = 37;
    out->nodes[4].parent = &out->nodes[1];
    out->nodes[1].children[BST_DIR_RIGHT] = &out->nodes[4];

    out->nodes[5].key = 63;
    out->nodes[5].parent = &out->nodes[2];
    out->nodes[2].children[BST_DIR_LEFT] = &out->nodes[5];

    out->nodes[6].key = 69;
    out->nodes[6].parent = &out->nodes[5];
    out->nodes[5].children[BST_DIR_RIGHT] = &out->nodes[6];

    // Note that to calculate subtree height of node X, its children height must be there first.
    // We only need to recalculate on leaf nodes, because recalculating them will also recalculate
    // height of its parent nodes.
    bst_recalculate_height(&out->nodes[6]);
    bst_recalculate_height(&out->nodes[5]);
    bst_recalculate_height(&out->nodes[4]);
    bst_recalculate_height(&out->nodes[3]);
    bst_recalculate_bf_tree(&out->bst);
}

static bool do_removenode_unbalenced(void) {
    struct testtree tree;
    /*
     * Remove a terminal node
     *        50
     *      /    \
     *     /      \
     *    25      75
     *   /  \     /
     *  12  37  63
     *
     *             69 <--- Removed
     */
    inittesttree(&tree);
    bst_remove_node_unbalenced(&tree.bst, ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 69)));
    TEST_EXPECT(bst_find_node(&tree.bst, 69) == nullptr);
    struct bst_node *node63 = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 63));
    TEST_EXPECT(node63->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node63->children[BST_DIR_RIGHT] == nullptr);

    /*
     * Reset tree, and then remove a node with right child.
     *         50
     *      /      \
     *     /        \
     *    25        75
     *   /  \       /
     *  12  37  63 |
     *          ^  |
     *          |  69
     *          +-------- Removed
     */
    inittesttree(&tree);
    bst_remove_node_unbalenced(&tree.bst, ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 63)));
    TEST_EXPECT(bst_find_node(&tree.bst, 63) == nullptr);
    struct bst_node *node75 = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 75));
    struct bst_node *node69 = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 69));
    TEST_EXPECT(node75->children[BST_DIR_LEFT] == node69);
    TEST_EXPECT(node75->children[BST_DIR_RIGHT] == nullptr);
    TEST_EXPECT(node69->parent == node75);
    TEST_EXPECT(node69->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node69->children[BST_DIR_RIGHT] == nullptr);
    /*
     * Remove a node with left child.
     *       50
     *      /  \
     *     /    |
     *    25    |  75 <-- Removed
     *   /  \   |
     *  12  37  69
     */
    bst_remove_node_unbalenced(&tree.bst, ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 75)));
    TEST_EXPECT(bst_find_node(&tree.bst, 75) == nullptr);
    struct bst_node *node25 = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 25));
    struct bst_node *node50 = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 50));
    TEST_EXPECT(node50->children[BST_DIR_LEFT] == node25);
    TEST_EXPECT(node50->children[BST_DIR_RIGHT] == node69);
    TEST_EXPECT(node69->parent == node50);
    TEST_EXPECT(node69->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(node69->children[BST_DIR_RIGHT] == nullptr);
    return true;
}

static bool do_findnode(void) {
    struct testtree tree;
    inittesttree(&tree);

    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 12))->key == 12);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 37))->key == 37);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 25))->key == 25);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 50))->key == 50);
    TEST_EXPECT(bst_find_node(&tree.bst, 100) == nullptr);

    return true;
}

static bool do_minmaxof(void) {
    struct testtree tree;
    inittesttree(&tree);

    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_min_of_tree(&tree.bst))->key == 12);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_max_of_tree(&tree.bst))->key == 75);

    return true;
}

static bool do_dirinparent(void) {
    struct testtree tree;
    inittesttree(&tree);

    TEST_EXPECT(bst_dir_in_parent(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 25))) == BST_DIR_LEFT);
    TEST_EXPECT(bst_dir_in_parent(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 75))) == BST_DIR_RIGHT);
    TEST_EXPECT(bst_dir_in_parent(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 12))) == BST_DIR_LEFT);
    TEST_EXPECT(bst_dir_in_parent(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 37))) == BST_DIR_RIGHT);

    return true;
}

static bool do_successor(void) {
    struct testtree tree;
    inittesttree(&tree);

    struct bst_node *node = ASSERT_NONNULL_BSTNODE(bst_min_of_tree(&tree.bst));
    TEST_EXPECT(node->key == 12);
    node = ASSERT_NONNULL_BSTNODE(bst_successor(node));
    TEST_EXPECT(node->key == 25);
    node = ASSERT_NONNULL_BSTNODE(bst_successor(node));
    TEST_EXPECT(node->key == 37);
    node = ASSERT_NONNULL_BSTNODE(bst_successor(node));
    TEST_EXPECT(node->key == 50);
    node = ASSERT_NONNULL_BSTNODE(bst_successor(node));
    TEST_EXPECT(node->key == 63);
    node = ASSERT_NONNULL_BSTNODE(bst_successor(node));
    TEST_EXPECT(node->key == 69);
    node = ASSERT_NONNULL_BSTNODE(bst_successor(node));
    TEST_EXPECT(node->key == 75);
    node = bst_successor(node);
    TEST_EXPECT(node == nullptr);

    return true;
}

static bool do_predecessor(void) {
    struct testtree tree;
    inittesttree(&tree);

    struct bst_node *node = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 69));
    TEST_EXPECT(node->key == 69);
    node = ASSERT_NONNULL_BSTNODE(bst_predecessor(node));
    TEST_EXPECT(node->key == 63);
    node = ASSERT_NONNULL_BSTNODE(bst_predecessor(node));
    TEST_EXPECT(node->key == 50);
    node = ASSERT_NONNULL_BSTNODE(bst_predecessor(node));
    TEST_EXPECT(node->key == 37);
    node = ASSERT_NONNULL_BSTNODE(bst_predecessor(node));
    TEST_EXPECT(node->key == 25);
    node = ASSERT_NONNULL_BSTNODE(bst_predecessor(node));
    TEST_EXPECT(node->key == 12);
    node = bst_predecessor(node);
    TEST_EXPECT(node == nullptr);

    return true;
}

static bool do_rotate(void) {
    struct testtree tree;
    inittesttree(&tree);

    struct bst_node *subtreeroot = ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 75));
    bst_rotate(&tree.bst, subtreeroot, BST_DIR_RIGHT);
    /*
     * Rotation result should look like this
     *
     *        50
     *      /    \
     *     /      \
     *    25      63 <---- New subtree root
     *   /  \       \
     *  12  37      75 <- Original subtree root
     *              /
     *             69
     */
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(subtreeroot->children[BST_DIR_LEFT])->key == 69);
    TEST_EXPECT(subtreeroot->children[BST_DIR_RIGHT] == nullptr);
    struct bst_node *newsubtreeroot = ASSERT_NONNULL_BSTNODE(subtreeroot->parent);
    TEST_EXPECT(newsubtreeroot->key == 63);
    TEST_EXPECT(newsubtreeroot->children[BST_DIR_LEFT] == nullptr);
    TEST_EXPECT(newsubtreeroot->children[BST_DIR_RIGHT] == subtreeroot);

    // Let's rotate back to initial state
    subtreeroot = newsubtreeroot;
    bst_rotate(&tree.bst, subtreeroot, BST_DIR_LEFT);
    TEST_EXPECT(subtreeroot->children[BST_DIR_LEFT] == nullptr)
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(subtreeroot->children[BST_DIR_RIGHT])->key == 69);
    newsubtreeroot = ASSERT_NONNULL_BSTNODE(subtreeroot->parent);
    TEST_EXPECT(newsubtreeroot->key == 75);
    TEST_EXPECT(newsubtreeroot->children[BST_DIR_LEFT] == subtreeroot);
    TEST_EXPECT(newsubtreeroot->children[BST_DIR_RIGHT] == nullptr);

    // Rotate on the root
    subtreeroot = tree.bst.root;
    bst_rotate(&tree.bst, subtreeroot, BST_DIR_LEFT);
    /*
     *        75 <- New subtree root
     *       /
     *      50 <--- Original subtree root
     *     /  \
     *    25  63
     *   /  \   \
     *  12  37   69
     */
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(subtreeroot->children[BST_DIR_LEFT])->key == 25);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(subtreeroot->children[BST_DIR_RIGHT])->key == 63);
    newsubtreeroot = ASSERT_NONNULL_BSTNODE(subtreeroot->parent);
    TEST_EXPECT(newsubtreeroot == tree.bst.root);
    TEST_EXPECT(newsubtreeroot->key == 75);
    TEST_EXPECT(newsubtreeroot->children[BST_DIR_LEFT] == subtreeroot);
    TEST_EXPECT(newsubtreeroot->children[BST_DIR_RIGHT] == nullptr);

    return true;
}

static bool do_height(void) {
    struct testtree tree;
    inittesttree(&tree);

    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 25))->height == 1);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 75))->height == 2);
    TEST_EXPECT(ASSERT_NONNULL_BSTNODE(bst_find_node(&tree.bst, 50))->height == 3);

    return true;
}

static struct test const TESTS[] = {
    {.name = "insert node unbalenced", .fn = do_insertnode_unbalenced},
    {.name = "remove node unbalenced", .fn = do_removenode_unbalenced},
    {.name = "insert node & balencing", .fn = do_balencing},
    {.name = "find node", .fn = do_findnode},
    {.name = "minimum, maximum node", .fn = do_minmaxof},
    {.name = "child direction in parent ", .fn = do_dirinparent},
    {.name = "successor", .fn = do_successor},
    {.name = "predecessor", .fn = do_predecessor},
    {.name = "rotate", .fn = do_rotate},
    {.name = "height", .fn = do_height},
};

const struct test_group TESTGROUP_BST = {
    .name = "bst",
    .tests = TESTS,
    .testslen = sizeof(TESTS) / sizeof(*TESTS),
};
