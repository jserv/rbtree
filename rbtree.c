#include <stddef.h>

#include "rbtree.h"

typedef enum { RED = 0, BLACK = 1 } rb_color_t;

static inline rb_node_t *get_child(rb_node_t *n, uint8_t side)
{
    if (side != 0U)
        return n->children[1];

    uintptr_t l = (uintptr_t) n->children[0];
    l &= ~1UL;
    return (rb_node_t *) l;
}

static inline void set_child(rb_node_t *n, uint8_t side, void *val)
{
    if (side != 0U) {
        n->children[1] = val;
    } else {
        uintptr_t old = (uintptr_t) n->children[0];
        uintptr_t new = (uintptr_t) val;

        n->children[0] = (void *) (new | (old & 1UL));
    }
}

static inline rb_color_t get_color(rb_node_t *n)
{
    return ((uintptr_t) n->children[0]) & 1UL;
}

static inline bool is_black(rb_node_t *n)
{
    return get_color(n) == BLACK;
}

static inline bool is_red(rb_node_t *n)
{
    return get_color(n) == RED;
}

static inline void set_color(rb_node_t *n, rb_color_t color)
{
    uintptr_t *p = (void *) &n->children[0];
    *p = (*p & ~1UL) | (uint8_t) color;
}

/* Traverse the red-black tree to find a node that either matches the given
 * "node" argument or reaches a leaf with an empty child pointer where the
 * "node" would be inserted. All nodes encountered are pushed onto the stack.
 *
 * Note: The tree must not be empty, and the stack must be large enough to hold
 * at least 'tree->max_depth' entries. Returns the number of nodes pushed onto
 * the stack.
 */
static int find_and_stack(rb_t *tree, rb_node_t *node, rb_node_t **stack)
{
    int sz = 0;
    stack[sz] = tree->root;
    ++sz;

    while (stack[sz - 1] != node) {
        uint8_t side = tree->cmp_func(node, stack[sz - 1]) ? 0U : 1U;
        rb_node_t *ch = get_child(stack[sz - 1], side);

        if (!ch)
            break;
        stack[sz] = ch;
        ++sz;
    }

    return sz;
}

rb_node_t *__rb_get_minmax(rb_t *tree, uint8_t side)
{
    rb_node_t *n;
    for (n = tree->root; n && get_child(n, side); n = get_child(n, side))
        ;
    return n;
}

static inline uint8_t get_side(rb_node_t *parent, const rb_node_t *child)
{
    return (get_child(parent, 1U) == child) ? 1U : 0U;
}

/* Swap the positions of the two nodes at the top of the given stack, updating
 * the stack accordingly. The colors of the nodes remain unchanged.
 * This operation performs the following transformation (or its mirror image if
 * node N is on the opposite side of P):
 *
 *    P          N
 *  N  c  -->  a   P
 * a b            b c
 */
static void rotate(rb_node_t **stack, int stacksz)
{
    rb_node_t *parent = stack[stacksz - 2];
    rb_node_t *child = stack[stacksz - 1];
    uint8_t side = get_side(parent, child);
    rb_node_t *a = get_child(child, side);
    rb_node_t *b = get_child(child, (side == 0U) ? 1U : 0U);

    if (stacksz >= 3) {
        rb_node_t *grandparent = stack[stacksz - 3];

        set_child(grandparent, get_side(grandparent, parent), child);
    }

    set_child(child, side, a);
    set_child(child, (side == 0U) ? 1U : 0U, parent);
    set_child(parent, side, b);
    stack[stacksz - 2] = child;
    stack[stacksz - 1] = parent;
}

/* The top node on the given stack is red, and its parent is also red.
 * Iteratively restore the tree structure to maintain the red-black tree
 * properties.
 */
static void fix_extra_red(rb_node_t **stack, int stacksz)
{
    while (stacksz > 1) {
        rb_node_t *node = stack[stacksz - 1];
        rb_node_t *parent = stack[stacksz - 2];

        /* Correct child colors are a precondition of the loop */
        if (is_black(parent))
            return;

        rb_node_t *grandparent = stack[stacksz - 3];
        uint8_t side = get_side(grandparent, parent);
        rb_node_t *aunt = get_child(grandparent, (side == 0U) ? 1U : 0U);

        if (aunt && is_red(aunt)) {
            set_color(grandparent, RED);
            set_color(parent, BLACK);
            set_color(aunt, BLACK);

            /* The grandparent node was colored red, may having a red parent.
             * Continue iterating to fix the tree from this point.
             */
            stacksz -= 2;
            continue;
        }

        /* A local rotation can restore the entire tree structure. First, ensure
         * that the node is on the same side of the parent as the parent is of
         * the grandparent.
         */
        uint8_t parent_side = get_side(parent, node);

        if (parent_side != side)
            rotate(stack, stacksz);

        /* Rotate the grandparent with parent, swapping colors */
        rotate(stack, stacksz - 1);
        set_color(stack[stacksz - 3], BLACK);
        set_color(stack[stacksz - 2], RED);
        return;
    }

    /* Exiting the loop indicates that the node has become the root, which must
     * be black.
     */
    set_color(stack[0], BLACK);
}

