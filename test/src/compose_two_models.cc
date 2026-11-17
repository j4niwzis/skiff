// Trees read from two models, as mux's are -- what the window shows (its
// settings' facts, what a list is told) and the chats: a mount made from one
// reading the other, a window told by one and its rows read from the other;
// and a scroll view's screens drawn ahead let go when a picture comes into a
// box that keeps its size.
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

#include "compose_common.inc"

namespace {

namespace two_models {
namespace c = skiff::compose;
struct Opened {
  bool on = false;
  friend bool operator==(const Opened &, const Opened &) = default;
};
struct Tally {
  int n = 0;
  friend bool operator==(const Tally &, const Tally &) = default;
};
struct ShowRoot {
  model::Tracked<Opened> opened;
};
struct ChatRoot {
  model::Tracked<Tally> tally;
};
using ShowModel = model::Model<ShowRoot, bind::NoReactions>;
using ChatModel = model::Model<ChatRoot, bind::NoReactions>;
struct Said {
  std::string operator()(int n) const { return std::format("{} asking", n); }
};
inline auto said_text() { return c::build(c::text_for<model::Field<&Tally::n>>(Said{}, nodes::Text("", 12.0f, 0u))); }
using SaidText = decltype(said_text());
struct MakeSaid {
  std::tuple<SaidText> operator()(int) const { return {said_text()}; }
};
struct OpenedKey {
  std::optional<int> operator()(const Opened &now) const { return now.on ? std::optional<int>(1) : std::nullopt; }
};
}  // namespace two_models

// mux's Join Requests page: mounted from the settings' facts, its rows read
// from the chats. The chats did not move as it was made; read only by the
// refresh that made it, its content stood empty until they did.
TEST(Compose, AMountMadeFromOneModelIsReadByAnotherAtOnce) {
  using namespace two_models;
  auto tree = c::build(c::column(c::vbox(0.0f), c::projected<Opened>(OpenedKey{}, c::mount<SaidText, int>(MakeSaid{}))));
  ShowModel shown{ShowRoot{}};
  ChatModel chats{ChatRoot{.tally = {Tally{4}}}};
  bind::Binding<ShowModel> bs;
  bind::Binding<ChatModel> bc;
  bs.refresh(tree, shown);
  bc.refresh(tree, chats);
  EXPECT_TRUE((all_made_as<SaidText>(tree).empty()));
  // Opened: made from the facts; the chats as they were.
  (void)shown.apply(model::edit(model::placeOf<Opened, ShowRoot>(), model::setTo(Opened{true})));
  bs.refresh(tree, shown);
  bc.refresh(tree, chats);
  const auto said = all_made_as<SaidText>(tree);
  ASSERT_EQ(said.size(), 1u);
  EXPECT_EQ(said.front()->text(), "4 asking");
  // And it follows them as they move.
  (void)chats.apply(model::edit(model::placeOf<Tally, ChatRoot>(), model::setTo(Tally{5})));
  bc.refresh(tree, chats);
  EXPECT_EQ(said.front()->text(), "5 asking");
}

namespace two_model_window {
namespace c = skiff::compose;
using window_edge::FeedTold;
struct Feed {
  int count = 300;
  int first = 0;
};
struct ShowRoot {
  model::Tracked<FeedTold> told;
};
struct ChatRoot {
  model::Tracked<Feed> feed;
};
using ShowModel = model::Model<ShowRoot, bind::NoReactions>;
using ChatModel = model::Model<ChatRoot, bind::NoReactions>;
using Item = std::tuple<std::string, int>;
struct Src {
  using Told = FeedTold;
  static std::size_t count(const ChatModel &m, const Told &) { return static_cast<std::size_t>(m.root().feed.fValue.count); }
  static std::string key(const ChatModel &m, const Told &, std::size_t i) {
    return std::format("m{}", m.root().feed.fValue.first + static_cast<int>(i));
  }
  static std::optional<std::size_t> index(const ChatModel &m, const Told &t, const std::string &id) {
    for (std::size_t i = 0; i < count(m, t); ++i)
      if (key(m, t, i) == id)
        return i;
    return std::nullopt;
  }
  static bool counts(const ChatModel &, const Told &, std::size_t) { return true; }
  static model::Revision revision(const ChatModel &m, const Told &) { return m.revision(); }
  static std::vector<Item> items(const ChatModel &m, const Told &t, std::size_t from, std::size_t to, const std::optional<std::string> &) {
    return std::views::iota(from, to) | std::views::transform([&](std::size_t i) { return Item{key(m, t, i), static_cast<int>(i)}; }) |
           std::ranges::to<std::vector>();
  }
};
inline std::set<std::string> &made_rows() {
  static std::set<std::string> kept;
  return kept;
}
struct Row : scene::Node {
  Row() { this->apply({.fillX = true, .height = 40.0f}); }
};
struct MakeRow {
  Row operator()(const Item &one) const {
    made_rows().insert(std::get<0>(one));
    return Row{};
  }
};
struct Tag {};
using Window = c::WindowFrom<Src, ChatModel, std::string, int, Row, MakeRow, c::NoLocate, Tag>;
struct Heard {
  int older = 0;
  void take(const c::ListEdge<Tag> &edge) { older += edge.older ? 1 : 0; }
  void take(const c::ListAimed<std::string, Tag> &) {}
  void take(const c::ListSeen<std::string, Tag> &) {}
};
}  // namespace two_model_window

// mux's timeline: told what to show by the window's model, its rows read
// from the chats. Scrolled up, it slides through what is held, then asks the
// program; what the program brings above is taken in, and asked past again.
TEST(Compose, AWindowToldByOneModelAndReadFromAnotherPagesBackAtItsTop) {
  using namespace two_model_window;
  made_rows().clear();
  Window window(MakeRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), SKIFF_NAMED(timeline_list, bind::Bound<FeedTold, Window>(std::move(window)))));
  ShowModel shown{ShowRoot{.told = {FeedTold{.of = 1}}}};
  ChatModel chats{ChatRoot{.feed = {Feed{}}}};
  bind::Binding<ShowModel> bs;
  bind::Binding<ChatModel> bc;
  bs.refresh(tree.root(), shown);
  bc.refresh(tree.root(), chats);
  scene::hostWork().pressed.clear();
  Heard heard;
  double t = 0.0;
  const auto frames = [&](double until) {
    for (; t < until; t += 16.0) {
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
      tree.update(t);
      for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
        (void)(bind::press(tree.root(), chats, way, &heard) || bind::press(tree.root(), shown, way, &heard));
      bs.refresh(tree.root(), shown);
      bc.refresh(tree.root(), chats);
    }
  };
  frames(600.0);
  EXPECT_FALSE(made_rows().contains("m0"));  // opened at its end: the newest 80
  int asked = 0;
  for (int round = 0; round < 40 && asked < 2; ++round) {
    for (int notch = 0; notch < 30; ++notch)
      (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
    const int before = heard.older;
    frames(t + 800.0);
    if (heard.older > before) {  // the program answers: fifty older come above
      ++asked;
      Feed now = chats.root().feed.fValue;
      now.first -= 50;
      now.count += 50;
      (void)chats.apply(model::edit(model::placeOf<Feed, ChatRoot>(), model::setTo(now)));
    }
  }
  std::println("asked {} times; m0 made {}, m-1 made {}", heard.older, made_rows().contains("m0"), made_rows().contains("m-1"));
  EXPECT_TRUE(made_rows().contains("m0"));
  EXPECT_GE(heard.older, 2);
  EXPECT_TRUE(made_rows().contains("m-1"));
}

