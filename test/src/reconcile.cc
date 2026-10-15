import std;
import skiff.scene;
import skiff.nodes.flow;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

TEST(Flow, ReconcileKeepsRowsWithCapturedTransforms) {
  namespace c = skiff::compose;
  using item = std::pair<int, int>;
  int made = 0;
  const auto make = [&](const item& one) {
    ++made;
    return c::onClick(one.first,
        c::applied<int>([offset = one.second](int value) { return value + offset; },
            [](auto& node, int value) { node.apply({.width = static_cast<float>(value)}); },
            c::column(c::vbox())), std::to_string(one.second));
  };
  using row = decltype(make(std::declval<const item&>()));
  static_assert(std::move_constructible<row>);
  static_assert(!std::is_move_assignable_v<row>);
  std::vector<row> rows;
  std::vector<item> items{{1, 10}, {2, 20}, {1, 30}};
  const auto reconcile = [&] {
    return skiff::nodes::reconcile(rows, items, [](const item& one) { return one.first; },
        [](const row& shown) { return shown.onPress(); }, make,
        [](const row& shown, const item& one) { return shown.semantics().fLabel == std::to_string(one.second); });
  };
  ASSERT_TRUE(reconcile());
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(made, 3);
  const auto first = rows[0].fState.fId;
  const auto second = rows[1].fState.fId;
  const auto third = rows[2].fState.fId;
  const auto* storage = rows.data();
  EXPECT_FALSE(reconcile());
  EXPECT_EQ(rows.data(), storage);
  EXPECT_EQ(made, 3);
  EXPECT_EQ(rows[0].fState.fId, first);
  EXPECT_EQ(rows[2].fState.fId, third);

  items[1].second = 21;
  ASSERT_TRUE(reconcile());
  EXPECT_EQ(made, 4);
  EXPECT_EQ(rows[0].fState.fId, first);
  EXPECT_NE(rows[1].fState.fId, second);
  EXPECT_EQ(rows[2].fState.fId, third);
  EXPECT_EQ(rows[1].semantics().fLabel, "21");
  const auto changed = rows[1].fState.fId;

  // Repeated keys each retain the row whose view still matches.
  items = {{1, 30}, {1, 10}, {2, 21}};
  ASSERT_TRUE(reconcile());
  EXPECT_EQ(made, 4);
  EXPECT_EQ(rows[0].fState.fId, third);
  EXPECT_EQ(rows[1].fState.fId, first);
  EXPECT_EQ(rows[2].fState.fId, changed);
  items.pop_back();
  ASSERT_TRUE(reconcile());
  EXPECT_EQ(rows.size(), 2u);
  EXPECT_EQ(made, 4);
  EXPECT_EQ(rows[0].fState.fId, third);
  EXPECT_EQ(rows[1].fState.fId, first);
}
