/*
 * This red-black tree implementation is optimized for minimal memory usage.
 *
 * A red-black tree is a self-balancing binary search tree that functions as
 * a dynamic ordered dictionary, as long as the elements can be arranged in a
 * strict weak order.
 *
 * The data structure is intrusive, meaning that the node handle is designed
 * to be embedded within a user-defined struct, similar to the doubly-linked
 * list implementation in the Linux kernel. No additional data pointer is stored
 * in the node itself.
 * Reference:
 * https://www.kernel.org/doc/html/latest/core-api/kernel-api.html#list-management-functions
 *
 * The color bit is combined with one of the child pointers, reducing memory
 * overhead. Importantly, this implementation does not store a 'parent' pointer.
 * Instead, the tree's upper structure is managed dynamically using a stack
 * during traversal. As a result, each node requires only two pointers, making
 * the memory overhead comparable to that of a doubly-linked list.
 */

#ifndef _RBTREE_H_
#define _RBTREE_H_

#include <stdbool.h>
#include <stdint.h>

/* Compile-time verification that pointer alignment is sufficient for color bit
 * storage. The implementation stores the red/black color in the least
 * significant bit (LSB) of the left child pointer, which requires at least
 * 2-byte pointer alignment. This assertion ensures the assumption is valid on
 * the target platform.
 */
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L
_Static_assert(_Alignof(void *) >= 2,
               "Pointer alignment insufficient for color bit storage");
#endif
#else
/* For older C standards or compilers without _Static_assert */
typedef char __rbtree_alignment_check[((_Alignof(void *) >= 2) ? 1 : -1)];
#endif

/* Although the use of alloca(3) is generally discouraged due to the lack of
 * guarantees that it returns a valid and usable memory block, it does provide
 * temporary space that is automatically freed upon function return. This
 * behavior makes alloca(3) faster than malloc(3) because of its simpler
 * allocation and deallocation process. Additionally, since the compiler is
 * aware of the allocation size, it can optimize the usage of alloca(3). For
 * example, Clang can optimize fixed-size calls to alloca(3), making it a
 * legitimate choice when using the GNU built-in function form.
 *
 * Reference: https://nullprogram.com/blog/2019/10/28/
 */
#ifndef _RB_DISABLE_ALLOCA
#define _RB_DISABLE_ALLOCA 0
#endif

/* Enable leftmost caching for O(1) rb_get_min() and iterator initialization.
 * This adds one pointer to the tree structure but significantly improves
 * performance for minimum access and traversal operations.
 */
#ifndef _RB_ENABLE_LEFTMOST_CACHE
#define _RB_ENABLE_LEFTMOST_CACHE 1
#endif

/* Enable rightmost caching for O(1) rb_get_max().
 * This adds one pointer to the tree structure. Less commonly needed than
 * leftmost caching, so disabled by default following Linux kernel approach.
 */
#ifndef _RB_ENABLE_RIGHTMOST_CACHE
#define _RB_ENABLE_RIGHTMOST_CACHE 0
#endif

/* Enable safety checks for stack overflow and bounds checking.
 * This adds runtime checks to prevent crashes from corrupted trees.
 */
#ifndef _RB_ENABLE_SAFETY_CHECKS
#define _RB_ENABLE_SAFETY_CHECKS 1
#endif

/* Enable batch operations for efficient bulk insertions.
 * This adds batch API support with optimized tree construction for empty trees.
 */
#ifndef _RB_ENABLE_BATCH_OPS
#define _RB_ENABLE_BATCH_OPS 1
#endif

/* Enable property-based invariant testing for debugging and validation.
 * This adds comprehensive red-black tree property validation functions.
 * Disable in production builds for minimal code size and maximum performance.
 */
#ifndef _RB_ENABLE_PROPERTY_VALIDATION
#define _RB_ENABLE_PROPERTY_VALIDATION 1
#endif

#if _RB_DISABLE_ALLOCA == 0
#if defined(__GNUC__) || defined(__clang__)
#undef alloca
#define alloca __builtin_alloca
#else
#include <alloca.h>
#endif
#endif

typedef enum { RB_LEFT = 0, RB_RIGHT = 1 } rb_side_t;