namespace kept_box_picture {
namespace c = skiff::compose;
using namespace window_edge;
using waiting_picture::FromStore;
using waiting_picture::Store;
struct MakeKeptRow {
  compose::Held operator()(const Item &) const {
    nodes::Image<FromStore> picture({"k"});
    picture.keepBox();
    return compose::Held(c::build(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                            c::styled({.width = 32.0f, .height = 32.0f}, std::move(picture)))));
  }
};
using KeptWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeKeptRow, c::NoLocate, Tag>;
}  // namespace kept_box_picture

// A picture come into a box that keeps its size repaints it and lays nothing
// out: the scroll view it is in is told something under it was repainted --
// what it drew ahead of the view no longer shows it (mux's quote thumbnails,
// put back as placeholders by a scroll).
TEST(Compose, APictureComingIntoAKeptBoxTellsItsScrollViewItRepainted) {
  using namespace kept_box_picture;
  Store::image = nullptr;
  KeptWindow window(MakeKeptRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, KeptWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool laidOutAfter = false;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    const std::uint64_t before = scene::walkCounts().laidOut;
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    laidOutAfter = laidOutAfter || scene::walkCounts().laidOut != before;
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  auto &list = made_as<bind::Bound<FeedTold, KeptWindow>>(tree.root());
  list.fState.fBelowRedrawn = false;
  laidOutAfter = false;
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  Store::image = surface->makeImageSnapshot();
  Store::waiting.wake();
  for (double at = 600.0; at < 1000.0; at += 16.0)
    frame(at);
  std::println("laid out after it came: {}", laidOutAfter);
  EXPECT_TRUE(list.fState.fBelowRedrawn);
  Store::image = nullptr;
}
namespace recorded_picture {
namespace c = skiff::compose;
using namespace window_edge;
using waiting_picture::FromStore;
using waiting_picture::Store;
// Drawn: counted, so that a recording played back -- not walked -- shows.
struct Spy : scene::Node {
  static inline int draws = 0;
  Spy() { this->apply({.width = 10.0f, .height = 10.0f}); }
  template <class P> void drawSelf(P &, float) { ++draws; }
};
struct MakeRecordedRow {
  compose::Held operator()(const Item &) const {
    nodes::Image<FromStore> picture({"k"});
    picture.keepBox();
    // As mux's quote in a bubble: a masked, rounded row, its thumbnail a
    // fixed box, the whole row recorded and played back.
    return compose::Held(c::build(c::recorded(c::row(c::hbox(4.0f) | c::fill_x | c::content_y | c::with({.cornerRadius = 5.0f, .masking = true}),
                                                     c::styled({.width = 32.0f, .height = 32.0f}, std::move(picture)), Spy()))));
  }
};
using RecordedWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeRecordedRow, c::NoLocate, Tag>;
}  // namespace recorded_picture

// A picture come into a box that keeps its size, in a row recorded and
// played back (mux's quote thumbnail, since 16c94): the recording let go and
// the row drawn again -- kept, it was played back with its placeholder for
// good.
TEST(Compose, APictureComingIntoAKeptBoxInARecordedRowIsDrawn) {
  using namespace recorded_picture;
  Store::image = nullptr;
  RecordedWindow window(MakeRecordedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, RecordedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 1}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(300, 400));
  skia::SkCanvas canvas(pixels);
  scene::detail::PlainPaint plain;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
    tree.draw(plain, &canvas);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  const int before = Spy::draws;
  frame(600.0);
  frame(616.0);
  EXPECT_EQ(Spy::draws, before);  // played back, not walked
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  Store::image = surface->makeImageSnapshot();
  Store::waiting.wake();
  const int still = Spy::draws;
  for (double at = 632.0; at < 900.0; at += 16.0)
    frame(at);
  std::println("the row drawn again after its picture came: {} times", Spy::draws - still);
  EXPECT_GT(Spy::draws, still);
  Store::image = nullptr;
}
namespace rekeyed_across {
struct Item {
  std::string label;
  friend bool operator==(const Item &, const Item &) = default;
};
struct Pick {
  int key = 1;
  friend bool operator==(const Pick &, const Pick &) = default;
};
struct ItemsRoot {
  model::Keyed<int, Item> items;
};
struct PickRoot {
  model::Tracked<Pick> pick;
};
using ItemsModel = model::Model<ItemsRoot, bind::NoReactions>;
using PickModel = model::Model<PickRoot, bind::NoReactions>;
struct ItemLabel {
  std::string operator()(const Item &one) const { return one.label; }
};
inline auto item_text() { return skiff::compose::build(skiff::compose::text_for<Item>(ItemLabel{}, nodes::Text("", 13.0f, 0u))); }
using ItemText = decltype(item_text());
}  // namespace rekeyed_across

