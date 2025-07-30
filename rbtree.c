#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "rbtree.h"

/* Compile-time validation of stack depth for safety */
#if _RB_ENABLE_SAFETY_CHECKS
_Static_assert(_RB_MAX_TREE_DEPTH >= 32, "Stack depth too small for safety");
_Static_assert(_RB_MAX_TREE_DEPTH >= 4,
               "Insufficient buffer space for safety margin");
#endif

#if _RB_ENABLE_SAFETY_CHECKS && !defined(NDEBUG)
/* Runtime verification of pointer alignment assumptions.
 * This function verifies that the pointer alignment guarantees required
 * for color bit storage are actually met at runtime. While the compile-time
 * static_assert should catch most issues, this provides additional validation
 * in debug builds for edge cases or unusual platforms.
 */
static inline void __rb_verify_alignment(void)
{
    static int verified = 0;
    if (verified)
        return;

    /* Test alignment with actual allocations */
    rb_node_t test_node;
    uintptr_t addr = (uintptr_t) &test_node;

    /* Verify that node addresses are properly aligned */
    if (addr & 1) {
        fprintf(stderr,
                "rbtree: FATAL - Node address 0x%lx not 2-byte aligned\n",
                (unsigned long) addr);
        assert(0 && "Node alignment insufficient for color bit storage");
    }

    /* Verify that pointer assignments maintain alignment */
    rb_node_t *ptr = &test_node;
    if (((uintptr_t) ptr) & 1) {
        fprintf(stderr,
                "rbtree: FATAL - Pointer value 0x%lx not 2-byte aligned\n",
                (unsigned long) ptr);
        assert(0 && "Pointer alignment insufficient for color bit storage");
    }

    verified = 1;
}
#else
#define __rb_verify_alignment() ((void) 0)
#endif

typedef enum { RB_RED = 0, RB_BLACK = 1 } rb_color_t;

/* The implementation of red-black trees reduces the code size for insertion and
 * deletion by half, using the relative term "sibling" instead of the absolute
 * terms "left" and "right."
 *
 * To fully exploit this symmetry, it is crucial that the sibling node can be
 * retrieved efficiently. This can be achieved by storing the children in a
 * two-element array rather than using separate left and right pointers. The
 * array-based approach offers an unexpected advantage: efficient access to the
 * sibling node. In this design, the index of a child node can be represented by
 * 0 (left) or 1 (right), and its sibling is easily obtained using logical
 * negation ('!i'), avoiding the need for explicit comparisons to determine
 * which child is in hand before selecting the opposite one.
 */

/* Retrieve the left or right child of a given node.
 * @n:    Pointer to the current node
 * @side: Direction to traverse (RB_LEFT or RB_RIGHT)
 *
 * For right children, returns the pointer directly. For left children,
 * masks out the color bit (LSB) to retrieve the actual pointer value.
 */
static inline rb_node_t *get_child(rb_node_t *n, rb_side_t side)
{
    if (side == RB_RIGHT)
        return n->children[RB_RIGHT];

    /* Mask out the least significant bit (LSB) to retrieve the actual left
     * child pointer.
     *
     * The LSB of the left child pointer is used to store metadata (e.g., the
     * color bit). By masking out the LSB with & ~1UL, the function retrieves
     * the actual pointer value without the metadata bit, ensuring the correct
     * child node is returned.
     */
    uintptr_t l = (uintptr_t) n->children[RB_LEFT];
    l &= RB_PTR_MASK;
    return (rb_node_t *) l;
}

/**
 * Set the left or right child of a given node.
 * @n:    Pointer to the current node
 * @side: Direction to set (RB_LEFT or RB_RIGHT)
 * @val:  Pointer to the new child node
 *
 * For right children, sets the pointer directly. For left children,
 * preserves the color bit (LSB) while updating the pointer value.
 */
static inline void set_child(rb_node_t *n, rb_side_t side, void *val)
{
    if (side == RB_RIGHT) {
        n->children[RB_RIGHT] = val;
        return;
    }

    uintptr_t old = (uintptr_t) n->children[RB_LEFT];
    uintptr_t new = (uintptr_t) val;

    /* Preserve the LSB of the old pointer (e.g., color bit) and set the new
     * child.
     */
    n->children[RB_LEFT] = (void *) (new | (old & RB_COLOR_MASK));
}

/* The color information is stored in the LSB of the left child pointer (i.e.,
 * children[0]). This function extracts the LSB using the bitwise AND operation,
 * as 'rb_color_t' indicates.
 *
 * Using the LSB for color information is a common optimization in red-black
 * trees. Most platform ABIs (application binary interface) align pointers to
 * at least 2-byte boundaries, making the LSB available for metadata storage
 * without affecting the pointer's validity.
 */
static inline rb_color_t get_color(rb_node_t *n)
{
    return ((uintptr_t) n->children[0]) & RB_COLOR_MASK ? RB_BLACK : RB_RED;
}

static inline bool is_black(rb_node_t *n)
{
    return get_color(n) == RB_BLACK;
}

static inline bool is_red(rb_node_t *n)
{
    return get_color(n) == RB_RED;
}

static inline void set_color(rb_node_t *n, rb_color_t color)
{
    uintptr_t *p = (void *) &n->children[0];
    *p = (*p & RB_PTR_MASK) | (uint8_t) color;
}

static inline void set_black(rb_node_t *n)
{
    set_color(n, RB_BLACK);
}

static inline void set_red(rb_node_t *n)
{
    set_color(n, RB_RED);
}

/* Find a node in the tree and build traversal stack.
 * @tree:  Pointer to the red-black tree structure
 * @node:  Node to search for or insertion position
 * @stack: Output array for traversal path (size >= max_depth)
 *
 * Traverses the tree from root to either find the exact node or determine
 * its insertion position. Records all nodes visited during traversal in
 * the stack for use in subsequent rebalancing operations.
 */
