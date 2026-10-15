import std;
import skia;
import skiff.scene;
import skiff.nodes.scroll;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace c = skiff::compose;
namespace scene = skiff::scene;
auto scroll_view() {
  auto view = skiff::nodes::ScrollContainer(c::column(c::vbox(0.0f, {.fillX = true, .height = 1000.0f})));
  view.apply({.fillX = true, .height = 100.0f});
  view.setHoldsInView(false);
  return view;
}
const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 200.0f, 100.0f);
}

TEST(Views, EachListRestoresItsOffsetAfterShorterContentsWereShown) {
  auto node = c::remembered_scroll<std::string>(scroll_view());
  node.read("home");
  scene::layout(node, viewport);
  node.setCurrent(500.0f);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 500.0f);
  node.read("space");
  std::get<0>(node.fChildren).apply({.height = 40.0f});
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
  // Read the target key before its new, taller rows have been laid out.
  node.read("home");
  std::get<0>(node.fChildren).apply({.height = 1000.0f});
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 500.0f);
  node.read("home");
  EXPECT_FLOAT_EQ(node.current(), 500.0f);
}

TEST(Views, NewSearchResultsRestartButUnchangedResultsKeepTheirOffset) {
  auto node = c::restarted_scroll<int>(scroll_view());
  node.read(1);
  scene::layout(node, viewport);
  node.setCurrent(300.0f);
  scene::layout(node, viewport);
  node.read(1);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 300.0f);
  node.read(2);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
}

TEST(Views, WidthTracksTheBoundRequestAndWindowLimits) {
  auto node = c::clamped_width({}, c::column(c::vbox()));
  const auto wide = skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 600.0f);
  node.read({.wanted = 900.0f});
  node.measure(wide);
  EXPECT_FLOAT_EQ(node.fState.fWidth, 600.0f);
  node.read({.wanted = 100.0f});
  node.measure(wide);
  EXPECT_FLOAT_EQ(node.fState.fWidth, 240.0f);
  node.read({.wanted = 350.0f});
  node.measure(wide);
  EXPECT_FLOAT_EQ(node.fState.fWidth, 350.0f);
  node.read({.whole = true});
  node.measure(viewport);
  EXPECT_FLOAT_EQ(node.fState.fWidth, 200.0f);
}

TEST(Views, CaptureObserverLeavesTheTargetsNormalClickAvailable) {
  int observed = 0;
  auto& work = scene::hostWork();
  work.answers.clear();
  work.pressed.clear();
  auto node = c::onCapture<int>([&](const auto&, const scene::pointer::down&, scene::PointerReply&) {
    ++observed;
    return std::optional<int>(7);
  }, c::column(c::vbox(0.0f, {.fill = true}),
      c::onClick(9, c::column(c::vbox(0.0f, {.fillX = true, .height = 50.0f})), "Choose item")));
  scene::Scene tree(std::move(node));
  tree.layoutIfNeeded(viewport);
  tree.dispatchPointer(scene::pointer::down{20.0f, 20.0f, 1});
  tree.dispatchPointer(scene::pointer::up{20.0f, 20.0f, 1});
  EXPECT_EQ(observed, 1);
  EXPECT_FALSE(work.pressed.empty());
  EXPECT_EQ(std::get<0>(tree.root().fParts).onPress(), 9);
  work.answers.clear();
  work.pressed.clear();
  tree.dispatchPointer(scene::pointer::down{180.0f, 80.0f, 1});
  tree.dispatchPointer(scene::pointer::up{180.0f, 80.0f, 1});
  EXPECT_EQ(observed, 2);
  EXPECT_TRUE(work.pressed.empty());
  work.answers.clear();
}
