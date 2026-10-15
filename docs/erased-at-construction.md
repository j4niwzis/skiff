# Debug builds: every combinator erases what it returns

What the owner asked for (2026-10-15): in a debug build, no composed static
type at all -- each combinator builds its own small table where it is called,
over children already erased, so the whole UI is one node type and no unit
ever instantiates a tree. Release stays fully static. The walks units, the
`kOpsElsewhere` tables and the global erased-walk mode go.

## Why it is slow now

`AnyNode::opsOf<T>()` makes the table for `T`, and the walks reach a node's
children through `forEachChild`, which makes each child's table in turn: the
table of a composed type is the tables of its whole subtree. Every unit that
names `chats_screen_t` or `window_body_t` made all of them; `kOpsElsewhere`
moved them to one unit each (walks_*.cc), which only moved the cost.

## The one type: `Held`

A real `scene::Node`, the same in every debug unit, that owns one node through
`AnyNode` and is *transparent* in layout -- to what holds it, it is the node it
holds:

- **Geometry goes to the holder**: as it is erased, the node's place, anchor,
  origin, x/y, margin, fixed width/height, relative size, grow, shrink,
  min/max and alignSelf move to the `Held`. The node keeps its own padding,
  background, corners, border, shadow, gradient and the rest of its drawing.
- **Size, axis by axis**: where the node auto-sizes on an axis, the holder
  auto-sizes on it and the node keeps `autoSize` there, so the holder is as
  big as the node. Anywhere else the holder decides (fixed, relative, fill or
  grow), and the node fills the holder on that axis.
- **Later specs**: `Held::apply(spec)` splits a spec the same way --
  geometry to itself, drawing to the node, and the state flags (selected,
  disabled, alpha, hidden) to both where they mean something to the node
  (`selected` lights its `selectedBackground`).
- **Walks and events**: the node is the holder's only child, walked through
  its table; it is the one `Held` table, made once in skiff.

## Combinators

- One name per combinator, two faces chosen by the build. Release: the
  concrete type, exactly as now. Debug: the same node built, then returned
  as `Held{std::move(node)}`.
- A combinator takes any node: a leaf widget (`Toggle`, `Text`, a `Button`)
  arrives concrete, so `applied<Want>(f, [](toggle_t&, bool) ...)` and
  `bound<Field<...>>(widget)` still see their widget's type; what comes back
  is erased. Wrappers over an erased node wrap `Held`.
- Containers (`Box`, `many`, `mount`, `Shown`) hold `Held` children: their
  tables are per arity, not per tree.
- Wrappers that derive from their node (34 in compose: `Projected`, `OnKeys`,
  `KeysFor`, `Appearing`, ...) derive from `Held` when given one. Each wrapper
  is then a node of its own, so stacked answering wrappers no longer share one
  Answer type (the class of errors fixed by hand today).
- A choice among node types -- `spl::variant<A, B, ...>` of pages -- is
  `c::one_of<A, B, ...>`: the variant in release, `Held` in debug (every
  alternative is `Held` there, and a variant of one type repeated cannot be
  built by type).

## Binding and answers

Debug already binds and answers through the runtime walks (skiff.bind:runtime,
answers taken by type from the program's served table); that becomes the only
debug path. `BindTableOf<Held>` forwards to the held node's table.

## In mux

- No reach-ins by static type: `body_of`, `frame_of`, `chat_line_of`,
  `composer_field`, `layer_of<N>`, `std::get<N>(...fParts)`, `input_emoji`
  bounds, `open_panel`, `drop_closed`, `emoji_pictures_shown`'s layer walk.
  Each becomes a model read (most have one now) or a small named query a node
  answers by its id (focus it, its bounds).
- The `_t` aliases stay as names for what a function returns; in debug they
  are `Held`.
- walks_*.cc, every `kOpsElsewhere`/`opsElsewhere`, the erased manage pages
  (`manage_page_erased`, `erased_page`) and the program's served table for the
  runtime walks (where it is only for them) go.

## Order

1. skiff: `Held` with its layout transparency, tests in boxcheck (sizes and
   places equal to the unerased node for each spec kind).
2. skiff compose: debug faces of every combinator; `one_of`.
3. skiff-widgets: erased widgets (`ErasedTextArea`, `AnyCall`, `AnyBlocks`)
   replaced by the same rule: a widget is concrete, what a combinator makes of
   it is erased.
4. mux: reach-ins removed, `one_of` where variants of nodes are, walks units
   and tables deleted.
5. Every unit measured alone: under 30 s CPU.