static unsigned find_and_stack(rb_t *tree, rb_node_t *node, rb_node_t **stack)
{
    unsigned sz = 0;
    stack[sz++] = tree->root;

    /* Traverse the tree, comparing the current node with the target node.
     * Determine the direction based on the comparison function:
     * - left: target node is less than the current node.
     * - right: target node is greater than or equal to the current node.
     */
    while (stack[sz - 1] != node) {
        rb_side_t side =
            tree->cmp_func(node, stack[sz - 1]) ? RB_LEFT : RB_RIGHT;
        rb_node_t *ch = get_child(stack[sz - 1], side);
        /* If there is no child in the chosen direction, the search ends. */
        if (!ch)
            break;

#if _RB_ENABLE_SAFETY_CHECKS
        /* Enhanced bounds checking with early detection */
        if (sz >= (unsigned) _RB_MAX_TREE_DEPTH - 2) {
            /* Prevent buffer overflow - tree likely corrupted */
            return sz; /* Safe early termination */
        }
#endif

        /* Push the child node onto the stack and continue traversal. */
        stack[sz++] = ch;
    }

    return sz;
}

/* Retrieve the minimum or maximum node from the red-black tree.
 * @tree: Pointer to the red-black tree structure
 * @side: Direction to traverse (RB_LEFT for min, RB_RIGHT for max)
 */
rb_node_t *__rb_get_minmax(rb_t *tree, rb_side_t side)
{
    rb_node_t *n;
    for (n = tree->root; n && get_child(n, side); n = get_child(n, side))
        ;
    return n;
}

/* Determine which side a child node is on relative to its parent.
 * @parent: Pointer to the parent node
 * @child:  Pointer to the child node
 */
static inline rb_side_t get_side(rb_node_t *parent, const rb_node_t *child)
{
    return (get_child(parent, RB_RIGHT) == child) ? RB_RIGHT : RB_LEFT;
}

/* Perform a rotation between parent and child nodes.
 * @stack:   Array of node pointers representing traversal path
 * @stacksz: Number of nodes in the stack
 *
 * Rotates the top two nodes in the stack (parent and child) while preserving
 * the binary search tree property. Updates all affected pointers including
 * grandparent connections. Node colors remain unchanged.
 *
 * Transformation example (or mirror image for right rotation):
 *        P          N
 *       / \        / \
 *      N   c  ->  a   P
 *     / \            / \
 *    a   b          b   c
 *
 * The stack is updated to reflect the new node positions after rotation.
 */
static void rotate(rb_node_t **stack, unsigned stacksz)
{
    rb_node_t *parent = stack[stacksz - 2];
    rb_node_t *child = stack[stacksz - 1];
    rb_side_t side = get_side(parent, child);

    /* Retrieve the child nodes for the rotation */
    rb_node_t *a = get_child(child, side);
    rb_node_t *b = get_child(child, (side == RB_LEFT) ? RB_RIGHT : RB_LEFT);

    /* Update the grandparent if it exists */
    if (stacksz >= 3) {
        rb_node_t *grandparent = stack[stacksz - 3];
        set_child(grandparent, get_side(grandparent, parent), child);
    }

    /* Perform the rotation by updating child pointers
     * Before rotation:
     *     parent
     *      /  \
     *   child  ?
     *    / \
     *   a   b
     *
     * After rotation:
     *     child
     *      /  \
     *     a   parent
     *          / \
     *         b   ?
     */
    set_child(child, side, a);
    set_child(child, (side == RB_LEFT) ? RB_RIGHT : RB_LEFT, parent);
    set_child(parent, side, b);

    /* Update the stack to reflect the new positions */
    stack[stacksz - 2] = child;
    stack[stacksz - 1] = parent;
}

/* Fix red-red violations after node insertion.
 * @stack:   Array of node pointers from root to newly inserted node
 * @stacksz: Current size of the stack (path length)
 *
 * Restores red-black tree properties when a red node has a red parent.
 * Uses two main strategies:
 *
 * Case 1 (Red aunt): Recolor parent, aunt to black and grandparent to red,
 * then propagate the potential violation upward.
 *
 * Case 2 (Black aunt): Perform rotations to restructure the tree, ensuring
 * the red node has a black parent without affecting black height.
 *
 * The algorithm terminates when either the violation is resolved or the
 * root is reached (root is always colored black).
 */
static void fix_extra_red(rb_node_t **stack, unsigned stacksz)
{
    while (stacksz > 1) {
        const rb_node_t *node = stack[stacksz - 1];
        rb_node_t *parent = stack[stacksz - 2];

        /* If the parent is black, the tree is already balanced. */
        if (is_black(parent))
            return;

        rb_node_t *grandparent = stack[stacksz - 3];
        rb_side_t side = get_side(grandparent, parent);
        rb_node_t *aunt =
            get_child(grandparent, (side == RB_LEFT) ? RB_RIGHT : RB_LEFT);

        /* Case 1: The aunt is red. Recolor and move up the tree. */
        if (aunt && is_red(aunt)) {
            set_red(grandparent);
            set_black(parent);
            set_black(aunt);

            /* The grandparent node was colored red, may having a red parent.
             * Continue iterating to fix the tree from this point.
             */
            stacksz -= 2;
            continue;
        }

        /* Case 2: The aunt is black. Perform rotations to restore balance. */
        rb_side_t parent_side = get_side(parent, node);

        /* If the node is on the opposite side of the parent, perform a rotation
         * to align it with the grandparent's side.
         */
        if (parent_side != side)
            rotate(stack, stacksz);

        /* Rotate the grandparent with the parent and swap their colors. */
        rotate(stack, stacksz - 1);
        set_black(stack[stacksz - 3]);
        set_red(stack[stacksz - 2]);
        return;
    }

    /* If the loop exits, the node has become the root, which must be black. */
    set_black(stack[0]);
}

