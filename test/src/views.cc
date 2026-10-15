import std;
import skia;
import skiff.scene;
import skiff.nodes.scroll;
import skiff.nodes.flow;
import skiff.compose;
import skiff.model;
import skiff.bind;
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
struct arrangement_gap {
  float value = 2.0f;
  friend bool operator==(const arrangement_gap&, const arrangement_gap&) = default;
};
struct arrangement_model { skiff::model::Tracked<arrangement_gap> gap; };
struct projection_amount { int value = 2; };
struct projection_hint { int value = 3; };
struct projection_source { skiff::model::Tracked<projection_amount> amount; };
struct projection_facts { skiff::model::Tracked<projection_hint> hint; };
struct projection_probe : scene::Node {
  int value = 0;
  void read(int now) { value = now; }
};
struct gap_policy {
  using State = arrangement_gap;
  static void prepare(auto& node, const State& state) { node.setGap(state.value); }
  static void arrange(auto&, const State&) {}
  static bool settling(const auto&, const State&) { return false; }
  static void settle(auto&, const State&) {}
};
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

TEST(Views, AFieldsFrameTracksFocusWithoutChangingItsGeometry) {
  auto field = c::onClick(1, c::column(c::vbox(0.0f, {.fillX = true, .height = 28.0f,
      .margin = {2.0f, 14.0f, 0.0f, 14.0f}})), "Search");
  auto node = c::focus_frame(c::vbox(0.0f, {.fillX = true, .height = 36.0f}), std::move(field));
  scene::Scene tree(std::move(node));
  tree.layoutIfNeeded(viewport);
  const auto frame_box = tree.root().bounds();
  const auto field_box = std::get<0>(tree.root().fParts).bounds();
  tree.focus(std::get<0>(tree.root().fParts).id());
  EXPECT_TRUE(tree.root().wantsTick());
  tree.update(1000.0);
  EXPECT_TRUE(tree.root().selected());
  EXPECT_FALSE(tree.root().wantsTick());
  EXPECT_EQ(tree.root().bounds(), frame_box);
  EXPECT_EQ(std::get<0>(tree.root().fParts).bounds(), field_box);
  tree.clearFocus();
  tree.update(1100.0);
  EXPECT_FALSE(tree.root().selected());
}

TEST(Views, ArrangementParametersReadFromTheModelChangeTheRealLayout) {
  using model_type = skiff::model::Model<arrangement_model, skiff::bind::NoReactions>;
  model_type model(arrangement_model{});
  skiff::bind::Binding<model_type> binding;
  auto node = c::bound<arrangement_gap>(c::arranged<gap_policy>({}, c::column(
      c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::column(c::vbox(0.0f, {.width = 40.0f, .height = 30.0f})),
      c::column(c::vbox(0.0f, {.width = 40.0f, .height = 30.0f})))));
  binding.refresh(node, model);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(std::get<1>(node.fParts).bounds().fTop, 32.0f);
  model.apply(skiff::model::over<arrangement_gap>(skiff::model::setTo(arrangement_gap{8.0f})));
  binding.refresh(node, model);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(std::get<1>(node.fParts).bounds().fTop, 38.0f);
  EXPECT_TRUE(node.fState.fLayoutValid);
  node.read(arrangement_gap{8.0f});
  EXPECT_TRUE(node.fState.fLayoutValid);
}

TEST(Views, ModelAndViewFactsCanArriveInEitherOrderAndBothRefreshTheProjection) {
  using source_type = skiff::model::Model<projection_source, skiff::bind::NoReactions>;
  using facts_type = skiff::model::Model<projection_facts, skiff::bind::NoReactions>;
  source_type source(projection_source{});
  facts_type facts(projection_facts{});
  skiff::bind::Binding<source_type> source_binding;
  skiff::bind::Binding<facts_type> facts_binding;
  int computed = 0;
  const auto make = [&] {
    return c::bound<projection_hint>(c::with_model<source_type, projection_hint>(
        [&](const source_type& now, const projection_hint& hint) {
          ++computed;
          return now.look<projection_amount>()->value + hint.value;
        }, projection_probe{}));
  };
  auto source_first = make();
  source_binding.refresh(source_first, source);
  EXPECT_EQ(source_first.value, 0);
  facts_binding.refresh(source_first, facts);
  EXPECT_EQ(source_first.value, 5);
  const int before = computed;
  source_binding.refresh(source_first, source);
  EXPECT_EQ(computed, before);
  source.apply(skiff::model::over<projection_amount>(skiff::model::setTo(projection_amount{4})));
  source_binding.refresh(source_first, source);
  EXPECT_EQ(source_first.value, 7);
  facts.apply(skiff::model::over<projection_hint>(skiff::model::setTo(projection_hint{7})));
  facts_binding.refresh(source_first, facts);
  EXPECT_EQ(source_first.value, 11);

  auto facts_first = make();
  skiff::bind::refresh(facts_first, facts);
  EXPECT_EQ(facts_first.value, 0);
  skiff::bind::refresh(facts_first, source);
  EXPECT_EQ(facts_first.value, 11);
}

