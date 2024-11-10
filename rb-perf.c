#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#include "rbtree.h"

#define TREE_SIZE 4096
/* dlog_N = 2log(TREE_SIZE) */
const static uint32_t dlog_N = 24;

/* rbnode structure is embeddable in user structure */
struct container_node {
    struct rbnode node;
    int value;
};
static struct rbnode nodes[TREE_SIZE];
static struct rbtree test_rbtree;

/* Our "lessthan" is just the location of the struct */
bool node_lessthan(struct rbnode *a, struct rbnode *b)
{
    return a < b;
}

/* initialize and insert a tree */
static void init_tree(struct rbtree *tree, int size)
{
    tree->lessthan_fn = node_lessthan;

    for (int i = 0; i < size; i++)
        rb_insert(tree, &nodes[i]);
}

static int search_height_recurse(struct rbnode *node,
                                 struct rbnode *final_node,
                                 uint32_t current_height)
{
    if (!node)
        return -1;

    if (node == final_node)
        return current_height;

    current_height++;
    struct rbnode *ch =
        __rb_child(node, !test_rbtree.lessthan_fn(final_node, node));

    return search_height_recurse(ch, final_node, current_height);
}

static void verify_rbtree_perf(struct rbnode *root, struct rbnode *test)
{
    uint32_t node_height = 0;

    node_height = search_height_recurse(root, test, node_height);
    assert(node_height <= dlog_N);
}

int main()
{
    /*
     * Test whether rbtree node struct is embedded
     * in any user struct
     *
     * Define and initialize a rbtree, and test two features:
     * first, rbtree node struct can be embedded in any user struct.
     * last, rbtree can be walked though by some macro APIs.
     */
    int count = 0;
    struct rbtree test_tree_l;
    struct container_node *c_foreach_node;
    struct rbnode *foreach_node;
    struct container_node tree_node[10];

    (void) memset(&test_tree_l, 0, sizeof(test_tree_l));
    (void) memset(tree_node, 0, sizeof(tree_node));

    test_tree_l.lessthan_fn = node_lessthan;
    for (uint32_t i = 0; i < ARRAY_SIZE(tree_node); i++) {
        tree_node[i].value = i;
        rb_insert(&test_tree_l, &tree_node[i].node);
    }

    RB_FOREACH (&test_tree_l, foreach_node) {
        assert(container_of(foreach_node, struct container_node, node)->value ==
                   count &&
               "RB_FOREACH failed");
        count++;
    }

    count = 0;

    RB_FOREACH_CONTAINER (&test_tree_l, c_foreach_node, node) {
        assert(c_foreach_node->value == count && "RB_FOREACH_CONTAINER failed");
        count++;
    }

    /* Test some operations of rbtree are running in logarithmic time */
    /* The inserted, removed, get minimum and get maximum operations
     * of rbtree are in logarithmic time by comparing a node's operation
     * height with the worst-case's height.
     */
    init_tree(&test_rbtree, TREE_SIZE);
    struct rbnode *root = test_rbtree.root;
    struct rbnode *test = NULL;

    test = rb_get_min(&test_rbtree);
    verify_rbtree_perf(root, test);

    test = rb_get_max(&test_rbtree);
    verify_rbtree_perf(root, test);

    /* insert and remove a same node with same height.Assume that
     * the node nodes[TREE_SIZE/2] will be removed and inserted,
     * verify that searching times is less than 2logN
     * using the height of this node.
     */
    test = &nodes[TREE_SIZE / 2];
    verify_rbtree_perf(root, test);
}
