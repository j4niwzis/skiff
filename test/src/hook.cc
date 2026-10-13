import std;
import gtest;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.compose;
// (skiff.bind not imported here: only reachable, through skiff.compose --
// as a program's units that make node tables see it.)

#include "gtest/gtest-macros.h"

namespace {
struct Count {
  int n = 0;
};
} // namespace

// A node's table made in a unit that does not see skiff.bind has its
// binding entry all the same: the same table every unit makes.
TEST(Hook, ATableMadeWhereTheBindingLayerIsOnlyReachableHasItsBinding) {
  auto node = skiff::compose::bound<Count>(skiff::nodes::Text("", 12.0f, 0u));
  EXPECT_NE(skiff::scene::AnyNodeRef::of(node).bindTable(), nullptr);
}
