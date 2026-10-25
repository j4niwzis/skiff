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
#include "made.h"

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
  scene::Scene tree(c::remembered_scroll<std::string>(scroll_view()));
  auto& node = tree.root();
  node.read("home");
  tree.layoutIfNeeded(viewport);
  node.setCurrent(500.0f);
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 500.0f);
  node.read("space");
  std::get<0>(node.view().fChildren).apply({.height = 40.0f});
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
  // Read the target key before its new, taller rows have been laid out.
  node.read("home");
  std::get<0>(node.view().fChildren).apply({.height = 1000.0f});
  tree.layoutIfNeeded(viewport);
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
  using Item = c::OnClick<int, c::part_t<decltype(c::column(c::vbox(0.0f, {.fillX = true, .height = 50.0f})))>>;
  EXPECT_EQ(made_as<Item>(tree.root()).onPress(), 9);
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
  using Frame = c::FocusFrame<decltype(field)>;
  auto node = c::focus_frame(c::vbox(0.0f, {.fillX = true, .height = 36.0f}), std::move(field));
  scene::Scene tree(std::move(node));
  tree.layoutIfNeeded(viewport);
  auto &frame = made_as<Frame>(tree.root());
  const auto frame_box = frame.bounds();
  const auto field_box = std::get<0>(frame.fParts).bounds();
  tree.focus(std::get<0>(frame.fParts).id());
  EXPECT_TRUE(frame.wantsTick());
  tree.update(1000.0);
  EXPECT_TRUE(frame.selected());
  EXPECT_FALSE(frame.wantsTick());
  EXPECT_EQ(frame.bounds(), frame_box);
  EXPECT_EQ(std::get<0>(frame.fParts).bounds(), field_box);
  tree.clearFocus();
  tree.update(1100.0);
  EXPECT_FALSE(frame.selected());
}

TEST(Views, ArrangementParametersReadFromTheModelChangeTheRealLayout) {
  using model_type = skiff::model::Model<arrangement_model, skiff::bind::NoReactions>;
  model_type model(arrangement_model{});
  skiff::bind::Binding<model_type> binding;
  const auto arrangement = [] {
    // Made as the box it is, in either build: the policy sets the gap of
    // the column it arranges, which a Held -- what column() makes where
    // combinators are erased -- does not lay out by.
    return c::arranged<gap_policy>({}, c::Box<c::Box<>, c::Box<>>(
        c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        c::Box<>(c::vbox(0.0f, {.width = 40.0f, .height = 30.0f})),
        c::Box<>(c::vbox(0.0f, {.width = 40.0f, .height = 30.0f}))));
  };
  using Bound = skiff::bind::Bound<arrangement_gap, c::part_t<decltype(arrangement())>>;
  auto node = c::bound<arrangement_gap>(arrangement());
  // The second of the two columns arranged, found as it is.
  const auto second = [&] { return all_made_as<c::Box<>>(node).at(1)->bounds(); };
  binding.refresh(node, model);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(second().fTop, 32.0f);
  model.apply(skiff::model::over<arrangement_gap>(skiff::model::setTo(arrangement_gap{8.0f})));
  binding.refresh(node, model);
  scene::layout(node, viewport);
  EXPECT_FLOAT_EQ(second().fTop, 38.0f);
  auto &bound = made_as<Bound>(node);
  EXPECT_TRUE(bound.fState.fLayoutValid);
  bound.read(arrangement_gap{8.0f});
  EXPECT_TRUE(bound.fState.fLayoutValid);
}

