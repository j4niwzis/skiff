// Windowed lists: asking for older and newer at an edge, rows arriving, swipes, timelines aimed at a row and coming to rest.
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

TEST(Compose, AWindowAtItsTopAsksForWhatIsOlder) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  std::println("heard after opening: {}", heard.older);
  // Scrolled up to its top, as by the wheel.
  for (int notch = 0; notch < 60; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 600.0; t < 2000.0; t += 16.0)
    frame(t);
  std::println("heard after scrolling up: {}", heard.older);
  EXPECT_GE(heard.older, 1);
}

// A list going on past what its source holds -- a chat a window back from
// its newest -- scrolled to its bottom: the next page asked for, as the top
// asks for what is older.
namespace window_goes_on {
using namespace window_edge;
struct GoingSrc : Src {
  static bool goes_on(const FeedModel &, const Told &) { return true; }
};
using GoingWindow = c::WindowFrom<GoingSrc, FeedModel, std::string, int, RowNode, MakeFeedRow, c::NoLocate, Tag>;
struct HeardBelow {
  int newer = 0;
  void take(const c::ListEdge<Tag> &edge) { newer += edge.older ? 0 : 1; }
  void take(const c::ListAimed<std::string, Tag> &) {}
  void take(const c::ListSeen<std::string, Tag> &) {}
};
}  // namespace window_goes_on
TEST(Compose, AWindowGoingOnAtItsBottomAsksForWhatIsNewer) {
  using namespace window_goes_on;
  GoingWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, GoingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  HeardBelow heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  // Up a little, then down to its bottom again, as by the wheel.
  for (int notch = 0; notch < 3; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 600.0; t < 1200.0; t += 16.0)
    frame(t);
  for (int notch = 0; notch < 10; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, -120.0f});
  for (double t = 1200.0; t < 2400.0; t += 16.0)
    frame(t);
  std::println("newer asked: {}", heard.newer);
  EXPECT_GE(heard.newer, 1);
}
TEST(Compose, APartNotInAScopeIsReadFromTheRoot) {
  using namespace read_from_root;
  said_now() = "?";
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), scoped_page()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  const auto frame = [&] {
    binding.refresh(tree.root(), m);
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 300.0f));
    (void)tree.finishFrame();
  };
  frame();
  const std::string before = said_now();
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 9})));
  frame();
  std::println("read in a scope from the root: {} then {}", before, said_now());
  EXPECT_EQ(before, "Expand");
  EXPECT_EQ(said_now(), "Collapse");
}
TEST(Compose, AFittedNamedViewIsFilledByItsContent) {
  using namespace fitted_named;
  float wide = 0;
  using box_t = decltype(box(nullptr));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::Fitted<NoRatio, box_t>(c::Fit{.width = 1100.0f, .height = 720.0f, .most = 430.0f}, NoRatio{}, box(&wide))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 600.0f));
  EXPECT_FLOAT_EQ(wide, 430.0f);
}
TEST(Compose, RowsComingInMovingAreSeenOnceStill) {
  using namespace window_arrive;
  ArrivingWindow window(MakeArriving{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  // Two more come.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  binding.refresh(tree.root(), m);
  for (double at = 600.0; at < 1600.0; at += 16.0)
    frame(at);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}


// A row that comes in moving, made after the frame's tick (as the bindings
// read a model that changed): the frame that lays it out asks for another,
// so that its coming plays rather than waiting for something else to wake
// the window.
TEST(Compose, AFrameAfterARowIsMadeAsksForAnother) {
  using namespace window_arrive;
  ArrivingWindow window(MakeArriving{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  for (double at = 0.0; at < 2000.0 && wants; at += 16.0) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    // What it answered, delivered as a program does after each frame.
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  }
  ASSERT_FALSE(wants) << "settled";
  // A message comes: read after the tick, as an event's change is.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 4})));
  tree.update(2100.0);
  binding.refresh(tree.root(), m);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
  EXPECT_TRUE(tree.finishFrame().fWantsAnotherFrame);
}
TEST(Compose, ASwipedRowFollowsTheFinger) {
  using namespace swipe_follow;
  auto row = c::swiped(48.0f, 72.0f, [] { return Answered{}; }, [](nodes::Text &, float) {}, nodes::Text("", 12.0f, 0u), ShiftProbe());
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(row)));
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f);
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  ShiftProbe *probe = scene::findNode<ShiftProbe>(tree.root());
  ASSERT_NE(probe, nullptr);
  (void)tree.dispatchPointer(scene::pointer::down{200.0f, 20.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::move{185.0f, 20.0f});
  frame(216.0);
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 20.0f});
  frame(232.0);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, -50.0f);
  (void)tree.dispatchPointer(scene::pointer::up{150.0f, 20.0f, 1});
  for (double at = 248.0; at < 800.0; at += 16.0)
    frame(at);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, 0.0f);
}

