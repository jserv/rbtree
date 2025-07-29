#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "rbtree.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

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

#define TREE_SIZE_SMALL BIT(16)
#define TREE_SIZE_LARGE (10 * 1000 * 1000)
static const uint32_t dlog_N_small = 2 * ilog2_compile_time(TREE_SIZE_SMALL);

/* rb_node_t is embeddable in user structure */
struct container_node {
    rb_node_t node;
    int value;
};

struct perf_node {
    rb_node_t node;
    uint32_t key;
};

static rb_node_t nodes[TREE_SIZE_SMALL];
static rb_t test_rbtree;

/* The comparator is just the location of the structure */
static bool node_lessthan(const rb_node_t *a, const rb_node_t *b)
{
    return a < b;
}

/* Comparator for performance nodes based on key value */
static bool perf_node_lessthan(const rb_node_t *a, const rb_node_t *b)
{
    if (!a || !b)
        return false;
    const struct perf_node *node_a = container_of(a, struct perf_node, node);
    const struct perf_node *node_b = container_of(b, struct perf_node, node);
    return node_a->key < node_b->key;
}

/* Timing utilities */
static double timespec_diff(const struct timespec *start,
                            const struct timespec *end)
{
    return (double) (end->tv_sec - start->tv_sec) +
           (double) (end->tv_nsec - start->tv_nsec) * 1e-9;
}

static void print_timing(const char *operation, int count, double elapsed)
{
    printf("%-20s: %d ops in %.3f sec (%.3f µs/op, %.0f ops/sec)\n", operation,
           count, elapsed, elapsed / count * 1e6, count / elapsed);
}

/* initialize and insert a tree */
static void init_tree(rb_t *tree, int size)
{
    tree->cmp_func = node_lessthan;

    for (int i = 0; i < size; i++)
        rb_insert(tree, &nodes[i]);
}