void rb_insert(rb_t *tree, rb_node_t *node)
{
    set_child(node, 0U, NULL);
    set_child(node, 1U, NULL);

    if (!tree->root) {
        tree->root = node;
        tree->max_depth = 1;
        set_color(node, BLACK);
        return;
    }

#if _RB_DISABLE_ALLOCA != 0
    rb_node_t **stack = &tree->iter_stack[0];
#else
    rb_node_t *stack[tree->max_depth + 1];
#endif

    int stacksz = find_and_stack(tree, node, stack);

    rb_node_t *parent = stack[stacksz - 1];

    uint8_t side = tree->cmp_func(node, parent) ? 0U : 1U;

    set_child(parent, side, node);
    set_color(node, RED);

    stack[stacksz] = node;
    ++stacksz;
    fix_extra_red(stack, stacksz);

    if (stacksz > tree->max_depth)
        tree->max_depth = stacksz;

    /* We may have rotated up into the root! */
    tree->root = stack[0];
}

/* Handle the case for node N (at the top of the stack) which, after a deletion,
 * has a "black deficit" in its subtree. By construction, N must be black (if
 * it were red, recoloring would resolve the issue, and this function would not
 * be needed). This function rebalances the tree to maintain red-black
 * properties.
 *
 * The "null_node" pointer is used when removing a black node without children.
 * For simplicity, a real node is needed during the tree adjustments, so we use
 * "null_node" temporarily and replace it with a NULL child in the parent when
 * the operation is complete.
 */
static void fix_missing_black(rb_node_t **stack,
                              int stacksz,
                              const rb_node_t *null_node)
{
    /* Loop upward until we reach the root */
    while (stacksz > 1) {
        rb_node_t *c0, *c1, *inner, *outer;
        rb_node_t *n = stack[stacksz - 1];
        rb_node_t *parent = stack[stacksz - 2];
        uint8_t n_side = get_side(parent, n);
        rb_node_t *sib = get_child(parent, (n_side == 0U) ? 1U : 0U);

        /* Ensure the sibling is black, rotating N down a level if necessary.
         * After rotate(), the parent becomes the child of the previous sibling,
         * placing N lower in the tree.
         */
        if (!is_black(sib)) {
            stack[stacksz - 1] = sib;
            rotate(stack, stacksz);
            set_color(parent, RED);
            set_color(sib, BLACK);
            stack[stacksz] = n;
            ++stacksz;

            parent = stack[stacksz - 2];
            sib = get_child(parent, (n_side == 0U) ? 1U : 0U);
        }

        /* Situations where the sibling has only black children can be resolved
         * straightforwardly.
         */
        c0 = get_child(sib, 0U);
        c1 = get_child(sib, 1U);
        if ((!c0 || is_black(c0)) && (!c1 || is_black(c1))) {
            if (n == null_node)
                set_child(parent, n_side, NULL);

            set_color(sib, RED);
            if (is_black(parent)) {
                /* Rebalance the sibling's subtree by coloring it red.
                 * The parent now has a black deficit, so continue iterating
                 * upwards.
                 */
                stacksz--;
                continue;
            }
            /* Recoloring makes the whole tree OK */
            set_color(parent, BLACK);
            return;
        }

        /* The sibling has at least one red child. Adjust the tree so that the
         * far/outer child (i.e., on the opposite side of N) is guaranteed to
         * be red.
         */
        outer = get_child(sib, (n_side == 0U) ? 1U : 0U);
        if (!(outer && is_red(outer))) {
            inner = get_child(sib, n_side);

            stack[stacksz - 1] = sib;
            stack[stacksz] = inner;
            ++stacksz;
            rotate(stack, stacksz);
            set_color(sib, RED);
            set_color(inner, BLACK);

            /* Update the stack to place N at the top and set 'sib' to the
             * updated sibling.
             */
            sib = stack[stacksz - 2];
            outer = get_child(sib, (n_side == 0U) ? 1U : 0U);
            stack[stacksz - 2] = n;
            stacksz--;
        }

        /* Lastly, the sibling must have a red child in the far/outer position.
         * Rotating 'sib' with the parent and recoloring will restore a valid
         * tree.
         */
        set_color(sib, get_color(parent));
        set_color(parent, BLACK);
        set_color(outer, BLACK);
        stack[stacksz - 1] = sib;
        rotate(stack, stacksz);
        if (n == null_node)
            set_child(parent, n_side, NULL);
        return;
    }
}