TEST(Compose, AWindowAimedAtARowSaysSoAndComesToRest) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                            model::setTo(FeedTold{.of = 1, .aim = {.key = std::string("m5"), .number = 1}})));
  binding.refresh(tree.root(), m);
  for (double t = 600.0; t < 4000.0; t += 16.0)
    frame(t);
  std::println("aimed heard: {}", heard.aimed);
  EXPECT_EQ(heard.aimed, 1);
}

// mux's timeline as it is put together: the window under a spanned_for,
// named, under with_way_down -- aimed at a row, it says so.
namespace timeline_like {
namespace c = skiff::compose;
using namespace window_edge;
struct Corner : scene::Node {
  Corner() { this->apply({.place = scene::anchor::kBottomRight, .width = 10.0f, .height = 10.0f}); }
};
struct Tell {
  bool operator()(bool, std::size_t) const { return true; }
};
inline auto timeline() {
  FeedWindow window(MakeFeedRow{});
  window.setCopiesOnScroll(false);
  window.apply({.fill = true});
  return SKIFF_NAMED(timeline_list, c::spanned_for<FeedTold>([](const FeedTold &) { return false; }, bind::Bound<FeedTold, FeedWindow>(std::move(window))));
}
}  // namespace timeline_like
struct HeardAll : window_edge::Heard {
  using Heard::take;
  void take(bool) {}
  void take(const compose::ListSpan<std::string, window_edge::Tag> &) {}
};
TEST(Compose, ATimelineAsMuxMakesItSaysItIsAimed) {
  using namespace timeline_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::projected<FeedTold>([](const FeedTold &) { return false; },
                                                     c::with_way_down(c::vbox(0.0f, {.fill = true}), timeline(), Corner(), Tell{}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 30}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  HeardAll heard;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                            model::setTo(FeedTold{.of = 1, .aim = {.key = std::string("m5"), .number = 1}})));
  binding.refresh(tree.root(), m);
  for (double t = 600.0; t < 4000.0; t += 16.0)
    frame(t);
  std::println("timeline aimed heard: {}", heard.aimed);
  EXPECT_EQ(heard.aimed, 1);
}

// Ctrl+Up in mux: at the end, the newest row aimed at as a bar opens under
// the list (the answer's) -- the view goes there and stays, not back and
// forth; and, the aim done, the top asks for what is older again.
TEST(Compose, AWindowAimedAsItsViewShrinksComesToRestOnce) {
  using namespace timeline_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::projected<FeedTold>([](const FeedTold &) { return false; },
                                                     c::with_way_down(c::vbox(0.0f, {.fill = true}), timeline(), Corner(), Tell{}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 30}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  HeardAll heard;
  float height = 400.0f;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, height));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  FeedWindow *list = scene::findNode<bind::Bound<FeedTold, FeedWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  // Back and forth: the offset's direction turned, within one aim.
  int turns = 0;
  const auto aim = [&](std::string key, std::uint64_t number, double from) {
    (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                              model::setTo(FeedTold{.of = 1, .aim = {.key = std::move(key), .number = number}})));
    binding.refresh(tree.root(), m);
    std::vector<float> offsets;
    for (double t = from; t < from + 1500.0; t += 16.0) {
      if (height > 350.0f)
        height -= 5.0f;
      frame(t);
      offsets.push_back(list->current());
    }
    // From the second step on: a jump there and the bottom then kept as
    // the bar opens is not a swing.
    for (std::size_t i = 3; i < offsets.size(); ++i)
      if ((offsets[i] - offsets[i - 1]) * (offsets[i - 1] - offsets[i - 2]) < -0.01f)
        ++turns;
    std::println("offsets: {}", offsets | std::views::take(16));
  };
  aim("m29", 1, 600.0);
  aim("m28", 2, 2100.0);
  // One in the middle, glided to as the bar opens again.
  height = 400.0f;
  for (double t = 3600.0; t < 4200.0; t += 16.0)
    frame(t);
  aim("m12", 3, 4200.0);
  // Near the end, glided to, as the bar opens: Ctrl+Up's.
  aim("m29", 4, 5700.0);
  height = 400.0f;
  for (double t = 7200.0; t < 7800.0; t += 16.0)
    frame(t);
  aim("m25", 5, 7800.0);
  // Further up, still glided to: a long glide's steps are large.
  aim("m29", 6, 9300.0);
  height = 400.0f;
  for (double t = 10800.0; t < 11400.0; t += 16.0)
    frame(t);
  aim("m9", 7, 11400.0);
  // One way: aimed from where the row is on the screen, the target was off
  // by each frame's step of the glide, and the view swung about the row.
  EXPECT_EQ(turns, 0);
  EXPECT_EQ(heard.aimed, 7);
  EXPECT_FALSE(list->settling());
}

