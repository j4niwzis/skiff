# Erased at construction

Outside a release build, every combinator in `skiff.compose` erases what it
returns: it builds its node from the concrete node it was handed, makes that
node's table there, and returns it held (`compose::Held`). No composed type
exists in a debug build -- a view's type is `Held` however deep it is -- so no
unit instantiates the walks of a tree it did not build. A release build
returns the concrete types, as before, and stays fully static.

## Held is not in the tree

`Held` owns its node through an `AnyNode`. The scene sees through it
(`scene::kSeenThrough<Held>`): in every walk, in `AnyNode`, and at a scene's
root, what is walked is the node it holds, with that node's own state. What a
node does to itself later -- hides itself, slides in, is emptied -- is what
its parent sees, with no copy to fall out of date.

What a `Held` is asked or told as a node goes to the node it holds: `apply`,
`setVisible`, `visible`, `bounds`, `id`, `state`, the setters, damage and
layout. These are overloads on the object's type (`this Held&` beside a
template for anything derived), not tests of it.

## Wrappers over held content

A combinator that wraps what another one returned -- `onClick` over a
`column` -- makes a node of its own over held content: `W<Held>`, derived from
`Held`. It stands where its content stood:

- As it is erased (or as it is made, for a `Marked`), its content's placement
  is folded into it once (`Held::adopt`): where it stands, its margin, how it
  is aligned, what bounds its size, how it is drawn as a whole.
- Its size follows its content's, axis by axis: relative to the parent, the
  wrapper is and the content fills it; growing, the wrapper grows and the
  content fills it; any other way, the wrapper is as big as the content. The
  content's own sizing is kept apart, so a later spec starts from it.
- A spec given it later is split: placement to the wrapper, the rest to the
  content. Disabling disables both.
- It is shown only while its content is (`walkedState`), so content that
  hides itself hides its wrapper.

## Nothing reaches into a built tree

A program never takes a part of a built tree to read or change it. What
it reached into the tree for is now the model's, or the nodes' own:

- **What a part shows** is read from the model (`bound`, `projected`, `applied`,
  `focused_by`, `kept_scroll`). A combinator may use the concrete type of what
  it is handed; erasure happens only at its return.
- **What a part says** is an answer: from an event, or from a tick
  (`scene::pressLater` in `update`, delivered as a press): a view's resting
  offset (`kept_scroll`), the way down over a list (`with_way_down`).
- **Program-level signals** are data the program drains: `work::focusWanted`,
  `work::subtreeMade`, an image source's `want()`.
- **A widget's own rules over its parts** (a bubble placing its time, an album
  shrinking its cells, a drag between lanes) find marked parts as the walks
  see them: `Marked<Tag, N>` with `scene::findNode` and
  `scene::forEachNode`, and `state_of(node)` for a part's state.

Widgets with rules that name their parts by position (the message bubble,
the info cards) are a flat `Box` of their parts: one static type for the
widget, as a `Text` is one, its parts held.

Combinators whose node another one reads as itself stay concrete in every
build -- transitionally:

- read by a widget around them: `window_list` (by `spanned_for`),
  `spanned_for` (by `with_way_down`), `page_view` (by `kept_scroll`);
- read by the binder around them (`bound`, `derived`, `projected`): `mount`,
  `mount_now`, `listed`, `sections`, `clamped_width`, `remembered_scroll`,
  `restarted_scroll`, `virtual_rows`, `with_model`, `arranged`,
  `with_way_down`;
- a leaf's own options: `styled`, `visible`, `wrapped`, `elided`, `primary`,
  `on_now`, `with_cursor`, `recorded`.

The binder or widget immediately around such a node erases, so no chain of
static types forms: at most `Binder<Reader<Held>>`. The end state fuses each
reader with its binding -- `mount_for<Want>`, `clamped_width_for<Want>` and
the like, which bind and erase in one call -- leaving only the leaf options
concrete.