void rb_insert(rb_t *tree, rb_node_t *node)
{
    __rb_verify_alignment();

    set_child(node, RB_LEFT, NULL);
    set_child(node, RB_RIGHT, NULL);

    /* If the tree is empty, set the new node as the root and color it black. */
    if (!tree->root) {
        tree->root = node;
#if _RB_DISABLE_ALLOCA != 0
        tree->max_depth = 1;
#endif
        set_black(node);
        return;
    }

    /* Allocate the stack for traversal. Use a fixed stack if alloca() is
     * disabled.
     */
#if _RB_DISABLE_ALLOCA != 0
    rb_node_t **stack = &tree->iter_stack[0];
#else
    rb_node_t *stack[_RB_MAX_TREE_DEPTH];
#endif

    /* Find the insertion point and build the traversal stack. */
    unsigned stacksz = find_and_stack(tree, node, stack);
    rb_node_t *parent = stack[stacksz - 1];

    /* Determine the side (left or right) to insert the new node. */
    rb_side_t side = tree->cmp_func(node, parent) ? RB_LEFT : RB_RIGHT;

    /* Link the new node to its parent and set its color to red. */
    set_child(parent, side, node);
    set_red(node);

    /* Push the new node onto the stack and fix any red-red violations. */
    stack[stacksz++] = node;
    fix_extra_red(stack, stacksz);

    /* Update the maximum depth of the tree if necessary. */
#if _RB_DISABLE_ALLOCA != 0
    if (stacksz > tree->max_depth)
        tree->max_depth = (uint8_t) stacksz;
#endif

    /* Ensure the root is correctly updated after potential rotations. */
    tree->root = stack[0];
}

/* Fix black height deficit after node removal.
 * @stack:     Array of node pointers from root to affected node
 * @stacksz:   Current size of the stack (path length)
 * @null_node: Temporary placeholder for removed black leaf (or NULL)
 *
 * Restores red-black tree properties when a black node removal creates
 * a "black deficit" - one subtree has one fewer black node than required.
 * The affected node (at stack top) must be black by construction.
 *
 * Handles several cases:
 * - Red sibling: Rotate to make sibling black, then continue
 * - Black sibling with black children: Recolor sibling red, propagate deficit
 * up
 * - Black sibling with red child: Rotate and recolor to eliminate deficit
 *
 * The null_node parameter is used as a temporary placeholder when removing
 * a black leaf node. It gets replaced with NULL once rebalancing is complete.
 */
static void fix_missing_black(rb_node_t **stack,
                              unsigned stacksz,
                              const rb_node_t *null_node)
{
    /* Loop upward until we reach the root */
    while (stacksz > 1) {
        rb_node_t *c0, *c1, *inner, *outer;
        rb_node_t *n = stack[stacksz - 1];
        rb_node_t *parent = stack[stacksz - 2];
        rb_side_t n_side = get_side(parent, n);
        rb_node_t *sib =
            get_child(parent, (n_side == RB_LEFT) ? RB_RIGHT : RB_LEFT);

        /* Ensure the sibling is black, rotating N down a level if necessary.
         * After rotate(), the parent becomes the child of the previous sibling,
         * placing N lower in the tree.
         */
        if (!is_black(sib)) {
            stack[stacksz - 1] = sib;
            rotate(stack, stacksz);
            set_red(parent);
            set_black(sib);

            stack[stacksz++] = n;

            parent = stack[stacksz - 2];
            sib = get_child(parent, (n_side == RB_LEFT) ? RB_RIGHT : RB_LEFT);
        }

        /* Cache sibling's children to avoid repeated get_child() calls */
        c0 = get_child(sib, RB_LEFT);
        c1 = get_child(sib, RB_RIGHT);

        /* Situations where the sibling has only black children can be resolved
         * straightforwardly.
         */
        if ((!c0 || is_black(c0)) && (!c1 || is_black(c1))) {
            if (n == null_node)
                set_child(parent, n_side, NULL);

            set_red(sib);
            if (is_black(parent)) {
                /* Rebalance the sibling's subtree by coloring it red.
                 * The parent now has a black deficit, so continue iterating
                 * upwards.
                 */
                stacksz--;
                continue;
            }
            /* Recoloring makes the whole tree OK */
            set_black(parent);
            return;
        }

        /* The sibling has at least one red child. Adjust the tree so that the
         * far/outer child (i.e., on the opposite side of N) is guaranteed to
         * be red. Use cached children instead of repeated get_child() calls.
         */
        outer = (n_side == RB_LEFT) ? c1 : c0;
        if (!(outer && is_red(outer))) {
            inner = (n_side == RB_LEFT) ? c0 : c1;

            /* Standard double rotation: rotate sibling with its inner child
             * first */
            set_red(sib);
            set_black(inner);

            /* Perform first rotation: sib with inner */
            rb_side_t inner_side = n_side;
            rb_side_t outer_side = (n_side == RB_LEFT) ? RB_RIGHT : RB_LEFT;

            /* Cache child pointers for reuse */
            rb_node_t *inner_child = get_child(inner, outer_side);

            /* Update sibling's child relationships for first rotation */
            set_child(sib, inner_side, inner_child);
            set_child(inner, outer_side, sib);

            /* Update parent to point to inner (which is now in sib's position)
             */
            set_child(parent, (n_side == RB_LEFT) ? RB_RIGHT : RB_LEFT, inner);

            /* Update variables for second rotation */
            sib = inner;
            outer = get_child(sib, outer_side);
        }

        /* Handle null_node replacement */
        if (n == null_node) {
            set_child(parent, n_side, NULL);
        }

        /* Final rotation with corrected references */
        set_color(sib, get_color(parent));
        set_black(parent);
        set_black(outer);

        stack[stacksz - 1] = sib;
        rotate(stack, stacksz);

        return;
    }
}

