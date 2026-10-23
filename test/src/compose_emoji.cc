// Emoji cards and grids, and a rest in a scene.
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

TEST(Compose, AnEmojiCardIsAsBigAsItsShapeSaysAndHoldsItsParts) {
  using namespace emoji_card;
  using card_t = decltype(emoji_card::card());
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::projected<FeedTold>([](const FeedTold &now) { return now.of == 2 ? std::optional(true) : std::nullopt; },
                             c::mount<card_t, bool>([](bool) { return std::tuple{emoji_card::card()}; }, {.fill = true}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  const auto frame = [&] {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 800.0f, 600.0f));
    (void)tree.finishFrame();
  };
  frame();
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  frame();
  frame();
  std::print("{}", scene::laidOutText(tree.root()));
  float cardTop = -1.0f, cardHigh = 0.0f, barBottom = 0.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root())) {
    if (one.bounds.width() == 345.0f && one.bounds.fLeft == 40.0f && one.bounds.height() > cardHigh) {
      cardTop = one.bounds.fTop;
      cardHigh = one.bounds.height();
    }
    if (one.bounds.height() == 30.0f)
      barBottom = std::max(barBottom, one.bounds.fBottom);
  }
  std::println("emoji card at {} {} high, groups bar to {}", cardTop, cardHigh, barBottom);
  EXPECT_FLOAT_EQ(cardTop, 100.0f);
  EXPECT_FLOAT_EQ(cardHigh, 400.0f);
  EXPECT_FLOAT_EQ(barBottom, 500.0f);
}
TEST(Compose, AnEmojiGridOfItsSizeDoesLittleAFrame) {
  using namespace emoji_grid;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::styled({.width = 345.0f, .height = 400.0f}, panel())));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  double at = 0.0;
  const auto frame = [&] {
    const auto start = std::chrono::steady_clock::now();
    const scene::WalkCounts before = scene::walkCounts();
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 345.0f, 400.0f));
    const bool more = tree.finishFrame().fWantsAnotherFrame;
    at += 16.0;
    const scene::WalkCounts now = scene::walkCounts();
    return std::tuple{std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), now.tick - before.tick,
                      now.layout - before.layout, now.laidOut - before.laidOut, more};
  };
  for (int i = 0; i < 3; ++i) {
    const auto [ms, ticked, layouts, laid, more] = frame();
    std::println("first frames: {:.1f} ms, ticked {}, layouts {}, laid out {}, wants more {}", ms, ticked, layouts, laid, more);
  }
  for (int i = 0; i < 3; ++i) {
    const auto [ms, ticked, layouts, laid, more] = frame();
    std::println("idle: {:.1f} ms, ticked {}, layouts {}, laid out {}, wants more {}", ms, ticked, layouts, laid, more);
    // Idle, its cells' rests are not ticked: none has the pointer on it.
    EXPECT_LT(ticked, 50u);
  }
  for (int i = 0; i < 4; ++i) {
    (void)tree.dispatchPointer(scene::pointer::move{20.0f + 37.0f * static_cast<float>(i), 60.0f});
    const auto [ms, ticked, layouts, laid, more] = frame();
    std::println("hovered: {:.1f} ms, ticked {}, layouts {}, laid out {}, wants more {}", ms, ticked, layouts, laid, more);
  }
  for (int i = 0; i < 4; ++i) {
    (void)tree.dispatchPointer(scene::pointer::scroll{100.0f, 200.0f, 0.0f, -120.0f});
    const auto [ms, ticked, layouts, laid, more] = frame();
    std::println("scrolled: {:.1f} ms, ticked {}, layouts {}, laid out {}, wants more {}", ms, ticked, layouts, laid, more);
  }
  const auto [ms, ticked, layouts, laid, more] = frame();
  EXPECT_LT(laid, 200u);
}

// A rest in a scene, ticked only while the pointer is on it: the pointer
// coming wakes it, and it says so once it has rested; leaving wakes it, and
// it says that too. Idle, it is not ticked.
namespace rest_in_scene {
namespace c = skiff::compose;
struct RestSaid {
  std::vector<bool> *to = nullptr;
  int operator()(bool on) const {
    to->push_back(on);
    return 0;
  }
};
struct RestBox : scene::Node {
  RestBox() { this->apply({.width = 40.0f, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct RestNothing {
  int n = 0;
};
struct RestRoot {
  model::Tracked<RestNothing> nothing;
};
struct RestSink {
  template <class E> void take(const E &) {}
};
}  // namespace rest_in_scene
TEST(Compose, ARestInASceneSaysWhenThePointerHasRestedAndWhenItLeaves) {
  using namespace rest_in_scene;
  std::vector<bool> said;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::resting(300.0, RestSaid{&said}, RestBox())));
  model::Model<RestRoot, bind::NoReactions> m{RestRoot{}};
  RestSink sink;
  double at = 0.0;
  std::uint64_t ticked = 0;
  const auto frame = [&] {
    const std::uint64_t before = scene::walkCounts().tick;
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 200.0f, 200.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &sink);
    (void)tree.finishFrame();
    ticked = scene::walkCounts().tick - before;
    at += 16.0;
  };
  for (int i = 0; i < 5; ++i)
    frame();
  std::println("idle ticked {}", ticked);
  (void)tree.dispatchPointer(scene::pointer::move{20.0f, 20.0f});
  for (int i = 0; i < 30; ++i)
    frame();
  std::println("rested: {}", said.size());
  EXPECT_EQ(said, std::vector<bool>{true});
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 150.0f});
  for (int i = 0; i < 5; ++i)
    frame();
  EXPECT_EQ(said, (std::vector<bool>{true, false}));
  // Back, and away before it has rested: nothing said, and the next rest
  // counted from when the pointer came, not from the first.
  (void)tree.dispatchPointer(scene::pointer::move{20.0f, 20.0f});
  frame();
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 150.0f});
  for (int i = 0; i < 40; ++i)
    frame();
  (void)tree.dispatchPointer(scene::pointer::move{20.0f, 20.0f});
  for (int i = 0; i < 3; ++i)
    frame();
  EXPECT_EQ(said, (std::vector<bool>{true, false}));
}

}  // namespace
