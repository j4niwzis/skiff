# Layout schedule benchmark

Box layout said as an attribute grammar (Meyerovich et al., *Parallel Schedule Synthesis for Attribute Grammars*, PPoPP 2013): each kind of node says what it computes -- its natural width (`pref`), its children's widths, its height, its children's places -- from what of itself, its parent and its children. A fixed schedule of four passes (prefs up, widths down, heights up, places down) is checked against every kind's rules while compiling (`layout_bench.grammar`'s `check`), one kind at a time.

The schedule is then run three ways over a chat (`make_chat`: eight nodes per message) as it is edited:

- **full** -- every node in every pass: the tree is flat and in preorder, so a pass reads the arrays from one end to the other;
- **double dirty** -- only what changed, found through two bits per node and pass: its own, and "something under it", walked down the spine to it (browsers today; skiff's `fLayoutValid` / `fSubtreeDirty`);
- **spineless** -- only what changed, kept in a queue in preorder that never visits a node it does not recompute (Kirisame, Wang, Panchekha, *Spineless Traversal for Layout Invalidation*, PLDI 2025).

Every result is checked against a layout from nothing. A Debug build reaches the kinds through tables, as skiff's erased walks do; a release build visits the variant.

```sh
cmake -S bench/layout -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench && build-bench/layout_bench [messages...]
bench/layout/compile_time.sh build-bench   # the check's cost while compiling
```

`compile_time.sh` builds `src/compile_kinds.cc` with 16, 64 and 256 kinds of node, the check on and off, and times each.