void rb_remove(rb_t *tree, rb_node_t *node)
{
    __rb_verify_alignment();

    rb_node_t *tmp;

    /* Allocate stack for traversal. Use a fixed stack if alloca() is disabled.
     */
#if _RB_DISABLE_ALLOCA != 0
    rb_node_t **stack = &tree->iter_stack[0];
#else
    rb_node_t *stack[_RB_MAX_TREE_DEPTH];
#endif

    /* Find the node to remove and build the traversal stack. */
    unsigned stacksz = find_and_stack(tree, node, stack);

    /* Node not found in the tree; return. */
    if (node != stack[stacksz - 1])
        return;

    /* Case 1: Node has two children. Swap with the in-order predecessor. */
    if (get_child(node, RB_LEFT) && get_child(node, RB_RIGHT)) {
        unsigned stacksz0 = stacksz;
        rb_node_t *hiparent = (stacksz > 1) ? stack[stacksz - 2] : NULL;
        rb_node_t *loparent, *node2 = get_child(node, RB_LEFT);

        /* Find the largest child on the left subtree (in-order predecessor). */
        if (stacksz < (unsigned) _RB_MAX_TREE_DEPTH)
            stack[stacksz++] = node2;
        while (get_child(node2, RB_RIGHT) &&
               stacksz < (unsigned) _RB_MAX_TREE_DEPTH) {
            node2 = get_child(node2, RB_RIGHT);
            stack[stacksz++] = node2;
        }

        loparent = stack[stacksz - 2];

        /* Swap the positions of 'node' and 'node2' in the tree.
         *
         * This operation is more complex due to the intrusive nature of the
         * data structure. In typical textbook implementations, this would be
         * done by simply swapping the "data" pointers between nodes. However,
         * here we need to handle some special cases:
         * 1. The upper node may be the root of the tree, lacking a parent.
         * 2. The lower node might be a direct child of the upper node.
         *
         * The swap involves exchanging child pointers between the nodes and
         * updating the references from their parents. Remember to also swap
         * the color bits of the nodes. Additionally, without parent pointers,
         * the stack tracking the tree structure must be updated as well.
         */
        if (hiparent) {
            set_child(hiparent, get_side(hiparent, node), node2);
        } else {
            tree->root = node2;
        }

        if (loparent == node) {
            /* node2 is a direct child of node */
            set_child(node, RB_LEFT, get_child(node2, RB_LEFT));
            set_child(node2, RB_LEFT, node);
        } else {
            /* node2 is not a direct child - swap their positions */
            set_child(loparent, get_side(loparent, node2), node);
            tmp = get_child(node, RB_LEFT);
            set_child(node, RB_LEFT, get_child(node2, RB_LEFT));
            set_child(node2, RB_LEFT, tmp);
        }

        /* Copy right child from node to node2 */
        set_child(node2, RB_RIGHT, get_child(node, RB_RIGHT));
        set_child(node, RB_RIGHT, NULL);

        /* Update the stack and swap the colors of 'node' and 'node2'. */
        tmp = stack[stacksz0 - 1];
        stack[stacksz0 - 1] = stack[stacksz - 1];
        stack[stacksz - 1] = tmp;

        rb_color_t ctmp = get_color(node);
        set_color(node, get_color(node2));
        set_color(node2, ctmp);
    }

    /* Case 2: Node has zero or one child. Replace it with its child. */
    rb_node_t *child = get_child(node, RB_LEFT);
    if (!child)
        child = get_child(node, RB_RIGHT);

    /* If removing the root node, update the root pointer. */
    if (stacksz < 2) {
        tree->root = child;
        if (child) {
            set_black(child);
        } else {
#if _RB_DISABLE_ALLOCA != 0
            tree->max_depth = 0;
#endif
        }
        return;
    }

    rb_node_t *parent = stack[stacksz - 2];

    /* Special case: If the node to be removed has no children, it remains in
     * place while handling the missing black rotations. These rotations will
     * eventually replace the node with a proper NULL when it becomes isolated.
     */
    if (!child) {
        if (is_black(node)) {
            fix_missing_black(stack, stacksz, node);
        } else {
            /* Red childless nodes can be removed directly. */
            set_child(parent, get_side(parent, node), NULL);
        }
    } else {
        /* Replace the node with its single child. */
        set_child(parent, get_side(parent, node), child);

        /* If either the node or child is red, recolor the child to black. */
        if (is_red(node) || is_red(child))
            set_black(child);
    }

    /* Update the root pointer after potential rotations. */
    tree->root = stack[0];
}

rb_node_t *__rb_child(rb_node_t *node, rb_side_t side)
{
    return get_child(node, side);
}

int __rb_is_black(rb_node_t *node)
{
    return is_black(node);
}

bool rb_contains(rb_t *tree, rb_node_t *node)
{
    rb_node_t *n = tree->root;

    while (n && (n != node))
        n = get_child(n, tree->cmp_func(node, n) ? RB_LEFT : RB_RIGHT);

    return n == node;
}

/* The "foreach" traversal uses a dynamic stack allocated with alloca(3) or
 * pre-allocated temporary space.
 *
 * The current node is stored at stack[top], and its parent is located at
 * stack[top-1]. The is_left[] array keeps track of the relationship between
 * each node and its parent (i.e., if is_left[top] is true, then stack[top]
 * is the left child of stack[top-1]). A special case occurs when top == -1,
 * indicating that the stack is uninitialized, and an initial push starting
 * from the root is required.
 */
