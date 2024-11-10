#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rbtree.h"

#define MAX_NODES 256

static struct rbtree test_rbtree;

static struct rbnode nodes[MAX_NODES];

/* Bit is set if node is in the tree */
static unsigned int node_mask[(MAX_NODES + 31) / 32];

/* Array of nodes dumped via traversal */
static struct rbnode *walked_nodes[MAX_NODES];

/* Node currently being inserted, for testing lessthan() argument order */
static struct rbnode *current_insertee;

void set_node_mask(int node, int val)
{
    unsigned int *p = &node_mask[node / 32];
    unsigned int bit = 1u << (node % 32);

    *p &= ~bit;
    *p |= val ? bit : 0;
}

int get_node_mask(int node)
{
    unsigned int *p = &node_mask[node / 32];
    unsigned int bit = 1u << (node % 32);

    return !!(*p & bit);
}

int node_index(struct rbnode *n)
{
    return (int) (n - &nodes[0]);
}

/* Our "lessthan" is just the location of the struct */
bool node_lessthan(struct rbnode *a, struct rbnode *b)
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

void visit_node(struct rbnode *node, void *cookie)
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

void check_rbnode(struct rbnode *node, int blacks_above)
{
    int side, bheight = blacks_above + __rb_is_black(node);

    for (side = 0; side < 2; side++) {
        struct rbnode *ch = __rb_child(node, side);

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
    struct rbnode *n, *last = NULL;

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
    test_rbtree.lessthan_fn = node_lessthan;
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
        int size = 1;

        do {
            size += next_rand_mod(size) + 1;

            if (size > MAX_NODES)
                size = MAX_NODES;

            printf("Checking trees built from %d nodes...\n", size);

            test_tree(size);
        } while (size < MAX_NODES);
        printf("[ OK ]\n");
    }

    /* Test removing a node with abnormal color */
    {
        printf("Removing a node with abnormal color...\n");
        struct rbnode temp = {0};

        /* Initialize a tree and insert it */
        (void) memset(&test_rbtree, 0, sizeof(test_rbtree));
        test_rbtree.lessthan_fn = node_lessthan;
        (void) memset(nodes, 0, sizeof(nodes));

        assert(rb_get_min(&test_rbtree) == NULL && "the tree is invalid");

        for (int i = 0; i < 8; i++)
            rb_insert(&test_rbtree, &nodes[i]);

        rb_remove(&test_rbtree, &temp);

        /* Check if tree's max and min node are expected */
        assert(rb_get_min(&test_rbtree) == &nodes[0] && "the tree is invalid");
        assert(rb_get_max(&test_rbtree) == &nodes[7] && "the tree is invalid");
        printf("[ OK ]\n");
    }

    return 0;
}
