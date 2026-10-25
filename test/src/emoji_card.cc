#include "skiff/named.h"
import std;
import skia;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;
import skiff.paint;

#include "gtest/gtest-macros.h"

// mux's emoji card as emoji_popup makes it -- a card placed by what is
// shown (spec_for), a flick across it, its page's two Locals (the preview,
// the search), the groups projected from the search, their tabs split into
// categories and paged packs -- with its groups made near the view, each
// one's height reckoned as mux reckons it. Wheeled down and dragged up over
// its cells through the scene, the model changing under it at every frame:
// the view goes the way it is sent, and stays where a drag lets it go.
namespace {
using namespace skiff;
namespace c = skiff::compose;

struct Told {
  int of = 1;
  friend bool operator==(const Told &, const Told &) = default;
};
struct Root {
  model::Tracked<Told> told;
};
using Model = model::Model<Root, bind::NoReactions>;

struct Group {
  std::string name;
  int count = 0;
  friend bool operator==(const Group &, const Group &) = default;
};
struct Pressed {
  int operator()() const { return 0; }
};
// The panel's preview, as mux's (emoji_kept.cc, with_preview): what a cell
// rested on shows large, the page's own state, an overlay mounted over it.
struct Previewed {
  int what = 0;
  friend bool operator==(const Previewed &, const Previewed &) = default;
};
struct PreviewShown {
  std::optional<Previewed> now;
};
struct PreviewRested {
  Previewed what;
  bool on = false;
};
struct PreviewEvents {
  auto on(const PreviewRested &rested, const PreviewShown &shown) const {
    PreviewShown next = shown;
    if (rested.on)
      next.now = rested.what;
    else if (next.now == rested.what)
      next.now.reset();
    return model::over<PreviewShown>(model::setTo(std::move(next)));
  }
};
struct Rest {
  using Answer = PreviewRested;
  Previewed what;
  PreviewRested operator()(bool on) const { return {what, on}; }
};
inline auto preview_of(const Previewed &one) {
  return c::column(c::vbox(8.0f, {.place = scene::anchor::kCentre, .autoSize = scene::axes::kBoth, .padding = {16.0f, 16.0f, 16.0f, 16.0f},
                                  .cornerRadius = 14.0f, .background = 0xf0202020u}),
                   nodes::Text(std::to_string(one.what), 120.0f, 0xffffffffu));
}
using PreviewOf = decltype(preview_of(Previewed{}));
struct Flick {
  std::optional<int> operator()(bool) const { return std::nullopt; }
};
struct Query {
  std::string text;
  friend bool operator==(const Query &, const Query &) = default;
};
struct Preview {
  int on = 0;
  friend bool operator==(const Preview &, const Preview &) = default;
};

inline auto cell(int n) {
  return SKIFF_NAMED(emoji_cell, c::resting(450.0, Rest{{n}},
      c::onPress(Pressed{},
          c::row(c::justified(c::hbox(0.0f, {.width = 37.0f, .height = 37.0f, .cornerRadius = 6.0f, .hoverBackground = 0xffeeeeeeu}),
                              nodes::justify::middle{}),
                 c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("x", 22.0f, 0xff000000u))), "e")));
}
using Cell = decltype(cell(0));
inline auto section(const Group &one) {
  return SKIFF_NAMED(cell_section, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.fillX = true, .height = 32.0f}, nodes::Text(one.name, 13.0f, 0xff000000u)),
      c::styled({.fillX = true, .autoSize = scene::axes::kY},
          nodes::Flow<std::vector<Cell>>({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true},
                                         std::views::iota(0, one.count) | std::views::transform(cell) | std::ranges::to<std::vector>()))));
}
inline auto tab(const Group &one) {
  auto made = SKIFF_NAMED(emoji_tab, c::row(c::justified(c::hbox(0.0f, {.width = 30.0f, .height = 30.0f, .minWidth = 24.0f,
                                                                      .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f}),
                                                         nodes::justify::middle{}),
      c::visible(true, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(one.name.substr(0, 1), 16.0f, 0xff000000u)))));
  if (!one.name.starts_with("Pack"))
    made.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .minWidth = 0.0f});
  return made;
}
inline auto arrow(bool next) { return c::styled({.width = 30.0f, .height = 30.0f}, nodes::Text(next ? ">" : "<", 20.0f, 0xff000000u)); }
inline auto groups() {
  return c::sections<Group>(c::vbox(4.0f, {.fill = true}), c::vbox(0.0f), tab, section,
      c::split<Group>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          [](const Group &one) { return one.name.starts_with("Pack"); },
          c::in_row<Group>(c::hbox(0.0f, {.fillX = true, .height = 36.0f})),
          c::paged<Group>(c::hbox(0.0f, {.fillX = true, .height = 30.0f}),
                          c::justified(c::hbox(0.0f, {.height = 30.0f, .grow = scene::axes::kX}), nodes::justify::middle{}), 30.0f, 60.0f,
                          arrow, "Previous emoji packs", "Next emoji packs")),
      [](std::size_t) { return scene::Taken{}; },
      [](const Group &one, float width) {
        const auto across = std::max<std::size_t>(1, static_cast<std::size_t>(width / 37.0f));
        const std::size_t rows = (static_cast<std::size_t>(one.count) + across - 1) / across;
        return 32.0f + static_cast<float>(rows) * 37.0f;
      });
}
// The groups as the page reads them: projected from its search -- a node of
// its own type, the Sections it is made over (found as it is).
inline auto read_groups() {
  return c::projected<model::Field<&Query::text>>([](const std::string &) {
    std::vector<Group> all = std::views::iota(0, 4) | std::views::transform([](int g) { return Group{std::format("Pack {}", g), 30}; }) |
                             std::ranges::to<std::vector>();
    all.append_range(std::views::iota(0, 9) | std::views::transform([](int g) { return Group{std::format("Group {}", g), 190}; }));
    return c::SectionsShown<Group>{all, all, true};
  }, groups());
}
using Groups = decltype(read_groups());
// The list that scrolls: the Sections' own child, found as it is in every
// build (the projection over the Sections is held, erased, outside one).
using Scroll = typename decltype(groups())::List;
inline auto page() {
  return SKIFF_NAMED(with_preview, c::local<PreviewShown>(PreviewEvents{}, c::column(c::vbox(0.0f, {.fill = true}),
      c::styled({.fill = true}, c::local<Query>(c::column(c::vbox(4.0f, {.fill = true, .padding = {7.0f, 0.0f, 4.0f, 7.0f}}),
          c::styled({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}}, nodes::Text("Search emoji", 13.0f, 0xff000000u)),
          c::styled({.fillX = true, .grow = scene::axes::kY}, read_groups())))),
      c::bound<model::Field<&PreviewShown::now>>(c::mount<PreviewOf, Previewed>(
          [](const Previewed &one) { return std::tuple{preview_of(one)}; }, {.place = scene::anchor::kTopLeft, .fill = true})))));
}
inline auto inside() {
  return c::column(c::vbox(0.0f, {}),
      c::row(c::hbox(4.0f, {.fillX = true, .height = 36.0f, .depth = 1.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f}}),
             c::styled({.width = 60.0f, .height = 28.0f}, nodes::Text("Emoji", 13.0f, 0xff000000u))),
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY, .masking = true}), page()),
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY, .visible = false}), nodes::Text("pages", 13.0f, 0xff000000u)));
}
inline scene::Spec card_spec() {
  return scene::Spec{.place = scene::anchor::kTopLeft, .x = 400.0f, .y = 100.0f, .width = 345.0f, .height = 400.0f, .cornerRadius = 8.0f,
                     .background = 0xff202020u, .border = scene::Border{0xff404040u, 1.0f}, .shadow = scene::Shadow{0x46000000u, 3.0f}};
}
inline auto card() { return c::spec_for<Told>([](const Told &) { return card_spec(); }, c::flicked(90.0f, Flick{}, inside())); }