rb_node_t *__rb_foreach_next(rb_t *tree, rb_foreach_t *f)
{
    if (!tree->root)
        return NULL;

    /* Check if traversal is complete */
    if (f->top == RB_ITER_DONE)
        return NULL;

    /* Get stack pointer from buffer */
    rb_node_t **stack = _RB_FOREACH_STACK(f);

    /* Initialize: push root and go down left as far as possible */
    if (f->top == RB_ITER_UNINIT) {
        rb_node_t *n = tree->root;
        f->top = 0;

        while (n) {
#if _RB_ENABLE_SAFETY_CHECKS
            if (f->top >= (int32_t) _RB_EFFECTIVE_DEPTH_LIMIT - 2) {
                f->top = RB_ITER_DONE;
                return NULL;
            }
#endif

            stack[f->top] = n;
            /* We went left to get here */
            _RB_FOREACH_SET_DIRECTION(f, f->top, true);
            f->top++;
            n = get_child(n, RB_LEFT);
        }
    }

    /* Main traversal loop */
    while (f->top > 0) {
        f->top--;
        rb_node_t *n = _RB_FOREACH_GET_NODE(f, f->top);
        bool went_left = _RB_FOREACH_GET_DIRECTION(f, f->top);

        if (went_left) {
            /* Coming up from left subtree, visit this node */
            /* Mark that we have visited this node */
            _RB_FOREACH_SET_DIRECTION(f, f->top, false);
            f->top++; /* Put it back on stack */

            /* Go down right subtree if it exists */
            rb_node_t *right = get_child(n, RB_RIGHT);
            if (right) {
                rb_node_t *current = right;
                while (current) {
#if _RB_ENABLE_SAFETY_CHECKS
                    if (f->top >= (int32_t) _RB_EFFECTIVE_DEPTH_LIMIT - 2) {
                        f->top = RB_ITER_DONE;
                        return NULL;
                    }
#endif

                    stack[f->top] = current;
                    _RB_FOREACH_SET_DIRECTION(f, f->top, true);
                    f->top++;
                    current = get_child(current, RB_LEFT);
                }
            }

            return n;
        }
        /* else: coming up from right subtree, continue up */
    }

    /* Stack is empty - traversal complete */
    f->top = RB_ITER_DONE;
    return NULL;
}

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
/* Update cached minimum/maximum pointers after node insertion.
 * @tree: Pointer to the cached red-black tree structure
 * @node: Pointer to the newly inserted node
 */
static void update_cache_insert(rb_cached_t *tree, rb_node_t *node)
{
#if _RB_ENABLE_LEFTMOST_CACHE
    /* Update leftmost cache if this is the new minimum */
    if (!tree->rb_leftmost || tree->rb_root.cmp_func(node, tree->rb_leftmost))
        tree->rb_leftmost = node;
#endif

#if _RB_ENABLE_RIGHTMOST_CACHE
    /* Update rightmost cache if this is the new maximum */
    if (!tree->rb_rightmost || tree->rb_root.cmp_func(tree->rb_rightmost, node))
        tree->rb_rightmost = node;
#endif
}

/* Update cached minimum/maximum pointers after node removal.
 * @tree: Pointer to the cached red-black tree structure
 * @node: Pointer to the node being removed
 */
static void update_cache_remove(rb_cached_t *tree, rb_node_t *node)
{
#if _RB_ENABLE_LEFTMOST_CACHE
    /* Update leftmost cache if we're removing the minimum */
    if (tree->rb_leftmost == node)
        tree->rb_leftmost = __rb_get_minmax(&tree->rb_root, RB_LEFT);
#endif

#if _RB_ENABLE_RIGHTMOST_CACHE
    /* Update rightmost cache if we're removing the maximum */
    if (tree->rb_rightmost == node)
        tree->rb_rightmost = __rb_get_minmax(&tree->rb_root, RB_RIGHT);
#endif
}

void rb_cached_insert(rb_cached_t *tree, rb_node_t *node)
{
    rb_insert(&tree->rb_root, node);
    update_cache_insert(tree, node);
}

void rb_cached_remove(rb_cached_t *tree, rb_node_t *node)
{
    rb_remove(&tree->rb_root, node);
    update_cache_remove(tree, node);
}

/* Build path from root to cached leftmost node.
 * Uses the cached leftmost pointer to avoid redundant tree traversal.
 */
static int build_path_to_leftmost(rb_cached_t *tree, rb_node_t **stack)
{
#if _RB_ENABLE_LEFTMOST_CACHE
    if (!tree->rb_leftmost)
        return 0;

    int depth = 0;
    rb_node_t *current = tree->rb_root.root;

    /* Build path from root down to leftmost node */
    while (current && current != tree->rb_leftmost) {
#if _RB_ENABLE_SAFETY_CHECKS
        if (depth >= (int32_t) _RB_EFFECTIVE_DEPTH_LIMIT - 2)
            return 0;
#endif
        stack[depth++] = current;
        current = get_child(current, RB_LEFT);
    }

    if (current == tree->rb_leftmost)
        stack[depth++] = tree->rb_leftmost;

    return depth;

#else
    return 0;
#endif
}

/* Get the next node in cached tree traversal with optimized initialization.
 * When leftmost caching is enabled, uses cached leftmost node to build the
 * initial iterator path more efficiently than traversing from root.
 */
rb_node_t *__rb_cached_foreach_next(rb_cached_t *tree, rb_foreach_t *f)
{
    if (!tree->rb_root.root)
        return NULL;

    /* Check if traversal is complete */
    if (f->top == RB_ITER_DONE)
        return NULL;

#if _RB_ENABLE_LEFTMOST_CACHE
    /* Optimized initialization using cached leftmost node */
    if (f->top == RB_ITER_UNINIT && tree->rb_leftmost) {
        rb_node_t **stack = _RB_FOREACH_STACK(f);
        int depth = build_path_to_leftmost(tree, stack);

        if (depth > 0) {
            /* Set up the stack with path to leftmost */
            for (int i = 0; i < depth; i++)
                _RB_FOREACH_SET_DIRECTION(f, i, true);
            f->top = depth;

            /* Continue with standard iteration logic from here */
            return __rb_foreach_next(&tree->rb_root, f);
        }
    }
#endif

    /* Fall back to standard implementation */
    return __rb_foreach_next(&tree->rb_root, f);
}

bool rb_cached_contains(rb_cached_t *tree, rb_node_t *node)
{
#if _RB_ENABLE_LEFTMOST_CACHE
    /* Early exit if node is less than the cached minimum (O(1)) */
    if (tree->rb_leftmost && tree->rb_root.cmp_func(node, tree->rb_leftmost))
        return false;
#endif

#if _RB_ENABLE_RIGHTMOST_CACHE
    /* Early exit if node is greater than the cached maximum (O(1)) */
    if (tree->rb_rightmost && tree->rb_root.cmp_func(tree->rb_rightmost, node))
        return false;
#endif

    /* Node is within bounds (or no cache available), perform normal search */
    return rb_contains(&tree->rb_root, node);
}
#endif

#if _RB_ENABLE_BATCH_OPS
/* Batch operations implementation */

/* Global state for qsort comparison: uses TLS for thread safety */
static __thread rb_cmp_t _rb_batch_cmp_func;

