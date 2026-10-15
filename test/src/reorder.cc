import std;
import skia;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace c = skiff::compose;
namespace scene = skiff::scene;
using bar = c::ReorderBar;
inline auto make_icon(int, int, bool) {
  return c::column(c::vbox(0.0f, {.width = 40.0f, .height = 40.0f}));
}
inline constexpr auto make_row = [](const std::pair<int, int>& one) { return make_icon(one.first, one.second, false); };
using rows = skiff::nodes::MemoRows<int, int, decltype(make_icon(0, 0, false)), std::remove_cv_t<decltype(make_row)>>;

auto fixture() {
  rows across(make_row), down(make_row);
  across.read(std::vector<std::pair<int, int>>{{1, 1}, {2, 2}, {3, 3}});
  down.read(std::vector<std::pair<int, int>>{{4, 4}, {5, 5}});
  auto surface = c::column(c::vbox(), c::side_scroll(std::move(across), true), skiff::nodes::ScrollContainer<rows>(std::move(down)));
  auto& top = std::get<0>(surface.fParts);
  auto& side = std::get<1>(surface.fParts);
  top.fState.fBounds = skia::SkRect::MakeXYWH(0.0f, 0.0f, 100.0f, 40.0f);
  top.line().fState.fBounds = skia::SkRect::MakeXYWH(0.0f, 0.0f, 128.0f, 40.0f);
  std::ranges::for_each(std::views::iota(0, 3), [&](int i) {
    top.line().fRows[i].fRow.fState.fBounds = skia::SkRect::MakeXYWH(i * 44.0f, 0.0f, 40.0f, 40.0f);
  });
  side.fState.fBounds = skia::SkRect::MakeXYWH(200.0f, 100.0f, 40.0f, 100.0f);
  auto& vertical = std::get<0>(side.fChildren);
  std::ranges::for_each(std::views::iota(0, 2), [&](int i) {
    vertical.fRows[i].fRow.fState.fBounds = skia::SkRect::MakeXYWH(200.0f, 100.0f + i * 48.0f, 40.0f, 40.0f);
  });
  return c::reorder_bars<int, int>(std::move(surface), make_icon, [](auto& surface) {
    auto& top = std::get<0>(surface.fParts);
    auto& side = std::get<1>(surface.fParts);
    return std::tuple{c::ReorderLane{top, top.line(), top.bounds(), 4.0f},
                      c::ReorderLane{side, std::get<0>(side.fChildren), side.bounds(), 8.0f}};
  }, 0xff123456u);
}
}

TEST(ReorderBars, HeldCrossBarDropAnswersKeysAndRestoresTheGhost) {
  auto node = fixture();
  scene::PointerReply down;
  node.onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, down);
  ASSERT_TRUE(node.fDrag);
  EXPECT_TRUE(down.fDeferClick);
  node.fDrag->pressed -= std::chrono::milliseconds(300);
  scene::PointerReply move;
  node.onPointer(scene::phase::capture{}, scene::pointer::move{220.0f, 180.0f}, move);
  EXPECT_TRUE(move.fCapturePointer);
  EXPECT_TRUE(move.fSuppressHover);
  ASSERT_TRUE(node.ghost());
  EXPECT_FALSE(std::get<0>(node.surface().fParts).line().fRows[0].fRow.visible());
  scene::PointerReply up;
  const auto placed = node.onPointer(scene::phase::capture{}, scene::pointer::up{220.0f, 180.0f, 1}, up);
  ASSERT_TRUE(placed);
  EXPECT_EQ(placed->from, bar::horizontal);
  EXPECT_EQ(placed->to, bar::vertical);
  EXPECT_EQ(placed->item, 1);
  EXPECT_EQ(placed->order, (std::vector{4, 5, 1}));
  EXPECT_TRUE(up.fReleasePointer);
  node.update(1000.0);
  node.update(2000.0);
  EXPECT_FALSE(node.ghost());
  EXPECT_TRUE(std::get<0>(node.surface().fParts).line().fRows[0].fRow.visible());
}

