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
  return c::column(c::vbox(0.0f, {.width = static_cast<float>(item.second), .height = 30.0f}));
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