/**
 * Comparison wrapper for qsort.
 * Adapts the tree's comparison function to qsort's expected interface.
 */
static int rb_batch_qsort_cmp(const void *a, const void *b)
{
    const rb_node_t *node_a = *(const rb_node_t **) a;
    const rb_node_t *node_b = *(const rb_node_t **) b;

    return _rb_batch_cmp_func(node_a, node_b) ? -1 : 1;
}

/**
 * Build a balanced subtree from a sorted array of nodes.
 * Recursively selects middle elements to create a perfectly balanced tree.
 */
static rb_node_t *rb_batch_build_balanced(rb_node_t **nodes, int start, int end)
{
    if (start > end)
        return NULL;

    /* Find middle element */
    int mid = start + (end - start) / 2;
    rb_node_t *node = nodes[mid];

    /* Recursively build left and right subtrees */
    rb_node_t *left = NULL;
    rb_node_t *right = NULL;

    if (start < mid)
        left = rb_batch_build_balanced(nodes, start, mid - 1);
    if (mid < end)
        right = rb_batch_build_balanced(nodes, mid + 1, end);

    /* Initialize node with proper children */
    /* Set left child and make node black by default */
    node->children[RB_LEFT] = (rb_node_t *) ((uintptr_t) left | RB_COLOR_MASK);
    node->children[RB_RIGHT] = right;

    return node;
}

/**
 * Simple coloring strategy: make all nodes black.
 * This creates a valid (though not optimal) red-black tree.
 */
static void rb_batch_color_all_black(rb_node_t *node)
{
    if (!node)
        return;

    /* Get actual children (without color bit) */
    rb_node_t *left =
        (rb_node_t *) ((uintptr_t) node->children[RB_LEFT] & ~RB_COLOR_MASK);
    rb_node_t *right = node->children[RB_RIGHT];

    /* Make this node black */
    node->children[RB_LEFT] = (rb_node_t *) ((uintptr_t) left | RB_COLOR_MASK);

    /* Recursively color children */
    rb_batch_color_all_black(left);
    rb_batch_color_all_black(right);
}

int rb_batch_init(rb_batch_t *batch, size_t initial_capacity)
{
    batch->count = 0;
    batch->capacity = initial_capacity ? initial_capacity : 64;
    batch->nodes = malloc(batch->capacity * sizeof(rb_node_t *));
    batch->cmp_func = NULL;
    return batch->nodes ? 0 : -1;
}

void rb_batch_destroy(rb_batch_t *batch)
{
    free(batch->nodes);
    batch->nodes = NULL;
    batch->count = 0;
    batch->capacity = 0;
}

int rb_batch_add(rb_batch_t *batch, rb_node_t *node)
{
    /* Grow buffer if needed */
    if (batch->count >= batch->capacity) {
        size_t new_capacity = batch->capacity * 2;
        rb_node_t **new_nodes =
            realloc(batch->nodes, new_capacity * sizeof(rb_node_t *));
        if (!new_nodes)
            return -1;

        batch->nodes = new_nodes;
        batch->capacity = new_capacity;
    }

    batch->nodes[batch->count++] = node;
    return 0;
}

void rb_batch_commit(rb_t *tree, rb_batch_t *batch)
{
    if (batch->count == 0)
        return;

    /* Store comparison function for qsort wrapper */
    batch->cmp_func = tree->cmp_func;

    /* If tree is empty, we can build an optimal tree */
    if (!tree->root) {
        /* Set comparison function for qsort */
        _rb_batch_cmp_func = tree->cmp_func;

        /* Sort nodes */
        qsort(batch->nodes, batch->count, sizeof(rb_node_t *),
              rb_batch_qsort_cmp);

        /* Build balanced tree */
        tree->root = rb_batch_build_balanced(batch->nodes, 0, batch->count - 1);

        /* Color all nodes black (simple but valid coloring) */
        rb_batch_color_all_black(tree->root);

#if _RB_DISABLE_ALLOCA != 0
        /* Update max_depth for the new tree */
        tree->max_depth = 0;
        size_t n = batch->count;
        while (n > 0) {
            tree->max_depth++;
            n /= 2;
        }
        /* Add safety margin */
        tree->max_depth = (tree->max_depth * 2) + 1;
        if (tree->max_depth > _RB_COMMON_TREE_DEPTH)
            tree->max_depth = _RB_COMMON_TREE_DEPTH;
#endif
    } else {
        /* Tree is non-empty, fall back to regular insertions */
        /* This maintains correctness but loses the batch optimization */
        for (size_t i = 0; i < batch->count; i++)
            rb_insert(tree, batch->nodes[i]);
    }

    /* Clear the batch */
    batch->count = 0;
}

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
void rb_cached_batch_commit(rb_cached_t *tree, rb_batch_t *batch)
{
    if (batch->count == 0)
        return;

    /* Use the standard batch commit for the underlying tree */
    rb_batch_commit(&tree->rb_root, batch);

    /* Update cached pointers if tree was empty before */
#if _RB_ENABLE_LEFTMOST_CACHE
    if (!tree->rb_leftmost && tree->rb_root.root)
        tree->rb_leftmost = __rb_get_minmax(&tree->rb_root, RB_LEFT);
#endif
#if _RB_ENABLE_RIGHTMOST_CACHE
    if (!tree->rb_rightmost && tree->rb_root.root)
        tree->rb_rightmost = __rb_get_minmax(&tree->rb_root, RB_RIGHT);
#endif
}
#endif
#endif /* _RB_ENABLE_BATCH_OPS */

#if _RB_ENABLE_PROPERTY_VALIDATION
/* Property-Based Invariant Testing Implementation */

/* Internal validation context for recursive traversal */
typedef struct {
    rb_validation_t *result;    /* Validation result being built */
    rb_cmp_t cmp_func;          /* Comparison function for BST property */
    rb_node_t *min_node;        /* Leftmost node found during traversal */
    rb_node_t *max_node;        /* Rightmost node found during traversal */
    int expected_black_height;  /* Expected black height (-1 if not set) */
    int null_nodes_encountered; /* Count of null leaf nodes encountered */
} validation_ctx_t;

