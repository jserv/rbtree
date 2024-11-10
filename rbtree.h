/*
 * This red-black tree implementation is optimized for minimal runtime memory
 * usage.
 *
 * The data structure is intrusive, meaning the rbnode handle is intended to be
 * embedded within a separate struct, similar to other structures like linked
 * lists. No data pointer is stored in the node.
 *
 * The color bit is combined with a pointer. Notably, there is no 'parent'
 * pointer stored in the node. Instead, the upper structure of the tree is
 * constructed dynamically using a stack during tree traversal. Thus, the
 * overall memory overhead of a node is limited to just two pointers, similar
 * to a doubly-linked list.
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
// #define RBTREE_DISABLE_ALLOCA 1

#if !defined(RBTREE_DISABLE_ALLOCA)
#if defined(__GNUC__) || defined(__clang__)
#define alloca __builtin_alloca
#else
#include <alloca.h>
#endif
#endif

/* red-black tree node */
struct rbnode {
    struct rbnode *children[2];
};

/* Maximum theoretical depth of the tree, calculated based on pointer size.
 * Assuming the memory is entirely filled with nodes containing two pointers,
 * and considering that the tree may be twice as deep as a perfect binary tree,
 * including the root node. For 32-bit pointers, the maximum depth is 59 nodes,
 * while for 64-bit pointers, it is 121 nodes.
 */
#define RBTREE_TBITS(t) ((sizeof(t)) < 8 ? 2 : 3)
#define RBTREE_PBITS(t) (8 * sizeof(t))
#define RBTREE_MAX_DEPTH \
    (2 * (RBTREE_PBITS(int *) - RBTREE_TBITS(int *) - 1) + 1)

/* Red-black tree comparison predicate.
 *
 * Compares two nodes and returns true if node A is strictly less than node B,
 * based on the tree's defined sorting criteria. Returns false otherwise.
 *
 * During insertion, the node being inserted is always designated as "A", while
 * "B" refers to the existing node within the tree for comparison. This behavior
 * can be leveraged (with caution) to implement "most/least recently added"
 * semantics for nodes that would otherwise have equal
 * comparison values.
 */
typedef bool (*rb_lessthan_t)(struct rbnode *a, struct rbnode *b);

/* Red-black tree structure */
struct rbtree {
    /** Root node of the tree */
    struct rbnode *root;
    /** Comparison function for nodes in the tree */
    rb_lessthan_t lessthan_fn;
    int max_depth;
#ifdef RBTREE_DISABLE_ALLOCA
    struct rbnode *iter_stack[RBTREE_MAX_DEPTH];
    bool iter_left[RBTREE_MAX_DEPTH];
#endif
};

/* Prototype for node visitor callback */
typedef void (*rb_visit_t)(struct rbnode *node, void *cookie);

struct rbnode *__rb_child(struct rbnode *node, uint8_t side);
int __rb_is_black(struct rbnode *node);
struct rbnode *__rb_get_minmax(struct rbtree *tree, uint8_t side);

/* Insert a node into the red-black tree */
void rb_insert(struct rbtree *tree, struct rbnode *node);

/* Remove a node from the red-black tree */
void rb_remove(struct rbtree *tree, struct rbnode *node);

/* Return the lowest-sorted member of the red-black tree */
static inline struct rbnode *rb_get_min(struct rbtree *tree)
{
    return __rb_get_minmax(tree, 0U);
}

/* Return the highest-sorted member of the red-black tree */
static inline struct rbnode *rb_get_max(struct rbtree *tree)
{
    return __rb_get_minmax(tree, 1U);
}

/* Check if the given node is present in the red-black tree.
 *
 * This function does not dereference the node pointer internally (though the
 * tree's "lessthan" callback might). It only tests for equality with nodes
 * already in the tree. As a result, it can be used to implement a "set"
 * construct by simply comparing the pointer value directly.
 */
bool rb_contains(struct rbtree *tree, struct rbnode *node);

struct _rb_foreach {
    struct rbnode **stack;
    bool *is_left;
    int32_t top;
};

#ifdef RBTREE_DISABLE_ALLOCA
#define _RB_FOREACH_INIT(tree, node)      \
    {                                     \
        .stack = &(tree)->iter_stack[0],  \
        .is_left = &(tree)->iter_left[0], \
        .top = -1,                        \
    }
#else
#define _RB_FOREACH_INIT(tree, node)                                  \
    {                                                                 \
        .stack = alloca((tree)->max_depth * sizeof(struct rbnode *)), \
        .is_left = alloca((tree)->max_depth * sizeof(bool)),          \
        .top = -1,                                                    \
    }
#endif

struct rbnode *__rb_foreach_next(struct rbtree *tree, struct _rb_foreach *f);

/* Perform an in-order traversal of the tree without recursion.
 *
 * This macro provides a non-recursive "foreach" loop for iterating through the
 * red-black tree, offering a moderate trade-off in code size.
 *
 * Note that the loop is not safe for concurrent modifications. Any changes to
 * the tree structure during iteration may result in incorrect behavior, such as
 * nodes being skipped or visited multiple times.
 *
 * Additionally, the macro expands its arguments multiple times, so they should
 * not be expressions with side effects.
 *
 * @param tree A pointer to a "struct rbtree" to traverse
 * @param node The name of a local "struct rbnode *" variable to use as the
 *             iterator
 */
#define RB_FOREACH(tree, node)                                  \
    for (struct _rb_foreach __f = _RB_FOREACH_INIT(tree, node); \
         ((node) = __rb_foreach_next((tree), &__f));            \
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

/* Iterate over an rbtree with implicit container field handling.
 *
 * Similar to RB_FOREACH(), but the "node" can be any type that includes a
 * "struct rbnode" member.
 * @param tree  A pointer to the "struct rbtree" to traverse.
 * @param node  The name of the local iterator variable.
 * @param field The name of the "struct rbnode" field within the node.
 */
#define RB_FOREACH_CONTAINER(tree, node, field)                               \
    for (struct _rb_foreach __f = _RB_FOREACH_INIT(tree, node); ({            \
             struct rbnode *n = __rb_foreach_next(tree, &__f);                \
             (node) = n ? container_of(n, __typeof__(*(node)), field) : NULL; \
             (node);                                                          \
         });                                                                  \
         /**/)

#endif /* _RBTREE_H_ */
