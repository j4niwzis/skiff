// Menus in local state and on a grid of rows, asked frames; history at the top; a hidden button's badge; a viewed list beside its row.
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

TEST(Compose, AMenuInItsLocalStateIsSeenAtItsPlace) {
  using namespace local_menu;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), menu(150.0f, 120.0f)));
  window_edge::FeedModel m{window_edge::FeedRoot{.told = {window_edge::FeedTold{.of = 1}}, .feed = {window_edge::Feed{}}}};
  bind::Binding<window_edge::FeedModel> binding;
  bool wants = true;
  int frames = 0;
  for (double at = 0.0; at < 1000.0 && (wants || frames < 2); at += 16.0, ++frames) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
    wants = tree.finishFrame().fWantsAnotherFrame;
  }
  std::print("{}", scene::laidOutText(tree.root()));
  bool seen = false;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.outOfFlow && one.visible && one.alpha > 0.0f && one.bounds.fLeft == 150.0f && one.bounds.fTop == 120.0f)
      seen = true;
  EXPECT_TRUE(seen);
}
TEST(Compose, ARightPressAnywhereOnARowAsksItsMenu) {
  using namespace row_grid;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row()));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  float bottom = 0.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.depth == 1)
      bottom = std::max(bottom, one.bounds.fBottom);
  std::print("{}", scene::laidOutText(tree.root()));
  std::vector<std::string> missed;
  for (float y = 2.0f; y < bottom; y += 6.0f)
    for (float x = 2.0f; x < 400.0f; x += 12.0f) {
      scene::hostWork().answers.clear();
      (void)tree.dispatchPointer(scene::pointer::down{x, y, 3});
      (void)tree.dispatchPointer(scene::pointer::up{x, y, 3});
      if (scene::hostWork().answers.empty())
        missed.push_back(std::format("{},{}", x, y));
    }
  scene::hostWork().answers.clear();
  std::println("row to {}: no menu at {} points: {}", bottom, missed.size(), missed | std::views::take(20));
  if constexpr (scene::kErasedWalks)
    EXPECT_TRUE(missed.empty());
}

// A menu made as the model asks for it -- mounted then, as mux's window
// mounts its menu -- seen at its place with only the frames the window
// then asks for, not once something else lays the window out.
TEST(Compose, AMenuMountedAsAskedIsSeenWithTheFramesItAsks) {
  using namespace local_menu;
  using namespace window_edge;
  using menu_t = decltype(local_menu::menu(0.0f, 0.0f));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::projected<FeedTold>([](const FeedTold &now) { return now.of == 2 ? std::optional(true) : std::nullopt; },
                             c::mount<menu_t, bool>([](bool) { return std::tuple{local_menu::menu(150.0f, 120.0f)}; }, {.fill = true}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  bool wants = true;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  double at = 0.0;
  for (; at < 300.0; at += 16.0)
    frame(at);
  // Asked for, as an event's answer is taken: then the frames it asks for.
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  wants = true;
  int frames = 0;
  for (; wants && frames < 60; at += 16.0, ++frames)
    frame(at);
  std::print("{}", scene::laidOutText(tree.root()));
  bool seen = false;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.outOfFlow && one.visible && one.alpha > 0.0f && one.bounds.fLeft == 150.0f && one.bounds.fTop == 120.0f)
      seen = true;
  std::println("menu seen: {} after {} frames", seen, frames);
  EXPECT_TRUE(seen);
}

// Away from the end, history come at the top is not counted as come: only
// what comes after the newest.
TEST(Compose, HistoryAtTheTopIsNotCountedAsUnseen) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 40}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  double at = 0.0;
  for (; at < 600.0; at += 16.0)
    frame(at);
  // Scrolled up, away from the end.
  for (int notch = 0; notch < 10; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (; at < 1600.0; at += 16.0)
    frame(at);
  auto *list = scene::findNode<bind::Bound<FeedTold, FeedWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  // A page of history above.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 60, .first = -20})));
  for (; at < 2200.0; at += 16.0)
    frame(at);
  const std::size_t afterHistory = list->unseen();
  // And one that comes at the end.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 61, .first = -20})));
  for (; at < 2800.0; at += 16.0)
    frame(at);
  std::println("unseen after history: {}, after one more: {}", afterHistory, list->unseen());
  EXPECT_EQ(afterHistory, 0u);
  EXPECT_EQ(list->unseen(), 1u);
}

// A button hidden -- the way down, at the end already: all it drew is
// repainted, the badge standing out over its top too.
namespace badge_hidden {
namespace c = skiff::compose;
using namespace window_edge;
struct Plate : scene::Node {
  Plate() { this->apply({.width = 42.0f, .height = 42.0f, .background = 0xff808080u}); }
};
struct Badge : scene::Node {
  Badge() { this->apply({.place = scene::anchor::kTopCentre, .y = -10.0f, .width = 20.0f, .height = 18.0f, .background = 0xffff0000u}); }
};
}  // namespace badge_hidden
TEST(Compose, AHiddenButtonRepaintsItsBadgeToo) {
  using namespace badge_hidden;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true, .padding = {40.0f, 40.0f, 40.0f, 40.0f}}),
      c::shown_for<FeedTold>([](const FeedTold &now) { return now.of == 1; },
                             c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), Plate(), Badge()))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  const auto frame = [&] {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    return tree.finishFrame().fDamage;
  };
  (void)frame();
  (void)frame();
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  const skia::SkRect damage = frame();
  std::println("repainted as hidden: {},{} to {},{}", damage.fLeft, damage.fTop, damage.fRight, damage.fBottom);
  // The badge stood 10 over the plate's top, at 30.
  EXPECT_LE(damage.fTop, 30.0f);
}
TEST(Compose, AViewedListOpensBesideItsRow) {
  using namespace viewed_list;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), menu()));
  window_edge::FeedModel m{window_edge::FeedRoot{.told = {window_edge::FeedTold{.of = 1}}, .feed = {window_edge::Feed{}}}};
  bind::Binding<window_edge::FeedModel> binding;
  window_edge::Heard heard;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  double at = 0.0;
  for (; at < 300.0; at += 16.0)
    frame(at);
  // Over the row: 150 + 10 across, 120 + 30 + 10 down.
  (void)tree.dispatchPointer(scene::pointer::move{160.0f, 160.0f});
  for (; at < 800.0; at += 16.0)
    frame(at);
  std::print("{}", scene::laidOutText(tree.root()));
  float left = -1.0f, top = -1.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.outOfFlow && one.visible && one.bounds.width() == 100.0f) {
      left = one.bounds.fLeft;
      top = one.bounds.fTop;
    }
  std::println("viewed list at {},{}", left, top);
  EXPECT_FLOAT_EQ(left, 150.0f + 120.0f - 6.0f);
  EXPECT_FLOAT_EQ(top, 150.0f);
}

}  // namespace