/* Magic number */
#define RB_COLOR_MASK 1UL
#define RB_PTR_MASK (~RB_COLOR_MASK)
#define RB_ITER_UNINIT (-1)
#define RB_ITER_DONE (-2)

/* red-black tree node.
 *
 * Red-black trees often use an additional "parent" pointer for upward traversal
 * during rebalancing after modifications. However, this avoids the need for
 * a third pointer by using a local stack of node pointers constructed during
 * downward traversal. This stack-based approach dynamically handles
 * modifications without explicit parent pointers, reducing memory overhead.
 */
typedef struct __rb_node {
    struct __rb_node *children[2];
} rb_node_t;

/* Maximum theoretical depth of the tree, calculated based on pointer size.
 * Assuming the memory is entirely filled with nodes containing two pointers,
 * and considering that the tree may be twice as deep as a perfect binary tree,
 * including the root node. For 32-bit pointers, the maximum depth is 59 nodes,
 * while for 64-bit pointers, it is 121 nodes.
 */
#define _RB_PTR_TAG_BITS(t) ((sizeof(t)) < 8 ? 2 : 3)
#define _RB_PTR_SIZE_BITS(t) (8 * sizeof(t))
#define _RB_MAX_TREE_DEPTH \
    (2 * (_RB_PTR_SIZE_BITS(int *) - _RB_PTR_TAG_BITS(int *) - 1) + 1)

/* Optimize buffer size based on expected tree depth */
#ifndef _RB_COMMON_TREE_DEPTH
#define _RB_COMMON_TREE_DEPTH 32 /* Supports 2^31 nodes efficiently */
#endif

/* Use appropriate depth limit based on alloca setting */
#if _RB_DISABLE_ALLOCA != 0
#define _RB_EFFECTIVE_DEPTH_LIMIT _RB_COMMON_TREE_DEPTH
#else
#define _RB_EFFECTIVE_DEPTH_LIMIT _RB_MAX_TREE_DEPTH
#endif

/* Red-black tree comparison predicate.
 *
 * Compares two nodes and returns true if node A is strictly less than node B,
 * based on the tree's defined sorting criteria. Returns false otherwise.
 *
 * During insertion, the node being inserted is always designated as "A", while
 * "B" refers to the existing node within the tree for comparison. This behavior
 * can be leveraged (with caution) to implement "most/least recently added"
 * semantics for nodes that would otherwise have equal comparison values.
 */
typedef bool (*rb_cmp_t)(const rb_node_t *a, const rb_node_t *b);

/* Red-black tree structure */
typedef struct {
    rb_node_t *root;   /**< Root node of the tree */
    rb_cmp_t cmp_func; /**< Comparison function for nodes */
#if _RB_DISABLE_ALLOCA != 0
    uint8_t max_depth; /**< Maximum depth (only needed for fixed stack) */
    /* Single buffer for iterator state: node ptr with flags packed in LSB */
    union {
        rb_node_t *iter_stack[_RB_COMMON_TREE_DEPTH];
        uint8_t iter_buffer[_RB_COMMON_TREE_DEPTH * sizeof(rb_node_t *)];
    };
#endif
} rb_t;

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/* Cached red-black tree structure.
 *
 * Following the Linux kernel's approach, we cache the leftmost node for O(1)
 * minimum access and iterator initialization. Rightmost caching is optional
 * due to less frequent usage patterns.
 */
typedef struct {
    rb_t rb_root; /**< Embedded standard tree structure */
#if _RB_ENABLE_LEFTMOST_CACHE
    rb_node_t *rb_leftmost; /**< Leftmost (minimum) node cache */
#endif
#if _RB_ENABLE_RIGHTMOST_CACHE
    rb_node_t *rb_rightmost; /**< Rightmost (maximum) node cache */
#endif
} rb_cached_t;
#endif

/* forward declaration for helper functions, used for inlining */
rb_node_t *__rb_child(rb_node_t *node, rb_side_t side);
int __rb_is_black(rb_node_t *node);
rb_node_t *__rb_get_minmax(rb_t *tree, rb_side_t side);

