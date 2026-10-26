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
#include "made.h"

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
struct Drag {
  dragged operator()(float x) const { return {x}; }
};
TEST(Compose, DragAndRightPressKeepTheirActions) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  skia::SkFont font;
  skiff::paint::defaultFont() = &font;
  using Column = decltype(c::column(c::vbox()));
  auto dragging = c::onDragAcross(Drag{}, c::column(c::vbox()));
  auto &edge = made_as<c::OnDragAcross<Drag, c::part_t<Column>>>(dragging);
  scene::PointerReply reply;
  edge.onPointer(scene::phase::target{}, scene::pointer::down{0.0f, 0.0f}, reply);
  edge.onPointer(scene::phase::target{}, scene::pointer::move{42.0f, 0.0f}, reply);
  EXPECT_EQ(edge.onPress().x, 42.0f);
  edge.onPointer(scene::phase::target{}, scene::pointer::up{42.0f, 0.0f}, reply);
  using Clicked = decltype(c::onClick(jump_to_mark{}, c::column(c::vbox())));
  auto button = c::onRightPress(list_marks{}, c::onClick(jump_to_mark{}, c::column(c::vbox())));
  auto &pressed = made_as<c::OnRightPress<list_marks, c::part_t<Clicked>>>(button);
  auto right = pressed.onPointer(scene::phase::target{}, scene::pointer::down{0.0f, 0.0f, 3}, reply);
  EXPECT_TRUE(right);
  auto left = pressed.onPointer(scene::phase::target{}, scene::pointer::down{0.0f, 0.0f, 1}, reply);
  EXPECT_FALSE(left);
  static_assert(std::same_as<decltype(made_as<c::OnClick<jump_to_mark, c::part_t<Column>>>(button).onPress()), jump_to_mark>);
  (void)scene::tableOf<std::remove_cvref_t<decltype(pressed)>>();
  auto& work = scene::hostWork();
  work.answerNow.reset();
  work.answerType = nullptr;
  scene::PointerHandler{}(pressed, scene::phase::target{},
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
  const auto label = [](const item& value) { return value.label; };
  auto node = c::rekeyed_by<chosen>([](const chosen& key) { return std::tuple(key.key); },
      c::scoped<item>(c::handlers(), c::column(c::vbox(),
          c::text_for<item>(label, skiff::nodes::Text("", 13.0f, 0))), 1));
  const auto textOf = [&](auto &root) -> const std::string & { return made_as<projected_t<item, decltype(label), c::TextOf>>(root).text(); };
  state values;
  values.items.put(1, item{"first"});
  values.items.put(2, item{"second"});
  skiff::model::Model<state, skiff::bind::NoReactions> model(std::move(values));
  skiff::model::Model<selection_state, skiff::bind::NoReactions> selection(selection_state{});
  skiff::bind::refresh(node, selection);
  skiff::bind::refresh(node, model);
  EXPECT_EQ(textOf(node), "first");
  selection.apply(skiff::model::over<chosen>(skiff::model::setTo(chosen{2})));
  skiff::bind::refresh(node, selection);
  skiff::bind::refresh(node, model);
  EXPECT_EQ(textOf(node), "second");
  auto moved = std::move(node);
  skiff::bind::refresh(moved, model);
  EXPECT_EQ(textOf(moved), "second");
  skiff::paint::defaultFont() = nullptr;

}

}  // namespace
