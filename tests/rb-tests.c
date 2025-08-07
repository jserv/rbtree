#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rbtree.h"


#define MAX_NODES 256

/* ANSI color codes */
#define COLOR_GREEN "\033[32m"
#define COLOR_RESET "\033[0m"
#define STEP_INTERVAL_MS 400000 /* 0.4 s */

/* Test message handling macros */
#define TEST_OK_MSG "[ " COLOR_GREEN "OK" COLOR_RESET " ]"
#define print_test_ok() printf(TEST_OK_MSG "\n")
#define print_test_start(msg)          \
    do {                               \
        printf("Testing " msg "... "); \
        fflush(stdout);                \
    } while (0)
#define print_test_progress(msg) \
    do {                         \
        printf("\r" msg);        \
        fflush(stdout);          \
    } while (0)
#define print_test_complete(msg) printf("\r\033[2K" msg "... " TEST_OK_MSG "\n")

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

#if _RB_ENABLE_PROPERTY_VALIDATION
/* Helper function to validate tree using property-based testing */
static void validate_tree_properties(int expected_nodes)
{
    rb_validation_t validation = rb_validate_tree(&test_rbtree);
    if (!validation.valid) {
        printf("ERROR: Property-based validation failed!\n");
        rb_print_validation_report(&validation);
        assert(0 && "Property-based validation detected tree corruption");
    }

    /* Verify node count matches expectation */
    if ((int) validation.node_count != expected_nodes) {
        printf("ERROR: Node count mismatch - expected: %d, validation: %zu\n",
               expected_nodes, validation.node_count);
        assert(0);
    }
}
#else
#define validate_tree_properties(expected_nodes) ((void) 0)
#endif

#if _RB_ENABLE_PROPERTY_VALIDATION
/* Property-Based Testing with Dedicated Node Structure */

/* Test node structure for comprehensive property-based tests */
typedef struct property_test_node {
    rb_node_t rb_link;
    int key;
    int value;
} property_test_node_t;

/* Comparison function for property test nodes */
static bool property_test_node_cmp(const rb_node_t *a, const rb_node_t *b)
{
    const property_test_node_t *node_a =
        container_of(a, property_test_node_t, rb_link);
    const property_test_node_t *node_b =
        container_of(b, property_test_node_t, rb_link);
    return node_a->key < node_b->key;
}

/* Helper to create property test nodes */
static property_test_node_t *create_property_test_node(int key, int value)
{
    property_test_node_t *node = malloc(sizeof(property_test_node_t));
    if (node) {
        node->key = key;
        node->value = value;
        memset(&node->rb_link, 0, sizeof(rb_node_t));
    }
    return node;
}
#endif

#if _RB_ENABLE_PROPERTY_VALIDATION
/* Validate tree and assert it's correct */
static void assert_property_tree_valid(rb_t *tree, const char *operation)
{
    rb_validation_t result = rb_validate_tree(tree);
    if (!result.valid) {
        fprintf(stderr, "PROPERTY VALIDATION FAILED after %s:\n", operation);
        rb_print_validation_report(&result);
        assert(0 && "Property-based tree validation failed");
    }

    /* Verify all 5 fundamental properties are satisfied */
    assert(result.node_colors &&
           "Property 1: Every node is either red or black");
    assert(result.null_nodes_black &&
           "Property 2: All null nodes are considered black");
    assert(result.red_children_black &&
           "Property 3: A red node does not have a red child");
    assert(result.black_height_consistent &&
           "Property 4: All paths have same black height");
    assert(result.single_child_red &&
           "Property 5: Single children must be red");
    printf("\r\033[2K" "Validated after %s (nodes: %zu, black_height: %d)", operation, result.node_count, result.black_height);
    fflush(stdout);
    usleep(STEP_INTERVAL_MS); 
}
#else
#define assert_property_tree_valid(tree, operation) ((void) 0)
#endif

#if _RB_ENABLE_PROPERTY_VALIDATION && \
    (_RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE)
/* Validate cached tree and assert it's correct */
static void assert_property_cached_tree_valid(rb_cached_t *tree,
                                              const char *operation)
{
    rb_validation_t result = rb_validate_cached_tree(tree);
    if (!result.valid) {
        fprintf(stderr, "PROPERTY CACHED VALIDATION FAILED after %s:\n",
                operation);
        rb_print_validation_report(&result);
        assert(0 && "Property-based cached tree validation failed");
    }
    printf("\r\033[2K" "Validated after %s (nodes: %zu, black_height: %d)", operation,
           result.node_count, result.black_height);
    fflush(stdout);
    usleep(STEP_INTERVAL_MS); 
}
#else
#define assert_property_cached_tree_valid(tree, operation) ((void) 0)
#endif

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
            if (!node_lessthan(last, n)) {
                printf("ERROR: nodes out of order at position %d:\n", i);
                printf("last = %p (index %d)\n", last, node_index(last));
                printf("n    = %p (index %d)\n", n, node_index(n));
                printf("Full traversal order:\n");
                for (int j = 0; j < nwalked; j++) {
                    printf("  [%d] %p (index %d)\n", j, walked_nodes[j],
                           node_index(walked_nodes[j]));
                }
                fflush(stdout);
            }
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

    if (test_rbtree.root)
        check_rb();

    /* Additional property-based validation */
    validate_tree_properties(nwalked);
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

