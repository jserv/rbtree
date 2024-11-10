# rbtree

This package provides an implementation of a balanced tree, with search and
deletion operations guaranteed to run in $O(\log_2(N))$ time for a tree
containing $N$ elements. It uses a standard red-black tree structure.

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

One method is provided for iterating through all elements of an `rb_t` using
the `RB_FOREACH` iterator. This iterator allows for a more natural iteration
over the tree with a nested code block instead of a callback function. It is
non-recursive but requires $O(\log(N))$ stack space by default. This
behavior can be configured to use a fixed, maximally sized buffer to avoid
dynamic allocation. Additionally, there is an `RB_FOREACH_CONTAINER` variant
that iterates using a pointer to the container field rather than the raw node
pointer.

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

These rotations are conceptually built on a primitive operation that "swaps" the
position of one node with another in the tree. While typical implementations
achieve this by simply swapping the internal data pointers of the nodes, this
approach cannot be used because the `rb_node_t` structure in this package is
intrusive. Instead, the package includes more complex logic to handle edge
cases, such as when one of the swapped nodes is the root or when the nodes are
already in a parent-child relationship.

The `rb_node_t` structure for this package's `rb_t` only contains two pointers,
representing the "left" and "right" children of a node within the binary tree.
However, during tree rebalancing after a modification, it is often necessary to
traverse "upwards" from a node. In many red-black tree implementations, this is
accomplished using an additional "parent" pointer. This package avoids the need
for a third pointer by constructing a "stack" of node pointers locally as it
traverses downward through the tree and updating it as needed during
modifications. This way, the `rb_t` can be implemented without any additional
runtime storage overhead beyond that of a doubly-linked list.

```
+-------------+     node 2 (black)
| rb_t        |    +------------------+
| * root ----------|    rb_node_t     |
| * max_depth |    | * left | * right |
+-------------+    +----/---------\---+
                       /           \
       node 1 (black) /             \ node 4 (red)
      +------------------+       +------------------+
      |    rb_node_t     |       |    rb_node_t     |
      | * left | * right |       | * left | * right |
      +----/---------\---+       +----/---------\---+
          /           \              /           \
       NULL           NULL          /             \
                        node 3 (black)        node 5 (black)
                       +------------------+  +------------------+
                       |    rb_node_t     |  |    rb_node_t     |
                       | * left | * right |  | * left | * right |
                       +----/---------\---+  +----/---------\---+
                           /           \         /           \
                        NULL           NULL    NULL           \
                                                     node 6 (red)
                                                   +------------------+
                                                   |    rb_node_t     |
                                                   | * left | * right |
                                                   +----/---------\---+
                                                       /           \
                                                    NULL           NULL
```
