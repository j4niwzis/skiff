// SPDX-License-Identifier: AGPL-3.0-only
import std;
import skia;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;

#include "gtest/gtest-macros.h"

namespace {
using namespace skiff;
using namespace skiff::compose;
struct ClickState { int unused = 0; };

TEST(Compose, ClickAndComputedPressOnPlainTextReceivePointerClicks) {
  const auto clicked = [](auto button) {
    button.apply({.width = 120.0f, .height = 32.0f});
    scene::Scene tree(std::move(button));
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0, 0, 200, 100));
    scene::hostWork().pressed.clear();
    (void)tree.dispatchPointer(scene::pointer::down{20, 16, 1});
    (void)tree.dispatchPointer(scene::pointer::up{20, 16, 1});
    const auto paths = std::exchange(scene::hostWork().pressed, {});
    EXPECT_EQ(paths.size(), 1u);
    struct Heard { int value = 0; void take(int one) { value = one; } } heard;
    model::Model<ClickState, bind::NoReactions> state{ClickState{}};
    for (const auto& path : paths)
      EXPECT_TRUE(bind::press(tree.root(), state, path, &heard));
    EXPECT_EQ(heard.value, 42);
  };
  clicked(onClick(42, nodes::Text("Open thread", 13.0f, 0u)));
  clicked(onPress([] { return 42; }, nodes::Text("Open emoji", 13.0f, 0u)));
}

TEST(Compose, PlainTextClickHonoursDeferredReleaseOutsideReleaseAndCancellation) {
  auto button = onClick(42, nodes::Text("Open thread", 13.0f, 0u));
  button.fState.fBounds = skia::SkRect::MakeXYWH(0, 0, 120, 32);
  scene::hostWork().pressedNow = nullptr;
  scene::PointerReply down;
  down.fDeferClick = true;
  button.onPointer(scene::phase::target{}, scene::pointer::down{20, 16, 1}, down);
  EXPECT_TRUE(down.fHandled);
  EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
  scene::PointerReply up;
  button.onPointer(scene::phase::target{}, scene::pointer::up{20, 16, 1}, up);
  EXPECT_EQ(std::exchange(scene::hostWork().pressedNow, nullptr), &button.fState);
  down = {};
  down.fDeferClick = true;
  up = {};
  button.onPointer(scene::phase::target{}, scene::pointer::down{20, 16, 1}, down);
  button.onPointer(scene::phase::target{}, scene::pointer::up{180, 16, 1}, up);
  EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
  down = {};
  down.fDeferClick = true;
  up = {};
  button.onPointer(scene::phase::target{}, scene::pointer::down{20, 16, 1}, down);
  scene::PointerReply cancelled;
  button.onPointer(scene::phase::target{}, scene::pointer::cancel{}, cancelled);
  button.onPointer(scene::phase::target{}, scene::pointer::up{20, 16, 1}, up);
  EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
  scene::PointerReply right;
  button.onPointer(scene::phase::target{}, scene::pointer::down{20, 16, 3}, right);
  EXPECT_FALSE(right.fHandled);
  EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
}

TEST(Compose, ClickWrapperPreservesAContentHandlersTypedAnswer) {
  struct Content : scene::Node {
    using scene::Node::onPointer;
    std::optional<int> onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply& reply) {
      reply.handle();
      return 7;
    }
  };
  auto button = onClick(42, Content{});
  scene::hostWork().pressedNow = nullptr;
  scene::PointerReply reply;
  EXPECT_EQ(button.onPointer(scene::phase::target{}, scene::pointer::down{20, 16, 1}, reply), 7);
  EXPECT_TRUE(reply.fHandled);
  EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
}

TEST(Compose, ClickWrapperLeavesInputOwningGesturesWithTheContent) {
  struct Content : scene::Node {
    using scene::Node::onPointer;
    int downs = 0;
    bool acceptsInput() const { return true; }
    void onPointer(scene::phase::target, const scene::pointer::down&, scene::PointerReply&) { ++downs; }
  };
  auto button = onClick(42, Content{});
  scene::hostWork().pressedNow = nullptr;
  scene::PointerReply reply;
  button.onPointer(scene::phase::target{}, scene::pointer::down{20, 16, 1}, reply);
  EXPECT_EQ(button.downs, 1);
  EXPECT_FALSE(reply.fHandled);
  EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
}

