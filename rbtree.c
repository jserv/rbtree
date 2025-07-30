#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "rbtree.h"

/* Compile-time validation of stack depth for safety */
#if _RB_ENABLE_SAFETY_CHECKS
_Static_assert(_RB_MAX_TREE_DEPTH >= 32, "Stack depth too small for safety");
_Static_assert(_RB_MAX_TREE_DEPTH >= 4,
               "Insufficient buffer space for safety margin");
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
    set_child(node, RB_LEFT, NULL);
    set_child(node, RB_RIGHT, NULL);

    /* If the tree is empty, set the new node as the root and color it black. */
    if (!tree->root) {
        tree->root = node;
        tree->max_depth = 1;
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
    if (stacksz > tree->max_depth)
        tree->max_depth = (uint8_t) stacksz;

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

        /* Situations where the sibling has only black children can be resolved
         * straightforwardly.
         */
        c0 = get_child(sib, RB_LEFT);
        c1 = get_child(sib, RB_RIGHT);
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
         * be red.
         */
        outer = get_child(sib, (n_side == RB_LEFT) ? RB_RIGHT : RB_LEFT);
        if (!(outer && is_red(outer))) {
            inner = get_child(sib, n_side);

            /* Standard double rotation: rotate sibling with its inner child
             * first */
            set_red(sib);
            set_black(inner);

            /* Perform first rotation: sib with inner */
            rb_side_t inner_side = n_side;
            rb_side_t outer_side = (n_side == RB_LEFT) ? RB_RIGHT : RB_LEFT;

            /* Update sibling's child relationships for first rotation */
            rb_node_t *inner_child = get_child(inner, outer_side);
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
            tree->max_depth = 0;
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
    /* Precompute flag array offset to reduce memory indirection */
    uint8_t *flags = _RB_FOREACH_FLAGS(f);

    /* Initialize: push root and go down left as far as possible */
    if (f->top == RB_ITER_UNINIT) {
        rb_node_t *n = tree->root;
        f->top = 0;

        while (n) {
#if _RB_ENABLE_SAFETY_CHECKS
            if (f->top >= (int32_t) _RB_MAX_TREE_DEPTH - 2) {
                f->top = RB_ITER_DONE;
                return NULL;
            }
#endif

            stack[f->top] = n;
            /* We went left to get here */
            flags[f->top >> 3] |= (1 << (f->top & 7));
            f->top++;
            n = get_child(n, RB_LEFT);
        }
    }

    /* Main traversal loop */
    while (f->top > 0) {
        f->top--;
        rb_node_t *n = stack[f->top];
        bool went_left = (flags[f->top >> 3] >> (f->top & 7)) & 1;

        if (went_left) {
            /* Coming up from left subtree, visit this node */
            /* Mark that we have visited this node */
            flags[f->top >> 3] &= ~(1 << (f->top & 7));
            f->top++; /* Put it back on stack */

            /* Go down right subtree if it exists */
            rb_node_t *right = get_child(n, RB_RIGHT);
            if (right) {
                rb_node_t *current = right;
                while (current) {
#if _RB_ENABLE_SAFETY_CHECKS
                    if (f->top >= (int32_t) _RB_MAX_TREE_DEPTH - 2) {
                        f->top = RB_ITER_DONE;
                        return NULL;
                    }
#endif

                    stack[f->top] = current;
                    flags[f->top >> 3] |= (1 << (f->top & 7));
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

/* Get the next node in cached tree traversal.
 * @tree: Pointer to the cached red-black tree structure
 * @f:    Pointer to the traversal state structure
 */
rb_node_t *__rb_cached_foreach_next(rb_cached_t *tree, rb_foreach_t *f)
{
    /* Simply delegate to standard implementation - the real optimization
     * is in knowing the leftmost node without traversal, not in the
     * iteration logic itself. This keeps the implementation simple and
     * correct while still providing the O(1) benefit for direct min access.
     */
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
