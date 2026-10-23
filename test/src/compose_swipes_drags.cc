// Rows swiped under a flick or dragged under a list; what moves under a fade; a list dragged wider; a window at rest.
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


// A row swiped far, under what watches the whole screen for a flick (mux's
// chats screen): let go, it goes back -- its lift its own, not taken by the
// flick on its way down.
namespace swipe_under_flick {
struct Flick {
  std::optional<int> operator()(bool) const { return 1; }
};
}  // namespace swipe_under_flick
TEST(Compose, ARowSwipedFarUnderAFlickIsLetGo) {
  using namespace swipe_follow;
  auto row = c::swiped(48.0f, 120.0f, [] { return Answered{}; }, [](nodes::Text &, float) {}, nodes::Text("", 12.0f, 0u), ShiftProbe());
  scene::Scene tree(c::flicked(90.0f, swipe_under_flick::Flick{}, c::column(c::vbox(0.0f, {.fill = true}), std::move(row))));
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f);
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  ShiftProbe *probe = scene::findNode<ShiftProbe>(tree.root());
  ASSERT_NE(probe, nullptr);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{250.0f, 20.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::move{235.0f, 20.0f});
  frame(216.0);
  (void)tree.dispatchPointer(scene::pointer::move{140.0f, 20.0f});
  frame(232.0);
  (void)tree.dispatchPointer(scene::pointer::up{140.0f, 20.0f, 1});
  const auto replied = scene::hostWork().pressed.size();
  for (double at = 248.0; at < 900.0; at += 16.0)
    frame(at);
  std::println("swiped far: replied {}, shift after {}", replied, probe->fState.fShiftX);
  EXPECT_EQ(replied, 1u);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, 0.0f);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
}

// A row in a list dragged up and down -- the list's drag, its release the
// list's too: the pointer moved on with no button held is not a swipe.
// (Taken for one, the row followed the pointer, stuck, until a click.)
TEST(Compose, ARowUnderAListDraggedIsNotSwipedOnceLetGo) {
  using namespace swipe_follow;
  auto row = c::swiped(48.0f, 120.0f, [] { return Answered{}; }, [](nodes::Text &, float) {}, nodes::Text("", 12.0f, 0u), ShiftProbe());
  auto view = nodes::ScrollContainer(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(row),
                                               c::column(c::vbox(0.0f, {.fillX = true, .height = 1000.0f}))));
  view.apply({.fill = true});
  view.setHoldsInView(false);
  scene::Scene tree(std::move(view));
  const auto box = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f);
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(box);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  ShiftProbe *probe = scene::findNode<ShiftProbe>(tree.root());
  ASSERT_NE(probe, nullptr);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
  // Pressed on the row and dragged up: the list's drag, and its release.
  (void)tree.dispatchPointer(scene::pointer::down{200.0f, 30.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::move{200.0f, 20.0f});
  frame(216.0);
  (void)tree.dispatchPointer(scene::pointer::move{200.0f, 15.0f});
  frame(232.0);
  (void)tree.dispatchPointer(scene::pointer::up{200.0f, 15.0f, 1});
  // Then moved left, no button held, over the row.
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 15.0f});
  frame(248.0);
  (void)tree.dispatchPointer(scene::pointer::move{110.0f, 15.0f});
  frame(264.0);
  std::println("row moved after the list let go: {}", probe->fState.fShiftX);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, 0.0f);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
}

