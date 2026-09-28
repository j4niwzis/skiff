# skiff as a static tree

Status: proposal. Nothing here is implemented yet.

## Why

Today every node derives from `Drawable`: 22 virtual functions, and every
container keeps its children as `std::vector<std::unique_ptr<Drawable>>`.
Layout, drawing, hit testing, focus, routing, damage, styles and semantics all
walk that one erased type. The static tree makes the scene's type the tree:
nodes are values of their own types, containers know their children's types,
and every walk is a template instantiated per node type. Type erasure stays
available where it is the right tool, as a value type in the `std::function`
style (`AnyNode`, `NodeRef`), never as a base class every node must inherit.

The API breaks (there is no 1.0.0 yet); skiff-widgets, mux and osu-cpp are
updated in the same round.

## What stays

Everything skiff does now is kept, only reached differently: `Spec` and its
layout model (anchors, relative and automatic sizes, grow, margins, padding),
transforms and easing, alpha and masking, followers, damage tracking and
frame scheduling, stylesheets, pointer capture, hover, focus and Tab order,
text input and composition, scroll gestures, semantics and their actions,
and `CachedContainer`. The scene stays retained: the tree lives between
frames and keeps focus, animations and damage.

## A node

A node is any type that models `skiff::Node`:

```cpp
template <class T>
concept Node = requires(T& node) {
  { node.fState } -> std::same_as<skiff::State&>;   // what every node has
};
```

`skiff::State` is what `Drawable`'s data members are now: the box and the
`Spec`, visibility, alpha, scale, transforms in flight, style roles,
selected/disabled, damage, the semantic id. A node holds it as a member; it
does not inherit it.

Everything else is optional and found with `requires`, not declared
`virtual`:

| hook | when absent |
| --- | --- |
| `forEachChild(f)` | a leaf |
| `measure(parent)`, `layoutChildren()` | the default box layout |
| `drawSelf(canvas, alpha)` | draws nothing |
| `update(nowMs)`, `settling()` | nothing animates |
| `onPointerEvent`, `onKeyEvent`, `onTextInput`, `onClick`, `onScroll` | not interactive |
| `acceptsInput()`, `focusable()` | `false` |
| `semantics()`, `onSemanticAction` | a group with no role |
| `applyNodeStyle(style, animate)` | the common properties only |

The walks (`layout`, `draw`, `update`, `hitTest`, `dispatch*`,
`collectSemantics`, `collectFocusable`) are function templates over `Node`
that recurse through `forEachChild`. With the tree's type known, they
inline.

## Composition

Two ways, meant to be mixed.

A screen is a struct whose members are its nodes, so it names what it
changes:

```cpp
struct LoginScreen {
  skiff::State fState;
  nodes::Text title{{.fillX = true}, "Log in", 20.0f};
  widgets::TextBox<> address{{.fillX = true}, "user@example.com"};
  widgets::TextBox<> password{{.fillX = true}, "Password"};
  widgets::Button<LogIn> logIn{{.width = 120.0f}, "Log in", LogIn{this}};

  void forEachChild(auto&& f) { f(title); f(address); f(password); f(logIn); }
  void layoutChildren() { skiff::stackVertically(*this, 8.0f); }
};
```

And ready-made containers build types from values when nothing needs
naming:

```cpp
auto row = nodes::row({.fillX = true, .spacing = 8.0f},
                      widgets::Button({}, "Cancel", cancel),
                      widgets::Button({}, "Log in", logIn));
// nodes::Row<widgets::Button<Cancel>, widgets::Button<LogIn>>
```

`Row`, `Column` (today's `FillFlow`), `Box`, `Grid`, `ScrollContainer` and
`CachedContainer` hold a `std::tuple` of their children.

## What changes at run time

The tree's shape is fixed by its type; what varies is said explicitly:

- `nodes::Each<T>`: any number of children of one type, kept in stable
  storage, for lists (the conversation list is `Each<ConversationRow>`).
- `nodes::OneOf<Ts...>`: one of a known set, over `std::variant` (the
  conversations screen, the login screen, the settings screen).
- `nodes::Optional<T>`: present or not.
- `skiff::AnyNode`: any node by value, erased the way `std::function`
  erases a callable (a small table of the walks, one allocation). For what
  cannot be known where the container is written: a plugin's panel.

## References to nodes

Focus, pointer capture, hover, followers and semantic lookups need to point
at a node whatever its type. `skiff::NodeRef` is a non-owning erased
reference: a pointer and a table of the walks, like `std::function_ref`.
Nodes do not move while they are in a scene (the root is not movable, and
`Each` keeps elements in stable storage), so a `NodeRef` stays valid until
its node leaves; a container that drops children releases every reference
into them first, as `releaseInputForSubtree` does now.

## Styles

A rule selects a node type, as now. A class template that should be styled
as one type says so: `using StyleAs = widgets::Button<>;`, so every
`Button<Action>` matches rules written for `Button<>`.

## Callbacks

Widgets keep their callbacks as members of the callable's own type
(`Button<Action>`), with `NoAction` when none is given. No `std::function`
anywhere; a program that wants erasure there can pass one itself.

## Order of work

1. skiff: `State`, the `Node` concept, the walks, `NodeRef`, the
   containers and `Each`/`OneOf`/`Optional`/`AnyNode`; `Drawable` removed.
   Its tests ported.
2. skiff-widgets on it, tests ported.
3. mux: the screens, and the login and config screens written on it.
4. osu-cpp.

Each step is pushed with its dependents pinned to it, so every main
builds.