TEST(Views, ModelAndViewFactsCanArriveInEitherOrderAndBothRefreshTheProjection) {
  using source_type = skiff::model::Model<projection_source, skiff::bind::NoReactions>;
  using facts_type = skiff::model::Model<projection_facts, skiff::bind::NoReactions>;
  source_type source(projection_source{});
  facts_type facts(projection_facts{});
  skiff::bind::Binding<source_type> source_binding;
  skiff::bind::Binding<facts_type> facts_binding;
  int computed = 0;
  struct Summed {
    int *computed;
    int operator()(const source_type& now, const projection_hint& hint) const {
      ++*computed;
      return now.look<projection_amount>()->value + hint.value;
    }
  };
  using Probe = skiff::bind::Bound<projection_hint, c::part_t<c::WithModel<source_type, projection_hint, Summed, projection_probe>>>;
  const auto make = [&] { return c::bound<projection_hint>(c::with_model<source_type, projection_hint>(Summed{&computed}, projection_probe{})); };
  auto source_made = make();
  auto &source_first = made_as<Probe>(source_made);
  source_binding.refresh(source_made, source);
  EXPECT_EQ(source_first.value, 0);
  facts_binding.refresh(source_made, facts);
  EXPECT_EQ(source_first.value, 5);
  const int before = computed;
  source_binding.refresh(source_made, source);
  EXPECT_EQ(computed, before);
  source.apply(skiff::model::over<projection_amount>(skiff::model::setTo(projection_amount{4})));
  source_binding.refresh(source_made, source);
  EXPECT_EQ(source_first.value, 7);
  facts.apply(skiff::model::over<projection_hint>(skiff::model::setTo(projection_hint{7})));
  facts_binding.refresh(source_made, facts);
  EXPECT_EQ(source_first.value, 11);

  auto facts_made = make();
  auto &facts_first = made_as<Probe>(facts_made);
  skiff::bind::refresh(facts_made, facts);
  EXPECT_EQ(facts_first.value, 0);
  skiff::bind::refresh(facts_made, source);
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
  scene::Scene tree(c::remembered_scroll<std::string>(virtual_view()));
  auto& node = tree.root();
  node.read("long");
  // The outer read selects a list; its rows arrive through their own part.

  node.view().read(virtual_items(1000));
  tree.layoutIfNeeded(viewport);
  node.setCurrent(50000.0f);
  tree.layoutIfNeeded(viewport);
  node.read("short");
  node.view().read(virtual_items(1));
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
  node.read("long");
  node.view().read(virtual_items(1000));
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 50000.0f);
  EXPECT_LE(node.view().fTops[node.view().fFrom], 50000.0f);
  EXPECT_GE(node.view().fTops[node.view().fTo], 50100.0f);
  node.view().read(virtual_items(3));
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 106.0f);
  EXPECT_EQ(node.view().fFrom, 0u);
  EXPECT_EQ(node.view().fTo, 3u);
  node.view().read({});
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.extent(), 0.0f);
  EXPECT_EQ(node.view().fFrom, 0u);
  EXPECT_EQ(node.view().fTo, 0u);
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

TEST(Views, RememberedViewportKeepsItsIdentityAndContentsInDistinctBindingNodes) {
  using source_type = skiff::model::Model<projection_source, skiff::bind::NoReactions>;
  using facts_type = skiff::model::Model<projection_facts, skiff::bind::NoReactions>;
  source_type source(projection_source{});
  facts_type facts(projection_facts{});
  struct Items {
    std::vector<virtual_item> operator()(const projection_amount& amount) const { return virtual_items(amount.value * 100); }
  };
  // The view that scrolls named: what it is made over is a projection,
  // erased (a Held) where combinators are.
  // As the walks find it: under the projection that reads its items, which
  // derives from it -- a node of its own type where combinators are erased.
  using View = projected_t<projection_amount, Items, decltype(virtual_view())>;
  using Remembered = decltype(c::remembered_scroll<int, View>(c::projected<projection_amount>(Items{}, virtual_view())));
  struct HintValue {
    int operator()(const projection_hint& hint) const { return hint.value; }
  };
  auto expression = c::projected<projection_hint>(HintValue{},
      c::remembered_scroll<int, View>(c::projected<projection_amount>(Items{}, virtual_view())));
  scene::Scene tree(std::move(expression));
  auto& root = tree.root();
  // The remembered view as the walks find it: under the projection read
  // over it, which derives from it.
  auto& node = made_as<projected_t<projection_hint, HintValue, Remembered>>(root);
  skiff::bind::refresh(root, facts);
  skiff::bind::refresh(root, source);
  tree.layoutIfNeeded(viewport);
  EXPECT_EQ(node.view().fItems.size(), 200u);
  EXPECT_FLOAT_EQ(node.extent(), 14300.0f);
  node.setCurrent(5000.0f);
  tree.layoutIfNeeded(viewport);
  facts.apply(skiff::model::over<projection_hint>(skiff::model::setTo(projection_hint{7})));
  skiff::bind::refresh(root, facts);
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 0.0f);
  source.apply(skiff::model::over<projection_amount>(skiff::model::setTo(projection_amount{3})));
  skiff::bind::refresh(root, source);
  tree.layoutIfNeeded(viewport);
  EXPECT_EQ(node.view().fItems.size(), 300u);
  facts.apply(skiff::model::over<projection_hint>(skiff::model::setTo(projection_hint{3})));
  skiff::bind::refresh(root, facts);
  tree.layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(node.current(), 5000.0f);
}

TEST(Views, ClampedWidthUsesPixelsInsideAFormerlyFillingRoot) {
  auto node = c::clamped_width({}, c::column(c::vbox(0.0f, {.fill = true})));
  scene::layout(node, skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 600.0f));
  EXPECT_FLOAT_EQ(node.bounds().width(), 300.0f);
  EXPECT_FLOAT_EQ(node.bounds().height(), 600.0f);
  node.read({.whole = true});
  scene::layout(node, skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 600.0f));
  EXPECT_FLOAT_EQ(node.bounds().width(), 1000.0f);
  EXPECT_FLOAT_EQ(node.bounds().height(), 600.0f);
}
