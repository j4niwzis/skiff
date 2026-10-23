// Pictures that come later, in a row and in mux's box; a card popped at the pointer; a flash that does not flash again.
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

TEST(Compose, APictureThatComesLaterIsDrawn) {
  using namespace waiting_picture;
  PictureWindow window(MakePictureRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, PictureWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 2}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  skia::SkRect damage = skia::SkRect::MakeEmpty();
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    const auto done = tree.finishFrame();
    wants = done.fWantsAnotherFrame;
    damage = scene::joined(damage, done.fDamage);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  std::println("waiting before it came: {}", !Store::waiting.empty());
  // It comes: put where it is looked up, those waiting woken -- as mux's
  // cache does -- and the frames the window then asks for.
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  Store::image = surface->makeImageSnapshot();
  Store::waiting.wake();
  damage = skia::SkRect::MakeEmpty();
  wants = true;
  int frames = 0;
  for (double at = 600.0; at < 1600.0 && (wants || frames < 1); at += 16.0, ++frames)
    frame(at);
  std::println("frames after it came: {}, repainted {} x {}", frames, damage.width(), damage.height());
  EXPECT_FALSE(damage.isEmpty());
  Store::image = nullptr;
}
TEST(Compose, APictureInMuxsBoxThatComesLaterIsDrawn) {
  using namespace waiting_picture_box;
  using waiting_picture::Store;
  BoxWindow window(MakeBoxRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, BoxWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 2}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  skia::SkRect damage = skia::SkRect::MakeEmpty();
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    const auto done = tree.finishFrame();
    wants = done.fWantsAnotherFrame;
    damage = scene::joined(damage, done.fDamage);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  std::println("box: waiting before it came: {}", !Store::waiting.empty());
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  Store::image = surface->makeImageSnapshot();
  Store::waiting.wake();
  damage = skia::SkRect::MakeEmpty();
  wants = true;
  int frames = 0;
  for (double at = 600.0; at < 1600.0 && (wants || frames < 1); at += 16.0, ++frames)
    frame(at);
  std::println("box: frames after it came: {}, repainted {} x {}", frames, damage.width(), damage.height());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_FALSE(damage.isEmpty());
  Store::image = nullptr;
}
TEST(Compose, ACardPoppedAtThePointerGoesThere) {
  using namespace popped_menu;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), menu()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  bool wants = true;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  wants = true;
  // Seen only where it goes: never drawn first in the corner it is laid out
  // in before its place is known.
  int seenElsewhere = 0;
  for (double at = 200.0; at < 1200.0 && wants; at += 16.0) {
    frame(at);
    for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
      if (one.outOfFlow && one.visible && one.alpha > 0.0f && (one.bounds.fLeft != 150.0f || one.bounds.fTop != 120.0f))
        ++seenElsewhere;
  }
  EXPECT_EQ(seenElsewhere, 0);
  float left = -1.0f, top = -1.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.outOfFlow) {
      left = one.bounds.fLeft;
      top = one.bounds.fTop;
    }
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_FLOAT_EQ(left, 150.0f);
  EXPECT_FLOAT_EQ(top, 120.0f);
}
TEST(Compose, AFlashLeavingARowDoesNotFlashItAgain) {
  using namespace wash_once;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row(1), row(2)));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  double at = 0.0;
  const auto frames = [&](double until) {
    for (; at < until; at += 16.0) {
      binding.refresh(tree.root(), m);
      tree.update(at);
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
      (void)tree.finishFrame();
    }
  };
  const auto aim = [&](std::string key, std::uint64_t number) {
    (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                              model::setTo(FeedTold{.of = 1, .aim = {.key = std::move(key), .number = number}})));
  };
  frames(200.0);
  aim("1", 1);
  frames(2000.0);  // its flash done
  painted().clear();
  aim("2", 2);
  frames(2100.0);
  // Flashing now: the second only -- not the first, flashed again as the
  // flash left it.
  const auto lit = [](int which) {
    return std::ranges::count_if(painted(), [&](const auto &one) { return one.first == which && one.second != 0; });
  };
  std::println("painted lit: first {}, second {}", lit(1), lit(2));
  EXPECT_EQ(lit(1), 0);
  EXPECT_GT(lit(2), 0);
}

namespace waiting_picture_box {
struct NoRules {
  struct State {
    friend bool operator==(const State &, const State &) = default;
  };
  static void prepare(auto &, State &) {}
  static void arrange(auto &, State &) {}
  static bool settling(const auto &, const State &) { return false; }
  static void settle(auto &, State &) {}
};
}  // namespace waiting_picture_box
// A right press on a message's picture -- pressed to open it in the viewer:
// asks the row's menu, as anywhere else in the row.
TEST(Compose, ARightPressOnARowsPictureAsksItsMenu) {
  using namespace waiting_picture_box;
  using row_menu::Ask;
  auto picture = SKIFF_NAMED(picture_view, c::onClick(Open{}, fitted_t(c::Fit{.width = 200.0f, .height = 100.0f, .most = 430.0f, .least = 100.0f}, NoRatio{}, box()), "Open picture"));
  picture.apply({.alpha = 0.9f});
  const auto answered = row_menu_parts::rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::swiped(48.0f, 120.0f, [] { return row_menu::Reply{}; }, row_menu::Show{}, nodes::Text("", 12.0f, 0u),
                c::onClickAt([](auto &, float, float) { return std::optional<int>(); },
                             c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                    // The bubble, held as mux holds it (made in its own unit).
                                    compose::Held(c::arranged<NoRules>(NoRules::State{}, c::column(c::vbox(2.0f, {.autoSize = scene::axes::kBoth}), std::move(picture))))))))));
  std::println("right press on a picture answered: {}", answered);
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(answered, 1u);
}

}  // namespace
