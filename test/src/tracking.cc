import std;
import splice;
import gtest;
import skia;
import skiff.nodes;
import skiff.paint;
import skiff.scene;

#include "gtest/gtest-macros.h"

// What moves, shows or hides a node apart from its layout, tracked by the
// scene itself: where it was and where it is repainted, its parent laid out
// again, it drawn wherever its own area is repainted, it shown or hidden as
// its holder is hovered. Each was once left to whoever moved it to say, and
// what nobody said stayed on the screen as it had been.

namespace {

using namespace skiff::scene;
using skiff::nodes::Box;
using skiff::nodes::ScrollContainer;
using skiff::nodes::Stack;

const auto kView = skia::SkRect::MakeWH(200.0f, 120.0f);
inline constexpr skia::SkColor kFill = 0xff405060;

// A node that counts how often it is drawn.
struct DrawProbe : Node {
  int fDraws = 0;
  void drawSelf(skia::SkCanvas *, float) { ++fDraws; }
};

struct OneBox : Node {
  struct parts_t {
    Box<> box = make<Box>({.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f}, kFill);
  } parts;
};

TEST(Tracking, SetShiftRepaintsWhereItWasAndWhereItIs) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();

  scene.root().parts.box.fState.setShift(100.0f, 50.0f);
  const skia::SkRect damage = scene.finishFrame().fDamage;
  EXPECT_TRUE(damage.contains(skia::SkRect::MakeXYWH(10.0f, 10.0f, 40.0f, 20.0f)));
  EXPECT_TRUE(damage.contains(skia::SkRect::MakeXYWH(110.0f, 60.0f, 40.0f, 20.0f)));
}

TEST(Tracking, SetShiftToWhereItIsRepaintsNothing) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  scene.root().parts.box.fState.setShift(5.0f, 5.0f);
  (void)scene.finishFrame();

  scene.root().parts.box.fState.setShift(5.0f, 5.0f);
  EXPECT_TRUE(scene.finishFrame().fDamage.isEmpty());
}

TEST(Tracking, SetShiftQuietlyRepaintsNothing) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();

  scene.root().parts.box.fState.setShiftQuietly(30.0f, 0.0f);
  EXPECT_TRUE(scene.finishFrame().fDamage.isEmpty());
}

TEST(Tracking, ShownBoundsAreTheBoundsMovedByTheShift) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  auto &box = scene.root().parts.box;
  box.fState.setShift(7.0f, -3.0f);
  EXPECT_EQ(box.shownBounds(), box.bounds().makeOffset(7.0f, -3.0f));
}

struct Column : Stack {
  struct parts_t {
    Box<> first = make<Box>({.width = 40.0f, .height = 20.0f}, kFill);
    Box<> second = make<Box>({.width = 40.0f, .height = 20.0f}, kFill);
  } parts;
};

TEST(Tracking, SetOutOfFlowLaysTheParentOutAgain) {
  Scene<Column> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  const float before = scene.root().parts.second.bounds().fTop;

  scene.root().parts.first.fState.setOutOfFlow(true);
  EXPECT_TRUE(scene.layoutIfNeeded(kView));
  EXPECT_LT(scene.root().parts.second.bounds().fTop, before);

  scene.root().parts.first.fState.setOutOfFlow(false);
  EXPECT_TRUE(scene.layoutIfNeeded(kView));
  EXPECT_FLOAT_EQ(scene.root().parts.second.bounds().fTop, before);
}

struct Small : Node {
  struct parts_t {
    DrawProbe probe = make<DrawProbe>({.width = 10.0f, .height = 10.0f});
  } parts;
};
struct Outer : Node {
  struct parts_t {
    Small small = make<Small>({.width = 20.0f, .height = 20.0f});
  } parts;
};

TEST(Tracking, AChildMovedPastItsParentIsDrawnWhereItIs) {
  Scene<Outer> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  auto &probe = scene.root().parts.small.parts.probe;
  probe.fState.setShift(100.0f, 60.0f);
  (void)scene.finishFrame();  // the damage walk works out each node's reach

  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(200, 120));
  skia::SkCanvas canvas(pixels);
  // Only where it is now repainted: its parent's own box is not.
  canvas.clipRect(skia::SkRect::MakeXYWH(100.0f, 60.0f, 10.0f, 10.0f));
  const int before = probe.fDraws;
  scene.draw(&canvas);
  EXPECT_EQ(probe.fDraws, before + 1);
}

struct Pushed : Stack {
  struct parts_t {
    Box<> spacer = make<Box>({.width = 40.0f, .height = 10.0f}, kFill);
    ScrollContainer<Box<>> view{make<Box>({.width = 40.0f, .height = 200.0f}, kFill)};
  } parts;
  Pushed() { parts.view.apply({.width = 100.0f, .height = 50.0f}); }
};

TEST(Tracking, AScrollViewPushedDownIsRepaintedWhereItIs) {
  Scene<Pushed> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();

  scene.root().parts.spacer.apply({.height = 40.0f});
  scene.layoutIfNeeded(kView);
  const skia::SkRect damage = scene.finishFrame().fDamage;
  EXPECT_TRUE(damage.contains(scene.root().parts.view.bounds()));
}

struct HolderBox : Node {
  struct parts_t {
    Box<> tip = make<Box>({.x = 50.0f, .width = 20.0f, .height = 10.0f, .visible = false, .revealOnHover = true}, kFill);
  } parts;
};
struct RevealScreen : Node {
  struct parts_t {
    HolderBox holder = make<HolderBox>({.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f});
  } parts;
};

TEST(Tracking, RevealOnHoverShowsWhileTheHolderIsHovered) {
  Scene<RevealScreen> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  auto &tip = scene.root().parts.holder.parts.tip;
  EXPECT_FALSE(tip.visible());

  scene.setHover(20.0f, 20.0f);
  EXPECT_TRUE(tip.visible());
  EXPECT_FALSE(scene.finishFrame().fDamage.isEmpty());

  scene.setHover(180.0f, 100.0f);
  EXPECT_FALSE(tip.visible());
}

} // namespace