/**
 * Insert a new node into the red-black tree.
 * @tree: Pointer to the red-black tree structure
 * @node: Pointer to the node to be inserted (must be uninitialized)
 *
 * Inserts a new node into the tree while maintaining red-black properties.
 * The node's children are automatically initialized to NULL, and its position
 * is determined by the tree's comparison function. Tree rebalancing is
 * performed as needed to preserve invariants.
 *
 * Complexity: O(log N) where N is the number of nodes in the tree
 *
 * Note: The node must not already be part of any tree structure.
 */
void rb_insert(rb_t *tree, rb_node_t *node);

/**
 * Remove a node from the red-black tree.
 * @tree: Pointer to the red-black tree structure
 * @node: Pointer to the node to be removed
 *
 * Removes the specified node from the tree while maintaining red-black
 * properties. If the node has two children, it is replaced by its in-order
 * predecessor. Tree rebalancing is performed as needed to preserve invariants.
 *
 * Complexity: O(log N) where N is the number of nodes in the tree
 *
 * Note: If the node is not found in the tree, this function has no effect.
 */
void rb_remove(rb_t *tree, rb_node_t *node);

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/**
 * Initialize a cached red-black tree.
 * @tree: Pointer to the cached red-black tree structure
 * @cmp_func: Comparison function for determining node ordering
 *
 * Initializes an empty cached red-black tree with the specified comparison
 * function. The cached tree maintains pointers to minimum and/or maximum
 * nodes for O(1) access, depending on build-time configuration.
 *
 * The comparison function must implement a strict weak ordering:
 * - cmp(a,b) returns true if a < b, false otherwise
 * - Antisymmetric: if cmp(a,b) then !cmp(b,a)
 * - Transitive: if cmp(a,b) && cmp(b,c) then cmp(a,c)
 */
static inline void rb_cached_init(rb_cached_t *tree, rb_cmp_t cmp_func)
{
    tree->rb_root.root = NULL;
    tree->rb_root.cmp_func = cmp_func;
#if _RB_DISABLE_ALLOCA != 0
    tree->rb_root.max_depth = 0;
#endif
#if _RB_ENABLE_LEFTMOST_CACHE
    tree->rb_leftmost = NULL;
#endif
#if _RB_ENABLE_RIGHTMOST_CACHE
    tree->rb_rightmost = NULL;
#endif
}

/**
 * Insert a new node into the cached red-black tree.
 * @tree: Pointer to the cached red-black tree structure
 * @node: Pointer to the node to be inserted (must be uninitialized)
 *
 * Inserts a node into the cached tree and updates cache pointers as needed.
 * When leftmost caching is enabled, minimum access remains O(1). When
 * rightmost caching is enabled, maximum access remains O(1).
 *
 * Complexity: O(log N) insertion + O(1) cache update
 */
void rb_cached_insert(rb_cached_t *tree, rb_node_t *node);

/**
 * Remove a node from the cached red-black tree.
 * @tree: Pointer to the cached red-black tree structure
 * @node: Pointer to the node to be removed
 *
 * Removes a node from the cached tree and updates cache pointers as needed.
 * If the removed node was cached as minimum or maximum, the cache is updated
 * by finding the new extreme values.
 *
 * Complexity: O(log N) removal + O(log N) cache update if needed
 */
void rb_cached_remove(rb_cached_t *tree, rb_node_t *node);
#endif

/**
 * Return the lowest-sorted member of the red-black tree.
 * @tree: Pointer to the red-black tree structure
 *
 * Complexity: O(log N) where N is the number of nodes
 */
static inline rb_node_t *rb_get_min(rb_t *tree)
{
    return __rb_get_minmax(tree, RB_LEFT);
}

/**
 * Return the highest-sorted member of the red-black tree.
 * @tree: Pointer to the red-black tree structure
 *
 * Complexity: O(log N) where N is the number of nodes
 */
static inline rb_node_t *rb_get_max(rb_t *tree)
{
    return __rb_get_minmax(tree, RB_RIGHT);
}

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/**
 * Return the lowest-sorted member of the cached red-black tree.
 * @tree: Pointer to the cached red-black tree structure
 *
 * Complexity: O(1) when leftmost caching enabled, O(log N) otherwise
 */