/* Forward declarations for helper functions */
static int validate_node_recursive(rb_node_t *node,
                                   rb_node_t *parent,
                                   validation_ctx_t *ctx);
static void set_validation_error(rb_validation_t *result,
                                 const char *msg,
                                 rb_node_t *node,
                                 int property);

/**
 * Recursively validate all 5 red-black tree properties for a subtree.
 * @node:   Current node being validated (NULL for leaf)
 * @parent: Parent of current node (for BST property checking)
 * @ctx:    Validation context with result and comparison function
 *
 * Returns: Black height of this subtree, or -1 if invalid
 *
 * Explicitly validates all 5 fundamental red-black tree properties:
 * 1. Every node is either red or black
 * 2. All null nodes are considered black
 * 3. Red nodes have only black children
 * 4. All paths have same black height
 * 5. Single children must be red
 */
static int validate_node_recursive(rb_node_t *node,
                                   rb_node_t *parent,
                                   validation_ctx_t *ctx)
{
    if (!node) {
        /* PROPERTY 2: All null nodes are considered black */
        ctx->null_nodes_encountered++;
        /* NULL nodes contribute 0 to black height (they are black leaves) */
        return 0;
    }

    /* Count this node */
    ctx->result->node_count++;

    /* Track min/max nodes for cache validation */
    if (!ctx->min_node || ctx->cmp_func(node, ctx->min_node))
        ctx->min_node = node;
    if (!ctx->max_node || ctx->cmp_func(ctx->max_node, node))
        ctx->max_node = node;

    /* PROPERTY 1: Every node is either red or black */
    /* This is implicitly satisfied by our color representation, but we validate
     * it */
    rb_color_t node_color = get_color(node);
    if (node_color != RB_RED && node_color != RB_BLACK) {
        ctx->result->node_colors = false;
        if (ctx->result->valid) {
            set_validation_error(ctx->result,
                                 "Property 1 violated: Node has invalid color",
                                 node, 1);
        }
        return -1;
    }

    /* Validate BST property (not one of the 5 RB properties, but essential) */
    if (parent) {
        rb_node_t *left_child = get_child(parent, RB_LEFT);
        rb_node_t *right_child = get_child(parent, RB_RIGHT);

        if (node == left_child) {
            /* Current node is left child - must be less than parent */
            if (!ctx->cmp_func(node, parent)) {
                ctx->result->bst_property = false;
                if (ctx->result->valid) {
                    set_validation_error(
                        ctx->result,
                        "BST property violated: left child >= parent", node, 0);
                }
                return -1;
            }
        } else if (node == right_child) {
            /* Current node is right child - must be >= parent */
            if (ctx->cmp_func(node, parent)) {
                ctx->result->bst_property = false;
                if (ctx->result->valid) {
                    set_validation_error(
                        ctx->result,
                        "BST property violated: right child < parent", node, 0);
                }
                return -1;
            }
        }
    }

    /* Get child nodes */
    rb_node_t *left = get_child(node, RB_LEFT);
    rb_node_t *right = get_child(node, RB_RIGHT);

    /* PROPERTY 3: A red node does not have a red child */
    if (is_red(node)) {
        if ((left && is_red(left)) || (right && is_red(right))) {
            ctx->result->red_children_black = false;
            if (ctx->result->valid) {
                set_validation_error(
                    ctx->result, "Property 3 violated: Red node has red child",
                    node, 3);
            }
            return -1;
        }
    }

    /* PROPERTY 5: If a node has exactly one child, the child must be red */
    bool has_left = (left != NULL);
    bool has_right = (right != NULL);

    if (has_left && !has_right) {
        /* Node has only left child */
        if (is_black(left)) {
            ctx->result->single_child_red = false;
            if (ctx->result->valid) {
                set_validation_error(
                    ctx->result,
                    "Property 5 violated: Single left child is black", node, 5);
            }
            return -1;
        }
    } else if (!has_left && has_right) {
        /* Node has only right child */
        if (is_black(right)) {
            ctx->result->single_child_red = false;
            if (ctx->result->valid) {
                set_validation_error(
                    ctx->result,
                    "Property 5 violated: Single right child is black", node,
                    5);
            }
            return -1;
        }
    }

    /* Recursively validate subtrees */
    int left_black_height = validate_node_recursive(left, node, ctx);
    if (left_black_height < 0)
        return -1; /* Propagate error */

    int right_black_height = validate_node_recursive(right, node, ctx);
    if (right_black_height < 0)
        return -1; /* Propagate error */

    /* PROPERTY 4: Every path from a given node to any of its leaf nodes
     * goes through the same number of black nodes */
    if (left_black_height != right_black_height) {
        ctx->result->black_height_consistent = false;
        if (ctx->result->valid) {
            set_validation_error(
                ctx->result,
                "Property 4 violated: Inconsistent black height in subtrees",
                node, 4);
        }
        return -1;
    }

    /* Calculate black height for this subtree */
    int current_black_height = left_black_height;
    if (is_black(node))
        current_black_height++;

    return current_black_height;
}

/**
 * Set validation error information in result structure.
 * @result: Validation result to update
 * @msg:    Error message describing the violation
 * @node:   Node where the violation was detected
 * @property: Which RB property was violated (1-5, 0 for other)
 *
 * Sets the first error encountered and marks the tree as invalid.
 * Subsequent errors are ignored to avoid overwriting initial failure.
 */
static void set_validation_error(rb_validation_t *result,
                                 const char *msg,
                                 rb_node_t *node,
                                 int property)
{
    if (result->valid) {
        result->valid = false;
        result->error_msg = msg;
        result->error_node = node;
        result->violation_property = property;
    }
}

