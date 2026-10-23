// Short windows asking for older until full; a hidden tab row; a scroll view under a combinator; a field growing with its lines; a window asking again.
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

// Screens as combinator expressions, bound to a model; local and hidden
// state; components wired by what they send (>>, +); a processor; time; a
// Binding doing only what can have happened; events whose types are
// deduced.

#include "compose_common.inc"

namespace {

// A chat with less than a screen of messages: its top asks for what is older
// at once, and again as each page comes and still leaves room, without
// anything scrolled.
TEST(Compose, AShortWindowAsksForOlderUntilItFillsItsView) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  double at = 0.0;
  const auto frames = [&](int n) {
    for (int i = 0; i < n; ++i, at += 16.0) {
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
      tree.update(at);
      (void)tree.finishFrame();
      for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
        (void)bind::press(tree.root(), m, way, &heard);
      binding.refresh(tree.root(), m);
    }
  };
  frames(30);
  std::println("older asked with 3 rows: {}", heard.older);
  EXPECT_GE(heard.older, 1);
  // A page of history come above: still less than a screen.
  (void)m.apply(model::over<Feed>(model::setTo(Feed{.count = 6, .first = -3})));
  binding.refresh(tree.root(), m);
  frames(30);
  std::println("older asked after a page came: {}", heard.older);
  EXPECT_GE(heard.older, 2);
}