/* Comprehensive Randomized Testing Support Functions */

/* Test configuration */
#define TEST_NODES 25       /* Number of nodes per iteration */
#define TEST_ITERATIONS 100 /* Iterations for random tests */
#define TEST_SEED 42        /* Fixed seed for reproducible tests */

/* Magic number for corruption detection */
#define NODE_MAGIC 0x9823af7e

/* Fast 32-bit RNG - rapidhash variant
 * Reference: github.com/Nicoshev/rapidhash
 */

/**
 * Advance state and return a new 32-bit pseudo-random value
 *
 * @state : Pointer to RNG state (modified)
 * Return New pseudo-random 32-bit value
 */
static inline uint32_t rand_u32(uint32_t *state)
{
    *state += 0xe120fc15u;
    uint64_t tmp = (uint64_t) (*state) * 0x4a39b70d;
    uint32_t mix = (uint32_t) ((tmp >> 32) ^ tmp);
    tmp = (uint64_t) mix * 0x12fad5c9;
    return (uint32_t) ((tmp >> 32) ^ tmp);
}

/* Test node structure with corruption detection and metadata */
typedef struct test_node {
    uint32_t magic;    /* Corruption detection */
    rb_node_t rb_link; /* Red-black tree linkage */
    uint64_t key;      /* Ordering key */
    bool removed;      /* Track removal status */
} test_node_t;

/* Comparison function for test nodes */
static bool test_node_cmp(const rb_node_t *a, const rb_node_t *b)
{
    if (!a || !b)
        return false;

    const test_node_t *node_a = container_of(a, test_node_t, rb_link);
    const test_node_t *node_b = container_of(b, test_node_t, rb_link);

    /* Validate magic numbers */
    assert(node_a->magic == NODE_MAGIC);
    assert(node_b->magic == NODE_MAGIC);

    if (node_a->key != node_b->key)
        return node_a->key < node_b->key;

    /* Handle duplicates by pointer comparison for deterministic ordering */
    return (uintptr_t) node_a < (uintptr_t) node_b;
}

/* Initialize test node with magic number and metadata */
static void init_test_node(test_node_t *node, uint64_t key)
{
    memset(node, 0, sizeof(*node));
    node->magic = NODE_MAGIC;
    node->key = key;
    node->removed = false;
    node->rb_link.children[0] = NULL;
    node->rb_link.children[1] = NULL;
}

/* Count nodes in tree using traversal */
static size_t count_tree_nodes(rb_t *tree)
{
    size_t count = 0;
    rb_node_t *node;

    RB_FOREACH (tree, node) {
        test_node_t *test_node = container_of(node, test_node_t, rb_link);
        assert(test_node->magic == NODE_MAGIC);
        count++;
    }

    return count;
}

