// Themes and styles on named views; clamped sides, typed and marked parts; a node asking on its own behalf.
#include "skiff/named.h"
import std;
import skia;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;
import skiff.paint;

#include "gtest/gtest-macros.h"

// Screens as combinator expressions, bound to a model; local and hidden
// state; components wired by what they send (>>, +); a processor; time; a
// Binding doing only what can have happened; events whose types are
// deduced.

#include "compose_common.inc"

namespace {

TEST(Compose, APlainPressInABoundNamedViewChangesTheBoundPart) {
  namespace c = skiff::compose;
  // The second card, at whatever depth the build holds its press: the
  // scope is its row where nothing is erased, holds it where it is.
  const std::array paths{scene::Path{1}, scene::Path{1, 0}, scene::Path{0, 1}, scene::Path{0, 1, 0}, scene::Path{0, 1, 0, 0}};
  const auto themes = paths | std::views::transform([](const scene::Path &path) {
    model::Model<theme_tests::Root, bind::NoReactions> m;
    auto page = c::scoped<theme_tests::Look>(c::handlers(), c::row(c::hbox(0.0f, {.width = 40.0f, .height = 10.0f}),
                                                                   theme_tests::card(1), theme_tests::card(2)));
    bind::refresh(page, m);
    (void)bind::press(page, m, path);
    return m.root().look.fValue.theme;
  }) | std::ranges::to<std::vector>();
  EXPECT_EQ(std::ranges::count(themes, 2), 1);
  EXPECT_EQ(std::ranges::count(themes, 1), 0);
}
TEST(Compose, AStylesTextFieldsAreAsWideAsTheirColumn) {
  using namespace style_tests;
  float width = 0;
  auto page = c::Box(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::make_unique<decltype(c::scoped<style_tests::Look>(c::handlers(), controls(&width)))>(c::scoped<style_tests::Look>(c::handlers(), controls(&width))));
  scene::Scene tree(std::move(page));
  model::Model<style_tests::Root, bind::NoReactions> m;
  bind::Binding<decltype(m)> binding;
  binding.refresh(tree.root(), m);
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
  EXPECT_TRUE(scene::overflows().empty());
  (void)std::exchange(scene::overflows(), {});
  EXPECT_FLOAT_EQ(width, 560.0f);
  // The tree as laid out, told a row a node: each field's view, as wide as
  // its column's room.
  const auto rows = scene::laidOutTree(tree.root());
  EXPECT_EQ(std::ranges::count_if(rows, [](const scene::LaidOutNode &row) {
    return row.names && row.view == "style_text_field" && row.bounds.width() == 560.0f;
  }), 2);
  EXPECT_TRUE(std::ranges::none_of(rows, &scene::LaidOutNode::overflows));
  // Each field's view holds the field, its label empty or not: as tall as
  // the label, the gap and the field.
  const auto view = std::ranges::find_if(rows, [](const scene::LaidOutNode &row) { return row.names && row.view == "style_text_field"; });
  ASSERT_NE(view, rows.end());
  const auto field = std::ranges::find_if(view, rows.end(), [](const scene::LaidOutNode &row) { return row.kind.name == "Bound"; });
  ASSERT_NE(field, rows.end());
  EXPECT_LE(field->bounds.fBottom, view->bounds.fBottom);
  EXPECT_TRUE(scene::laidOutText(tree.root()).contains("[style_controls]"));
}
TEST(Compose, AViewNamedOverANamedViewIsTheSameHeld) {
  using namespace named_twice;
  float wide = 0;
  auto view = outer(&wide);
  static_assert(compose::NamedOverHeld<decltype(view)> == scene::kErasedCombinators);
  static_assert(std::same_as<compose::part_t<decltype(view)>, compose::Held> == scene::kErasedCombinators);
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(view)));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
  EXPECT_FLOAT_EQ(wide, 300.0f);
  const auto rows = scene::laidOutTree(tree.root());
  EXPECT_TRUE(std::ranges::any_of(rows, [](const scene::LaidOutNode &row) { return row.names && row.view == "accounts_view"; }));
}
TEST(Compose, AClampedSideIsAsWideAsItsColumn) {
  using namespace side_tests;
  float wide = 0, chat = 0;
  scene::Scene tree(c::row(c::hbox(0.0f, {.fill = true}),
      c::shown_for<SideUp>([](const SideUp &now) { return now.up; }, c::column(c::vbox(0.0f, {.fillY = true, .autoSize = scene::axes::kX}), side(&wide))),
      c::shown_for<SideUp>([](const SideUp &now) { return now.up; },
          c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}), c::styled({.fillX = true}, MeasuredWidth(&chat, {.height = 20.0f}))))));
  model::Model<ShownRoot, bind::NoReactions> m;
  bind::Binding<decltype(m)> binding;
  binding.refresh(tree.root(), m);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 470.0f, 285.0f));
  EXPECT_FLOAT_EQ(wide, 253.0f);
  EXPECT_FLOAT_EQ(chat, 470.0f - 253.0f);
}

// A named view held as itself in a typed part (a Box of its type, as a
// variant of message pieces holds one): placed and measured by its content,
// as it is laid out -- by its own state, never set, it was nothing.
namespace typed_part {
namespace c = skiff::compose;
inline auto piece(float *wide) {
  return SKIFF_NAMED(code_piece, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                           c::styled({.fillX = true}, MeasuredWidth(wide, {.height = 10.0f}))));
}
}  // namespace typed_part
TEST(Compose, ANamedViewInATypedPartIsMeasuredByItsContent) {
  using namespace typed_part;
  float wide = 0;
  using piece_t = decltype(piece(nullptr));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::Box<piece_t>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), piece(&wide))));
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
  EXPECT_FLOAT_EQ(wide, 300.0f);
  EXPECT_TRUE(scene::overflows().empty());
  const auto rows = scene::laidOutTree(tree.root());
  EXPECT_EQ(std::ranges::count_if(rows, [](const scene::LaidOutNode &row) { return row.names && row.view == "code_piece"; }), 1);
  EXPECT_TRUE(std::ranges::none_of(rows, &scene::LaidOutNode::overflows));
}

// A mark made by the program around a named view (mux's code pieces,
// marked to be found): a wrapper no combinator erased, adopted where it is
// laid out -- unadopted, it was 0 by 0 and its content stuck out of it.
namespace marked_part {
struct code_kind {};
}  // namespace marked_part
TEST(Compose, AMarkMadeAroundANamedViewIsAsWideAsItsContent) {
  using namespace typed_part;
  float wide = 0;
  using piece_t = decltype(piece(nullptr));
  using marked_t = compose::Marked<marked_part::code_kind, piece_t>;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::Box<marked_t>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), marked_t(piece(&wide)))));
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
  EXPECT_FLOAT_EQ(wide, 300.0f);
  EXPECT_TRUE(scene::overflows().empty());
}
TEST(Compose, ANodeAskingOnItsOwnBehalfIsHeard) {
  using namespace edge_press;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), view()));
  model::Model<side_tests::ShownRoot, bind::NoReactions> m;
  bind::Binding<decltype(m)> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  for (double at : {0.0, 16.0, 32.0, 48.0}) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
    tree.update(at);
  }
  std::println("presses kept: {}", scene::hostWork().pressed.size());
  ASSERT_EQ(scene::hostWork().pressed.size(), 1u);
  edge_press::Asked program;
  const auto path = scene::hostWork().pressed.front();
  scene::hostWork().pressed.clear();
  EXPECT_TRUE(bind::press(tree.root(), m, path, &program));
  EXPECT_EQ(program.asked, 1);
}

}  // namespace
