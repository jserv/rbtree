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

#define BIT(n) (1 << (n))

/* Compute the floor of log2 for a compile-time constant.
 * @n: The input value (32-bit unsigned integer).
 *
 * This function-like macro calculates the integer floor of log2 for a 32-bit
 * unsigned integer. It should only be used with compile-time constants when
 * the input value is known during preprocessing.
 */
#define ilog2_compile_time(n)            \
    (((n) < 2)                      ? 0  \
     : (((n) & BIT(31)) == BIT(31)) ? 31 \
     : (((n) & BIT(30)) == BIT(30)) ? 30 \
     : (((n) & BIT(29)) == BIT(29)) ? 29 \
     : (((n) & BIT(28)) == BIT(28)) ? 28 \
     : (((n) & BIT(27)) == BIT(27)) ? 27 \
     : (((n) & BIT(26)) == BIT(26)) ? 26 \
     : (((n) & BIT(25)) == BIT(25)) ? 25 \
     : (((n) & BIT(24)) == BIT(24)) ? 24 \
     : (((n) & BIT(23)) == BIT(23)) ? 23 \
     : (((n) & BIT(22)) == BIT(22)) ? 22 \
     : (((n) & BIT(21)) == BIT(21)) ? 21 \
     : (((n) & BIT(20)) == BIT(20)) ? 20 \
     : (((n) & BIT(19)) == BIT(19)) ? 19 \
     : (((n) & BIT(18)) == BIT(18)) ? 18 \
     : (((n) & BIT(17)) == BIT(17)) ? 17 \
     : (((n) & BIT(16)) == BIT(16)) ? 16 \
     : (((n) & BIT(15)) == BIT(15)) ? 15 \
     : (((n) & BIT(14)) == BIT(14)) ? 14 \
     : (((n) & BIT(13)) == BIT(13)) ? 13 \
     : (((n) & BIT(12)) == BIT(12)) ? 12 \
     : (((n) & BIT(11)) == BIT(11)) ? 11 \
     : (((n) & BIT(10)) == BIT(10)) ? 10 \
     : (((n) & BIT(9)) == BIT(9))   ? 9  \
     : (((n) & BIT(8)) == BIT(8))   ? 8  \
     : (((n) & BIT(7)) == BIT(7))   ? 7  \
     : (((n) & BIT(6)) == BIT(6))   ? 6  \
     : (((n) & BIT(5)) == BIT(5))   ? 5  \
     : (((n) & BIT(4)) == BIT(4))   ? 4  \
     : (((n) & BIT(3)) == BIT(3))   ? 3  \
     : (((n) & BIT(2)) == BIT(2))   ? 2  \
                                    : 1)

static const uint32_t dlog_N = 2 * ilog2_compile_time(TREE_SIZE);

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
    /* Verify if the 'rb_node_t' structure is embedded within a user-defined
     * struct. This test initializes an 'rb_t' and checks two features:
     * 1. 'rb_node_t' structure can be embedded in any user-defined struct.
     * 2. 'rb_t' can be traversed using macro-based APIs.
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