int main()
{
    /* rbtree_api */
    {
        print_test_start("Testing basic red-black tree operations");
        usleep(STEP_INTERVAL_MS); 
        int size = 1;

        do {
            size += next_rand_mod(size) + 1;

            if (size > MAX_NODES)
                size = MAX_NODES;
            printf("\r\033[2K" "Checking trees built from %d nodes... ", size);
            test_tree(size);
            printf(TEST_OK_MSG);
            fflush(stdout);
            usleep(STEP_INTERVAL_MS); 
        } while (size < MAX_NODES);
        print_test_complete("Testing basic red-black tree operations");
    }

    /* Test removing a node with abnormal color */
    {
        print_test_start("edge case: removing node with abnormal color");
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
        print_test_ok();
    }

    /* Cached tree tests */
#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
    {
        /* Use a separate array to avoid conflicts with existing tests */
        static rb_node_t cached_nodes[MAX_NODES];
        static rb_cached_t cached_tree;

        /* Test basic cached operations */
        {
            print_test_start("cached tree basic operations");

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

            print_test_ok();
        }

        /* Test cached vs standard consistency */
        {
            print_test_start("cached vs standard tree consistency");

            rb_t standard_tree = {.root = NULL,
                                  .cmp_func = node_lessthan
#if _RB_DISABLE_ALLOCA != 0
                                  ,
                                  .max_depth = 0
#endif
            };
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

            print_test_ok();
        }

        /* Test cache consistency during rebalancing */
        {
            print_test_start("cache consistency during rebalancing");

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

            print_test_ok();
        }

        /* Test rb_cached_contains() optimization */
        {
            print_test_start("rb_cached_contains() optimization");

            rb_cached_init(&cached_tree, node_lessthan);

            /* Test empty tree */
            assert(!rb_cached_contains(&cached_tree, &cached_nodes[0]));

            /* Insert nodes 10-19 (middle range) */
            for (int i = 10; i < 20; i++)
                rb_cached_insert(&cached_tree, &cached_nodes[i]);

            /* Test nodes in tree */
            for (int i = 10; i < 20; i++) {
                assert(rb_cached_contains(&cached_tree, &cached_nodes[i]));
                /* Should match regular contains */
                assert(rb_cached_contains(&cached_tree, &cached_nodes[i]) ==
                       rb_contains(&cached_tree.rb_root, &cached_nodes[i]));
            }

            /* Test nodes below minimum (should benefit from leftmost cache) */
            for (int i = 0; i < 10; i++) {
                assert(!rb_cached_contains(&cached_tree, &cached_nodes[i]));
                /* Should match regular contains */
                assert(rb_cached_contains(&cached_tree, &cached_nodes[i]) ==
                       rb_contains(&cached_tree.rb_root, &cached_nodes[i]));
            }

            /* Test nodes above maximum (should benefit from rightmost cache if
             * enabled) */
            for (int i = 20; i < 30; i++) {
                assert(!rb_cached_contains(&cached_tree, &cached_nodes[i]));
                /* Should match regular contains */
                assert(rb_cached_contains(&cached_tree, &cached_nodes[i]) ==
                       rb_contains(&cached_tree.rb_root, &cached_nodes[i]));
            }

            /* Test after removing minimum - cache should update */
            rb_node_t *old_min = rb_cached_get_min(&cached_tree);
            rb_cached_remove(&cached_tree, old_min);

            /* Old minimum should no longer be found */
            assert(!rb_cached_contains(&cached_tree, old_min));
            assert(!rb_contains(&cached_tree.rb_root, old_min));

            /* New minimum should still be found */
            rb_node_t *new_min = rb_cached_get_min(&cached_tree);
            assert(rb_cached_contains(&cached_tree, new_min));
            assert(rb_contains(&cached_tree.rb_root, new_min));

            /* Test after removing maximum - cache should update */
            rb_node_t *old_max = rb_cached_get_max(&cached_tree);
            rb_cached_remove(&cached_tree, old_max);

            /* Old maximum should no longer be found */
            assert(!rb_cached_contains(&cached_tree, old_max));
            assert(!rb_contains(&cached_tree.rb_root, old_max));

            /* New maximum should still be found */
            rb_node_t *new_max = rb_cached_get_max(&cached_tree);
            assert(rb_cached_contains(&cached_tree, new_max));
            assert(rb_contains(&cached_tree.rb_root, new_max));

            /* Stress test: verify consistency across many operations */
            for (int round = 0; round < 5; round++) {
                /* Add some random nodes */
                for (int i = 30 + round * 10; i < 40 + round * 10; i++)
                    rb_cached_insert(&cached_tree, &cached_nodes[i]);

                /* Test all nodes - cached and regular should always match */
                for (int i = 0; i < MAX_NODES; i++) {
                    bool cached_result =
                        rb_cached_contains(&cached_tree, &cached_nodes[i]);
                    bool regular_result =
                        rb_contains(&cached_tree.rb_root, &cached_nodes[i]);

                    if (cached_result != regular_result) {
                        printf(
                            "\nERROR: Mismatch for node %d (cached=%d, "
                            "regular=%d)\n",
                            i, cached_result, regular_result);
                        assert(false);
                    }
                }

                /* Remove some nodes */
                for (int i = 30 + round * 10; i < 35 + round * 10; i++) {
                    if (rb_cached_contains(&cached_tree, &cached_nodes[i]))
                        rb_cached_remove(&cached_tree, &cached_nodes[i]);
                }
            }

            /* Clean up remaining nodes */
            rb_node_t *node;
            while ((node = rb_cached_get_min(&cached_tree)))
                rb_cached_remove(&cached_tree, node);

            assert(rb_cached_empty(&cached_tree));
            assert(!rb_cached_contains(&cached_tree, &cached_nodes[0]));

            print_test_ok();
        }
    }
#endif

#if _RB_ENABLE_BATCH_OPS
    /* Test batch operations */
    {
        print_test_start("batch operations");

        /* Test basic batch operations */
        rb_batch_t batch;
        assert(rb_batch_init(&batch, 0) == 0);

        /* Test batch insertion into empty tree */
        rb_t batch_tree = {0};
        batch_tree.cmp_func = node_lessthan;

        /* Add nodes to batch */
        const int batch_size = 100;
        for (int i = 0; i < batch_size; i++)
            assert(rb_batch_add(&batch, &nodes[i]) == 0);

        /* Commit batch to empty tree */
        rb_batch_commit(&batch_tree, &batch);

        /* Verify tree structure */
        assert(batch_tree.root != NULL);

        /* Count nodes using traversal */
        int node_count = 0;
        rb_node_t *n;
        RB_FOREACH (&batch_tree, n) {
            node_count++;
        }
        assert(node_count == batch_size);

        /* Verify all nodes are present */
        for (int i = 0; i < batch_size; i++)
            assert(rb_contains(&batch_tree, &nodes[i]));

        /* Test batch insertion into non-empty tree */
        rb_t tree2 = {0};
        tree2.cmp_func = node_lessthan;

        /* Insert some initial nodes */
        for (int i = 0; i < 10; i++)
            rb_insert(&tree2, &nodes[i * 10]);

        /* Clear and reuse batch */
        batch.count = 0;

        /* Add different nodes to batch */
        for (int i = 100; i < 150; i++)
            assert(rb_batch_add(&batch, &nodes[i]) == 0);

        /* Commit to non-empty tree (should fall back to regular insertions) */
        rb_batch_commit(&tree2, &batch);

        /* Verify all nodes are present */
        node_count = 0;
        RB_FOREACH (&tree2, n) {
            node_count++;
        }
        assert(node_count == 60); /* 10 initial + 50 batch */

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
        /* Test batch with cached tree */
        rb_cached_t cached_batch_tree;
        rb_cached_init(&cached_batch_tree, node_lessthan);

        /* Clear batch */
        batch.count = 0;

        /* Add nodes to batch */
        for (int i = 150; i < 200; i++)
            assert(rb_batch_add(&batch, &nodes[i]) == 0);

        /* Commit to cached tree */
        rb_cached_batch_commit(&cached_batch_tree, &batch);

        /* Verify tree and cache */
        assert(cached_batch_tree.rb_root.root != NULL);
        node_count = 0;
        rb_node_t *cn;
        RB_CACHED_FOREACH (&cached_batch_tree, cn) {
            node_count++;
        }
        assert(node_count == 50);

#if _RB_ENABLE_LEFTMOST_CACHE
        rb_node_t *min = rb_cached_get_min(&cached_batch_tree);
        assert(min == &nodes[150]);
#endif

#if _RB_ENABLE_RIGHTMOST_CACHE
        rb_node_t *max = rb_cached_get_max(&cached_batch_tree);
        assert(max == &nodes[199]);
#endif
#endif /* cached tree */

        /* Test batch buffer growth */
        rb_batch_t small_batch;
        assert(rb_batch_init(&small_batch, 2) == 0); /* Start small */

        /* Add many nodes to force growth */
        for (int i = 200; i < 250; i++)
            assert(rb_batch_add(&small_batch, &nodes[i]) == 0);

        assert(small_batch.count == 50);
        assert(small_batch.capacity >= 50);

        /* Cleanup */
        rb_batch_destroy(&batch);
        rb_batch_destroy(&small_batch);

        print_test_ok();
    }
#endif /* _RB_ENABLE_BATCH_OPS */

#if _RB_ENABLE_PROPERTY_VALIDATION
    /* Property-Based Invariant Testing */
    {
        /* Test basic operations with property validation */
        {
            print_test_start("Testing basic operations with property validation");
            usleep(STEP_INTERVAL_MS); 

            rb_t prop_tree = {0};
            prop_tree.cmp_func = property_test_node_cmp;

            assert_property_tree_valid(&prop_tree, "initialization");

            /* Insert nodes in various patterns */
            property_test_node_t *prop_nodes[10];

            /* Sequential insertion */
            for (int i = 0; i < 5; i++) {
                prop_nodes[i] = create_property_test_node(i, i * 10);
                rb_insert(&prop_tree, &prop_nodes[i]->rb_link);
                if (i % 2 == 0) { /* Validate every other insertion */
                    assert_property_tree_valid(&prop_tree, "sequential insert");
                }
            }

            /* Reverse insertion */
            for (int i = 9; i >= 5; i--) {
                prop_nodes[i] = create_property_test_node(i, i * 10);
                rb_insert(&prop_tree, &prop_nodes[i]->rb_link);
                if (i % 2 == 1) { /* Validate every other insertion */
                    assert_property_tree_valid(&prop_tree, "reverse insert");
                }
            }

            /* Random deletion */
            int delete_order[] = {3, 7, 1, 9, 5};
            for (size_t i = 0;
                 i < sizeof(delete_order) / sizeof(delete_order[0]); i++) {
                int idx = delete_order[i];
                rb_remove(&prop_tree, &prop_nodes[idx]->rb_link);
                free(prop_nodes[idx]);
                assert_property_tree_valid(&prop_tree, "random delete");
            }

            /* Clean up remaining nodes */
            for (int i = 0; i < 10; i++) {
                if (i != 3 && i != 7 && i != 1 && i != 9 && i != 5) {
                    rb_remove(&prop_tree, &prop_nodes[i]->rb_link);
                    free(prop_nodes[i]);
                }
            }
            assert_property_tree_valid(&prop_tree, "cleanup");
        }
        print_test_complete("Testing basic operations with property validation");

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
        /* Test cached tree property validation */
        {
            print_test_start("Testing cached tree property validation");
            usleep(STEP_INTERVAL_MS); 

            rb_cached_t prop_cached_tree;
            rb_cached_init(&prop_cached_tree, property_test_node_cmp);

            assert_property_cached_tree_valid(&prop_cached_tree,
                                              "cached initialization");

            /* Insert nodes */
            property_test_node_t *cached_prop_nodes[8];
            int keys[] = {4, 2, 6, 1, 3, 5, 7, 0};

            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
                cached_prop_nodes[i] =
                    create_property_test_node(keys[i], keys[i] * 100);
                rb_cached_insert(&prop_cached_tree,
                                 &cached_prop_nodes[i]->rb_link);
                if (i % 3 == 0) { /* Validate every third insertion */
                    assert_property_cached_tree_valid(&prop_cached_tree,
                                                      "cached insert");
                }
            }

            /* Delete nodes affecting cache */
            rb_cached_remove(&prop_cached_tree,
                             &cached_prop_nodes[7]->rb_link); /* key 0 */
            free(cached_prop_nodes[7]);
            assert_property_cached_tree_valid(&prop_cached_tree,
                                              "delete minimum");

            rb_cached_remove(&prop_cached_tree,
                             &cached_prop_nodes[6]->rb_link); /* key 7 */
            free(cached_prop_nodes[6]);
            assert_property_cached_tree_valid(&prop_cached_tree,
                                              "delete maximum");

            /* Clean up */
            for (size_t i = 0; i < 6; i++) {
                rb_cached_remove(&prop_cached_tree,
                                 &cached_prop_nodes[i]->rb_link);
                free(cached_prop_nodes[i]);
            }

            assert_property_cached_tree_valid(&prop_cached_tree,
                                              "cached cleanup");
        }
        print_test_complete("Testing cached tree property validation");
#endif

        /* Stress test with property validation */
        {
            print_test_start("stress operations with property validation");

            rb_t stress_tree = {0};
            stress_tree.cmp_func = property_test_node_cmp;

            const int NUM_STRESS_NODES = 50;
            const int NUM_STRESS_OPERATIONS = 200;
            property_test_node_t *stress_nodes[NUM_STRESS_NODES];
            bool stress_inserted[NUM_STRESS_NODES];
            memset(stress_inserted, false, sizeof(stress_inserted));

            srand(42); /* Deterministic randomness for reproducible tests */

            /* Initialize nodes */
            for (int i = 0; i < NUM_STRESS_NODES; i++) {
                stress_nodes[i] = create_property_test_node(i, i * 13);
            }

            /* Perform random insert/delete operations */
            for (int op = 0; op < NUM_STRESS_OPERATIONS; op++) {
                int idx = rand() % NUM_STRESS_NODES;

                if (!stress_inserted[idx] && (rand() % 3) != 0) {
                    /* Insert operation (higher probability) */
                    rb_insert(&stress_tree, &stress_nodes[idx]->rb_link);
                    stress_inserted[idx] = true;

                    /* Validate every 20th operation for performance */
                    if (op % 20 == 0) {
                        rb_validation_t result = rb_validate_tree(&stress_tree);
                        if (!result.valid) {
                            fprintf(stderr,
                                    "Property validation failed at operation "
                                    "%d (insert %d)\n",
                                    op, idx);
                            rb_print_validation_report(&result);
                            assert(0);
                        }
                    }
                } else if (stress_inserted[idx]) {
                    /* Delete operation */
                    rb_remove(&stress_tree, &stress_nodes[idx]->rb_link);
                    stress_inserted[idx] = false;

                    /* Validate every 20th operation */
                    if (op % 20 == 0) {
                        rb_validation_t result = rb_validate_tree(&stress_tree);
                        if (!result.valid) {
                            fprintf(stderr,
                                    "Property validation failed at operation "
                                    "%d (delete %d)\n",
                                    op, idx);
                            rb_print_validation_report(&result);
                            assert(0);
                        }
                    }
                }
            }

            /* Final comprehensive validation */
            rb_validation_t final_result = rb_validate_tree(&stress_tree);
            if (!final_result.valid) {
                fprintf(stderr, "Final property validation failed!\n");
                rb_print_validation_report(&final_result);
                assert(0);
            }

            /* Clean up */
            for (int i = 0; i < NUM_STRESS_NODES; i++) {
                if (stress_inserted[i]) {
                    rb_remove(&stress_tree, &stress_nodes[i]->rb_link);
                }
                free(stress_nodes[i]);
            }

            print_test_ok();
        }

        /* Test explicit validation of the 5 fundamental properties */
        {
            print_test_start("explicit validation of the 5 RB properties");

            rb_t prop_tree = {0};
            prop_tree.cmp_func = property_test_node_cmp;

            /* Test empty tree - all properties should be satisfied */
            rb_validation_t empty_result = rb_validate_tree(&prop_tree);
            assert(empty_result.valid);
            assert(empty_result.node_colors); /* Property 1: Node colors */
            assert(empty_result
                       .null_nodes_black); /* Property 2: Null nodes black */
            assert(
                empty_result
                    .red_children_black); /* Property 3: Red children black */
            assert(empty_result
                       .black_height_consistent); /* Property 4: Black height */
            assert(empty_result
                       .single_child_red); /* Property 5: Single children red */

            /* Create a valid red-black tree and test all properties */
            property_test_node_t *nodes[7];
            int keys[] = {4, 2, 6, 1,
                          3, 5, 7}; /* Will create a balanced tree */

            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
                nodes[i] = create_property_test_node(keys[i], keys[i] * 10);
                rb_insert(&prop_tree, &nodes[i]->rb_link);
            }

            /* Validate all properties on the constructed tree */
            rb_validation_t tree_result = rb_validate_tree(&prop_tree);
            assert(tree_result.valid);

            /* Explicitly test each of the 5 fundamental properties */
            assert(tree_result.node_colors);
            /* Property 1: Every node is either red or black
             * This should always pass with our implementation */

            assert(tree_result.null_nodes_black);
            /* Property 2: All null nodes are considered black
             * Our implementation treats NULL children as black */

            assert(tree_result.red_children_black);
            /* Property 3: A red node does not have a red child
             * Our tree construction ensures this property */

            assert(tree_result.black_height_consistent);
            /* Property 4: Every path from a node to its leaves has same black
             * count This is the core balancing property */

            assert(tree_result.single_child_red);
            /* Property 5: If a node has exactly one child, the child must be
             * red This prevents black height violations */

            /* Test that black height is correctly calculated */
            assert(tree_result.black_height > 0);
            assert(tree_result.node_count == 7);

            /* Clean up */
            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
                rb_remove(&prop_tree, &nodes[i]->rb_link);
                free(nodes[i]);
            }

            print_test_ok();
        }

        /* Test validation error detection */
        {
            print_test_start("validation error detection");

            /* Test NULL tree validation */
            rb_validation_t null_result = rb_validate_tree(NULL);
            assert(!null_result.valid);
            assert(null_result.error_msg != NULL);
            assert(null_result.violation_property ==
                   0); /* Not a property violation */

            /* Test empty tree validation */
            rb_t empty_prop_tree = {0};
            empty_prop_tree.cmp_func = property_test_node_cmp;
            rb_validation_t empty_result = rb_validate_tree(&empty_prop_tree);
            assert(empty_result.valid);
            assert(empty_result.node_count == 0);
            /* All 5 properties should be satisfied for empty tree */
            assert(empty_result.node_colors);
            assert(empty_result.null_nodes_black);
            assert(empty_result.red_children_black);
            assert(empty_result.black_height_consistent);
            assert(empty_result.single_child_red);

            print_test_ok();
        }

        /* Test property validation descriptions and reporting */
        {
            print_test_start("property validation descriptions");

            /* Create a simple tree for testing property descriptions */
            rb_t desc_tree = {0};
            desc_tree.cmp_func = property_test_node_cmp;

            property_test_node_t *root_node = create_property_test_node(5, 50);
            property_test_node_t *left_node = create_property_test_node(3, 30);
            property_test_node_t *right_node = create_property_test_node(7, 70);

            rb_insert(&desc_tree, &root_node->rb_link);
            rb_insert(&desc_tree, &left_node->rb_link);
            rb_insert(&desc_tree, &right_node->rb_link);

            /* Validate the tree and check property descriptions are working */
            rb_validation_t desc_result = rb_validate_tree(&desc_tree);
            assert(desc_result.valid);

            /* Verify all property flags are correctly set */
            assert(desc_result.node_colors == true);
            assert(desc_result.null_nodes_black == true);
            assert(desc_result.red_children_black == true);
            assert(desc_result.black_height_consistent == true);
            assert(desc_result.single_child_red == true);

            /* Verify additional checks */
            assert(desc_result.root_is_black == true);
            assert(desc_result.bst_property == true);

            /* Clean up */
            rb_remove(&desc_tree, &left_node->rb_link);
            rb_remove(&desc_tree, &right_node->rb_link);
            rb_remove(&desc_tree, &root_node->rb_link);
            free(left_node);
            free(right_node);
            free(root_node);

            print_test_ok();
        }

        print_test_complete("Testing comprehensive property-based invariants");
    }