static inline rb_node_t *rb_cached_get_min(rb_cached_t *tree)
{
#if _RB_ENABLE_LEFTMOST_CACHE
    return tree->rb_leftmost;
#else
    return __rb_get_minmax(&tree->rb_root, RB_LEFT);
#endif
}

/**
 * Return the highest-sorted member of the cached red-black tree.
 * @tree: Pointer to the cached red-black tree structure
 *
 * Complexity: O(1) when rightmost caching enabled, O(log N) otherwise
 */
static inline rb_node_t *rb_cached_get_max(rb_cached_t *tree)
{
#if _RB_ENABLE_RIGHTMOST_CACHE
    return tree->rb_rightmost;
#else
    return __rb_get_minmax(&tree->rb_root, RB_RIGHT);
#endif
}

/**
 * Check if the cached red-black tree is empty.
 * @tree: Pointer to the cached red-black tree structure
 *
 * Returns: true if the tree contains no nodes, false otherwise
 * Complexity: O(1)
 */
static inline bool rb_cached_empty(rb_cached_t *tree)
{
    return tree->rb_root.root == NULL;
}
#endif

/**
 * Check if the given node is present in the red-black tree.
 * @tree: Pointer to the red-black tree structure
 * @node: Pointer to the node to search for
 *
 * Searches the tree to determine if the specified node is present by
 * traversing from the root using the comparison function. The search
 * terminates when either the exact node is found (pointer equality) or
 * a leaf is reached.
 *
 * Returns: true if the node is found, false otherwise
 * Complexity: O(log N) where N is the number of nodes
 *
 * Note: This function tests for pointer equality, not value equality,
 * making it suitable for implementing set semantics.
 */
bool rb_contains(rb_t *tree, rb_node_t *node);

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/**
 * Check if the given node is present in the cached red-black tree.
 * @tree: Pointer to the cached red-black tree structure
 * @node: Pointer to the node to search for
 *
 * Optimized search that can perform early bounds checking using cached
 * minimum/maximum values. If the node is outside the cached bounds,
 * returns false without tree traversal.
 *
 * Returns: true if the node is found, false otherwise
 * Complexity: O(1) for out-of-bounds nodes, O(log N) otherwise
 *
 * Optimization: When caching is enabled, nodes outside [min,max] bounds
 * are rejected immediately without traversal.
 */
bool rb_cached_contains(rb_cached_t *tree, rb_node_t *node);
#endif

/**
 * Helper structure for non-recursive red-black tree traversal.
 *
 * Used internally by RB_FOREACH and related macros to maintain traversal state
 * during in-order tree traversal. The structure uses an optimized single-buffer
 * layout to minimize memory allocation overhead and improve cache locality.
 *
 * Memory Layout:
 * - Node pointers with direction flags packed in LSB
 * - Single allocation reduces malloc overhead
 * - Improved cache locality during traversal
 * - 16-byte memory saving from flag packing optimization
 *
 * Fields:
 * @buffer: Node stack with direction flags packed in pointer LSB
 * @top:    Current stack position (RB_ITER_UNINIT=uninitialized,
 *          RB_ITER_DONE=done, >=0=active)
 */
typedef struct {
    void *buffer; /**< Node stack with direction flags packed in LSB */
    int32_t top;  /**< Current position in the stack (RB_ITER_UNINIT = uninit,
                     RB_ITER_DONE = done) */
} rb_foreach_t;

/* Helper macros to access the optimized buffer layout */
#define _RB_FOREACH_STACK(f) ((rb_node_t **) (f)->buffer)

/* Pack direction flags into LSB of stack pointers (saves 16 bytes) */
#define _RB_FOREACH_SET_DIRECTION(f, idx, went_left)                         \
    do {                                                                     \
        rb_node_t **stack = _RB_FOREACH_STACK(f);                            \
        uintptr_t ptr = (uintptr_t) stack[idx];                              \
        stack[idx] = (rb_node_t *) ((ptr & ~1UL) | (went_left ? 1UL : 0UL)); \
    } while (0)

#define _RB_FOREACH_GET_DIRECTION(f, idx) \
    (((uintptr_t) _RB_FOREACH_STACK(f)[idx]) & 1UL)