// History paged in above a window at the list's start: made, and the next
// page asked for -- held by its first key, the window stayed where that key
// went and never asked again.
TEST(Compose, AWindowAtTheStartTakesInWhatComesAbove) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  for (int notch = 0; notch < 60; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 600.0; t < 2000.0; t += 16.0)
    frame(t);
  ASSERT_GE(heard.older, 1);
  const int asked = heard.older;
  // A page of the history come, above what is held.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 50, .first = -20})));
  binding.refresh(tree.root(), m);
  for (double t = 2000.0; t < 3000.0; t += 16.0)
    frame(t);
  auto *list = scene::findNode<bind::Bound<FeedTold, FeedWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  EXPECT_NE(list->row("m-20"), nullptr);
  // Scrolled up to it: the next page asked for.
  for (int notch = 0; notch < 60; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 3000.0; t < 4500.0; t += 16.0)
    frame(t);
  EXPECT_GT(heard.older, asked);
}

// Rows measured taller once laid out -- a message's text, its picture --
// as the view glides to one: still one way, not past it and back.
namespace window_grow {
namespace c = skiff::compose;
using namespace window_edge;
struct GrowingRow : scene::Node {
  GrowingRow() { this->apply({.fillX = true, .height = 40.0f}); }
  int fLaid = 0;
  [[nodiscard]] bool wantsTick() const { return fLaid < 3; }
  [[nodiscard]] bool settling() const { return fLaid < 3; }
  void update(double) {
    if (fLaid >= 3 || this->bounds().isEmpty())
      return;
    if (++fLaid == 3) {
      this->apply({.fillX = true, .height = 70.0f});
      this->invalidateLayout();
    }
  }
};
struct MakeGrowing {
  GrowingRow operator()(const Item &) const { return GrowingRow{}; }
};
using GrowWindow = c::WindowFrom<Src, FeedModel, std::string, int, GrowingRow, MakeGrowing, c::NoLocate, Tag>;
}  // namespace window_grow
TEST(Compose, AWindowGlidingToARowAsRowsGrowGoesOneWay) {
  using namespace window_grow;
  GrowWindow window(MakeGrowing{});
  window.setCopiesOnScroll(false);
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, GrowWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 60}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  float height = 400.0f;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, height));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  auto *list = scene::findNode<bind::Bound<FeedTold, GrowWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  std::vector<float> offsets;
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                            model::setTo(FeedTold{.of = 1, .aim = {.key = std::string("m50"), .number = 1}})));
  binding.refresh(tree.root(), m);
  for (double t = 600.0; t < 2600.0; t += 16.0) {
    if (height > 350.0f)
      height -= 5.0f;
    frame(t);
    offsets.push_back(list->current());
  }
  int turns = 0;
  for (std::size_t i = 2; i < offsets.size(); ++i)
    if ((offsets[i] - offsets[i - 1]) * (offsets[i - 1] - offsets[i - 2]) < -0.01f)
      ++turns;
  std::println("growing offsets: {}", offsets | std::views::take(40));
  EXPECT_LE(turns, 1);
  EXPECT_EQ(heard.aimed, 1);
  EXPECT_FALSE(list->settling());
}

}  // namespace
