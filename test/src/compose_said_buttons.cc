// Buttons whose words are read from the model: pressed where they are drawn, alone and in a card.
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

TEST(Compose, AButtonWhoseWordsAreReadIsPressedWhereItIsDrawn) {
  using namespace said_button;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), tile()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  // Laid out once before its words are read: a page shown, then its facts.
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
  (void)tree.finishFrame();
  for (int i = 0; i < 3; ++i) {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  }
  std::print("{}", scene::laidOutText(tree.root()));
  // The plate, as drawn: the widest node 32 high.
  float left = 0.0f, top = 0.0f, width = 0.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.bounds.height() == 32.0f && one.bounds.width() > width) {
      left = one.bounds.fLeft;
      top = one.bounds.fTop;
      width = one.bounds.width();
    }
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::move{left + width / 2.0f, top + 16.0f});
  (void)tree.dispatchPointer(scene::pointer::down{left + width / 2.0f, top + 16.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{left + width / 2.0f, top + 16.0f, 1});
  const auto took = scene::hostWork().answers.size() + scene::hostWork().pressed.size();
  std::println("said button at {},{} {} wide: {} answer(s)", left, top, width, took);
  EXPECT_GT(width, 24.0f);
  EXPECT_EQ(took, 1u);
}
TEST(Compose, AButtonWhoseTextIsReadIsPressedWhereItIsDrawn) {
  using namespace said_text;
  test_font::TestFont font;
  if (!font.ok)
    GTEST_SKIP() << "Needs DejaVu Sans to measure the words";
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), tile()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
  (void)tree.finishFrame();
  for (int i = 0; i < 3; ++i) {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  }
  std::print("{}", scene::laidOutText(tree.root()));
  float left = 0.0f, top = 0.0f, width = 0.0f, words = 0.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root())) {
    if (one.bounds.height() == 32.0f && one.bounds.width() > width) {
      left = one.bounds.fLeft;
      top = one.bounds.fTop;
      width = one.bounds.width();
    }
  }
  scene::forEachNode<nodes::Text>(tree.root(), [&](auto &one) { words = std::max(words, one.bounds().width()); });
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  const float x = left + width - 14.0f;  // on the words' end, past the padding
  (void)tree.dispatchPointer(scene::pointer::move{x, top + 16.0f});
  (void)tree.dispatchPointer(scene::pointer::down{x, top + 16.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{x, top + 16.0f, 1});
  const auto took = scene::hostWork().answers.size() + scene::hostWork().pressed.size();
  std::println("text button at {},{} {} wide, words {}: {} answer(s)", left, top, width, words, took);
  EXPECT_GT(width, 24.0f);
  EXPECT_EQ(took, 1u);
}

// The same button in mux's settings card: masked, its tile over a list of
// rows mounted as the facts say, hidden until it is expanded.
namespace said_card {
namespace c = skiff::compose;
using namespace window_edge;
using row_t = decltype(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), shown_later::Probe()));
inline auto rows() {
  return c::projected<Feed>([](const Feed &now) { return std::optional<int>(now.count); },
                            c::mount<row_t, int>([](int) { return std::tuple{c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), shown_later::Probe())}; },
                                                 {.fillX = true, .autoSize = scene::axes::kY}));
}
inline auto card() {
  return c::column(c::vbox(16.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {12.0f, 12.0f, 12.0f, 12.0f}, .cornerRadius = 10.0f,
                                   .background = 0xffeeeeeeu, .masking = true}),
                   said_text::tile(), c::shown_for<Feed>([](const Feed &now) { return now.first != 0; }, rows()));
}
}  // namespace said_card
TEST(Compose, AButtonWhoseTextIsReadInACardIsPressedWhereItIsDrawn) {
  using namespace said_card;
  skia::SkFont font;
  paint::defaultFont() = &font;
  struct cleanup {
    ~cleanup() { paint::defaultFont() = nullptr; }
  } clear;
  scene::Scene tree(c::column(c::vbox(32.0f, {.fill = true, .padding = {16.0f, 16.0f, 16.0f, 16.0f}}), said_card::card()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
  (void)tree.finishFrame();
  for (int i = 0; i < 3; ++i) {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  }
  std::print("{}", scene::laidOutText(tree.root()));
  float left = 0.0f, top = 0.0f, width = 0.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.bounds.height() == 32.0f && one.bounds.width() > width) {
      left = one.bounds.fLeft;
      top = one.bounds.fTop;
      width = one.bounds.width();
    }
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  const float x = left + width - 14.0f;
  (void)tree.dispatchPointer(scene::pointer::move{x, top + 16.0f});
  (void)tree.dispatchPointer(scene::pointer::down{x, top + 16.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{x, top + 16.0f, 1});
  const auto took = scene::hostWork().answers.size() + scene::hostWork().pressed.size();
  std::println("card button at {},{} {} wide: {} answer(s)", left, top, width, took);
  EXPECT_EQ(took, 1u);
  // Expanded, as the press asks: its list shown under the tile.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 31, .first = 1})));
  for (int i = 0; i < 3; ++i) {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  }
  std::print("{}", scene::laidOutText(tree.root()));
  bool listed = false;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.visible && one.bounds.width() == 40.0f && one.bounds.height() == 40.0f && one.bounds.fTop > top + 32.0f)
      listed = true;
  std::println("expanded list shown: {}", listed);
  EXPECT_TRUE(listed);
}

}  // namespace