#endif /* _RB_ENABLE_PROPERTY_VALIDATION */

    /* Comprehensive Randomized Testing */
    {
        printf("Testing comprehensive randomized patterns...\n");

        /* Test statistics */
        struct {
            size_t nodes_inserted;
            size_t nodes_removed;
            size_t iterator_operations;
            size_t tree_operations;
        } test_stats = {0};

        /* Test 1: Sequential insertion and deletion */
        {
            print_test_start("sequential operations");

            rb_t tree;
            memset(&tree, 0, sizeof(tree));
            tree.cmp_func = test_node_cmp;

            test_node_t nodes[TEST_NODES];

            /* Sequential insertion (0, 1, 2, ..., n-1) */
            for (size_t i = 0; i < TEST_NODES; i++) {
                init_test_node(&nodes[i], i);
                rb_insert(&tree, &nodes[i].rb_link);
                test_stats.nodes_inserted++;
            }

            /* Validate final tree */
            size_t count = count_tree_nodes(&tree);
            assert(count == TEST_NODES);

            /* Sequential deletion (0, 1, 2, ..., n-1) */
            for (size_t i = 0; i < TEST_NODES; i++) {
                assert(rb_contains(&tree, &nodes[i].rb_link));
                rb_remove(&tree, &nodes[i].rb_link);
                nodes[i].removed = true;
                test_stats.nodes_removed++;

                /* Validate remaining count */
                count = count_tree_nodes(&tree);
                assert(count == TEST_NODES - i - 1);
            }

            assert(tree.root == NULL);
            print_test_ok();
        }

        /* Test 2: Reverse order operations */
        {
            print_test_start("reverse order operations");

            rb_t tree;
            memset(&tree, 0, sizeof(tree));
            tree.cmp_func = test_node_cmp;

            test_node_t nodes[TEST_NODES];

            /* Reverse insertion (n-1, n-2, ..., 1, 0) */
            for (size_t i = 0; i < TEST_NODES; i++) {
                size_t key = TEST_NODES - 1 - i;
                init_test_node(&nodes[i], key);
                rb_insert(&tree, &nodes[i].rb_link);
                test_stats.nodes_inserted++;
            }

            /* Validate tree */
            size_t count = count_tree_nodes(&tree);
            assert(count == TEST_NODES);

            /* Reverse deletion (n-1, n-2, ..., 1, 0) */
            for (size_t i = 0; i < TEST_NODES; i++) {
                assert(rb_contains(&tree, &nodes[i].rb_link));
                rb_remove(&tree, &nodes[i].rb_link);
                nodes[i].removed = true;
                test_stats.nodes_removed++;

                count = count_tree_nodes(&tree);
                assert(count == TEST_NODES - i - 1);
            }

            assert(tree.root == NULL);
            print_test_ok();
        }

        /* Test 3: Random operations with various removal patterns */
        {
            printf("Testing random operations (%d iterations)... ",
                   TEST_ITERATIONS);
            fflush(stdout);

            /* Initialize RNG with fixed seed for reproducible tests */
            uint32_t rng_state = TEST_SEED;

            for (size_t iteration = 0; iteration < TEST_ITERATIONS;
                 iteration++) {
                rb_t tree;
                memset(&tree, 0, sizeof(tree));
                tree.cmp_func = test_node_cmp;

                test_node_t nodes[TEST_NODES];
                uint64_t keys[TEST_NODES];

                /* Generate random keys */
                for (size_t i = 0; i < TEST_NODES; i++)
                    keys[i] = rand_u32(&rng_state) % (TEST_NODES * 2);

                /* Random insertion */
                for (size_t i = 0; i < TEST_NODES; i++) {
                    init_test_node(&nodes[i], keys[i]);
                    rb_insert(&tree, &nodes[i].rb_link);
                    test_stats.nodes_inserted++;
                }

                /* Validate final tree */
                size_t count = count_tree_nodes(&tree);
                assert(count == TEST_NODES);

                /* Various removal patterns based on iteration */
                switch (iteration % 3) {
                case 0: /* Forward removal */
                    for (size_t i = 0; i < TEST_NODES; i++) {
                        rb_remove(&tree, &nodes[i].rb_link);
                        nodes[i].removed = true;
                        test_stats.nodes_removed++;
                    }
                    break;

                case 1: /* Backward removal */
                    for (size_t i = TEST_NODES; i > 0; i--) {
                        rb_remove(&tree, &nodes[i - 1].rb_link);
                        nodes[i - 1].removed = true;
                        test_stats.nodes_removed++;
                    }
                    break;

                case 2: /* Random removal */
                    for (size_t remaining = TEST_NODES; remaining > 0;
                         remaining--) {
                        size_t idx = rand_u32(&rng_state) % remaining;

                        /* Find the idx-th non-removed node */
                        size_t current = 0;
                        for (size_t j = 0; j < TEST_NODES; j++) {
                            if (!nodes[j].removed) {
                                if (current == idx) {
                                    rb_remove(&tree, &nodes[j].rb_link);
                                    nodes[j].removed = true;
                                    test_stats.nodes_removed++;
                                    break;
                                }
                                current++;
                            }
                        }
                    }
                    break;
                }

                assert(tree.root == NULL);
                test_stats.tree_operations++;

                /* Reset removed flags for next iteration */
                for (size_t i = 0; i < TEST_NODES; i++)
                    nodes[i].removed = false;
            }

            print_test_ok();
        }

        /* Test 4: Iterator robustness */
        {
            print_test_start("iterator robustness");

            uint32_t rng_state = TEST_SEED;

            rb_t tree;
            memset(&tree, 0, sizeof(tree));
            tree.cmp_func = test_node_cmp;

            test_node_t nodes[TEST_NODES];

            /* Insert nodes with random keys */
            for (size_t i = 0; i < TEST_NODES; i++) {
                init_test_node(&nodes[i],
                               rand_u32(&rng_state) % (TEST_NODES * 2));
                rb_insert(&tree, &nodes[i].rb_link);
                test_stats.nodes_inserted++;
            }

            /* Test 1: Verify complete traversal */
            size_t visit_count = 0;
            rb_node_t *node;

            RB_FOREACH (&tree, node) {
                test_node_t *test_node =
                    container_of(node, test_node_t, rb_link);
                assert(test_node->magic == NODE_MAGIC);
                visit_count++;
            }

            assert(visit_count == TEST_NODES);
            test_stats.iterator_operations++;

            /* Test 2: Verify ordering consistency */
            test_node_t *prev = NULL;
            RB_FOREACH (&tree, node) {
                test_node_t *current = container_of(node, test_node_t, rb_link);

                if (prev != NULL) {
                    /* Verify strict ordering */
                    assert(test_node_cmp(&prev->rb_link, &current->rb_link) ||
                           prev->key == current->key);
                }

                prev = current;
            }

            test_stats.iterator_operations++;

            /* Test 3: Multiple concurrent traversals should be consistent */
            for (size_t test_round = 0; test_round < 5; test_round++) {
                size_t count1 = 0, count2 = 0;

                RB_FOREACH (&tree, node) {
                    count1++;
                }

                RB_FOREACH (&tree, node) {
                    count2++;
                }

                assert(count1 == count2);
                assert(count1 == count_tree_nodes(&tree));
            }

            test_stats.iterator_operations += 10;

            /* Clean up */
            for (size_t i = 0; i < TEST_NODES; i++) {
                rb_remove(&tree, &nodes[i].rb_link);
                test_stats.nodes_removed++;
            }

            assert(tree.root == NULL);
            print_test_ok();
        }

        /* Print test statistics */
        printf("Comprehensive test statistics:\n");
        printf("  - Nodes inserted:      %zu\n", test_stats.nodes_inserted);
        printf("  - Nodes removed:       %zu\n", test_stats.nodes_removed);
        printf("  - Iterator operations: %zu\n",
               test_stats.iterator_operations);
        printf("  - Tree operations:     %zu\n", test_stats.tree_operations);
        printf("  - Total operations:    %zu\n",
               test_stats.nodes_inserted + test_stats.nodes_removed +
                   test_stats.iterator_operations);

        print_test_complete("Testing comprehensive randomized patterns");

#undef TEST_NODES
#undef TEST_ITERATIONS
#undef TEST_SEED
#undef NODE_MAGIC
    }

    return 0;
}