TEST(ReorderBars, QuickMovesScrollAndNeverPlaceItems) {
  auto node = fixture();
  scene::PointerReply down;
  node.onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, down);
  scene::PointerReply move;
  node.onPointer(scene::phase::capture{}, scene::pointer::move{10.0f, 20.0f}, move);
  ASSERT_TRUE(node.fDrag);
  EXPECT_TRUE(node.fDrag->scrolling);
  EXPECT_FLOAT_EQ(std::get<0>(node.surface().fParts).offset(), 10.0f);
  EXPECT_FALSE(node.ghost());
  scene::PointerReply up;
  EXPECT_FALSE(node.onPointer(scene::phase::capture{}, scene::pointer::up{10.0f, 20.0f, 1}, up));
  node.onPointer(scene::phase::capture{}, scene::pointer::down{220.0f, 120.0f, 1}, down);
  move = {};
  node.onPointer(scene::phase::capture{}, scene::pointer::move{220.0f, 140.0f}, move);
  EXPECT_FALSE(node.fDrag);
  EXPECT_FALSE(move.fHandled);
  EXPECT_FALSE(node.ghost());
}

TEST(ReorderBars, CancelRestoresHiddenDropTargetAndSource) {
  auto node = fixture();
  std::get<1>(node.surface().fParts).setVisible(false);
  scene::PointerReply down;
  node.onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, down);
  node.fDrag->pressed -= std::chrono::milliseconds(300);
  scene::PointerReply move;
  node.onPointer(scene::phase::capture{}, scene::pointer::move{60.0f, 80.0f}, move);
  EXPECT_TRUE(std::get<1>(node.surface().fParts).visible());
  scene::PointerReply cancel;
  node.onPointer(scene::phase::capture{}, scene::pointer::cancel{}, cancel);
  EXPECT_FALSE(node.fDrag);
  EXPECT_FALSE(node.ghost());
  EXPECT_FALSE(std::get<1>(node.surface().fParts).visible());
  EXPECT_TRUE(std::get<0>(node.surface().fParts).line().fRows[0].fRow.visible());
}

TEST(ReorderBars, AnItemListedInBothBarsStaysVisibleInTheOtherBarWhileDragged) {
  auto node = fixture();
  auto& side_rows = std::get<0>(std::get<1>(node.surface().fParts).fChildren);
  side_rows.fRows[0].fKey = 1;
  scene::PointerReply reply;
  node.onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, reply);
  node.fDrag->pressed -= std::chrono::milliseconds(300);
  node.onPointer(scene::phase::capture{}, scene::pointer::move{60.0f, 80.0f}, reply);
  EXPECT_FALSE(std::get<0>(node.surface().fParts).line().fRows[0].fRow.visible());
  EXPECT_TRUE(side_rows.fRows[0].fRow.visible());
}

TEST(ReorderBars, SceneRoutesCaptureMoveAndTargetReleaseToTheSameGesture) {
  auto node = fixture();
  node.fState.fBounds = skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f);
  node.surface().fState.fBounds = node.bounds();
  scene::Scene tree(std::move(node));
  tree.dispatchPointer(scene::pointer::down{20.0f, 20.0f, 1});
  ASSERT_TRUE(tree.root().fDrag);
  tree.root().fDrag->pressed -= std::chrono::milliseconds(300);
  EXPECT_TRUE(tree.dispatchPointer(scene::pointer::move{220.0f, 180.0f}));
  EXPECT_EQ(tree.capturedId(), tree.root().id());
  EXPECT_TRUE(tree.dispatchPointer(scene::pointer::up{220.0f, 180.0f, 1}));
  EXPECT_EQ(tree.capturedId(), 0u);
  EXPECT_FALSE(tree.root().fDrag);
  EXPECT_TRUE(tree.root().fLanding);
}

TEST(ReorderBars, HorizontalBarCanUseTheVerticalWheelWithoutMovingItems) {
  auto node = fixture();
  auto& top = std::get<0>(node.surface().fParts);
  scene::PointerReply wheel;
  top.onPointer(scene::phase::target{}, scene::pointer::scroll{20.0f, 20.0f, 0.0f, -1.0f}, wheel);
  EXPECT_TRUE(wheel.fHandled);
  top.update(1000.0);
  top.update(2000.0);
  EXPECT_GT(top.offset(), 0.0f);
  EXPECT_FALSE(node.fDrag);
  EXPECT_FALSE(node.ghost());
}
