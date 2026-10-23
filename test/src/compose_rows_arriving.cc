// Pictures and rows as mux makes them: coming in while the pointer moves, faces out of a row's flow, hidden rows, held content.
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

TEST(Compose, APictureAsMuxMakesItIsFilledByItsImages) {
  using namespace picture_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::onClick(Open{}, fitted_t(c::Fit{.width = 1100.0f, .height = 720.0f, .most = 430.0f, .least = 100.0f}, NoRatio{}, picture_box()))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 600.0f));
  // The preview and the picture, each found once whichever way it is asked
  // for: the marked picture is a ShownImage too.
  std::set<const scene::Node *> seen;
  const auto filled = [&](auto &one) {
    const auto box = one.bounds();
    std::println("image {}: {} x {}", seen.size(), box.width(), box.height());
    EXPECT_FLOAT_EQ(box.width(), 430.0f);
    EXPECT_FLOAT_EQ(box.height(), 281.0f);
    seen.insert(static_cast<const scene::Node *>(&one));
  };
  scene::forEachNode<ShownImage>(tree.root(), filled);
  scene::forEachNode<c::Marked<PictureTag, ShownImage>>(tree.root(), filled);
  EXPECT_EQ(seen.size(), 2u);
}

// Rows come in moving, and the pointer moves before the next frame: the
// restyle after its event walks the new rows without ticking them. Taken
// as seen there, they were never ticked -- in no ticking set, their fade-in
// held at its start, a gap in the list where they are.
TEST(Compose, RowsComingInAsThePointerMovesAreTicked) {
  using namespace window_arrive;
  ArrivingWindow window(MakeArriving{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  binding.refresh(tree.root(), m);
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 170.0f});
  (void)tree.dispatchPointer(scene::pointer::move{151.0f, 185.0f});
  // And restyled whole, as a hover over a styled row's parent restyles all
  // under it.
  tree.setStyleSheet<window_arrive::Plain>();
  for (double at = 600.0; at < 1600.0; at += 16.0)
    frame(at);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}

// mux's message row: the avatar hangs at the row's bottom left, out of its
// flow -- the row as tall as its bubble, the avatar not making it taller.
namespace face_row {
namespace c = skiff::compose;
struct Face : scene::Node {
  Face() { this->apply({.width = 40.0f, .height = 40.0f}); }
};
struct Bubble : scene::Node {
  Bubble() { this->apply({.width = 100.0f, .height = 20.0f}); }
};
struct OpenFace {
  int operator()() const { return 1; }
};
inline auto row() {
  auto face = c::visible(true, c::styled({.place = scene::anchor::kBottomLeft, .x = -48.0f, .y = -1.0f, .alpha = 1.0f},
                                         c::onPress(OpenFace{}, Face(), "name")));
  return c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {1.0f, 0.0f, 1.0f, 48.0f}}),
                             nodes::justify::start{}),
                std::move(face), compose::Held(Bubble()));
}
}  // namespace face_row
TEST(Compose, AFaceOutOfARowsFlowLeavesItAsTallAsItsBubble) {
  using namespace face_row;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row())));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  std::print("{}", scene::laidOutText(tree.root()));
  const Bubble *bubble = scene::findNode<Bubble>(tree.root());
  ASSERT_NE(bubble, nullptr);
  EXPECT_FLOAT_EQ(bubble->bounds().fTop, 1.0f);
  EXPECT_FLOAT_EQ(bubble->bounds().fLeft, 48.0f);
  int faces = 0;
  scene::forEachNode<c::OnPress<OpenFace, c::part_t<Face>>>(tree.root(), [&](auto &face) {
    ++faces;
    EXPECT_FLOAT_EQ(face.bounds().fBottom, bubble->bounds().fBottom - 1.0f);
    EXPECT_FLOAT_EQ(face.bounds().fLeft, 0.0f);
  });
  EXPECT_EQ(faces, 1);
}
TEST(Compose, AHiddenRowWrappedTakesNoRoom) {
  using namespace hidden_row;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row(true), row(false), row(true))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
  std::print("{}", scene::laidOutText(tree.root()));
  std::vector<float> tops;
  scene::forEachNode<Line>(tree.root(), [&](Line &one) { tops.push_back(one.bounds().fTop); });
  ASSERT_EQ(tops.size(), 3u);
  EXPECT_FLOAT_EQ(tops[2], 40.0f);
}

// As mux makes a row that comes in: its fade-in around a node that is held
// (held_off_for, erased outside a release build) -- the fade a wrapper over
// held content, ticked as itself all the same, and the row seen whole.
namespace window_arrive_held {
namespace c = skiff::compose;
using namespace window_edge;
struct MakeArrivingHeld {
  compose::Held operator()(const Item &) const {
    return compose::Held(c::appearing(true, compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), RowNode{}))));
  }
};
using ArrivingHeldWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeArrivingHeld, c::NoLocate, Tag>;
}  // namespace window_arrive_held
TEST(Compose, RowsComingInAroundHeldContentAreSeenOnceStill) {
  using namespace window_arrive_held;
  ArrivingHeldWindow window(MakeArrivingHeld{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingHeldWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  binding.refresh(tree.root(), m);
  // Only the frames the window asks for, as a window at rest makes them:
  // the first, woken by what came.
  wants = true;
  for (double at = 600.0; at < 1600.0 && wants; at += 16.0)
    frame(at);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}

// A row that comes in as mux makes its message rows: named, washed for the
// marks it reads, fading in around what is held off while rows are being
// selected, that around held content -- seen whole once still, with only
// the frames the window asks for.
namespace window_arrive_mux {
namespace c = skiff::compose;
using namespace window_edge;
inline auto mux_row(bool arrives) {
  auto row = compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), RowNode{}));
  return SKIFF_NAMED(message_row, c::washed_for<FeedTold>(0xff3080ffu, 56.0f, 80.0f, [](const FeedTold &) { return c::Wash{}; },
      c::appearing(arrives, c::held_off_for<FeedTold>([](const FeedTold &) { return false; }, std::move(row)))));
}
using mux_row_t = decltype(mux_row(false));
struct MakeMuxRow {
  mux_row_t operator()(const Item &) const { return mux_row(true); }
};
using MuxRowWindow = c::WindowFrom<Src, FeedModel, std::string, int, mux_row_t, MakeMuxRow, c::NoLocate, Tag>;
}  // namespace window_arrive_mux
TEST(Compose, RowsComingInAsMuxMakesThemAreSeenOnceStill) {
  using namespace window_arrive_mux;
  MuxRowWindow window(MakeMuxRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, MuxRowWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  wants = true;
  int frames = 0;
  for (double at = 600.0; at < 1600.0 && wants; at += 16.0, ++frames)
    frame(at);
  std::println("frames after it came: {}", frames);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}

}  // namespace
