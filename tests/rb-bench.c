/*
 * Benchmark test for rbtree implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include "rbtree.h"

/* Runtime platform detection using uname */
static char platform_name[256] = {0};
static char compiler_info[256] = {0};

static void detect_platform(void)
{
    struct utsname sys_info;
    
    if (uname(&sys_info) == 0) {
        strncpy(platform_name, sys_info.sysname, sizeof(platform_name) - 1);
        platform_name[sizeof(platform_name) - 1] = '\0';
    } else {
        strcpy(platform_name, "Unknown");
    }
}

static void detect_compiler(void)
{
    /* Compiler detection at compile time, formatted at runtime */
#if defined(__clang__)
    snprintf(compiler_info, sizeof(compiler_info), "clang %s", __VERSION__);
#elif defined(__GNUC__)
    snprintf(compiler_info, sizeof(compiler_info), "gcc %s", __VERSION__);
#elif defined(_MSC_VER)
    snprintf(compiler_info, sizeof(compiler_info), "msvc %d", _MSC_VER);
#elif defined(__INTEL_COMPILER)
    snprintf(compiler_info, sizeof(compiler_info), "icc %d", __INTEL_COMPILER);
#else
    snprintf(compiler_info, sizeof(compiler_info), "unknown compiler");
#endif
}

typedef struct {
    int key;
    bool is_member;
} test_data;

typedef struct {
    rb_node_t node;
    test_data data;
} test_node;

static rb_t tree;
static rb_cached_t cached_tree;

static bool cmp_nodes(const rb_node_t *a, const rb_node_t *b)
{
    const test_node *node_a = container_of(a, test_node, node);
    const test_node *node_b = container_of(b, test_node, node);
    return node_a->data.key < node_b->data.key;
}

/* Test infrastructure functions */
static void init_tree(void *tree_ptr)
{
    rb_t *t = (rb_t *)tree_ptr;
    t->root = NULL;
    t->cmp_func = cmp_nodes;
#if _RB_DISABLE_ALLOCA != 0
    t->max_depth = 0;
#endif
}

static void init_cached_tree(void *tree_ptr)
{
    rb_cached_t *t = (rb_cached_t *)tree_ptr;
    rb_cached_init(t, cmp_nodes);
}

static void init_node(void *node_ptr, int key)
{
    test_node *n = (test_node *)node_ptr;
    n->data.key = key;
    n->data.is_member = false;
}

static test_data *get_data(void *node_ptr)
{
    test_node *n = (test_node *)node_ptr;
    return &n->data;
}

static void insert_node(void *tree_ptr, void *node_ptr)
{
    rb_t *t = (rb_t *)tree_ptr;
    test_node *n = (test_node *)node_ptr;
    rb_insert(t, &n->node);
}

static void insert_cached_node(void *tree_ptr, void *node_ptr)
{
    rb_cached_t *t = (rb_cached_t *)tree_ptr;
    test_node *n = (test_node *)node_ptr;
    rb_cached_insert(t, &n->node);
}

static void extract_node(void *tree_ptr, void *node_ptr)
{
    rb_t *t = (rb_t *)tree_ptr;
    test_node *n = (test_node *)node_ptr;
    rb_remove(t, &n->node);
}

static void extract_cached_node(void *tree_ptr, void *node_ptr)
{
    rb_cached_t *t = (rb_cached_t *)tree_ptr;
    test_node *n = (test_node *)node_ptr;
    rb_cached_remove(t, &n->node);
}

/* Timing infrastructure */
typedef uint64_t ticks;