void rb_remove(rb_t *tree, rb_node_t *node)
{
    rb_node_t *tmp;
#if _RB_DISABLE_ALLOCA != 0
    rb_node_t **stack = &tree->iter_stack[0];
#else
    rb_node_t *stack[tree->max_depth + 1];
#endif

    int stacksz = find_and_stack(tree, node, stack);

    if (node != stack[stacksz - 1])
        return;

    /* Only nodes with zero or one child can be removed directly. If the node
     * has two children, select the largest child on the left side (or the
     * smallest on the right side) and swap its position with the current node.
     */
    if (get_child(node, 0U) && get_child(node, 1U)) {
        int stacksz0 = stacksz;
        rb_node_t *hiparent, *loparent;
        rb_node_t *node2 = get_child(node, 0U);

        hiparent = (stacksz > 1) ? stack[stacksz - 2] : NULL;
        stack[stacksz] = node2;
        ++stacksz;
        while (get_child(node2, 1U)) {
            node2 = get_child(node2, 1U);
            stack[stacksz] = node2;
            ++stacksz;
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
            set_child(node, 0U, get_child(node2, 0U));
            set_child(node2, 0U, node);
        } else {
            set_child(loparent, get_side(loparent, node2), node);
            tmp = get_child(node, 0U);
            set_child(node, 0U, get_child(node2, 0U));
            set_child(node2, 0U, tmp);
        }

        set_child(node2, 1U, get_child(node, 1U));
        set_child(node, 1U, NULL);

        tmp = stack[stacksz0 - 1];
        stack[stacksz0 - 1] = stack[stacksz - 1];
        stack[stacksz - 1] = tmp;

        rb_color_t ctmp = get_color(node);

        set_color(node, get_color(node2));
        set_color(node2, ctmp);
    }

    rb_node_t *child = get_child(node, 0U);
    if (!child)
        child = get_child(node, 1U);

    /* Removing the root */
    if (stacksz < 2) {
        tree->root = child;
        if (child) {
            set_color(child, BLACK);
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
            /* Red childless nodes can just be dropped */
            set_child(parent, get_side(parent, node), NULL);
        }
    } else {
        set_child(parent, get_side(parent, node), child);

        /* Check the node colors. If one is red (since a valid tree requires at
         * least one to be black), the operation is complete.
         */
        if (is_red(node) || is_red(child))
            set_color(child, BLACK);
    }

    /* We may have rotated up into the root! */
    tree->root = stack[0];
}

rb_node_t *__rb_child(rb_node_t *node, uint8_t side)
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
        n = get_child(n, tree->cmp_func(n, node));

    return n == node;
}

/* Push the given node and its left-side descendants onto the stack in the
 * "foreach" structure, returning the last node, which is the next node to
 * iterate. By design, the node is always a right child or the root, so
 * "is_left" must be false.
 */
static rb_node_t *stack_left_limb(rb_node_t *n, rb_foreach_t *f)
{
    f->top++;
    f->stack[f->top] = n;
    f->is_left[f->top] = false;

    for (n = get_child(n, 0U); n; n = get_child(n, 0U)) {
        f->top++;
        f->stack[f->top] = n;
        f->is_left[f->top] = true;
    }

    return f->stack[f->top];
}

/* The "foreach" traversal uses a dynamic stack allocated with alloca(3) or
 * pre-allocated temporary space.
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

    /* Initialization step: begin with the leftmost child of the root, setting
     * up the stack as nodes are traversed.
     */
    if (f->top == -1)
        return stack_left_limb(tree->root, f);

    /* If the current node has a right child, traverse to the leftmost
     * descendant of the right subtree.
     */
    rb_node_t *n = get_child(f->stack[f->top], 1U);
    if (n)
        return stack_left_limb(n, f);

    /* If the current node is a left child, the next node to visit is its
     * parent. The root node is pushed with 'is_left' set to false, ensuring
     * this condition is satisfied even if the node has no parent.
     */
    if (f->is_left[f->top])
        return f->stack[--f->top];

    /* If the current node is a right child with no left subtree, its parent
     * has already been visited. Move up the stack to find the nearest left
     * child whose parent has not been visited, as it will be the next node.
     */
    while ((f->top > 0) && (f->is_left[f->top] == false))
        f->top--;

    f->top--;
    return (f->top >= 0) ? f->stack[f->top] : NULL;
}