TEST(Compose, PassiveContentDoesNotAlsoClickTheButtonsParent) {
  auto child = onClick(42, nodes::Box<>(0u));
  child.apply({.width = 120.0f, .height = 32.0f});
  scene::Scene tree(onClick(7, column(vbox(0.0f, {.fill = true}), std::move(child))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0, 0, 200, 100));
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20, 16, 1});
  (void)tree.dispatchPointer(scene::pointer::up{20, 16, 1});
  struct Heard { std::vector<int> values; void take(int one) { values.push_back(one); } } heard;
  model::Model<ClickState, bind::NoReactions> state{ClickState{}};
  for (const auto& path : std::exchange(scene::hostWork().pressed, {}))
    EXPECT_TRUE(bind::press(tree.root(), state, path, &heard));
  EXPECT_EQ(heard.values, (std::vector<int>{42}));
}

}  // namespace

namespace {
struct OuterFlick {};
struct InnerFlick {};
struct Flicks {
  int outer = 0, inner = 0;
};
inline auto flicking() {
  return state<Flicks>(handlers(handle<OuterFlick>([] { return model::over<Flicks>([](Flicks now) { ++now.outer; return now; }); }),
                                handle<InnerFlick>([] { return model::over<Flicks>([](Flicks now) { ++now.inner; return now; }); })),
                       flicked(60.0f, [](bool) { return std::optional<OuterFlick>(OuterFlick{}); },
                               column(vbox(0.0f, {.fill = true}),
                                      flicked_holding(60.0f, [](bool) { return std::optional<InnerFlick>(InnerFlick{}); },
                                                      styled({.fillX = true, .height = 40.0f}, nodes::Text("pins", 13.0f, 0xff000000u))),
                                      styled({.fillX = true, .height = 200.0f}, nodes::Text("chat", 13.0f, 0xff000000u)))));
}
}  // namespace

namespace {
template <class N> void showBoxes(N& node, int level) {
  if constexpr (requires { node.has_value(); }) {
    if (node) showBoxes(*node, level);
  } else if constexpr (std::ranges::range<N>) {
    for (auto& one : node) showBoxes(one, level);
  } else {
    const auto& b = scene::stateOf(node).fBounds;
    std::println("box: {:{}}{}..{} x {}..{} input {}", "", level * 2, b.fLeft, b.fRight, b.fTop, b.fBottom, node.acceptsInput());
    if (level < 20) node.forEachChild([&](auto& child) { showBoxes(child, level + 1); });
  }
}
}  // namespace
// A bar that flicks inside a screen that flicks (mux's pinned bar, the
// screen's step back): the drag across the bar is the bar's alone; one
// across the rest, the screen's.
TEST(Compose, AHoldingFlickInsideAFlickTakesItsOwnDrag) {
  using Tree = decltype(flicking());
  bind::ModelOf<Tree> m;
  scene::Scene tree(flicking());
  bind::Binding<bind::ModelOf<Tree>> binding;
  binding.refresh(tree.root(), m);
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  const auto drag = [&](float y) {
    scene::hostWork().answers.clear();
    (void)tree.dispatchPointer(scene::pointer::down{20, y, 1});
    (void)tree.dispatchPointer(scene::pointer::move{60, y + 1});
    (void)tree.dispatchPointer(scene::pointer::move{220, y + 2});
    (void)tree.dispatchPointer(scene::pointer::up{220, y + 2, 1});
    for (const auto &one : std::exchange(scene::hostWork().answers, {}))
      (void)bind::answer(tree.root(), m, one);
    binding.refresh(tree.root(), m);
  };
  drag(20.0f);
  EXPECT_EQ(m.look<Flicks>()->inner, 1);
  EXPECT_EQ(m.look<Flicks>()->outer, 0);
  drag(150.0f);
  EXPECT_EQ(m.look<Flicks>()->inner, 1);
  EXPECT_EQ(m.look<Flicks>()->outer, 1);
}
