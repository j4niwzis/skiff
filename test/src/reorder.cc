import std;
import splice;
import skia;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.compose;
import skiff.model;
import skiff.bind;
import gtest;

#include "gtest/gtest-macros.h"
#include "made.h"

namespace {
namespace c = skiff::compose;
namespace scene = skiff::scene;
using bar = c::ReorderBar;
struct bar_visibility { bool side = true; };
struct visibility_root { skiff::model::Tracked<bar_visibility> visible; };
using visibility_model = skiff::model::Model<visibility_root, skiff::bind::NoReactions>;
template <class T> std::optional<T> event_as(const auto& event) {
  if (!event) return std::nullopt;
  return spl::visit(spl::overloaded{
      [](const T& value) { return std::optional<T>(value); },
      [](const auto&) { return std::optional<T>{}; }}, *event);
}
inline auto make_icon(int, int, bool) {
  return c::column(c::vbox(0.0f, {.width = 40.0f, .height = 40.0f}));
}
inline constexpr auto make_row = [](const std::pair<int, int>& one) { return make_icon(one.first, one.second, false); };
using rows = skiff::nodes::MemoRows<int, int, decltype(make_icon(0, 0, false)), std::remove_cv_t<decltype(make_row)>>;

// The surface the bars are over: a bar across, scrolled sideways, and one
// down, shown while the model says so.
struct SideShown {
  bool operator()(const bar_visibility& now) const { return now.side; }
};
auto surface_of(rows across, rows down) {
  return c::column(c::vbox(), c::side_scroll(std::move(across), true),
      c::shown_for<bar_visibility>(SideShown{}, skiff::nodes::ScrollContainer<rows>(std::move(down))));
}
using Surface = decltype(surface_of(rows(make_row), rows(make_row)));
// Its two bars, as they were made -- found by the walks under the Held each
// is erased into where combinators are.
using Top = c::SideScroll<rows>;
using Side = projected_t<bar_visibility, SideShown, c::ShownBy<c::part_t<skiff::nodes::ScrollContainer<rows>>>>;
// The lanes as reorder_bars finds them now: given the surface found, each
// lane where it is shown (as main's test has it).
struct Lanes {
  auto operator()(auto found) const {
    auto& top = made_as<Top>(*found.surface);
    auto& side = made_as<Side>(*found.surface);
    return std::tuple{std::optional{c::ReorderLane{top, top.line(), top.bounds(), 4.0f}},
                      std::optional{c::ReorderLane{side, std::get<0>(side.fChildren), side.bounds(), 8.0f}}};
  }
};
using Bars = c::ReorderBars<int, int, decltype(&make_icon), Surface, Lanes>;
Bars& bars(auto& node) { return made_as<Bars>(node); }

auto fixture() {
  rows across(make_row), down(make_row);
  across.read(std::vector<std::pair<int, int>>{{1, 1}, {2, 2}, {3, 3}});
  down.read(std::vector<std::pair<int, int>>{{4, 4}, {5, 5}});
  auto surface = surface_of(std::move(across), std::move(down));
  auto& top = made_as<Top>(surface);
  auto& side = made_as<Side>(surface);
  top.fState.fBounds = skia::SkRect::MakeXYWH(0.0f, 0.0f, 100.0f, 40.0f);
  top.line().fState.fBounds = skia::SkRect::MakeXYWH(0.0f, 0.0f, 128.0f, 40.0f);
  std::ranges::for_each(std::views::iota(0, 3), [&](int i) {
    c::state_of(top.line().fRows[i].fRow).fBounds = skia::SkRect::MakeXYWH(i * 44.0f, 0.0f, 40.0f, 40.0f);
  });
  side.fState.fBounds = skia::SkRect::MakeXYWH(200.0f, 100.0f, 40.0f, 100.0f);
  auto& vertical = std::get<0>(side.fChildren);
  std::ranges::for_each(std::views::iota(0, 2), [&](int i) {
    c::state_of(vertical.fRows[i].fRow).fBounds = skia::SkRect::MakeXYWH(200.0f, 100.0f + i * 48.0f, 40.0f, 40.0f);
  });
  return c::reorder_bars<int, int>(std::move(surface), make_icon, Lanes{}, 0xff123456u);
}
}

TEST(ReorderBars, HeldCrossBarDropAnswersKeysAndRestoresTheGhost) {
  auto node = fixture();
  scene::PointerReply down;
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, down);
  ASSERT_TRUE(bars(node).fDrag);
  EXPECT_TRUE(down.fDeferClick);
  bars(node).fDrag->pressed -= std::chrono::milliseconds(300);
  scene::PointerReply move;
  const auto dragging = event_as<c::ItemsDragging>(
      bars(node).onPointer(scene::phase::capture{}, scene::pointer::move{220.0f, 180.0f}, move));
  ASSERT_TRUE(dragging);
  EXPECT_TRUE(dragging->on);
  EXPECT_TRUE(move.fCapturePointer);
  EXPECT_TRUE(move.fSuppressHover);
  ASSERT_TRUE(bars(node).ghost());
  EXPECT_FALSE(made_as<Top>(node).line().fRows[0].fRow.visible());
  scene::PointerReply up;
  const auto placed = event_as<c::ItemsPlaced<int>>(
      bars(node).onPointer(scene::phase::capture{}, scene::pointer::up{220.0f, 180.0f, 1}, up));
  ASSERT_TRUE(placed);
  EXPECT_EQ(placed->from, bar::horizontal);
  EXPECT_EQ(placed->to, bar::vertical);
  EXPECT_EQ(placed->item, 1);
  EXPECT_EQ(placed->order, (std::vector{4, 5, 1}));
  EXPECT_TRUE(up.fReleasePointer);
  bars(node).update(1000.0);
  bars(node).update(2000.0);
  EXPECT_FALSE(bars(node).ghost());
  EXPECT_TRUE(made_as<Top>(node).line().fRows[0].fRow.visible());
}