/* Benchmark insertion with random keys */
static void bench_insertion(int count)
{
    printf("\n=== Insertion Benchmark ===\n");

    /* Allocate nodes on heap to avoid stack issues */
    struct perf_node *test_nodes = calloc(count, sizeof(struct perf_node));
    if (!test_nodes) {
        fprintf(stderr, "Failed to allocate memory for %d nodes\n", count);
        return;
    }

    /* Initialize tree */
    rb_t tree;
    memset(&tree, 0, sizeof(tree));
    tree.cmp_func = perf_node_lessthan;
    tree.root = NULL;
    tree.max_depth = 0;

    /* Generate unique sequential keys */
    for (int i = 0; i < count; i++) {
        test_nodes[i].key = i;
        /* Initialize node to ensure clean state */
        memset(&test_nodes[i].node, 0, sizeof(rb_node_t));
    }

    /* Shuffle for random insertion order using Fisher-Yates */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        uint32_t temp = test_nodes[i].key;
        test_nodes[i].key = test_nodes[j].key;
        test_nodes[j].key = temp;
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Insert nodes */
    for (int i = 0; i < count; i++) {
        rb_insert(&tree, &test_nodes[i].node);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    print_timing("Random insertion", count, elapsed);

    free(test_nodes);
}

/* Benchmark search operations */
static void bench_search(int count)
{
    printf("\n=== Search Benchmark ===\n");

    struct perf_node *test_nodes = calloc(count, sizeof(struct perf_node));
    if (!test_nodes) {
        fprintf(stderr, "Failed to allocate memory for %d nodes\n", count);
        return;
    }

    /* Initialize tree */
    rb_t tree;
    memset(&tree, 0, sizeof(tree));
    tree.cmp_func = perf_node_lessthan;
    tree.root = NULL;
    tree.max_depth = 0;

    /* Generate sequential keys and initialize nodes */
    for (int i = 0; i < count; i++) {
        test_nodes[i].key = i;
        memset(&test_nodes[i].node, 0, sizeof(rb_node_t));
    }

    /* Shuffle for random insertion */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        uint32_t temp = test_nodes[i].key;
        test_nodes[i].key = test_nodes[j].key;
        test_nodes[j].key = temp;
    }

    /* Insert all nodes */
    for (int i = 0; i < count; i++) {
        rb_insert(&tree, &test_nodes[i].node);
    }

    /* Benchmark search operations */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int found = 0;
    for (int i = 0; i < count; i++) {
        bool result = rb_contains(&tree, &test_nodes[i].node);
        if (result)
            found++;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    print_timing("Search existing", count, elapsed);
    printf("Found %d/%d nodes\n", found, count);

    free(test_nodes);
}

/* Benchmark deletion operations */
static void bench_deletion(int count)
{
    printf("\n=== Deletion Benchmark ===\n");

    struct perf_node *test_nodes = calloc(count, sizeof(struct perf_node));
    if (!test_nodes) {
        fprintf(stderr, "Failed to allocate memory for %d nodes\n", count);
        return;
    }

    /* Initialize tree */
    rb_t tree;
    memset(&tree, 0, sizeof(tree));
    tree.cmp_func = perf_node_lessthan;
    tree.root = NULL;
    tree.max_depth = 0;

    /* Generate sequential keys */
    for (int i = 0; i < count; i++) {
        test_nodes[i].key = i;
        memset(&test_nodes[i].node, 0, sizeof(rb_node_t));
    }

    /* Shuffle for random insertion */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        uint32_t temp = test_nodes[i].key;
        test_nodes[i].key = test_nodes[j].key;
        test_nodes[j].key = temp;
    }

    /* Insert all nodes */
    for (int i = 0; i < count; i++) {
        rb_insert(&tree, &test_nodes[i].node);
    }

    /* Create array of pointers for deletion order */
    rb_node_t **delete_order = malloc(count * sizeof(rb_node_t *));
    if (!delete_order) {
        free(test_nodes);
        return;
    }

    for (int i = 0; i < count; i++) {
        delete_order[i] = &test_nodes[i].node;
    }

    /* Shuffle deletion order */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        rb_node_t *temp = delete_order[i];
        delete_order[i] = delete_order[j];
        delete_order[j] = temp;
    }

    /* Benchmark deletion */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < count; i++) {
        rb_remove(&tree, delete_order[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    print_timing("Random deletion", count, elapsed);

    free(delete_order);
    free(test_nodes);
}

/* Benchmark mixed operations (insert/search/delete) */
static void bench_mixed_operations(int count)
{
    printf("\n=== Mixed Operations Benchmark ===\n");

    struct perf_node *test_nodes = calloc(count, sizeof(struct perf_node));
    if (!test_nodes) {
        fprintf(stderr, "Failed to allocate memory for %d nodes\n", count);
        return;
    }

    /* Initialize tree */
    rb_t tree;
    memset(&tree, 0, sizeof(tree));
    tree.cmp_func = perf_node_lessthan;
    tree.root = NULL;
    tree.max_depth = 0;

    /* Generate unique sequential keys */
    for (int i = 0; i < count; i++) {
        test_nodes[i].key = i;
        memset(&test_nodes[i].node, 0, sizeof(rb_node_t));
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Mixed operations */
    int total_ops = 0;
    int inserts = 0, searches = 0, deletes = 0;

    /* Insert first half */
    for (int i = 0; i < count / 2; i++) {
        rb_insert(&tree, &test_nodes[i].node);
        inserts++;
        total_ops++;
    }

    /* Mixed phase */
    for (int i = 0; i < count * 2; i++) {
        int op = rand() % 100;

        if (op < 40 && inserts < count) {
            rb_insert(&tree, &test_nodes[inserts].node);
            inserts++;
            total_ops++;
        } else if (op < 80 && inserts > deletes) {
            int idx = rand() % inserts;
            rb_contains(&tree, &test_nodes[idx].node);
            searches++;
            total_ops++;
        } else if (deletes < inserts / 2) {
            rb_remove(&tree, &test_nodes[deletes].node);
            deletes++;
            total_ops++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    printf(
        "Mixed operations    : %d ops in %.3f sec (%.3f µs/op, %.0f ops/sec)\n",
        total_ops, elapsed, elapsed / total_ops * 1e6, total_ops / elapsed);
    printf("  - Inserts: %d, Searches: %d, Deletes: %d\n", inserts, searches,
           deletes);

    free(test_nodes);
}

/* Benchmark cached tree operations */
#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
static void bench_cached_tree(int count)
{
    printf("\n=== Cached Tree Benchmark ===\n");

    struct perf_node *test_nodes = calloc(count, sizeof(struct perf_node));
    if (!test_nodes) {
        fprintf(stderr, "Failed to allocate memory for %d nodes\n", count);
        return;
    }

    /* Initialize cached tree */
    rb_cached_t cached_tree;
    rb_cached_init(&cached_tree, perf_node_lessthan);

    /* Generate unique sequential keys */
    for (int i = 0; i < count; i++) {
        test_nodes[i].key = i;
        memset(&test_nodes[i].node, 0, sizeof(rb_node_t));
    }

    /* Shuffle for random insertion */
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        uint32_t temp = test_nodes[i].key;
        test_nodes[i].key = test_nodes[j].key;
        test_nodes[j].key = temp;
    }

    /* Benchmark cached insertion */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < count; i++) {
        rb_cached_insert(&cached_tree, &test_nodes[i].node);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double insert_elapsed = timespec_diff(&start, &end);

    print_timing("Cached insertion", count, insert_elapsed);

    /* Benchmark get_min operations */
    int min_ops = 10000;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < min_ops; i++) {
        volatile rb_node_t *min = rb_cached_get_min(&cached_tree);
        (void) min;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double min_elapsed = timespec_diff(&start, &end);

    print_timing("Get min (cached)", min_ops, min_elapsed);

    /* Compare with regular tree get_min */
    rb_t *regular_tree = &cached_tree.rb_root;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < min_ops; i++) {
        volatile rb_node_t *min = rb_get_min(regular_tree);
        (void) min;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double regular_min_elapsed = timespec_diff(&start, &end);

    print_timing("Get min (regular)", min_ops, regular_min_elapsed);
    if (min_elapsed > 0) {
        printf("Cached speedup: %.1fx\n", regular_min_elapsed / min_elapsed);
    } else {
        printf("Cached speedup: >1000x (too fast to measure)\n");
    }

    free(test_nodes);
}
#endif

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

static void verify_rbtree(rb_node_t *root, rb_node_t *test)
{
    uint32_t node_height = 0;

    node_height = search_height_recurse(root, test, node_height);
    assert(node_height <= dlog_N_small);
}

/* Legacy verification test */
static void run_legacy_tests(void)
{
    printf("\n=== Legacy Verification Tests ===\n");

    /* Verify if the 'rb_node_t' structure is embedded within a user-defined
     * struct. This test initializes an 'rb_t' and checks two features:
     * 1. 'rb_node_t' structure can be embedded in any user-defined struct.
     * 2. 'rb_t' can be traversed using macro-based APIs.
     */
    int count = 0;
    rb_t test_tree_l;
    struct container_node tree_node[10];

    (void) memset(&test_tree_l, 0, sizeof(test_tree_l));
    (void) memset(tree_node, 0, sizeof(tree_node));

    test_tree_l.cmp_func = node_lessthan;
    for (uint32_t i = 0; i < ARRAY_SIZE(tree_node); i++) {
        tree_node[i].value = i;
        rb_insert(&test_tree_l, &tree_node[i].node);
    }

    rb_node_t *each;
    RB_FOREACH (&test_tree_l, each) {
        assert(container_of(each, struct container_node, node)->value ==
                   count &&
               "RB_FOREACH failed");
        count++;
    }

    count = 0;

    struct container_node *c_each;
    RB_FOREACH_CONTAINER (&test_tree_l, c_each, node) {
        assert(c_each->value == count && "RB_FOREACH_CONTAINER failed");
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
    init_tree(&test_rbtree, TREE_SIZE_SMALL);
    rb_node_t *root = test_rbtree.root;
    rb_node_t *test = NULL;

    test = rb_get_min(&test_rbtree);
    verify_rbtree(root, test);

    test = rb_get_max(&test_rbtree);
    verify_rbtree(root, test);

    /* Insert and remove the same node while maintaining the same height.
     * Assume that nodes[TREE_SIZE_SMALL / 2] will be removed and reinserted.
     * Verify that the search time is less than 2 * log(N), based on the height
     * of this node.
     */
    test = &nodes[TREE_SIZE_SMALL / 2];
    verify_rbtree(root, test);

    err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
    assert(err == 0);

    double elapsed = (double) (end.tv_sec - start.tv_sec) +
                     (double) (end.tv_nsec - start.tv_nsec) * 1e-9;

    struct rusage usage;
    err = getrusage(RUSAGE_SELF, &usage);
    assert(err == 0);

    /* Dump statistics */
    printf(
        "Operations performed on a red-black tree with %d nodes. Max RSS: %lu, "
        "~%.3f µs per iteration\n",
        TREE_SIZE_SMALL, usage.ru_maxrss,
        elapsed / (double) TREE_SIZE_SMALL * 1e6);

    printf("Legacy tests: PASSED\n");
}

int main(int argc, char *argv[])
{
    printf("Red-Black Tree Performance Benchmark\n");
    printf("=====================================\n");

    /* Parse command line arguments for test size */
    int test_sizes[] = {50, 100};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    if (argc > 1) {
        int custom_size = atoi(argv[1]);
        if (custom_size > 0) {
            test_sizes[0] = custom_size;
            num_sizes = 1;
        }
    }

    /* Test if command line argument was provided */
    if (argc <= 1) {
        printf("Use: %s <size> to run performance benchmarks\n", argv[0]);
        printf("Example: %s 50\n", argv[0]);
        return 0;
    }

    /* Check for --legacy flag to run legacy tests */
    if (argc > 1 && strcmp(argv[1], "--legacy") == 0) {
        run_legacy_tests();
        return 0;
    }

    /* Run comprehensive benchmarks for different sizes */
    for (int i = 0; i < num_sizes; i++) {
        int count = test_sizes[i];
        printf("Benchmarking with %d nodes:\n", count);

        /* Seed random number generator */
        srand(time(NULL));

        /* Run benchmarks */
        bench_insertion(count);
        bench_search(count);
        bench_deletion(count);
        bench_mixed_operations(count);
#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
        bench_cached_tree(count);
#endif

        /* Memory usage statistics */
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            printf("\nMemory Statistics:\n");
            printf("Max RSS: %lu KB\n", usage.ru_maxrss);
            printf("Memory per node: ~%.2f bytes\n",
                   (double) usage.ru_maxrss * 1024 / count);
        }
    }

    printf("\nBenchmark complete\n");

    return 0;
}
