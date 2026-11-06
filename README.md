# skiff

A small retained-mode UI framework on top of Skia, in the shape osu!framework
gives its drawables, sized for machines with no GPU acceleration. A screen is
written once as an expression of combinators; the same tree is drawn by
skiff's scene on Skia, or shown as a page's elements in a browser.

## Modules

| module | what is in it |
| --- | --- |
| `skiff.core` | what every backend shares, and no backend is in: `Spec`, anchors and margins, roles and typed stylesheets, events, replies and semantics, the host's work, a node of any type seen through the table of its type |
| `skiff.model` | what a program shows, kept in one place: one aggregate, its parts found by their types, changed only by edits that move the revisions of what they went through |
| `skiff.bind` | a tree's nodes showing a `skiff.model` and changing it: `Bound`, `Scoped`, keyed lists, locals, handlers |
| `skiff.compose` | screens written as expressions of combinators -- `column`, `row`, `bound<>`, `scoped<>`, `each<>`, `onClick` and the rest -- and components wired by the types of what they send |
| `skiff.scene` | the graph: layout, typed stylesheets, transforms, routed input, accessibility semantics, damage and frame scheduling |
| `skiff.nodes` | the drawables: `Box`, `Text`, `Stack`, flows, `ScrollContainer`, `Image`, `Icon`, `Canvas` and its shapes |
| `skiff.paint` | a font stack with fallbacks behind a primary face, a canvas painter, easing curves |
| `skiff.web` | the HTML backend: a tree that is a page -- stacks flex boxes, texts spans, a `Spec` the element's CSS, a named view (`SKIFF_NAMED`) its `data-view` -- bound through `skiff.bind` exactly as the scene's nodes are |
| `skiff.mirror` | a scene's tree as a document's elements, laid out and painted by an engine (a browser, Blitz, or an in-memory document for tests) |
| `skia` | a module wrapper over the Skia headers, so nothing downstream needs a global module fragment (`skia_lite` stands in where Skia is not built) |
| `skiff.aggregate` | an aggregate's members walked as a structured binding pack (C++26), or through Boost.PFR (C++23) |

## Writing a screen: builders

Every combinator is written once, as a **builder**: what a view will be,
described before it is built, so that a program can shape it where it sees
everything that shapes it. A combinator given a builder for any of its parts
gives a builder back; the program builds the whole -- its window, a list's
row maker -- under its rules, and nothing is built twice.

```cpp
import skiff.compose;
import skiff.nodes;
namespace c = skiff::compose;

// A piece: a view keyed by its own type, which rules can be put on.
inline constexpr struct name_piece_fn : c::piece<name_piece_fn> {
  using facts_type = who_facts;                         // what a rule sees
  static who_facts facts(const std::string &name) { return {name}; }
  auto make(const std::string &name) const { return nodes::Text(name, 13.0f, kInk); }
} name_piece{};

auto screen = c::column(c::vbox(8.0f), name_piece(user.name),
                        nodes::Text("Online", 12.0f, kMuted));   // a builder

// The program's rules: drop a piece, put something before, after or around it.
constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::before(initial{}, 4.0f))};
auto node = std::move(screen).build(rules);
```

In a release build the rules are folded while compiling; outside one, a piece
is erased where it is made and its rules are found in a table the program
fills as it starts (`register_rules`), so debug builds keep small types. The
rules and their verbs are the same code either way.

A single node is made with its spec applied:

```cpp
nodes::Text title = scene::make<nodes::Text>({.fillX = true}, "Log in", 20.0f);
```

A `Spec` holds every layout input a node has. `place` sets anchor and origin
together; `fill`, `fillX` and `fillY` are relative sizing at 1.0 on one axis
or both. Zero means unspecified, which is what lets `{}` be a no-op.

## Styling

Reusable appearance and layout belongs in a typed stylesheet. Node kinds and
application-defined roles are C++ types, so there are no selector strings,
runtime hashes or RTTI:

```cpp
struct Button;
struct ButtonLabel;
struct Sidebar;

struct LazerTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<nodes::Box, Button>(),
                {.height = 42.0f,
                 .padding = scene::Margin::horizontal(14.0f),
                 .cornerRadius = 8.0f,
                 .backgroundColour = kButton,
                 .transitionMs = 120.0})
          .rule(scene::select<nodes::Box, Button>().when(
                    scene::StyleState::kHover),
                {.scale = 1.03f, .backgroundColour = kButtonHover})
          .rule(scene::select<nodes::Text, ButtonLabel>(),
                {.colour = kWhite, .fontSize = 16.0f, .fontBold = true})
          .rule(scene::selectAny<Sidebar>().atMostWidth(900.0f),
                {.visible = false});
};

scene.setStyleSheet<LazerTheme>();
```

Later matching rules override earlier ones. Foreground colour, font size and
font weight inherit through containers; backgrounds do not. Rules can select
`kHover`, `kFocus`, `kSelected` and `kDisabled`; width constraints on
selectors are lightweight media queries. Position, size, scale and alpha use
the rule's transition when a selector starts or stops matching, and removing
the sheet restores each node's original values.

The selector's node and role list are template parameters, and each `.rule()`
returns a new stylesheet type. Because the theme owns its rule tuple as
`static constexpr`, resolving a node is an unrolled, allocation-free pass.

### CSS declarations

The web and mirror backends translate `scene::Spec` and `scene::Look` into
`web::css` declarations. Properties accept typed values; compound values such
as borders and transforms stay structured until the DOM or HTML writer
serializes them.

```cpp
namespace css = skiff::web::css;

void markup(skiff::web::Markup &to) {
  to.style(css::position{css::relative{}});
  to.style(css::width{css::pixels{120}});
  to.style(css::overflow_y{css::auto_{}});
  to.style(css::border{css::line{css::pixels{1}, css::argb{0xff808080}}});
  to.eraseStyle<css::height>();
}
```

## Input and accessibility

`dispatchPointer()`, `dispatchKey()` and `dispatchText()` route capture,
target and bubble phases through one path. Pointer capture and keyboard focus
are owned by the scene; `InputRouter` keeps one focus owner while Tab
traverses across scenes. Text events keep provisional IME composition
separate from committed UTF-8. Modal layers cancel covered pointer capture,
scope keyboard traversal, keep the previous focus for restoration, and hide
covered layers from `semantics()`.

Router layers hold `SceneHandle`s rather than raw pointers, so destroying or
replacing a scene makes its layer and any capture inert; nothing has to race
to unregister a tree before releasing it.

Nodes expose a platform-neutral semantics tree with stable IDs, roles,
labels, values, selection, disabled and focus state. The same IDs accept
focus, activation, increment, decrement and value-setting actions, so a
window-system accessibility adapter can inspect and operate the UI without
scene nodes knowing a specific OS API.

## Redrawing

Changing anything marks the node damaged, and the damage walks up through
the parents' bounds and masking. A frame is `update(now)`,
`layoutIfNeeded(viewport)`, `draw(canvas)`, `finishFrame()`; damage and
continuation are consumed together, so a caller cannot forget to schedule the
rest of an animation:

```cpp
scene.update(nowMs);
scene.layoutIfNeeded(screen);
scene.draw(canvas);
const scene::FrameResult frame = scene.finishFrame();
repaint(frame.fDamage);
if (frame.fWantsAnotherFrame)
  requestFrame();
```

That rectangle is the point of the whole thing: the framework skiff is shaped
after redraws everything every frame because it assumes a GPU. This one
redraws the part that moved.

## Building and testing

`skiff` is the library project, `test` is a standalone test consumer, and
`all` is the aggregate project. The test project fetches the module build of
`j4niwzis/googletest-modules` and requires CMake 4.3.4 or newer.

```sh
cmake -S all -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`-G Ninja` is not a preference: CMake only supports C++20 modules under
Ninja. You also need a compiler that can do `import std` -- clang 19 or newer
with libc++, or gcc 15 -- and Skia discoverable through `pkg-config` as
`skia` (or `skia_lite`, which draws nothing, where Skia is not wanted).

Configure `test` directly to build only the library and its tests. Pass
`-DTEST_INSTALLED_VERSION=ON` to test a previously installed `skiff` package
instead of the adjacent source tree.

## Status

Used by [mux](https://github.com/j4niwzis/mux), whose every screen is written
with `skiff.compose` builders, and by
[skiff-framework](https://github.com/j4niwzis/skiff-framework)'s site. The
controls are [skiff-widgets](https://github.com/j4niwzis/skiff-widgets).