#define _RB_FOREACH_GET_NODE(f, idx) \
    ((rb_node_t *) (((uintptr_t) _RB_FOREACH_STACK(f)[idx]) & ~1UL))


#if _RB_DISABLE_ALLOCA == 0
/* Calculate buffer size: only node pointers (flags packed in LSB) */
#define _RB_FOREACH_BUFFER_SIZE (_RB_MAX_TREE_DEPTH * sizeof(rb_node_t *))

#define _RB_FOREACH_INIT(tree, node)               \
    {                                              \
        .buffer = alloca(_RB_FOREACH_BUFFER_SIZE), \
        .top = RB_ITER_UNINIT,                     \
    }
#else
#define _RB_FOREACH_INIT(tree, node)   \
    {                                  \
        .buffer = (tree)->iter_buffer, \
        .top = RB_ITER_UNINIT,         \
    }
#endif

rb_node_t *__rb_foreach_next(rb_t *tree, rb_foreach_t *f);

/**
 * In-order traversal of a red-black tree without recursion.
 * @tree: Pointer to the red-black tree ('rb_t') to traverse
 * @node: Name of a local variable of type 'rb_node_t *' to use as iterator
 *
 * Performs non-recursive in-order traversal using an internal stack.
 * Nodes are visited in sorted order according to the tree's comparison
 * function. The traversal state is automatically managed using stack-allocated
 * buffers.
 *
 * Usage:
 *   RB_FOREACH(my_tree, current_node) {
 *       // Process current_node here
 *   }
 *
 * Complexity: O(N) total time, O(log N) space for traversal stack
 *
 * Thread Safety: Not safe for concurrent modifications
 *
 * Warning: Modifying the tree structure during traversal results in
 * undefined behavior. The macro expands arguments multiple times.
 */
#define RB_FOREACH(tree, node)                            \
    for (rb_foreach_t __f = _RB_FOREACH_INIT(tree, node); \
         ((node) = __rb_foreach_next((tree), &__f));      \
         /**/)

#ifndef container_of
/**
 * Compute the address of the object containing a given member.
 * @ptr:    Pointer to the member variable
 * @type:   Type of the structure that includes the member
 * @member: Name of the member variable in the structure @type
 *
 * This macro calculates the address of a containing structure given a pointer
 * to one of its members. It's essential for converting from rb_node_t pointers
 * back to user-defined container structures in intrusive data structures.
 *
 * Returns: Pointer to the enclosing object of type @type
 *
 * Example:
 *   struct my_data { int value; rb_node_t node; };
 *   rb_node_t *node_ptr = ...;
 *   struct my_data *data = container_of(node_ptr, struct my_data, node);
 */
#define container_of(ptr, type, member)                              \
    __extension__({                                                  \
        const __typeof__(((type *) 0)->member) *(__pmember) = (ptr); \
        (type *) ((char *) __pmember - offsetof(type, member));      \
    })
#endif

/**
 * In-order traversal of a red-black tree with container handling.
 * @tree:  Pointer to the red-black tree ('rb_t') to traverse
 * @node:  Name of local iterator variable (pointer to container type)
 * @field: Name of the 'rb_node_t' member within the container struct
 *
 * Traverses container objects that embed rb_node_t members, automatically
 * resolving from node pointers to container pointers using container_of.
 * This enables iteration over user-defined structures in sorted order.
 *
 * Usage:
 *   RB_FOREACH_CONTAINER(my_tree, entry, rb_node) {
 *       // Process entry (container struct) here
 *   }
 *
 * Complexity: O(N) total time, O(log N) space for traversal stack
 *
 * Note: The container type is automatically deduced from the node variable.
 */
#define RB_FOREACH_CONTAINER(tree, node, field)                               \
    for (rb_foreach_t __f = _RB_FOREACH_INIT(tree, node); ({                  \
             rb_node_t *n = __rb_foreach_next(tree, &__f);                    \
             (node) = n ? container_of(n, __typeof__(*(node)), field) : NULL; \
             (node);                                                          \
         });                                                                  \
         /**/)

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/**
 * Optimized in-order traversal of a cached red-black tree.
 * @tree: Pointer to the cached red-black tree ('rb_cached_t') to traverse
 * @node: Name of a local variable of type 'rb_node_t *' to use as iterator
 *
 * Similar to RB_FOREACH but optimized for cached trees. The primary benefit
 * comes from O(1) access to the leftmost node when leftmost caching is enabled,
 * rather than the O(log N) traversal to find the starting point.
 *
 * Usage:
 *   RB_CACHED_FOREACH(my_cached_tree, current_node) {
 *       // Process current_node here
 *   }
 *
 * Complexity: O(1) initialization + O(N) traversal when leftmost cache enabled
 */
