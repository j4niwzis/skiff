// Call buttons read from the model -- in a chat's head, as the calls plugin makes them, from a nested part.
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

TEST(Compose, ACallButtonInAChatHeadIsShownAndPressed) {
  using namespace head_button;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), head()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  const auto frame = [&] {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  };
  frame();
  frame();
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  frame();
  frame();
  std::print("{}", scene::laidOutText(tree.root()));
  float left = -1.0f, top = -1.0f, width = 0.0f;
  // Found as it is in the tree: the named view, which is the click itself
  // (shown_if over visible over styled over onClick, each the node it is
  // made over) -- the walks, erased, find a type as it is: there the click,
  // held under what is made over it.
  using Button = std::conditional_t<scene::kErasedCombinators, c::OnClick<Pressed, c::part_t<Probe>>, decltype(button())>;
  scene::forEachNode<Button>(tree.root(), [&](auto &one) {
    left = one.bounds().fLeft;
    top = one.bounds().fTop;
    width = one.bounds().width();
  });
  std::println("call button at {},{} {} wide", left, top, width);
  EXPECT_FLOAT_EQ(width, 40.0f);
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::down{left + 5.0f, top + 5.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{left + 5.0f, top + 5.0f, 1});
  EXPECT_EQ(std::exchange(scene::hostWork().pressed, {}).size(), 1u);
}
TEST(Compose, ACallButtonMadeAsTheCallsPluginMakesItIsShown) {
  using namespace named_call;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), head()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  const auto frame = [&] {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  };
  frame();
  frame();
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  frame();
  frame();
  std::print("{}", scene::laidOutText(tree.root()));
  bool seen = false;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.visible && one.bounds.width() == 36.0f && one.bounds.height() == 36.0f)
      seen = true;
  std::println("named call button seen: {}", seen);
  EXPECT_TRUE(seen);
}
TEST(Compose, ACallButtonReadFromANestedPartIsShown) {
  using namespace head_button;
  using nested_button::Model;
  using nested_button::Root;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), head()));
  Model m{Root{.feed = {Feed{}}, .layers = {.head = {FeedTold{.of = 1}}}}};
  bind::Binding<Model> binding;
  const auto frame = [&] {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  };
  frame();
  frame();
  (void)m.apply(model::edit(model::placeOf<FeedTold, Root>(), model::setTo(FeedTold{.of = 2})));
  frame();
  frame();
  float width = 0.0f;
  bool shown = false;
  scene::forEachNode<c::OnClick<shown_later::Pressed, c::part_t<shown_later::Probe>>>(tree.root(), [&](auto &one) {
    width = one.bounds().width();
  });
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.bounds.width() == 40.0f && one.bounds.fLeft == 308.0f)
      shown = shown || one.visible;
  std::println("nested call button {} wide, shown {}", width, shown);
  EXPECT_TRUE(shown);
}

}  // namespace
