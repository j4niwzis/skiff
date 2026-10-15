import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

struct dragged { float x; };
struct list_marks { int room = 7; };
struct jump_to_mark {};
struct chosen { int key = 1; };
struct item { std::string label; using ModelOpaque = void; };
struct state {
  skiff::model::Keyed<int, item> items;
};
struct selection_state { skiff::model::Tracked<chosen> selection; };
TEST(Compose, DragAndRightPressKeepTheirActions) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  auto edge = c::onDragAcross([](float x) { return dragged{x}; }, c::column(c::vbox()));
  scene::PointerReply reply;
  edge.onPointer(scene::phase::target{}, scene::pointer::down{0.0f, 0.0f}, reply);
  edge.onPointer(scene::phase::target{}, scene::pointer::move{42.0f, 0.0f}, reply);
  EXPECT_EQ(edge.onPress().x, 42.0f);
  edge.onPointer(scene::phase::target{}, scene::pointer::up{42.0f, 0.0f}, reply);
  auto button = c::onRightPress(list_marks{}, c::onClick(jump_to_mark{}, c::column(c::vbox())));
  auto right = button.onPointer(scene::phase::target{}, scene::pointer::down{0.0f, 0.0f, 3}, reply);
  EXPECT_TRUE(right);
  auto left = button.onPointer(scene::phase::target{}, scene::pointer::down{0.0f, 0.0f, 1}, reply);
  EXPECT_FALSE(left);
  static_assert(std::same_as<decltype(button.onPress()), jump_to_mark>);
  (void)scene::AnyNode::opsOf<decltype(button)>();
  auto& work = scene::hostWork();
  work.answerNow.reset();
  work.answerType = nullptr;
  scene::PointerHandler{}(button, scene::phase::target{},
                          scene::pointer::down{0.0f, 0.0f, 3}, reply);
  if constexpr (scene::kErasedWalks) {
    ASSERT_TRUE(work.answerNow);
    EXPECT_EQ(work.answerType, &scene::kTypeKey<list_marks>);
    EXPECT_EQ(std::static_pointer_cast<const list_marks>(work.answerNow)->room, 7);
  }
  work.answerNow.reset();
  work.answerType = nullptr;
  skiff::paint::defaultFont() = nullptr;
}

TEST(Compose, RekeyAcrossModelsReadsValuesWithEqualRevisions) {
  namespace c = skiff::compose;
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  auto node = c::rekeyed_by<chosen>([](const chosen& key) { return std::tuple(key.key); },
      c::scoped<item>(c::handlers(), c::column(c::vbox(),
          c::text_for<item>([](const item& value) { return value.label; },
                            skiff::nodes::Text("", 13.0f, 0))), 1));
  state values;
  values.items.put(1, item{"first"});
  values.items.put(2, item{"second"});
  skiff::model::Model<state, skiff::bind::NoReactions> model(std::move(values));
  skiff::model::Model<selection_state, skiff::bind::NoReactions> selection(selection_state{});
  skiff::bind::refresh(node, selection);
  skiff::bind::refresh(node, model);
  EXPECT_EQ(std::get<0>(node.fParts).text(), "first");
  selection.apply(skiff::model::over<chosen>(skiff::model::setTo(chosen{2})));
  skiff::bind::refresh(node, selection);
  skiff::bind::refresh(node, model);
  EXPECT_EQ(std::get<0>(node.fParts).text(), "second");
  auto moved = std::move(node);
  skiff::bind::refresh(moved, model);
  EXPECT_EQ(std::get<0>(moved.fParts).text(), "second");
  skiff::paint::defaultFont() = nullptr;

}

}  // namespace
