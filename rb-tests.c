#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rbtree.h"

#define MAX_NODES 256

static rb_t test_rbtree;

static rb_node_t nodes[MAX_NODES];

/* Bit is set if node is in the tree */
static unsigned int node_mask[(MAX_NODES + 31) / 32];

/* Array of nodes dumped via traversal */
static rb_node_t *walked_nodes[MAX_NODES];

/* Node currently being inserted, for testing 'cmp' argument order */
static rb_node_t *current_insertee;

void set_node_mask(int node, int val)
{
    unsigned int *p = &node_mask[node / 32];
    unsigned int bit = 1u << (node % 32);

    *p &= ~bit;
    *p |= val ? bit : 0;
}

int get_node_mask(int node)
{
    const unsigned int *p = &node_mask[node / 32];
    unsigned int bit = 1u << (node % 32);

    return !!(*p & bit);
}

int node_index(const rb_node_t *n)
{
    return (int) (n - &nodes[0]);
}

/* Our "lessthan" is just the location of the struct */
static bool node_lessthan(const rb_node_t *a, const rb_node_t *b)
{
    if (current_insertee) {
        assert(a == current_insertee);
        assert(b != current_insertee);
    }

    return a < b;
}

/* Simple Linear Congruential Random Number Generator (LCRNG) with a modulus of
 * 2^64, adapted from:
 * https://nuclear.llnl.gov/CNP/rng/rngman/node4.html
 *
 * The goal is to maintain repeatability across platforms, with no strict
 * requirement for high-quality randomness.
 */
static unsigned int next_rand_mod(unsigned int mod)
{
    static unsigned long long state = 123456789; /* seed */

    state = state * 2862933555777941757ul + 3037000493ul;

    return ((unsigned int) (state >> 32)) % mod;
}

void visit_node(rb_node_t *node, void *cookie)
{
    int *nwalked = cookie;

    assert(*nwalked < MAX_NODES);

    walked_nodes[*nwalked] = node;
    *nwalked += 1;
}

/* Holds the black height of the most recently encountered leaf during
 * check_rb(), or zero if no leaves have been processed yet.
 */
static int last_black_height;

void check_rbnode(rb_node_t *node, int blacks_above)
{
    int side, bheight = blacks_above + __rb_is_black(node);

    for (side = 0; side < 2; side++) {
        rb_node_t *ch = __rb_child(node, side);

        if (ch) {
            /* Basic tree requirement */
            if (side == 0) {
                assert(node_lessthan(ch, node));
            } else {
                assert(node_lessthan(node, ch));
            }

            /* Can't have adjacent red nodes */
            assert(__rb_is_black(node) || __rb_is_black(ch));

            /* Recurse */
            check_rbnode(ch, bheight);
        } else {
            /* All leaf nodes must be at the same black height */
            if (last_black_height)
                assert(last_black_height == bheight);
            last_black_height = bheight;
        }
    }
}

void check_rb(void)
{
    last_black_height = 0;

    assert(test_rbtree.root);
    assert(__rb_is_black(test_rbtree.root));

    check_rbnode(test_rbtree.root, 0);
}

/* First validates the external API behavior via a walk, then checks
 * interior tree and red/black state via internal APIs.
 */
void check_tree(void)
{
    int nwalked = 0, i, ni;
    rb_node_t *n, *last = NULL;

    (void) memset(walked_nodes, 0, sizeof(walked_nodes));

    RB_FOREACH (&test_rbtree, n) {
        visit_node(n, &nwalked);
    }

    /* Make sure all found nodes are in-order and marked in the tree */
    for (i = 0; i < nwalked; i++) {
        n = walked_nodes[i];
        ni = node_index(n);

        if (last) {
            assert(node_lessthan(last, n));
        }

        assert(get_node_mask(ni));

        last = n;
    }

    /* Make sure all tree bits properly reflect the set of nodes we found */
    ni = 0;
    for (i = 0; i < MAX_NODES; i++) {
        assert(get_node_mask(i) == rb_contains(&test_rbtree, &nodes[i]));

        if (get_node_mask(i)) {
            assert(node_index(walked_nodes[ni]) == i);
            ni++;
        }
    }

    assert(ni == nwalked);

    if (test_rbtree.root) {
        check_rb();
    }
}

