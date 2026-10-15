# Debug builds: containers erase their children as they are made

What the owner asked for (2026-10-15): in a debug build no composed static
type -- the tables are built where each combinator is called, over children
already erased, so no unit instantiates a tree. Release stays fully static.
The walks units, the `kOpsElsewhere` tables and the global erased-walk mode go.

## Why it is slow now

`AnyNode::opsOf<T>()` makes the table for `T`, and the walks reach a node's
children through `forEachChild`, which makes each child's table in turn: the
table of a composed type is the tables of its whole subtree. Every unit that
names `chats_screen_t` or `window_body_t` made all of them; `kOpsElsewhere`
moved them to one unit each (walks_*.cc), which only moved the cost.

## Where to erase: what holds children

The depth of a type comes from containers: `column`, `row`, `many`, a mount's
content, a dialog's or a page's content, the alternatives of a page variant.
In a debug build each of them erases the children it is given -- each child
that has children of its own -- as it is made: `Box<Held, Text, Held>`, not
`Box<Box<...>, Text, Projected<...>>`. A container's type then depends on its
arity and its leaves, never on what is below; its table is made where it is
called, over `Held`'s one table.

Leaves stay as they are (a `Text`, an `Icon`, a widget): cheap, and a row's
elided name keeps its own shrinking. Wrappers over a leaf or over a container
stay concrete too (`styled`, `projected`, `bound`, `applied`, `onKeys`): a
wrapper adds one level to one node and keeps its node's methods -- a scroll
container's `scrollToStart`, a toggle's `setOn` -- where its own code needs
them. Release erases nothing.

## `Held`: one node type, transparent in layout

A `scene::Node`, the same in every debug unit, owning one node through
`AnyNode` and laying it out as the node would lay itself out:

- Placement goes to the holder and is cleared on the node: place, anchor,
  origin, x/y, margin, alignSelf, depth, min/max.
- Size, axis by axis: where the node is relative to its parent, the holder is
  (the same fraction) and the node fills the holder; where the node grows,
  the holder grows and the node fills it; anywhere else the holder auto-sizes
  round the node, which keeps its own sizing -- fixed, auto, or measured by
  itself (a text area's lines).
- `Held::apply(spec)` splits a spec the same way: placement to itself, size
  to the node (mirrored as above), drawing and state flags to the node, alpha,
  visibility, shift and scale to itself.
- Walks and events: the node is its only child, through `AnyNode`; `Held`'s
  table is made once in skiff.

## Binding and answers

Debug binds and answers through the runtime walks (skiff.bind:runtime); that
is the one debug path. A child erased in a container keeps its own wrappers
(each a node of its own above it), so what it binds and answers is found
through its table as any erased child's is.

## Variants of nodes

A choice among node types (`spl::variant` of pages) is `c::one_of<A, B, ...>`:
the variant in release, `Held` in debug.

## In mux

- No reach-ins by static type through containers: `body_of`, `frame_of`,
  `chat_line_of`, `composer_field`, `layer_of<N>`, `std::get<N>(...fParts)`,
  `input_emoji` bounds, `open_panel`, `drop_closed`, the emoji layer walk.
  Each becomes a model read (most have one now) or a query a node answers by
  its id (focus it, its bounds).
- walks_*.cc, every `kOpsElsewhere`/`opsElsewhere`, the erased manage pages and
  what served only them go.

## Order

1. skiff: `Held` and its layout transparency; boxcheck: sizes and places equal
   to the unerased node for each kind of spec.
2. skiff compose: containers erase their composite children in debug;
   `one_of`; the global erased-walk switch removed.
3. skiff-widgets: the erased widgets (`ErasedTextArea`, `AnyCall`,
   `AnyBlocks`) go -- a widget is a leaf.
4. mux: reach-ins removed, `one_of` for variants of nodes, walks units and
   tables deleted.
5. Every unit measured alone: under 30 s CPU.