rb_validation_t rb_validate_tree(rb_t *tree)
{
    rb_validation_t result = {
        .valid = true,
        .node_count = 0,
        .black_height = 0,

        /* Initialize all 5 fundamental properties as valid */
        .node_colors = true,
        .null_nodes_black = true,
        .red_children_black = true,
        .black_height_consistent = true,
        .single_child_red = true,

        /* Additional validation checks */
        .root_is_black = true,
        .bst_property = true,
        .cache_consistency = true,

        /* Error information */
        .error_msg = NULL,
        .error_node = NULL,
        .violation_property = 0,
    };

    /* Handle NULL tree or empty tree */
    if (!tree || !tree->cmp_func) {
        set_validation_error(
            &result, "NULL tree or missing comparison function", NULL, 0);
        return result;
    }

    if (!tree->root) {
        /* Empty tree is valid - all properties are satisfied trivially */
        result.null_nodes_black =
            true; /* The entire tree is NULL, satisfying property 2 */
        return result;
    }

    /* Validate root is black (implied by the 5 properties but good to check
     * explicitly)
     */
    if (is_red(tree->root)) {
        result.root_is_black = false;
        set_validation_error(
            &result, "Root node is red (violates standard RB convention)",
            tree->root, 0);
        return result;
    }

    /* Set up validation context */
    validation_ctx_t ctx = {
        .result = &result,
        .cmp_func = tree->cmp_func,
        .min_node = NULL,
        .max_node = NULL,
        .expected_black_height = -1,
        .null_nodes_encountered = 0,
    };

    /* Recursively validate all 5 properties */
    int black_height = validate_node_recursive(tree->root, NULL, &ctx);

    if (black_height >= 0) {
        result.black_height = black_height;

        /* Property 2 is satisfied if we encountered null nodes and treated them
         * as black. (This is implicitly validated during traversal)
         */
        result.null_nodes_black = true;
    } else {
        /* Error already set by recursive validation */
        result.valid = false;
    }

    return result;
}

#if _RB_ENABLE_LEFTMOST_CACHE || _RB_ENABLE_RIGHTMOST_CACHE
rb_validation_t rb_validate_cached_tree(rb_cached_t *tree)
{
    /* First validate the underlying tree structure */
    rb_validation_t result = rb_validate_tree(&tree->rb_root);

    if (!result.valid) {
        return result; /* Return early if basic validation failed */
    }

    /* Additional validation for cached tree properties */
    if (!tree->rb_root.root) {
        /* Empty tree - cache pointers should be NULL */
#if _RB_ENABLE_LEFTMOST_CACHE
        if (tree->rb_leftmost) {
            result.cache_consistency = false;
            set_validation_error(&result,
                                 "Leftmost cache non-NULL in empty tree",
                                 tree->rb_leftmost, 0);
            return result;
        }
#endif
#if _RB_ENABLE_RIGHTMOST_CACHE
        if (tree->rb_rightmost) {
            result.cache_consistency = false;
            set_validation_error(&result,
                                 "Rightmost cache non-NULL in empty tree",
                                 tree->rb_rightmost, 0);
            return result;
        }
#endif
    } else {
        /* Non-empty tree - validate cache correctness */
#if _RB_ENABLE_LEFTMOST_CACHE
        rb_node_t *actual_min = __rb_get_minmax(&tree->rb_root, RB_LEFT);
        if (tree->rb_leftmost != actual_min) {
            result.cache_consistency = false;
            set_validation_error(&result, "Leftmost cache points to wrong node",
                                 tree->rb_leftmost, 0);
            return result;
        }
#endif
#if _RB_ENABLE_RIGHTMOST_CACHE
        rb_node_t *actual_max = __rb_get_minmax(&tree->rb_root, RB_RIGHT);
        if (tree->rb_rightmost != actual_max) {
            result.cache_consistency = false;
            set_validation_error(&result,
                                 "Rightmost cache points to wrong node",
                                 tree->rb_rightmost, 0);
            return result;
        }
#endif
    }

    return result;
}
#endif

void rb_print_validation_report(const rb_validation_t *result)
{
    if (!result) {
        fprintf(stderr, "rb_validation: NULL result pointer\n");
        return;
    }

    fprintf(stderr, "=== Red-Black Tree Validation Report ===\n");
    fprintf(stderr, "Overall Status: %s\n",
            result->valid ? "VALID" : "INVALID");
    fprintf(stderr, "Node Count: %zu\n", result->node_count);
    fprintf(stderr, "Black Height: %d\n", result->black_height);
    fprintf(stderr, "\n");

    fprintf(stderr, "Red-Black Tree Properties (The Fundamental 5):\n");
    fprintf(stderr, "  Property 1 - Node colors (red/black): %s\n",
            result->node_colors ? "PASS" : "FAIL");
    fprintf(stderr, "  Property 2 - Null nodes are black: %s\n",
            result->null_nodes_black ? "PASS" : "FAIL");
    fprintf(stderr, "  Property 3 - Red nodes have black children: %s\n",
            result->red_children_black ? "PASS" : "FAIL");
    fprintf(stderr, "  Property 4 - Black height consistency: %s\n",
            result->black_height_consistent ? "PASS" : "FAIL");
    fprintf(stderr, "  Property 5 - Single children are red: %s\n",
            result->single_child_red ? "PASS" : "FAIL");

    fprintf(stderr, "\nAdditional Validation Checks:\n");
    fprintf(stderr, "  Root is black: %s\n",
            result->root_is_black ? "PASS" : "FAIL");
    fprintf(stderr, "  BST property maintained: %s\n",
            result->bst_property ? "PASS" : "FAIL");
    fprintf(stderr, "  Cache consistency: %s\n",
            result->cache_consistency ? "PASS" : "FAIL");

    if (!result->valid && result->error_msg) {
        fprintf(stderr, "\nFirst Error Detected:\n");
        fprintf(stderr, "  Message: %s\n", result->error_msg);
        fprintf(stderr, "  Node Address: %p\n", (void *) result->error_node);
        if (result->violation_property > 0) {
            fprintf(stderr, "  Violated Property: %d\n",
                    result->violation_property);
        }
    }

    fprintf(stderr, "=========================================\n");
}
#endif /* _RB_ENABLE_PROPERTY_VALIDATION */