static inline uint64_t ticks_read(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define ticks_difference(t1, t0) ((t1) - (t0))
#define ticks_to_nanoseconds(t) (t)

/* Random number generator */
static inline uint32_t simple_random(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

/* Node management */
static inline void *get_node(void *nodes, size_t node_size, size_t i)
{
    return (void *)((char *)nodes + i * node_size);
}

static inline void *next_node(void *node, size_t node_size)
{
    return get_node(node, node_size, 1);
}

static inline void *create_nodes(
    size_t node_count,
    size_t node_size,
    void (*init_node_func)(void *, int))
{
    void *nodes;
    void *node;
    size_t i;

    nodes = calloc(node_count, node_size);
    assert(nodes != NULL);

    node = nodes;
    for (i = 0; i < node_count; ++i) {
        (*init_node_func)(node, (int)i);
        node = next_node(node, node_size);
    }

    return nodes;
}

/* Test implementations */
static inline void test_random_ops(
    void *tree,
    size_t node_count,
    size_t node_size,
    void (*init_tree_func)(void *),
    void (*init_node_func)(void *, int),
    test_data *(*get_data_func)(void *),
    void (*insert_func)(void *, void *),
    void (*extract_func)(void *, void *))
{
    uint32_t v = 0xdeadbeef;
    size_t m = 123 * node_count;
    unsigned long insert_count = 0;
    unsigned long extract_count = 0;
    int shift = 8;
    void *nodes;
    size_t i;
    ticks t0;
    ticks t1;
    ticks d;

    assert(node_count < (1UL << (32 - shift)));

    (*init_tree_func)(tree);
    nodes = create_nodes(node_count, node_size, init_node_func);

    t0 = ticks_read();

    for (i = 0; i < m; ++i) {
        size_t j = (v >> shift) % node_count;
        void *node = get_node(nodes, node_size, j);
        test_data *data = (*get_data_func)(node);

        if (data->is_member) {
            data->is_member = false;
            ++extract_count;
            (*extract_func)(tree, node);
        } else {
            data->is_member = true;
            ++insert_count;
            (*insert_func)(tree, node);
        }

        v = simple_random(v);
    }

    t1 = ticks_read();
    d = ticks_difference(t1, t0);

    printf(
        "\t\t\t<Sample nodeCount=\"%lu\" "
        "insertCount=\"%lu\" "
        "extractCount=\"%lu\" "
        "duration=\"%lu\"/>\n",
        (unsigned long)node_count,
        insert_count,
        extract_count,
        (unsigned long)ticks_to_nanoseconds(d));

    free(nodes);
}

static inline void test_linear(
    void *tree,
    size_t node_count,
    size_t node_size,
    void (*init_tree_func)(void *),
    void (*init_node_func)(void *, int),
    void (*insert_func)(void *, void *),
    void (*extract_func)(void *, void *))
{
    size_t m = 1000;
    void *nodes;
    size_t i;
    size_t j;
    ticks t0;
    ticks t1;
    ticks d;

    (*init_tree_func)(tree);
    nodes = create_nodes(node_count, node_size, init_node_func);

    t0 = ticks_read();

    for (i = 0; i < m; ++i) {
        void *node;

        node = nodes;
        for (j = 0; j < node_count; ++j) {
            (*insert_func)(tree, node);
            node = next_node(node, node_size);
        }

        node = nodes;
        for (j = 0; j < node_count; ++j) {
            (*extract_func)(tree, node);
            node = next_node(node, node_size);
        }
    }

    t1 = ticks_read();
    d = ticks_difference(t1, t0);

    printf(
        "\t\t\t<Sample nodeCount=\"%lu\" "
        "insertCount=\"%lu\" "
        "extractCount=\"%lu\" "
        "duration=\"%lu\"/>\n",
        (unsigned long)node_count,
        (unsigned long)m,
        (unsigned long)m,
        (unsigned long)ticks_to_nanoseconds(d));

    free(nodes);
}

static inline size_t large_set_next(size_t c)
{
    return (123 * c + 99) / 100;
}

static inline void run_test(
    const char *impl,
    void *tree,
    size_t node_size,
    void (*init_tree_func)(void *),
    void (*init_node_func)(void *, int),
    test_data *(*get_data_func)(void *),
    void (*insert_func)(void *, void *),
    void (*extract_func)(void *, void *))
{
    size_t small_set_size = 128;
    size_t large_set_size = 1024;
    size_t c;
    size_t i;

    printf(
        "\t<RBTest implementation=\"%s\" nodeSize=\"%lu\">\n",
        impl,
        (unsigned long)(node_size - sizeof(test_data)));

    printf("\t\t<SmallSetRandomOps>\n");

    for (i = 1; i < small_set_size; ++i) {
        test_random_ops(
            tree,
            i,
            node_size,
            init_tree_func,
            init_node_func,
            get_data_func,
            insert_func,
            extract_func);
    }

    printf("\t\t</SmallSetRandomOps>\n");

    printf("\t\t<LargeSetRandomOps>\n");

    c = i;

    while (c < large_set_size) {
        test_random_ops(
            tree,
            c,
            node_size,
            init_tree_func,
            init_node_func,
            get_data_func,
            insert_func,
            extract_func);

        c = large_set_next(c);
    }

    printf("\t\t</LargeSetRandomOps>\n");

    printf("\t\t<SmallSetLinear>\n");

    for (i = 1; i < small_set_size; ++i) {
        test_linear(
            tree,
            i,
            node_size,
            init_tree_func,
            init_node_func,
            insert_func,
            extract_func);
    }

    printf("\t\t</SmallSetLinear>\n");

    printf("\t\t<LargeSetLinear>\n");

    c = i;

    while (c < large_set_size) {
        test_linear(
            tree,
            c,
            node_size,
            init_tree_func,
            init_node_func,
            insert_func,
            extract_func);

        c = large_set_next(c);
    }

    printf("\t\t</LargeSetLinear>\n");

    printf("\t</RBTest>\n");
}

void test_rbtree(void)
{
    run_test(
        "rbtree",
        &tree,
        sizeof(test_node),
        init_tree,
        init_node,
        get_data,
        insert_node,
        extract_node);
}

void test_rbtree_cached(void)
{
#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
    run_test(
        "rbtree-cached",
        &cached_tree,
        sizeof(test_node),
        init_cached_tree,
        init_node,
        get_data,
        insert_cached_node,
        extract_cached_node);
#endif
}

/* Benchmark-specific tests */
static void benchmark_insert_only(size_t node_count)
{
    void *nodes = create_nodes(node_count, sizeof(test_node), init_node);
    ticks t0, t1;
    
    init_tree(&tree);
    
    t0 = ticks_read();
    for (size_t i = 0; i < node_count; ++i) {
        test_node *n = (test_node *)get_node(nodes, sizeof(test_node), i);
        rb_insert(&tree, &n->node);
    }
    t1 = ticks_read();
    
    printf("Insert %zu nodes: %llu ns (%.2f ns/op)\n", 
           node_count, 
           ticks_to_nanoseconds(ticks_difference(t1, t0)),
           (double)ticks_to_nanoseconds(ticks_difference(t1, t0)) / node_count);
    
    free(nodes);
}

static void benchmark_search_only(size_t node_count)
{
    void *nodes = create_nodes(node_count, sizeof(test_node), init_node);
    ticks t0, t1;
    
    init_tree(&tree);
    
    /* Insert all nodes first */
    for (size_t i = 0; i < node_count; ++i) {
        test_node *n = (test_node *)get_node(nodes, sizeof(test_node), i);
        rb_insert(&tree, &n->node);
    }
    
    t0 = ticks_read();
    for (size_t i = 0; i < node_count; ++i) {
        test_node *n = (test_node *)get_node(nodes, sizeof(test_node), i);
        rb_contains(&tree, &n->node);
    }
    t1 = ticks_read();
    
    printf("Search %zu nodes: %llu ns (%.2f ns/op)\n", 
           node_count, 
           ticks_to_nanoseconds(ticks_difference(t1, t0)),
           (double)ticks_to_nanoseconds(ticks_difference(t1, t0)) / node_count);
    
    free(nodes);
}

int main(int argc, char **argv)
{
    /* Detect platform and compiler at runtime */
    detect_platform();
    detect_compiler();
    
    if (argc > 1 && strcmp(argv[1], "--xml") == 0) {
        /* XML output mode compatible with rb-bench */
        printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        printf("<RBTestCollection platform=\"%s\" compiler=\"%s\">\n", 
               platform_name, compiler_info);
        
        test_rbtree();
        test_rbtree_cached();
        
        printf("</RBTestCollection>\n");
    } else {
        /* Default: Simple benchmark mode */
        size_t sizes[] = {100, 1000, 10000, 100000};
        size_t count = sizeof(sizes) / sizeof(sizes[0]);
        
        printf("=== Red-Black Tree Benchmark ===\n");
        
        for (size_t i = 0; i < count; ++i) {
            printf("\nTesting with %zu nodes:\n", sizes[i]);
            benchmark_insert_only(sizes[i]);
            benchmark_search_only(sizes[i]);
        }
        
        if (argc > 1) {
            printf("\nUsage: %s [--xml]\n", argv[0]);
            printf("  --xml  Generate XML output compatible with rb-bench\n");
            printf("  (no option runs simple benchmarks)\n");
        }
    }

    return 0;
}
