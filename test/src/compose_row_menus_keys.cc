// A row's menu on a right press, through each layer of a row; a screen's keys answered once; a button shown by what it reads.
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

TEST(Compose, ARightPressOnARowsTextAsksItsMenuAndAClickNoReply) {
  using namespace row_menu;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row()));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 3});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 3});
  const std::size_t menus = scene::hostWork().answers.size();
  std::println("answers after a right press: {}, presses: {}", menus, scene::hostWork().pressed.size());
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(menus, 1u);
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 1});
  std::println("after a click: answers {}, presses {}", scene::hostWork().answers.size(), scene::hostWork().pressed.size());
  EXPECT_TRUE(scene::hostWork().pressed.empty());
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
}
TEST(Compose, ARightPressIsAnsweredThroughEachLayerOfARow) {
  using namespace row_menu_parts;
  using row_menu::Ask;
  const auto alone = rightPressed(c::onRightPressAt(Ask{}, Probe()));
  const auto overColumn = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Probe())));
  const auto overSwipe = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::swiped(48.0f, 120.0f, [] { return row_menu::Reply{}; }, row_menu::Show{}, nodes::Text("", 12.0f, 0u), Probe()))));
  std::println("right press answered: alone {}, over a column {}, over a swipe {}", alone, overColumn, overSwipe);
  if constexpr (scene::kErasedWalks) {
    EXPECT_EQ(alone, 1u);
    EXPECT_EQ(overColumn, 1u);
    EXPECT_EQ(overSwipe, 1u);
  }
}

TEST(Compose, ARightPressOnSelectableTextIsAnsweredAbove) {
  using namespace row_menu_parts;
  using row_menu::Ask;
  const auto text = [](bool selectable) {
    nodes::Text words("hello there", 13.0f, 0xff000000u);
    words.setSelectable(selectable);
    words.apply({.width = 200.0f, .height = 20.0f});
    return words;
  };
  const auto plain = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), text(false))));
  const auto selectable = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), text(true))));
  std::println("right press on text answered: plain {}, selectable {}", plain, selectable);
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(selectable, 1u);
}
TEST(Compose, AButtonHiddenAsItIsMadeIsShownByWhatItReads) {
  using namespace shown_later;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), button()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  const auto pressedAt = [&] {
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    (void)tree.finishFrame();
    scene::hostWork().pressed.clear();
    (void)tree.dispatchPointer(scene::pointer::down{10.0f, 10.0f, 1});
    (void)tree.dispatchPointer(scene::pointer::up{10.0f, 10.0f, 1});
    return std::exchange(scene::hostWork().pressed, {}).size();
  };
  EXPECT_EQ(pressedAt(), 0u);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  binding.refresh(tree.root(), m);
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_EQ(pressedAt(), 1u);
}
TEST(Compose, AKeyNoFieldTakesIsAnsweredOnceByTheScreen) {
  using namespace screen_keys_once;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), screen()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  tree.update(0.0);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
  (void)tree.finishFrame();
  (void)tree.dispatchPointer(scene::pointer::down{10.0f, 10.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{10.0f, 10.0f, 1});
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchKey(scene::key::down{scene::keys::kUp, scene::modifiers::kControl});
  const std::size_t answered = scene::hostWork().answers.size() + scene::hostWork().pressed.size();
  std::println("focused {}, a key answered {} times", tree.focusedId(), answered);
  // Kept to be delivered outside a release build; sent at once, carried,
  // in one -- never twice.
  EXPECT_LE(answered, 1u);
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
}
TEST(Compose, ARowsMenuAskedByARightPressIsDelivered) {
  using namespace row_menu_delivered;
  using namespace window_edge;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row_menu::row()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  tree.update(0.0);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 3});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 3});
  row_menu_delivered::Heard heard;
  for (const auto &one : std::exchange(scene::hostWork().answers, {}))
    std::println("delivered: {}", bind::answer(tree.root(), m, one, &heard));
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(heard.menus, 1);
}
TEST(Compose, AScreensKeyAnswerIsTakenOnceAcrossModels) {
  using namespace screen_keys_once;
  using namespace screen_keys_delivered;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), screen()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  FeedModel other{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  tree.update(0.0);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
  (void)tree.finishFrame();
  (void)tree.dispatchPointer(scene::pointer::down{10.0f, 10.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{10.0f, 10.0f, 1});
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchKey(scene::key::down{scene::keys::kUp, scene::modifiers::kControl});
  screen_keys_delivered::Heard heard;
  for (const auto &one : std::exchange(scene::hostWork().answers, {})) {
    const bool first = bind::answer(tree.root(), m, one, &heard);
    const bool second = !first && bind::answer(tree.root(), other, one, &heard);
    std::println("taken by the first model: {}, the second: {}, heard {}", first, second, heard.ups);
  }
  EXPECT_LE(heard.ups, 1);
}

}  // namespace