// A scope moved to another key by one model -- the settings' facts saying
// which room -- over a part of another -- the chats: read under its new key
// by that model's binding, though that model did not move (mux's Join
// Requests, under the room the settings are for).
TEST(Compose, AScopeRekeyedByOneModelIsReadFromAnotherThatDidNotMove) {
  namespace c = skiff::compose;
  using namespace rekeyed_across;
  auto tree = c::build(c::column(c::vbox(0.0f), c::rekeyed_by<Pick>([](const Pick &now) { return std::tuple(now.key); },
      c::scoped<Item>(c::handlers(), c::column(c::vbox(0.0f), item_text()), 1))));
  ItemsRoot values;
  values.items.put(1, Item{"first"});
  values.items.put(2, Item{"second"});
  ItemsModel items(std::move(values));
  PickModel picked{PickRoot{}};
  bind::Binding<PickModel> bp;
  bind::Binding<ItemsModel> bi;
  bp.refresh(tree, picked);
  bi.refresh(tree, items);
  const auto said = all_made_as<ItemText>(tree);
  ASSERT_EQ(said.size(), 1u);
  EXPECT_EQ(said.front()->text(), "first");
  (void)picked.apply(model::edit(model::placeOf<Pick, PickRoot>(), model::setTo(Pick{2})));
  bp.refresh(tree, picked);
  bi.refresh(tree, items);
  EXPECT_EQ(said.front()->text(), "second");
}
}  // namespace