// What is under a dialog fading in, moving itself -- a drawer sliding away
// as the dialog it opened comes: drawn as it is at each frame, not kept as
// it was at the fade's first; once it rests, kept.
namespace frozen_moving {
namespace c = skiff::compose;
template <int Tag> struct Mover : scene::Node {
  bool fMoving = true;
  int fDraws = 0;
  Mover() { this->apply({.place = scene::anchor::kTopLeft, .fill = true}); }
  [[nodiscard]] bool settling() const { return fMoving; }
  [[nodiscard]] bool wantsTick() const { return fMoving; }
  void drawSelf(skia::SkCanvas *, float) { ++fDraws; }
};
}  // namespace frozen_moving
TEST(Compose, WhatMovesUnderAFadeIsNotFrozen) {
  using namespace frozen_moving;
  scene::Scene tree(c::frozen_under(c::vbox(0.0f, {.fill = true}), Mover<0>(), Mover<1>()));
  const auto box = skia::SkRect::MakeXYWH(0.0f, 0.0f, 100.0f, 80.0f);
  tree.layoutIfNeeded(box);
  (void)tree.finishFrame();
  auto *under = scene::findNode<Mover<0>>(tree.root());
  ASSERT_NE(under, nullptr);
  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(100, 80));
  skia::SkCanvas canvas(pixels);
  scene::detail::PlainPaint plain;
  // Both moving: what is under drawn at every frame.
  const int before = under->fDraws;
  tree.draw(plain, &canvas);
  tree.draw(plain, &canvas);
  const int moving = under->fDraws - before;
  // It rests, what is over it still moving: kept, drawn once.
  under->fMoving = false;
  const int still = under->fDraws;
  tree.draw(plain, &canvas);
  tree.draw(plain, &canvas);
  tree.draw(plain, &canvas);
  const int resting = under->fDraws - still;
  std::println("under drawn: {} in two frames moving, {} in three resting", moving, resting);
  EXPECT_EQ(moving, 2);
  EXPECT_EQ(resting, 1);
}

// A long list as its width is dragged: only the rows near the view laid out
// again at each width, the rest once the width holds -- and then all of
// them at it.
namespace dragged_width {
namespace c = skiff::compose;
using namespace window_edge;
inline int &measures() {
  static int kept = 0;
  return kept;
}
struct CountingRow : scene::Node {
  CountingRow() { this->apply({.fillX = true, .height = 40.0f}); }
  void measure(const skia::SkRect &) { ++measures(); }
};
struct MakeCounting {
  CountingRow operator()(const Item &) const { return CountingRow{}; }
};
using CountingWindow = c::WindowFrom<Src, FeedModel, std::string, int, CountingRow, MakeCounting, c::NoLocate, Tag>;
}  // namespace dragged_width
TEST(Compose, AListDraggedWiderLaysOutTheRowsInViewThenAll) {
  using namespace dragged_width;
  CountingWindow window(MakeCounting{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, CountingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 60}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  float width = 300.0f;
  bool wants = true;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, width, 400.0f));
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  // Dragged: a new width at each frame.
  int most = 0;
  double at = 600.0;
  for (int step = 0; step < 10; ++step, at += 16.0) {
    width += 7.0f;
    measures() = 0;
    frame(at);
    most = std::max(most, measures());
  }
  // Let go: the frames it asks for, then every row at the width.
  measures() = 0;
  wants = true;
  int frames = 0;
  for (; wants && frames < 20; at += 16.0, ++frames)
    frame(at);
  const int after = measures();
  float narrowest = 1e9f, widest = 0.0f;
  scene::forEachNode<CountingRow>(tree.root(), [&](CountingRow &row) {
    narrowest = std::min(narrowest, row.bounds().width());
    widest = std::max(widest, row.bounds().width());
  });
  std::println("dragged: at most {} rows laid out a frame; after, {} in {} frames; rows {}..{} wide (list {})", most, after, frames, narrowest,
               widest, width);
  // Fewer than all 60, at every step of the drag (twice each, for the list
  // sized by its rows is laid out twice): those in view, a screen around.
  EXPECT_LT(most, 2 * 60);
  EXPECT_LE(most, 2 * 30);
  EXPECT_FLOAT_EQ(narrowest, width);
  EXPECT_FLOAT_EQ(widest, width);
}

// A window list at rest asks for no frames: nothing in it settles for ever.
TEST(Compose, AWindowAtRestAsksForNoFrames) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 60}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  for (double at = 0.0; at < 3000.0; at += 16.0) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    // What it answered, delivered as a program does: kept undelivered, it
    // would settle for ever, its answers waiting.
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  }
  EXPECT_FALSE(wants);
}

}  // namespace
