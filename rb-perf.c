#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "rbtree.h"



struct perf_node {
    rb_node_t node;
    uint32_t key;
};


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
#ifdef __APPLE__
            /* On macOS, ru_maxrss is in bytes */
            printf("Max RSS: %.2f MB\n",
                   (double) usage.ru_maxrss / (1024 * 1024));
            printf("Memory per node: ~%.2f bytes\n",
                   (double) usage.ru_maxrss / count);
#else
            /* On Linux, ru_maxrss is in kilobytes */
            printf("Max RSS: %lu KB\n", usage.ru_maxrss);
            printf("Memory per node: ~%.2f bytes\n",
                   (double) usage.ru_maxrss * 1024 / count);
#endif
        }
    }

    printf("\nBenchmark complete\n");

    return 0;
}