#define RB_CACHED_FOREACH(tree, node)                                 \
    for (rb_foreach_t __f = _RB_FOREACH_INIT(&(tree)->rb_root, node); \
         ((node) = __rb_cached_foreach_next((tree), &__f));           \
         /**/)

/**
 * Optimized traversal of cached tree with container handling.
 * @tree:  Pointer to the cached red-black tree ('rb_cached_t') to traverse
 * @node:  Name of local iterator variable (pointer to container type)
 * @field: Name of the 'rb_node_t' member within the container struct
 *
 * Combines the benefits of cached tree optimization with automatic container
 * resolution. When leftmost caching is enabled, provides O(1) initialization
 * plus efficient traversal of user-defined container structures.
 *
 * Usage:
 *   RB_CACHED_FOREACH_CONTAINER(my_cached_tree, entry, rb_node) {
 *       // Process entry (container struct) here
 *   }
 */
#define RB_CACHED_FOREACH_CONTAINER(tree, node, field)                        \
    for (rb_foreach_t __f = _RB_FOREACH_INIT(&(tree)->rb_root, node); ({      \
             rb_node_t *n = __rb_cached_foreach_next((tree), &__f);           \
             (node) = n ? container_of(n, __typeof__(*(node)), field) : NULL; \
             (node);                                                          \
         });                                                                  \
         /**/)

/**
 * Get the next node in cached tree in-order traversal.
 * @tree: Pointer to the cached red-black tree structure
 * @f:    Pointer to the traversal state structure
 *
 * Returns: Next node in traversal order, or NULL when traversal complete
 */
rb_node_t *__rb_cached_foreach_next(rb_cached_t *tree, rb_foreach_t *f);
#endif

#if _RB_ENABLE_BATCH_OPS
/* Batch operation support for efficient bulk insertions.
 *
 * Batch operations allow collecting multiple nodes before inserting them
 * into the tree, providing optimization opportunities for bulk operations.
 * When inserting into an empty tree, nodes are sorted and a balanced tree
 * is constructed in O(n) time after O(n log n) sorting.
 */

/**
 * Batch operation context for collecting nodes before bulk insertion.
 *
 * @nodes:     Dynamic array of node pointers to be inserted
 * @count:     Current number of nodes in the batch
 * @capacity:  Allocated capacity of the nodes array
 * @cmp_func:  Comparison function (copied from tree during commit)
 */
typedef struct {
    rb_node_t **nodes; /**< Buffer of nodes to be inserted */
    size_t count;      /**< Current number of nodes in buffer */
    size_t capacity;   /**< Allocated capacity of buffer */
    rb_cmp_t cmp_func; /**< Comparison function for sorting */
} rb_batch_t;

/**
 * Initialize a batch operation context.
 * @batch: Pointer to the batch context to initialize
 * @initial_capacity: Initial buffer capacity (0 for default of 64)
 *
 * Returns: 0 on success, -1 on allocation failure
 *
 * The batch buffer will grow automatically as nodes are added.
 * Call rb_batch_destroy() when done to free resources.
 */
int rb_batch_init(rb_batch_t *batch, size_t initial_capacity);

/**
 * Free resources associated with a batch context.
 * @batch: Pointer to the batch context
 *
 * This only frees the internal buffer, not the nodes themselves.
 * The nodes remain valid for use in the tree.
 */
void rb_batch_destroy(rb_batch_t *batch);

/**
 * Add a node to the batch buffer.
 * @batch: Pointer to the batch context
 * @node: Node to add to the batch (must be uninitialized)
 *
 * Returns: 0 on success, -1 on allocation failure
 *
 * The buffer grows automatically if needed. Nodes are not
 * modified until rb_batch_commit() is called.
 */
