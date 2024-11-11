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
#define _RB_DISABLE_ALLOCA 0

#if _RB_DISABLE_ALLOCA == 0
#if defined(__GNUC__) || defined(__clang__)
#define alloca __builtin_alloca
#else
#include <alloca.h>
#endif
#endif

typedef enum { RB_LEFT = 0, RB_RIGHT = 1 } rb_side_t;

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
    int max_depth;
#if _RB_DISABLE_ALLOCA != 0
    rb_node_t *iter_stack[_RB_MAX_TREE_DEPTH];
    bool iter_left[_RB_MAX_TREE_DEPTH];
#endif
} rb_t;

/* forward declaration for helper functions, used for inlining */
rb_node_t *__rb_child(rb_node_t *node, rb_side_t side);
int __rb_is_black(rb_node_t *node);
rb_node_t *__rb_get_minmax(rb_t *tree, rb_side_t side);

/* Insert a new node into the red-black tree.
 * @tree: Pointer to the red-black tree.
 * @node: Pointer to the node to be inserted.
 *
 * This function initializes the new node, finds its insertion point,
 * and adjusts the tree to maintain red-black properties. It handles
 * the root case, allocates the traversal stack, performs the insertion,
 * and fixes any violations caused by the insertion.
 */
void rb_insert(rb_t *tree, rb_node_t *node);

/* Remove a node from the red-black tree.
 * @tree: Pointer to the red-black tree.
 * @node: Pointer to the node to be removed.
 *
 * This function handles the removal of a node from the red-black tree,
 * rebalancing the tree as necessary to maintain red-black properties.
 */
void rb_remove(rb_t *tree, rb_node_t *node);

/* Return the lowest-sorted member of the red-black tree */
static inline rb_node_t *rb_get_min(rb_t *tree)
{
    return __rb_get_minmax(tree, RB_LEFT);
}

/* Return the highest-sorted member of the red-black tree */
static inline rb_node_t *rb_get_max(rb_t *tree)
{
    return __rb_get_minmax(tree, RB_RIGHT);
}

/* Check if the given node is present in the red-black tree.
 * @tree: Pointer to the red-black tree.
 * @node: Pointer to the node to search for.
 *
 * This function searches the tree to determine if the specified node is
 * present. It starts from the root and traverses the tree based on the
 * comparison function until it finds the node or reaches a leaf. The function
 * does not internally dereference the node pointer (though the tree's
 * "cmp_func" callback might); it only tests for pointer equality with nodes
 * already in the tree. As a result, this function can be used to implement a
 * "set" construct by comparing pointers directly.
 */
bool rb_contains(rb_t *tree, rb_node_t *node);

/* Helper structure for non-recursive red-black tree traversal.
 *
 * This structure is used by the RB_FOREACH and RB_FOREACH_CONTAINER macros
 * to perform an in-order traversal of a red-black tree without recursion. It
 * maintains a dynamic stack of nodes and a corresponding array to indicate
 * whether each node is a left child of its parent.
 */
typedef struct {
    rb_node_t **stack; /**< Hold the nodes encountered during traversal */
    bool *is_left;     /**< Track the relationship of each node to its parent */
    int32_t top;       /**< Keeps track of the current position in the stack */
} rb_foreach_t;

#if _RB_DISABLE_ALLOCA == 0
#define _RB_FOREACH_INIT(tree, node)                              \
    {                                                             \
        .stack = alloca((tree)->max_depth * sizeof(rb_node_t *)), \
        .is_left = alloca((tree)->max_depth * sizeof(bool)),      \
        .top = -1,                                                \
    }
#else
#define _RB_FOREACH_INIT(tree, node)      \
    {                                     \
        .stack = &(tree)->iter_stack[0],  \
        .is_left = &(tree)->iter_left[0], \
        .top = -1,                        \
    }
#endif

rb_node_t *__rb_foreach_next(rb_t *tree, rb_foreach_t *f);

/* In-order traversal of a red-black tree without recursion.
 * @tree: Pointer to the red-black tree ('rb_t') to traverse.
 * @node: Name of a local variable of type 'rb_node_t *' to use as the iterator.
 *
 * This macro performs an in-order traversal of the red-black tree using a
 * non-recursive approach. It sets up a "foreach" loop for iterating through
 * the nodes of the tree in sorted order. The macro avoids recursion by using
 * an internal stack for traversal, providing a balance between code size
 * and efficiency.
 *
 * Notes:
 * - This loop is not safe for concurrent modifications. Changing the tree
 *   structure during traversal may result in undefined behavior, such as
 *   nodes being skipped or visited multiple times.
 * - The macro expands its arguments multiple times. Avoid using expressions
 *   with side effects (e.g., function calls) as arguments.
 */
#define RB_FOREACH(tree, node)                            \
    for (rb_foreach_t __f = _RB_FOREACH_INIT(tree, node); \
         ((node) = __rb_foreach_next((tree), &__f));      \
         /**/)

#ifndef container_of
/* Compute the address of the object containing a given member.
 * @ptr:    Pointer to the member variable.
 * @type:   Type of the structure that includes the member.
 * @member: Name of the member variable in the structure @type.
 * Return a pointer to the enclosing object of type @type.
 */
#define container_of(ptr, type, member)                              \
    __extension__({                                                  \
        const __typeof__(((type *) 0)->member) *(__pmember) = (ptr); \
        (type *) ((char *) __pmember - offsetof(type, member));      \
    })
#endif

/* In-order traversal of a red-black tree with container handling.
 * @tree:  Pointer to the red-black tree ('rb_t') to traverse.
 * @node:  Name of the local iterator variable, which is a pointer to the
 * container type.
 * @field: Name of the `rb_node_t` member within the container struct.
 *
 * This macro performs an in-order traversal of a red-black tree, similar to
 * RB_FOREACH(). However, instead of iterating over raw 'rb_node_t' nodes, it
 * iterates over user-defined container structs that embed an `rb_node_t`
 * member. The macro automatically resolves the container type using the
 * 'container_of' macro.
 */
#define RB_FOREACH_CONTAINER(tree, node, field)                               \
    for (rb_foreach_t __f = _RB_FOREACH_INIT(tree, node); ({                  \
             rb_node_t *n = __rb_foreach_next(tree, &__f);                    \
             (node) = n ? container_of(n, __typeof__(*(node)), field) : NULL; \
             (node) != NULL;                                                  \
         });                                                                  \
         /**/)

#endif /* _RBTREE_H_ */
