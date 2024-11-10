#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "rbtree.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define TREE_SIZE 4096
/* dlog_N = 2log(TREE_SIZE) */
static const uint32_t dlog_N = 24;

/* rbnode structure is embeddable in user structure */
struct container_node {
    rb_node_t node;
    int value;
};
static rb_node_t nodes[TREE_SIZE];
static rb_t test_rbtree;

/* Our "lessthan" is just the location of the struct */
static bool node_lessthan(rb_node_t *a, rb_node_t *b)
{
    return a < b;
}

/* initialize and insert a tree */
static void init_tree(rb_t *tree, int size)
{
    tree->cmp_func = node_lessthan;

    for (int i = 0; i < size; i++)
        rb_insert(tree, &nodes[i]);
}

static int search_height_recurse(rb_node_t *node,
                                 rb_node_t *final_node,
                                 uint32_t current_height)
{
    if (!node)
        return -1;

    if (node == final_node)
        return current_height;

    current_height++;
    rb_node_t *ch = __rb_child(node, !test_rbtree.cmp_func(final_node, node));

    return search_height_recurse(ch, final_node, current_height);
}

static void verify_rbtree_perf(rb_node_t *root, rb_node_t *test)
{
    uint32_t node_height = 0;

    node_height = search_height_recurse(root, test, node_height);
    assert(node_height <= dlog_N);
}

int main()
{
    /* Verify if the 'rbnode' structure is embedded within a user-defined
     * struct. This test initializes an 'rbtree' and checks two features:
     * 1. The 'rbnode' structure can be embedded in any user-defined struct.
     * 2. The 'rbtree' can be traversed using macro-based APIs.
     */
    int count = 0;
    rb_t test_tree_l;
    struct container_node *c_foreach_node;
    rb_node_t *foreach_node;
    struct container_node tree_node[10];

    (void) memset(&test_tree_l, 0, sizeof(test_tree_l));
    (void) memset(tree_node, 0, sizeof(tree_node));

    test_tree_l.cmp_func = node_lessthan;
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

    struct timespec start, end;
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    assert(err == 0);

    /* The insert, remove, get minimum, and get maximum operations in the
     * 'rbtree' have logarithmic time complexity, determined by comparing the
     * height of the node's operation with the worst-case tree height.
     */
    init_tree(&test_rbtree, TREE_SIZE);
    rb_node_t *root = test_rbtree.root;
    rb_node_t *test = NULL;

    test = rb_get_min(&test_rbtree);
    verify_rbtree_perf(root, test);

    test = rb_get_max(&test_rbtree);
    verify_rbtree_perf(root, test);

    /* Insert and remove the same node with an unchanged height.
     * Assume the node nodes[TREE_SIZE/2] will be removed and reinserted.
     * Verify that the search time is less than 2 * log(N) based on the height
     * of this node.
     */
    test = &nodes[TREE_SIZE / 2];
    verify_rbtree_perf(root, test);

    err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
    assert(err == 0);

    double elapsed = (double) (end.tv_sec - start.tv_sec) +
                     (double) (end.tv_nsec - start.tv_nsec) * 1e-9;

    struct rusage usage;
    err = getrusage(RUSAGE_SELF, &usage);
    assert(err == 0);

    /* Dump both machine and human readable versions */
    printf(
        "Operations performed on a red-black tree with %d nodes. Max RSS: %lu, "
        "~%.3f µs per "
        "loop\n",
        TREE_SIZE, usage.ru_maxrss, elapsed / (double) TREE_SIZE * 1e6);
    return 0;
}