namespace {
using virtual_item = std::pair<int, int>;
inline constexpr auto virtual_make = [](const virtual_item& item) {
  return c::column(c::vbox(0.0f, {.fillX = true, .height = static_cast<float>(item.second)}));
};
auto virtual_view() {
  using rows = skiff::nodes::MemoRows<int, int,
      std::invoke_result_t<decltype(virtual_make), const virtual_item&>,
      std::remove_cv_t<decltype(virtual_make)>>;
  auto contents = rows(virtual_make);
  contents.apply({.fillX = true, .autoSize = scene::axes::kY});
  auto scroll = skiff::nodes::ScrollContainer(std::move(contents));
  scroll.apply({.fillX = true, .height = 100.0f});
  return c::virtual_rows<virtual_item>([](const virtual_item& item) { return item.second; }, std::move(scroll));
}
std::vector<virtual_item> virtual_items(int count) {
  std::vector<virtual_item> out;
  for (int i = 0; i < count; ++i) out.emplace_back(i, i % 2 ? 82 : 62);
  return out;
}
}

TEST(Views, VirtualRowsKeepFullExtentAndOnlyBuildNearbyVariableHeightRows) {
  auto node = virtual_view();
  node.read(virtual_items(1000));
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.extent(), 72000.0f - 100.0f);
  EXPECT_EQ(node.fFrom, 0u);
  EXPECT_LT(std::get<0>(node.fChildren).fRows.size(), 100u);
  node.setCurrent(50000.0f);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 50000.0f);
  EXPECT_LE(node.fTops[node.fFrom], node.current());
  EXPECT_GE(node.fTops[node.fTo], node.current() + viewport.height());
  EXPECT_LT(std::get<0>(node.fChildren).fRows.size(), 100u);
  EXPECT_FLOAT_EQ(node.extent(), 71900.0f);
  const auto& row = std::get<0>(node.fChildren).fRows.front();
  EXPECT_FLOAT_EQ(row.bounds().fTop, node.fTops[node.fFrom]);
}

TEST(Views, VirtualRowsRestoreADistantListAfterShortContentsAndClampShrinks) {
  auto node = c::remembered_scroll<std::string>(virtual_view());
  node.read("long");
  // The outer read selects a list; its rows arrive through their own part.
  using base = decltype(virtual_view());
  static_cast<base&>(node).read(virtual_items(1000));
  scene::layout(node, viewport);
  node.setCurrent(50000.0f);
  scene::layout(node, viewport);
  node.read("short");
  static_cast<base&>(node).read(virtual_items(1));
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
  node.read("long");
  static_cast<base&>(node).read(virtual_items(1000));
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 50000.0f);
  EXPECT_LE(node.fTops[node.fFrom], 50000.0f);
  EXPECT_GE(node.fTops[node.fTo], 50100.0f);
  static_cast<base&>(node).read(virtual_items(3));
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 106.0f);
  EXPECT_EQ(node.fFrom, 0u);
  EXPECT_EQ(node.fTo, 3u);
  static_cast<base&>(node).read({});
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.extent(), 0.0f);
  EXPECT_EQ(node.fFrom, 0u);
  EXPECT_EQ(node.fTo, 0u);
}

TEST(Views, VirtualRowsTrackGlidingAndDirectScrollbarOffsets) {
  auto node = virtual_view();
  node.read(virtual_items(1000));
  scene::layout(node, viewport);
  node.scrollTo(50000.0f);
  for (int i = 1; i < 200; ++i) {
    node.update(i * 16.0);
    scene::layout(node, viewport);
    EXPECT_LE(node.fTops[node.fFrom], node.current());
    EXPECT_GE(node.fTops[node.fTo], node.current() + viewport.height());
  }
  EXPECT_NEAR(node.current(), 50000.0f, 1.0f);
  node.scrollToStart();
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
  EXPECT_EQ(node.fFrom, 0u);
}