TEST(ReorderBars, QuickMovesScrollAndNeverPlaceItems) {
  auto node = fixture();
  scene::PointerReply down;
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, down);
  scene::PointerReply move;
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::move{10.0f, 20.0f}, move);
  ASSERT_TRUE(bars(node).fDrag);
  EXPECT_TRUE(bars(node).fDrag->scrolling);
  EXPECT_FLOAT_EQ(made_as<Top>(node).offset(), 10.0f);
  EXPECT_FALSE(bars(node).ghost());
  scene::PointerReply up;
  EXPECT_FALSE(bars(node).onPointer(scene::phase::capture{}, scene::pointer::up{10.0f, 20.0f, 1}, up));
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::down{220.0f, 120.0f, 1}, down);
  move = {};
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::move{220.0f, 140.0f}, move);
  EXPECT_FALSE(bars(node).fDrag);
  EXPECT_FALSE(move.fHandled);
  EXPECT_FALSE(bars(node).ghost());
}

TEST(ReorderBars, CancelRestoresHiddenDropTargetAndSource) {
  auto node = fixture();
  visibility_model model(visibility_root{});
  skiff::bind::Binding<visibility_model> binding;
  model.apply(skiff::model::over<bar_visibility>(skiff::model::setTo(bar_visibility{false})));
  binding.refresh(node, model);
  EXPECT_FALSE(made_as<Side>(node).visible());
  scene::PointerReply down;
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, down);
  bars(node).fDrag->pressed -= std::chrono::milliseconds(300);
  scene::PointerReply move;
  const auto dragging = event_as<c::ItemsDragging>(
      bars(node).onPointer(scene::phase::capture{}, scene::pointer::move{60.0f, 80.0f}, move));
  ASSERT_TRUE(dragging);
  EXPECT_TRUE(dragging->on);
  EXPECT_FALSE(made_as<Side>(node).visible());
  model.apply(skiff::model::over<bar_visibility>(skiff::model::setTo(bar_visibility{dragging->on})));
  binding.refresh(node, model);
  binding.refresh(node, model);
  EXPECT_TRUE(made_as<Side>(node).visible());
  scene::PointerReply cancel;
  const auto stopped = event_as<c::ItemsDragging>(
      bars(node).onPointer(scene::phase::capture{}, scene::pointer::cancel{}, cancel));
  ASSERT_TRUE(stopped);
  EXPECT_FALSE(stopped->on);
  EXPECT_TRUE(made_as<Side>(node).visible());
  model.apply(skiff::model::over<bar_visibility>(skiff::model::setTo(bar_visibility{stopped->on})));
  binding.refresh(node, model);
  EXPECT_FALSE(bars(node).fDrag);
  EXPECT_FALSE(bars(node).ghost());
  EXPECT_FALSE(made_as<Side>(node).visible());
  EXPECT_TRUE(made_as<Top>(node).line().fRows[0].fRow.visible());
}

TEST(ReorderBars, AnItemListedInBothBarsStaysVisibleInTheOtherBarWhileDragged) {
  auto node = fixture();
  auto& side_rows = std::get<0>(made_as<Side>(node).fChildren);
  side_rows.fRows[0].fKey = 1;
  scene::PointerReply reply;
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::down{20.0f, 20.0f, 1}, reply);
  bars(node).fDrag->pressed -= std::chrono::milliseconds(300);
  bars(node).onPointer(scene::phase::capture{}, scene::pointer::move{60.0f, 80.0f}, reply);
  EXPECT_FALSE(made_as<Top>(node).line().fRows[0].fRow.visible());
  EXPECT_TRUE(side_rows.fRows[0].fRow.visible());
}

TEST(ReorderBars, SceneRoutesCaptureMoveAndTargetReleaseToTheSameGesture) {
  auto node = fixture();
  node.fState.fBounds = skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f);
  bars(node).fState.fBounds = node.bounds();
  bars(node).surface().fState.fBounds = node.bounds();
  scene::Scene tree(std::move(node));
  tree.dispatchPointer(scene::pointer::down{20.0f, 20.0f, 1});
  ASSERT_TRUE(bars(tree.root()).fDrag);
  bars(tree.root()).fDrag->pressed -= std::chrono::milliseconds(300);
  EXPECT_TRUE(tree.dispatchPointer(scene::pointer::move{220.0f, 180.0f}));
  EXPECT_EQ(tree.capturedId(), bars(tree.root()).id());
  EXPECT_TRUE(tree.dispatchPointer(scene::pointer::up{220.0f, 180.0f, 1}));
  EXPECT_EQ(tree.capturedId(), 0u);
  EXPECT_FALSE(bars(tree.root()).fDrag);
  EXPECT_TRUE(bars(tree.root()).fLanding);
}

TEST(ReorderBars, HorizontalBarCanUseTheVerticalWheelWithoutMovingItems) {
  auto node = fixture();
  auto& top = made_as<Top>(node);
  scene::PointerReply wheel;
  top.onPointer(scene::phase::target{}, scene::pointer::scroll{20.0f, 20.0f, 0.0f, -1.0f}, wheel);
  EXPECT_TRUE(wheel.fHandled);
  top.update(1000.0);
  top.update(2000.0);
  EXPECT_GT(top.offset(), 0.0f);
  EXPECT_FALSE(bars(node).fDrag);
  EXPECT_FALSE(bars(node).ghost());
}