inline auto floating_card() {
  auto made = card();
  c::state_of(made).setFloats(true);
  return made;
}
// A window with the card in it, ticked as a program's: the model changed
// at every frame (the sync, pictures come), the binding refreshed.
struct Window {
  scene::Scene<decltype(c::column(c::vbox(0.0f, {.fill = true}), floating_card()))> tree;
  Model m{Root{}};
  bind::Binding<Model> binding;
  double at = 0.0;
  int ticks = 0;
  Scroll *list = nullptr;
  Window() : tree(c::column(c::vbox(0.0f, {.fill = true}), floating_card())) {
    for (int i = 0; i < 5; ++i)
      frame();
    scene::forEachNode<Scroll>(tree.root(), [&](Scroll &one) { list = &one; });
  }
  void frame() {
    (void)m.apply(model::over<Told>(model::setTo(Told{.of = 1 + (++ticks % 7)})));
    // What was pressed and answered, delivered as a program does
    // (mux's after_event): along its path, up the Locals it is in.
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way);
    for (const auto &one : std::exchange(scene::hostWork().answers, {}))
      (void)bind::answer(tree.root(), m, one);
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 700.0f));
    (void)tree.finishFrame();
    at += 16.0;
  }
};

TEST(EmojiCard, ItsVirtualSectionsGoDownUnderTheWheel) {
  Window window;
  ASSERT_NE(window.list, nullptr);
  (void)window.tree.dispatchPointer(scene::pointer::move{520.0f, 300.0f});
  for (int i = 0; i < 40; ++i)
    window.frame();
  float last = window.list->current(), lowest = 0.0f;
  for (int notch = 0; notch < 200; ++notch) {
    (void)window.tree.dispatchPointer(scene::pointer::scroll{520.0f, 300.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      window.frame();
      lowest = std::min(lowest, window.list->current() - last);
      last = window.list->current();
    }
  }
  std::println("wheeled 200 notches: at {} (extent {}), back up {} at most in a frame", last, window.list->extent(), -lowest);
  EXPECT_GE(lowest, -0.5f);
  EXPECT_GT(last, 3000.0f);
}

TEST(EmojiCard, ItsVirtualSectionsStayWhereADragLetsThemGo) {
  Window window;
  ASSERT_NE(window.list, nullptr);
  float worst = 0.0f;
  for (int drag = 0; drag < 12; ++drag) {
    (void)window.tree.dispatchPointer(scene::pointer::move{520.0f, 420.0f});
    (void)window.tree.dispatchPointer(scene::pointer::down{520.0f, 420.0f, 1});
    window.frame();
    for (int step = 1; step <= 15; ++step) {
      (void)window.tree.dispatchPointer(scene::pointer::move{520.0f, 420.0f - 15.0f * static_cast<float>(step)});
      window.frame();
    }
    const float letGo = window.list->current();
    (void)window.tree.dispatchPointer(scene::pointer::up{520.0f, 195.0f, 1});
    for (int i = 0; i < 60; ++i)
      window.frame();
    worst = std::min(worst, window.list->current() - letGo);
  }
  std::println("dragged 12 times: at {} (extent {}), back {} at most after a release", window.list->current(),
               window.list->extent(), -worst);
  EXPECT_GE(worst, -0.5f) << "the view went back when the drag let it go";
  EXPECT_GT(window.list->current(), 2000.0f);
}

// As the reaction picker unrolls in a message's right-click menu (mux's
// context_menu): the menu's card sized by what it holds, its items in a
// column as high as they are, and the panel over them -- placed, filling
// that column, its own height none.
inline auto menu_card() {
  return c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f}}),
      c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 220.0f}),
                c::styled({.width = 260.0f, .height = 380.0f}, nodes::Text("items", 13.0f, 0xff000000u)),
                c::styled({.place = scene::anchor::kTopLeft, .fill = true, .masking = true}, page())));
}
inline auto placed_menu() {
  return c::styled({.place = scene::anchor::kTopLeft, .x = 300.0f, .y = 100.0f}, menu_card());
}
// As mux opens it: popped at the press, in the menu's own local state --
// low in the window, so it opens up and to the left of the press.
struct MenuSt {
  int unrolled = 0;
  friend bool operator==(const MenuSt &, const MenuSt &) = default;
};
inline auto popped_menu() {
  const c::PopAt at{.from = skia::SkRect::MakeXYWH(820.0f, 640.0f, 0.0f, 0.0f)};
  return c::local<MenuSt>(c::column(c::vbox(0.0f, {.fill = true}),
      c::popped_at<MenuSt>([at](const MenuSt &) { return std::optional(at); }, 8.0f, c::styled({}, menu_card()))));
}
struct Placed {
  auto operator()() const { return placed_menu(); }
};
struct PoppedAtPress {
  auto operator()() const { return popped_menu(); }
};
template <class Make> struct MenuWindowOf {
  scene::Scene<decltype(c::column(c::vbox(0.0f, {.fill = true}), Make{}()))> tree;
  Model m{Root{}};
  bind::Binding<Model> binding;
  double at = 0.0;
  Scroll *list = nullptr;
  MenuWindowOf() : tree(c::column(c::vbox(0.0f, {.fill = true}), Make{}())) {
    for (int i = 0; i < 5; ++i)
      frame();
    scene::forEachNode<Scroll>(tree.root(), [&](Scroll &one) { list = &one; });
  }
  void frame() {
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way);
    for (const auto &one : std::exchange(scene::hostWork().answers, {}))
      (void)bind::answer(tree.root(), m, one);
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 700.0f));
    (void)tree.finishFrame();
    at += 16.0;
  }
};
using MenuWindow = MenuWindowOf<Placed>;
TEST(EmojiCard, TheMenusPickerGoesDownUnderTheWheel) {
  MenuWindow window;
  ASSERT_NE(window.list, nullptr);
  (void)window.tree.dispatchPointer(scene::pointer::move{420.0f, 300.0f});
  for (int i = 0; i < 40; ++i)
    window.frame();
  float last = window.list->current(), lowest = 0.0f;
  for (int notch = 0; notch < 200; ++notch) {
    (void)window.tree.dispatchPointer(scene::pointer::scroll{420.0f, 300.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      window.frame();
      lowest = std::min(lowest, window.list->current() - last);
      last = window.list->current();
    }
  }
  std::println("menu picker wheeled 200 notches: at {} (extent {}, view {}), back up {} at most in a frame", last, window.list->extent(),
               window.list->bounds().height(), -lowest);
  EXPECT_GE(lowest, -0.5f);
  EXPECT_GT(last, 3000.0f);
}
TEST(EmojiCard, TheMenusPickerStaysWhereADragLetsItGo) {
  MenuWindow window;
  ASSERT_NE(window.list, nullptr);
  float worst = 0.0f;
  for (int drag = 0; drag < 12; ++drag) {
    (void)window.tree.dispatchPointer(scene::pointer::move{420.0f, 400.0f});
    (void)window.tree.dispatchPointer(scene::pointer::down{420.0f, 400.0f, 1});
    window.frame();
    for (int step = 1; step <= 15; ++step) {
      (void)window.tree.dispatchPointer(scene::pointer::move{420.0f, 400.0f - 15.0f * static_cast<float>(step)});
      window.frame();
    }
    const float letGo = window.list->current();
    (void)window.tree.dispatchPointer(scene::pointer::up{420.0f, 175.0f, 1});
    for (int i = 0; i < 60; ++i)
      window.frame();
    worst = std::min(worst, window.list->current() - letGo);
  }
  std::println("menu picker dragged 12 times: at {} (extent {}), back {} at most after a release", window.list->current(),
               window.list->extent(), -worst);
  EXPECT_GE(worst, -0.5f);
  EXPECT_GT(window.list->current(), 1000.0f);
}
TEST(EmojiCard, ThePoppedMenusPickerGoesDownUnderTheWheel) {
  MenuWindowOf<PoppedAtPress> window;
  ASSERT_NE(window.list, nullptr);
  for (int i = 0; i < 40; ++i)
    window.frame();
  const skia::SkRect seen = window.list->bounds();
  const float x = seen.centerX(), y = seen.centerY();
  (void)window.tree.dispatchPointer(scene::pointer::move{x, y});
  for (int i = 0; i < 10; ++i)
    window.frame();
  float last = window.list->current(), lowest = 0.0f;
  for (int notch = 0; notch < 200; ++notch) {
    (void)window.tree.dispatchPointer(scene::pointer::scroll{x, y, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      window.frame();
      lowest = std::min(lowest, window.list->current() - last);
      last = window.list->current();
    }
  }
  std::println("popped menu picker wheeled 200 notches: at {} (extent {}, view {} at {},{}), back up {} at most in a frame", last,
               window.list->extent(), window.list->bounds().height(), seen.fLeft, seen.fTop, -lowest);
  EXPECT_GE(lowest, -0.5f);
  EXPECT_GT(last, 3000.0f);
}

// A view that scrolls, of a size of its own, in a parent sized by what it
// holds: its box the same in the provisional layout and the real one, the
// real one is skipped -- it is only ever laid out provisionally (the rooms'
// list, a menu's picker). The wheel still takes it down.
struct Tall : scene::Node {
  Tall() { this->apply({.fillX = true, .height = 3000.0f}); }
};
inline auto sized_list() {
  return c::column(c::vbox(0.0f, {.place = scene::anchor::kTopLeft, .x = 100.0f, .y = 100.0f, .autoSize = scene::axes::kBoth}),
                   c::styled({.width = 300.0f, .height = 300.0f}, nodes::ScrollContainer(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Tall{}))));
}
TEST(EmojiCard, AViewOnlyEverLaidOutProvisionallyStillScrolls) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), sized_list()));
  using View = decltype(nodes::ScrollContainer(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Tall{})));
  double at = 0.0;
  const auto frame = [&] {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 700.0f));
    (void)tree.finishFrame();
    at += 16.0;
  };
  for (int i = 0; i < 5; ++i)
    frame();
  View *view = nullptr;
  scene::forEachNode<View>(tree.root(), [&](View &one) { view = &one; });
  ASSERT_NE(view, nullptr);
  for (int notch = 0; notch < 10; ++notch) {
    (void)tree.dispatchPointer(scene::pointer::scroll{250.0f, 250.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i)
      frame();
  }
  std::println("a view laid out only provisionally: at {} after 10 notches (extent {})", view->current(), view->extent());
  // Ten notches of 60: 600, where the wheel sent it -- not short of it, nor
  // at its end.
  EXPECT_NEAR(view->current(), 600.0f, 60.0f);
}
}  // namespace
