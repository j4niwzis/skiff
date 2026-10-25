import std;
import skiff.scene;
import skiff.nodes.flow;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace c = skiff::compose;
// A row's transform, capturing what it adds, and how it is shown.
struct Offset {
  const int offset = 0;  // captured: a row holding it is not assigned over
  int operator()(int value) const { return value + offset; }
};
struct Widen {
  void operator()(auto& node, int value) const { node.apply({.width = static_cast<float>(value)}); }
};
using Applied = decltype(c::applied<int>(Offset{}, Widen{}, c::column(c::vbox())));
using Click = c::OnClick<int, c::part_t<Applied>>;
// The click a row is, as it was made: the row itself where combinators are
// concrete, found under the Held it is erased into where they are not. (The
// walks take a node to change; a row's key is read, nothing changed: the
// const of the reconcile's callback put aside.)
const Click& clickOf(const auto& shown) {
  const Click* found = skiff::scene::findNode<Click>(const_cast<std::remove_cvref_t<decltype(shown)>&>(shown));
  if (found == nullptr)
    std::abort();
  return *found;
}
}  // namespace

TEST(Flow, ReconcileKeepsRowsWithCapturedTransforms) {
  using item = std::pair<int, int>;
  int made = 0;
  const auto make = [&](const item& one) {
    ++made;
    return c::onClick(one.first, c::applied<int>(Offset{one.second}, Widen{}, c::column(c::vbox())), std::to_string(one.second));
  };
  using row = decltype(make(std::declval<const item&>()));
  static_assert(std::move_constructible<row>);
  // Concrete, a row holds what it captured and is not assigned over; erased,
  // it is a Held, moved as any.
  static_assert(skiff::scene::kErasedCombinators || !std::is_move_assignable_v<row>);
  std::vector<row> rows;
  std::vector<item> items{{1, 10}, {2, 20}, {1, 30}};
  const auto reconcile = [&] {
    return skiff::nodes::reconcile(rows, items, [](const item& one) { return one.first; },
        [](const row& shown) { return clickOf(shown).onPress(); }, make,
        [](const row& shown, const item& one) { return clickOf(shown).semantics().fLabel == std::to_string(one.second); });
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
  EXPECT_EQ(clickOf(rows[1]).semantics().fLabel, "21");
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