int rb_batch_add(rb_batch_t *batch, rb_node_t *node);

/**
 * Commit all batched nodes to a red-black tree.
 * @tree: Target red-black tree
 * @batch: Batch context containing nodes to insert
 *
 * For empty trees: Sorts nodes and builds a balanced tree in O(n) time.
 * For non-empty trees: Falls back to individual insertions.
 *
 * After commit, the batch is automatically cleared and can be reused.
 * The tree's comparison function is used for sorting and insertion.
 */
void rb_batch_commit(rb_t *tree, rb_batch_t *batch);

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/**
 * Commit all batched nodes to a cached red-black tree.
 * @tree: Target cached red-black tree
 * @batch: Batch context containing nodes to insert
 *
 * Same as rb_batch_commit() but updates cached min/max pointers.
 */
void rb_cached_batch_commit(rb_cached_t *tree, rb_batch_t *batch);
#endif
#endif /* _RB_ENABLE_BATCH_OPS */

#if _RB_ENABLE_PROPERTY_VALIDATION
/* Property-Based Invariant Testing Support
 *
 * Comprehensive validation of red-black tree properties for testing and
 * debugging. These functions verify all invariants are maintained after tree
 * operations.
 *
 * See https://dl.acm.org/doi/10.1145/3597503.3639581
 */

/**
 * Result structure for red-black tree validation.
 *
 * Contains detailed information about tree validity, including specific
 * property violations and tree statistics. Used by validation functions
 * to report comprehensive analysis results.
 */
typedef struct {
    bool valid;        /**< Overall validity - true if all properties hold */
    size_t node_count; /**< Total number of nodes in the tree */
    int black_height;  /**< Black height (consistent across all paths) */

    /* The 5 Fundamental Red-Black Tree Properties */
    bool node_colors;      /**< Property 1: Every node is either red or black */
    bool null_nodes_black; /**< Property 2: All null nodes are considered black
                            */
    bool red_children_black; /**< Property 3: Red nodes have only black children
                              */
    bool black_height_consistent; /**< Property 4: All paths have same black
                                     height */
    bool single_child_red; /**< Property 5: Single children must be red */

    /* Additional validation checks */
    bool root_is_black; /**< Root node color property (implied by properties) */
    bool bst_property;  /**< Binary search tree ordering property */
    bool cache_consistency; /**< Cached min/max pointers are correct */

    /* Detailed error information */
    const char *error_msg;  /**< Description of first violation found */
    rb_node_t *error_node;  /**< Node where violation was detected */
    int violation_property; /**< Which property was violated (1-5, 0 for other)
                             */
} rb_validation_t;

/**
 * Validate all red-black tree properties.
 * @tree: Pointer to the red-black tree structure
 *
 * Performs comprehensive validation of all red-black tree invariants:
 * 1. Root is black (or NULL)
 * 2. Red nodes have only black children
 * 3. All paths from root to leaves have the same black height
 * 4. Binary search tree ordering is maintained
 *
 * Returns: Validation result with detailed analysis
 * Complexity: O(N) where N is the number of nodes
 *
 * Note: This function is intended for testing and debugging. It should
 * not be used in production code due to performance overhead.
 */
rb_validation_t rb_validate_tree(rb_t *tree);

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/**
 * Validate cached red-black tree properties.
 * @tree: Pointer to the cached red-black tree structure
 *
 * Validates both standard red-black properties and cached tree consistency:
 * - All standard red-black tree properties
 * - Leftmost cache points to actual minimum (if enabled)
 * - Rightmost cache points to actual maximum (if enabled)
 * - Cache pointers are NULL iff tree is empty
 *
 * Returns: Validation result with cache-specific analysis
 */
rb_validation_t rb_validate_cached_tree(rb_cached_t *tree);
#endif

/**
 * Print detailed validation report to stderr.
 * @result: Validation result from rb_validate_tree()
 *
 * Outputs human-readable analysis of tree properties and any violations
 * found. Useful for debugging test failures and understanding tree state.
 */
void rb_print_validation_report(const rb_validation_t *result);

#endif /* _RB_ENABLE_PROPERTY_VALIDATION */

#endif /* _RBTREE_H_ */