// A page of history come that shows nothing of its own (a room's state,
// hidden kinds): the same rows, still less than a screen -- asked again.
TEST(Compose, AShortWindowAsksAgainWhenAPageAddsNothingShown) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  double at = 0.0;
  const auto frames = [&](int n) {
    for (int i = 0; i < n; ++i, at += 16.0) {
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
      tree.update(at);
      (void)tree.finishFrame();
      for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
        (void)bind::press(tree.root(), m, way, &heard);
      binding.refresh(tree.root(), m);
    }
  };
  frames(30);
  const int first = heard.older;
  // The model moved, the rows did not.
  (void)m.apply(model::over<Feed>(model::setTo(Feed{.count = 3})));
  binding.refresh(tree.root(), m);
  frames(30);
  std::println("older asked: {} then {}", first, heard.older);
  EXPECT_GE(first, 1);
  EXPECT_GT(heard.older, first);
}
// A tab row as mux's room settings make it -- named, its look read from the
// model, pressed -- hidden as made: stays hidden.
namespace hidden_tab {
namespace c = skiff::compose;
using namespace window_edge;
struct Tap {
  int operator()() const { return 0; }
};
struct Mark : scene::Node {
  Mark() { this->apply({.width = 20.0f, .height = 20.0f}); }
};
inline auto tab_row() {
  return SKIFF_NAMED(manage_tab_row, c::spec_for<FeedTold>([](const FeedTold &) { return scene::Spec{.selected = false}; },
      c::onPress(Tap{}, c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f}), Mark{}))));
}
inline auto no_spec() { return SKIFF_NAMED(manage_tab_row, c::onPress(Tap{}, c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f}), Mark{}))); }
inline auto no_name() {
  return c::spec_for<FeedTold>([](const FeedTold &) { return scene::Spec{.selected = false}; },
      c::onPress(Tap{}, c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f}), Mark{})));
}
inline auto only_row() { return c::row(c::hbox(8.0f, {.fillX = true, .height = 40.0f}), Mark{}); }
template <class Row> int shown_of(Row hidden) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::visible(false, std::move(hidden))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  for (double at = 0.0; at < 100.0; at += 16.0) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    (void)tree.finishFrame();
  }
  int marks = 0;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    marks += one.visible && one.bounds.height() == 20.0f ? 1 : 0;
  return marks;
}
}  // namespace hidden_tab
TEST(Compose, ATabRowHiddenAsMadeStaysHidden) {
  using namespace hidden_tab;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::visible(false, tab_row()), c::visible(true, tab_row())));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  for (double at = 0.0; at < 100.0; at += 16.0) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    (void)tree.finishFrame();
  }
  int shown = 0;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.view == "manage_tab_row" && one.names && one.visible && one.bounds.height() > 0.0f)
      ++shown;
  std::println("tab rows shown: {} (of 2, one hidden)", shown);
  EXPECT_EQ(shown_of(tab_row()), 0);
  EXPECT_EQ(shown_of(no_name()), 0);
  EXPECT_EQ(shown, 1);
}
TEST(Compose, AScrollViewWrappedByACombinatorStillScrolls) {
  using namespace wrapped_wheel;
  const float bare = wheeled(list());
  const float marked = wheeled(c::marked(WheelTag{}, list()));
  const float ticking = wheeled(c::keep_ticking(true, list()));
  const float clamped = wheeled(c::clamped_width(c::WidthLimit{.wanted = 300.0f, .whole = true}, list()));
  std::println("scrolled by the wheel: bare {}, marked {}, ticking {}, clamped {}", bare, marked, ticking, clamped);
  EXPECT_GT(clamped, 100.0f);
  EXPECT_GT(bare, 100.0f);
  EXPECT_GT(marked, 100.0f);
  EXPECT_GT(ticking, 100.0f);
}
// A field that grows with its lines (as skiff-widgets' TextArea: its height
// said in measure, each edit invalidating its layout), at the bottom of a
// column whose list above grows into what is left -- mux's chat column and
// composer: lines typed, the field grows upwards, the list giving way; the
// field never past the window's bottom.
namespace growing_field {
namespace c = skiff::compose;
struct FieldTag {};
struct Lines;
inline Lines *gField = nullptr;  // where the field is, as it was last measured (erased, findNode cannot see it)
struct Area;
inline Area *gArea = nullptr;
struct Lines : scene::Node {
  int lines = 1;
  // One line high as it is made, as TextArea is.
  Lines() { this->fState.fHeight = 20.0f; }
  void measure(const skia::SkRect &) {
    gField = this;
    this->fState.fHeight = 20.0f * static_cast<float>(lines);
  }
  void addLine() {
    ++lines;
    this->fState.invalidateLayout();
    this->fState.markDamaged();
  }
};
struct Area : scene::Node {
  void measure(const skia::SkRect &) { gArea = this; }
};
// The area above as mux's chat_area: a named column growing on its own
// account, a backdrop filling it, under styled's grow and a wrapper.
inline auto area_like() {
  return SKIFF_NAMED(area_like, c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), c::styled({.fill = true}, Area{})));
}
template <class Above, class Field> auto screen_with(Above above, Field field) {
  return c::column(c::vbox(0.0f, {.fill = true}),
      c::styled({.fillX = true, .grow = scene::axes::kY}, std::move(above)),
      SKIFF_NAMED(composer_like, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          c::styled({.fillX = true, .height = 1.0f}, nodes::Box<>(0xff000000u)),
          c::row(c::hbox(6.0f, {.fillX = true, .autoSize = scene::axes::kY, .minHeight = 54.0f, .padding = {9.0f, 8.0f, 9.0f, 8.0f}}),
              c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, std::move(field))))));
}
template <class Field> auto screen(Field field) { return screen_with(Area{}, std::move(field)); }
template <class Tree> std::pair<float, float> typed(Tree &tree, int extra) {
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 600.0f);
  double at = 0.0;
  const auto frame = [&] {
    tree.update(at += 16.0);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  };
  gField = nullptr;
  gArea = nullptr;
  frame();
  Lines *field = gField;
  Area *area = gArea;
  if (!field || !area)
    return {1e9f, 1e9f};
  for (int i = 0; i < extra; ++i) {
    field->addLine();
    frame();
  }
  const float bottom = scene::work::drawnOffset(field->fState.fId).y + field->fState.fBounds.height();
  std::println("  field {} lines: bounds {} {} {} {}, drawn y {}, area height {}", field->lines, field->fState.fBounds.fLeft, field->fState.fBounds.fTop,
               field->fState.fBounds.width(), field->fState.fBounds.height(), scene::work::drawnOffset(field->fState.fId).y, area->fState.fBounds.height());
  return {std::max(bottom, field->fState.fBounds.fBottom), area->fState.fBounds.height()};
}
}  // namespace growing_field
TEST(Compose, AFieldGrowingWithItsLinesPushesTheListUp) {
  using namespace growing_field;
  scene::Scene bare(screen(Lines{}));
  scene::Scene wrapped(screen(c::marked(FieldTag{}, c::keep_ticking(true, Lines{}))));
  std::println("bare:");
  const auto [bareBottom, bareArea] = typed(bare, 5);
  std::println("wrapped:");
  const auto [wrappedBottom, wrappedArea] = typed(wrapped, 5);
  scene::Scene named(screen_with(c::marked(FieldTag{}, area_like()), c::marked(FieldTag{}, c::keep_ticking(true, Lines{}))));
  std::println("area a named growing column:");
  const auto [namedBottom, namedArea] = typed(named, 5);
  EXPECT_LE(namedBottom, 600.5f);
  EXPECT_LT(namedArea, 600.0f - 120.0f);
  // Six lines, 120 high: within the window, the area above smaller by it.
  EXPECT_LE(bareBottom, 600.5f);
  EXPECT_LE(wrappedBottom, 600.5f);
  EXPECT_LT(bareArea, 600.0f - 120.0f);
  EXPECT_LT(wrappedArea, 600.0f - 120.0f);
}

// An ask for what is older that brings nothing -- the request failed, or the
// program let it go as one on its way: nothing read, the model as it was.
// Still at its top, the list asks again a while later; not at every frame.
TEST(Bind, AWindowAtItsTopAsksAgainWhereAnAskBroughtNothing) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  double at = 0.0;
  const auto frames = [&](int n) {
    for (int i = 0; i < n; ++i, at += 16.0) {
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
      tree.update(at);
      (void)tree.finishFrame();
      for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
        (void)bind::press(tree.root(), m, way, &heard);
      binding.refresh(tree.root(), m);
    }
  };
  frames(30);
  const int first = heard.older;
  frames(60);  // a second more, nothing come
  const int soon = heard.older;
  frames(250);  // four seconds more
  std::println("older asked: {}, a second later {}, five seconds later {}", first, soon, heard.older);
  EXPECT_GE(first, 1);
  EXPECT_EQ(soon, first);
  EXPECT_GT(heard.older, first);
  EXPECT_LE(heard.older, first + 2);
}


}  // namespace