void test_tree(int size)
{
    /* Small trees get checked after every op, big trees less often */
    int small_tree = size <= 32;

    (void) memset(&test_rbtree, 0, sizeof(test_rbtree));
    test_rbtree.cmp_func = node_lessthan;
    (void) memset(nodes, 0, sizeof(nodes));
    (void) memset(node_mask, 0, sizeof(node_mask));

    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < size; i++) {
            int node = next_rand_mod(size);

            if (!get_node_mask(node)) {
                rb_insert(&test_rbtree, &nodes[node]);
                set_node_mask(node, 1);
            } else {
                rb_remove(&test_rbtree, &nodes[node]);
                set_node_mask(node, 0);
            }

            if (small_tree)
                check_tree();
        }

        if (!small_tree)
            check_tree();
    }
}

int main()
{
    /* rbtree_api */
    {
        printf("Testing basic red-black tree operations...\n");
        int size = 1;

        do {
            size += next_rand_mod(size) + 1;

            if (size > MAX_NODES)
                size = MAX_NODES;

            printf("  Checking trees built from %d nodes...\n", size);

            test_tree(size);
        } while (size < MAX_NODES);
        printf("[ OK ] Basic tree operations\n");
    }

    /* Test removing a node with abnormal color */
    {
        printf("Testing edge case: removing node with abnormal color...\n");
        rb_node_t temp = {0};

        /* Initialize a tree and insert it */
        (void) memset(&test_rbtree, 0, sizeof(test_rbtree));
        test_rbtree.cmp_func = node_lessthan;
        (void) memset(nodes, 0, sizeof(nodes));

        assert(rb_get_min(&test_rbtree) == NULL && "the tree is invalid");

        for (int i = 0; i < 8; i++)
            rb_insert(&test_rbtree, &nodes[i]);

        rb_remove(&test_rbtree, &temp);

        /* Check if tree's max and min node are expected */
        assert(rb_get_min(&test_rbtree) == &nodes[0] && "the tree is invalid");
        assert(rb_get_max(&test_rbtree) == &nodes[7] && "the tree is invalid");
        printf("[ OK ] Edge case handling\n");
    }

    /* Cached tree tests */
#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
    {
        printf("Testing cached red-black tree functionality...\n");

        /* Use a separate array to avoid conflicts with existing tests */
        static rb_node_t cached_nodes[MAX_NODES];
        static rb_cached_t cached_tree;

        /* Test basic cached operations */
        {
            printf("Testing cached tree basic operations... ");

            rb_cached_init(&cached_tree, node_lessthan);

            /* Test empty tree */
            assert(rb_cached_empty(&cached_tree));
            assert(rb_cached_get_min(&cached_tree) == NULL);
            assert(rb_cached_get_max(&cached_tree) == NULL);

            /* Insert some nodes */
            for (int i = 0; i < 10; i++)
                rb_cached_insert(&cached_tree, &cached_nodes[i]);

            /* Verify tree is not empty */
            assert(!rb_cached_empty(&cached_tree));

            /* Test min/max caching */
            rb_node_t *min = rb_cached_get_min(&cached_tree);
            rb_node_t *max = rb_cached_get_max(&cached_tree);

            /* Since cached_nodes[0] has the lowest address, it should be
             * minimum */
            assert(min == &cached_nodes[0]);
            assert(max == &cached_nodes[9]);

            /* Test traversal */
            int count = 0;
            rb_node_t *node;
            RB_CACHED_FOREACH (&cached_tree, node) {
                count++;
            }
            assert(count == 10);

            /* Test removal of minimum */
            rb_cached_remove(&cached_tree, &cached_nodes[0]);
            min = rb_cached_get_min(&cached_tree);
            assert(min == &cached_nodes[1]); /* Next lowest address */

            /* Test removal of maximum */
            rb_cached_remove(&cached_tree, &cached_nodes[9]);
            max = rb_cached_get_max(&cached_tree);
            assert(max == &cached_nodes[8]); /* Next highest address */

            /* Clean up remaining nodes */
            for (int i = 1; i < 9; i++)
                rb_cached_remove(&cached_tree, &cached_nodes[i]);

            assert(rb_cached_empty(&cached_tree));
            assert(rb_cached_get_min(&cached_tree) == NULL);
            assert(rb_cached_get_max(&cached_tree) == NULL);

            printf("[ OK ]\n");
        }

        /* Test cached vs standard consistency */
        {
            printf("Testing cached vs standard tree consistency... ");

            rb_t standard_tree = {
                .root = NULL, .cmp_func = node_lessthan, .max_depth = 0};
            rb_cached_init(&cached_tree, node_lessthan);

            /* Insert same nodes in both trees */
            for (int i = 0; i < 20; i++) {
                rb_insert(&standard_tree, &nodes[i]);
                rb_cached_insert(&cached_tree, &cached_nodes[i + 20]);
            }

            /* Compare min/max results */
            rb_node_t *std_min = rb_get_min(&standard_tree);
            rb_node_t *std_max = rb_get_max(&standard_tree);
            rb_node_t *cached_min = rb_cached_get_min(&cached_tree);
            rb_node_t *cached_max = rb_cached_get_max(&cached_tree);

            /* They should have the same relative positions */
            assert(node_index(std_min) == 0);
            assert(node_index(std_max) == 19);
            assert((cached_min - &cached_nodes[0]) == 20);
            assert((cached_max - &cached_nodes[0]) == 39);

            /* Test traversal order consistency */
            int std_count = 0, cached_count = 0;
            rb_node_t *node;

            RB_FOREACH (&standard_tree, node) {
                std_count++;
            }

            RB_CACHED_FOREACH (&cached_tree, node) {
                cached_count++;
            }

            assert(std_count == cached_count);
            assert(std_count == 20);

            printf("[ OK ]\n");
        }

        /* Test cache consistency during rebalancing */
        {
            printf("Testing cache consistency during rebalancing... ");

            rb_cached_init(&cached_tree, node_lessthan);

            /* Insert enough nodes to trigger multiple rebalancing operations */
            for (int i = 0; i < 50; i++) {
                rb_cached_insert(&cached_tree, &cached_nodes[i]);

                /* Verify cache consistency after each insertion */
                rb_node_t *expected_min =
                    __rb_get_minmax(&cached_tree.rb_root, RB_LEFT);
                rb_node_t *expected_max =
                    __rb_get_minmax(&cached_tree.rb_root, RB_RIGHT);

                assert(rb_cached_get_min(&cached_tree) == expected_min);
                assert(rb_cached_get_max(&cached_tree) == expected_max);
            }

            /* Remove nodes in various patterns */
            for (int i = 49; i >= 25; i--) {
                rb_cached_remove(&cached_tree, &cached_nodes[i]);

                /* Verify cache consistency after each removal */
                rb_node_t *expected_min =
                    __rb_get_minmax(&cached_tree.rb_root, RB_LEFT);
                rb_node_t *expected_max =
                    __rb_get_minmax(&cached_tree.rb_root, RB_RIGHT);

                assert(rb_cached_get_min(&cached_tree) == expected_min);
                assert(rb_cached_get_max(&cached_tree) == expected_max);
            }

            /* Clean up */
            for (int i = 0; i < 25; i++)
                rb_cached_remove(&cached_tree, &cached_nodes[i]);

            printf("[ OK ]\n");
        }

        printf("Cached tree functionality [ OK ]\n");
    }
#endif

    return 0;
}
