import std;
import skia;
import skiff.scene;
import skiff.nodes.flow;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace c = skiff::compose;
namespace scene = skiff::scene;
using entry = std::pair<int, int>;
inline constexpr auto make = [](const entry& item) {
  return ::skiff::compose::build(c::column(c::vbox(0.0f, {.width = static_cast<float>(item.second), .height = 30.0f})));
};
using rows = skiff::nodes::MemoRows<int, int, std::invoke_result_t<decltype(make), const entry&>, std::remove_cv_t<decltype(make)>>;
}

TEST(MemoRowsLayout, HorizontalRowsKeepTheirNaturalWidthAndCrossAlignment) {
  skiff::nodes::Stack container;
  container.fStack = {.direction = skiff::nodes::direction::horizontal{}, .spacingX = 4.0f, .wrap = false};
  rows node(make, std::move(container), {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
  node.apply({.fillX = true, .height = 34.0f});
  node.read(std::vector<entry>{{1, 30}, {2, 40}});
  scene::layout(node, skia::SkRect::MakeXYWH(0.0f, 0.0f, 200.0f, 34.0f));
  ASSERT_EQ(node.fRows.size(), 2u);
  EXPECT_FLOAT_EQ(node.fRows[0].bounds().width(), 30.0f);
  EXPECT_FLOAT_EQ(node.fRows[1].bounds().width(), 40.0f);
  EXPECT_FLOAT_EQ(node.fRows[1].bounds().fLeft, 34.0f);
  EXPECT_FLOAT_EQ(node.fRows[0].bounds().fTop, 2.0f);
}

TEST(MemoRowsLayout, VerticalIconsCenterWithinTheirPaddedBar) {
  rows node(make, {}, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle});
  node.apply({.width = 56.0f, .height = 200.0f, .padding = {4.0f, 6.0f, 8.0f, 6.0f}});
  node.read(std::vector<entry>{{1, 40}});
  scene::layout(node, skia::SkRect::MakeXYWH(0.0f, 0.0f, 56.0f, 200.0f));
  EXPECT_FLOAT_EQ(node.fRows[0].bounds().fLeft, 8.0f);
  EXPECT_FLOAT_EQ(node.fRows[0].fRow.bounds().fLeft, 8.0f);
  EXPECT_FLOAT_EQ(node.fRows[0].bounds().fTop, 4.0f);
}

TEST(MemoRowsLayout, NaturallySizedTabsWrapAtTheirActualWidths) {
  skiff::nodes::Stack container;
  container.fStack = {.direction = skiff::nodes::direction::horizontal{}, .spacingX = 2.0f, .spacingY = 2.0f, .wrap = true};
  rows node(make, std::move(container), {.autoSize = scene::axes::kBoth});
  node.apply({.fillX = true, .autoSize = scene::axes::kY});
  node.read(std::vector<entry>{{1, 70}, {2, 70}});
  scene::layout(node, skia::SkRect::MakeXYWH(0.0f, 0.0f, 100.0f, 100.0f));
  EXPECT_FLOAT_EQ(node.fRows[0].bounds().width(), 70.0f);
  EXPECT_FLOAT_EQ(node.fRows[1].bounds().fLeft, 0.0f);
  EXPECT_FLOAT_EQ(node.fRows[1].bounds().fTop, 32.0f);
}

TEST(MemoRowsLayout, ExistingDefaultRowsStillFillAcross) {
  rows node(make);
  node.apply({.fillX = true, .autoSize = scene::axes::kY});
  node.read(std::vector<entry>{{1, 40}});
  scene::layout(node, skia::SkRect::MakeXYWH(0.0f, 0.0f, 200.0f, 100.0f));
  EXPECT_FLOAT_EQ(node.fRows[0].bounds().width(), 200.0f);
}

// A wrapped flow's children that grow, as CSS's flex-wrap has them: broken
// onto lines at the least each takes (its min-width), then each line's
// growing children share it -- two sides beside each other where there is
// room, one over the other, each the whole width, where there is not.
TEST(FlowLayout, WrappedChildrenThatGrowShareTheirLine) {
  const auto side = [] { return ::skiff::compose::build(c::column(c::vbox(0.0f, {.height = 20.0f, .grow = scene::axes::kX, .minWidth = 100.0f}))); };
  skiff::nodes::Flow flow(skiff::nodes::FlowOptions{.direction = skiff::nodes::direction::horizontal{}, .spacingX = 10.0f,
                                                    .spacingY = 5.0f, .wrap = true},
                          side(), side());
  flow.apply({.fillX = true, .autoSize = scene::axes::kY});
  const auto boxes = [&] {
    std::vector<skia::SkRect> all;
    flow.forEachChild([&](auto &child) { all.push_back(scene::stateOf(child).fBounds); });
    return all;
  };
  scene::layout(flow, skia::SkRect::MakeXYWH(0.0f, 0.0f, 310.0f, 100.0f));
  auto wide = boxes();
  ASSERT_EQ(wide.size(), 2u);
  EXPECT_FLOAT_EQ(wide[0].width(), 150.0f);
  EXPECT_FLOAT_EQ(wide[1].width(), 150.0f);
  EXPECT_FLOAT_EQ(wide[1].fLeft, 160.0f);
  EXPECT_FLOAT_EQ(wide[1].fTop, 0.0f);
  scene::layout(flow, skia::SkRect::MakeXYWH(0.0f, 0.0f, 150.0f, 100.0f));
  auto narrow = boxes();
  EXPECT_FLOAT_EQ(narrow[0].width(), 150.0f);
  EXPECT_FLOAT_EQ(narrow[1].width(), 150.0f);
  EXPECT_FLOAT_EQ(narrow[1].fLeft, 0.0f);
  EXPECT_FLOAT_EQ(narrow[1].fTop, 25.0f);
  // Wide again: beside each other again, not kept at the width they had.
  scene::layout(flow, skia::SkRect::MakeXYWH(0.0f, 0.0f, 310.0f, 100.0f));
  EXPECT_FLOAT_EQ(boxes()[1].fLeft, 160.0f);
}

// A list laid out in part -- a row made again where it was, the rows below
// moved by what it grew, what they hold moved only when gone into -- comes
// to what a whole layout of the same rows does, node for node, frame after
// frame; and so with a row added at the end.
namespace in_part {
using entry = std::pair<int, int>;  // key, height
inline constexpr auto make = [](const entry &item) {
  return ::skiff::compose::build(c::column(c::vbox(2.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                           c::column(c::vbox(0.0f, {.width = 40.0f, .height = static_cast<float>(item.second)})),
                                           c::column(c::vbox(0.0f, {.width = 20.0f, .height = 10.0f}))));
};
using rows = skiff::nodes::MemoRows<int, int, std::invoke_result_t<decltype(make), const entry &>, std::remove_cv_t<decltype(make)>>;
struct List {
  scene::Scene<rows> tree{std::in_place, make};
  double at = 0.0;
  explicit List(const std::vector<entry> &items) {
    tree.root().fStack.spacingY = 4.0f;
    tree.root().apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f}});
    show(items);
  }
  void show(const std::vector<entry> &items) {
    tree.root().read(items);
    for (int frame = 0; frame < 2; ++frame) {
      tree.update(at += 16.0);
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 2000.0f));
      (void)tree.finishFrame();
    }
  }
  [[nodiscard]] std::vector<skia::SkRect> boxes() {
    return scene::laidOutTree(tree.root()) | std::views::transform([](const scene::LaidOutNode &one) { return one.bounds; }) |
           std::ranges::to<std::vector>();
  }
};
}  // namespace in_part

TEST(MemoRowsLayout, ARowMadeAgainInPlaceLaysOutAsAWholeLayoutWould) {
  using namespace in_part;
  std::vector<entry> items = std::views::iota(0, 50) | std::views::transform([](int i) { return entry{i, 20 + i % 7}; }) |
                             std::ranges::to<std::vector>();
  List list(items);
  for (const auto &[place, height] : {std::pair{10, 60}, std::pair{3, 5}, std::pair{49, 90}, std::pair{0, 33}}) {
    items[static_cast<std::size_t>(place)].second = height;
    list.show(items);
    List whole(items);
    EXPECT_EQ(list.boxes(), whole.boxes()) << "after row " << place << " came to " << height;
  }
  items.push_back(entry{50, 44});
  list.show(items);
  List whole(items);
  EXPECT_EQ(list.boxes(), whole.boxes()) << "after a row added";
}
