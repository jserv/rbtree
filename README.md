# rbtree

This package provides a memory-optimized red-black tree implementation with
search and deletion operations guaranteed to run in $O(\log_2(N))$ time for
a tree containing $N$ elements. Features include intrusive node design,
pointer alignment optimization, optional caching for O(1) min/max access,
and configurable build-time options for performance tuning.

## Build-time Configuration

The implementation supports several build-time options:
- `_RB_ENABLE_LEFTMOST_CACHE` (default: 1): Enable O(1) minimum node access
- `_RB_ENABLE_RIGHTMOST_CACHE` (default: 0): Enable O(1) maximum node access
- `_RB_ENABLE_SAFETY_CHECKS` (default: 1): Enable bounds checking (<5% overhead)

## Data Structure

The `rb_t` tracking structure can be initialized anywhere in user-accessible
memory. It should contain only zero bits before first use, and no specific
initialization API is needed.

Unlike a linked list, where the position of elements is explicit, the ordering
of nodes in an `rb_t` must be defined by a user-provided predicate function.
A function of type `rb_cmp_t` should be assigned to the `cmp_func` field of the
`rb_t` structure before any tree operations are performed. This function must
return `true` if the first node argument is "less than" the second, according to
the desired ordering. Note that "equal" values are not allowed; nodes within the
tree must have a unique and fixed order for the algorithm to function correctly.

Nodes within an `rb_t` are represented as an `rb_node_t` structure, which
resides  in user-managed memory, typically embedded within the data structure
tracked by the tree. However, unlike linked list structures, the data within an
`rb_node_t` is entirely opaque, and users cannot manually traverse the tree's
binary topology as they can with lists.

Nodes can be inserted into the tree with `rb_insert` and removed with
`rb_remove`. The "first" and "last" nodes in the tree, as determined by the
comparison function, can be accessed using `rb_get_min` and `rb_get_max`,
respectively. Additionally, the `rb_contains` function checks if a specific
node pointer exists within the tree. All of these operations have a maximum time
complexity of $O(\log(N))$ based on the size of the tree.

### Operations

Tree iteration is provided through the `RB_FOREACH` iterator, which allows
natural iteration with nested code blocks instead of callbacks. The iterator
is non-recursive and uses optimized memory allocation - either dynamic stack
allocation or a fixed buffer to avoid runtime overhead. Recent improvements
include reduced memory usage and enhanced bounds checking. An
`RB_FOREACH_CONTAINER` variant iterates using container pointers rather than
raw node pointers.

### Implementation Details

This package uses a conventional red-black tree algorithm. Low-level algorithmic
details are omitted here since they follow established conventions. Instead,
this document focuses on aspects specific to this package's implementation.

The core invariant of the red-black tree ensures that the path from the root to
any leaf is no more than twice as long as the path to any other leaf. This
balance is maintained by associating a "color" bit with each node, either red or
black, and enforcing a rule that no red node can have a red child (i.e., the
number of black nodes along any path from the root must be equal, and the number
of red nodes must not exceed this count). This property is maintained using
a series of tree rotations to restore balance after modifications.

These rotations are conceptually based on a primitive operation that "swaps" the
positions of two nodes in the tree. In many implementations, this is done by
simply exchanging the internal data pointers of the nodes. However, this package
uses an intrusive `rb_node_t` structure, where the node metadata is embedded
directly within the user-defined data structure. This design improves cache
locality and eliminates additional memory allocations, but it also requires more
complex logic to handle edge cases, such as when one of the nodes is the root or
when the nodes have a parent-child relationship.

The `rb_node_t` structure contains only two pointers for left and right
children. Node colors are stored in the least significant bit of the left
pointer, leveraging pointer alignment guarantees. Unlike implementations with
parent pointers, this package uses a traversal stack for upward navigation
during rebalancing. Recent optimizations include magic number extraction,
improved bounds checking, and pointer alignment verification to ensure the
color bit storage remains valid. Memory usage has been further reduced through
iterator buffer optimization.

```
+-------------+     node 2 (black, color in left ptr LSB)
| rb_t        |    +------------------+
| * root ----------|    rb_node_t     |
| * cmp_func  |    | * left¹| * right |  ¹ LSB stores color bit
+-------------+    +----/---------\---+
                       /           \
       node 1 (black) /             \ node 4 (red)
      +------------------+       +------------------+
      |    rb_node_t     |       |    rb_node_t     |
      | * left¹| * right |       | * left¹| * right |
      +----/---------\---+       +----/---------\---+
          /           \              /           \
       NULL           NULL          /             \
                        node 3 (black)        node 5 (black)
                       +------------------+  +------------------+
                       |    rb_node_t     |  |    rb_node_t     |
                       | * left¹| * right |  | * left¹| * right |
                       +----/---------\---+  +----/---------\---+
                           /           \         /           \
                        NULL           NULL    NULL           \
                                                     node 6 (red)
                                                   +------------------+
                                                   |    rb_node_t     |
                                                   | * left¹| * right |
                                                   +----/---------\---+
                                                       /           \
                                                    NULL           NULL
```
