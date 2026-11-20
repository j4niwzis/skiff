// A list of rooms as mux makes them -- names and previews cut to the room
// they have, measured with a real font -- standing still, read again at
// every frame: no frame asked for and nothing laid out, at its top or away
// from it.
import std;
import skia;
import skiff.scene;
import skiff.nodes.scroll;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.box;
import skiff.paint;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace c = skiff::compose;
namespace scene = skiff::scene;
const auto window = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 600.0f);
using room_item = std::pair<int, int>;
std::vector<room_item> room_items(int count) {
  std::vector<room_item> out;
  for (int i = 0; i < count; ++i) out.emplace_back(i, i % 3 ? 72 : 62);
  return out;
}
struct room_menu { int at = 0; };
struct room_ask {
  std::optional<room_menu> operator()(auto&, float, float) const { return room_menu{1}; }
};
auto room_row(const room_item& item) {
  const std::string name = "A room with a name far too long for the sidebar it is in, number " + std::to_string(item.first);
  auto text = c::column(c::vbox(6.0f) | c::grow_x | c::content_y | c::with({.alignSelf = scene::align::kMiddle}),
      c::row(c::hbox(8.0f) | c::fill_x | c::content_y,
          c::styled({.grow = scene::axes::kX}, c::elided(skiff::nodes::Text(name, 13.0f, 0xff000000u, true))),
          skiff::nodes::Text("12:34", 13.0f, 0xff808080u)),
      c::row(c::hbox(4.0f) | c::fill_x | c::content_y,
          c::styled({.alignSelf = scene::align::kMiddle}, c::elided(skiff::nodes::Text("Someone", 13.0f, 0xff0000ffu))),
          c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                    c::elided(skiff::nodes::Text("and the last message said in it, which is long as well, longer than a line", 13.0f, 0xff808080u)))));
  auto clicked = c::onClick(item.first, c::row(c::hbox(12.0f) | c::fill_x | c::with({.height = static_cast<float>(item.second), .padding = {0.0f, 12.0f, 0.0f, 10.0f},
                                                                       .hoverBackground = 0xFFFF0000u, .selectedBackground = 0xFF0000FFu, .focusBackground = 0xFFFF0000u, .selected = false}),
      std::move(text),
      c::visible(item.first % 3 == 0, c::styled({.place = scene::anchor::kCentreLeft, .x = -8.0f, .width = 4.0f, .height = 38.0f, .cornerRadius = 2.0f},
                                                skiff::nodes::Box<>(0xFF00FF00u)))), "Room");
  return ::skiff::compose::build(c::onRightPressAt(room_ask{}, c::recorded(c::with_cursor(scene::cursor::arrow{},
      c::said(scene::SemanticRole{scene::semantic_role::list_item{}}, false, std::move(clicked))))));
}
inline constexpr auto room_make = [](const room_item& item) { return room_row(item); };
auto rooms_view(bool grows = false) {
  auto line = c::styled({.fillX = true, .autoSize = scene::axes::kY},
      c::memo_rows<int, int, room_item>(room_make, skiff::nodes::ColumnStack{}));
  auto scroll = c::virtual_rows<room_item>([](const room_item& item) { return item.second; },
      c::styled(grows ? scene::Spec{.fillX = true, .grow = scene::axes::kY} : scene::Spec{.fillX = true, .height = 600.0f}, c::scrolled(std::move(line))));
  return ::skiff::compose::build(std::move(scroll));
}
bool haveFont() {
  static const bool have = [] {
    auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
    auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
    if (!face) return false;
    skiff::paint::fonts().setPrimary(face);
    static skia::SkFont font(face);
    skiff::paint::defaultFont() = &font;
    return true;
  }();
  return have;
}
}

TEST(VirtualRowsFrames, RoomsStandingStillAskForNoFramesAtTheTopOrAwayFromIt) {
  if (!haveFont()) GTEST_SKIP() << "Needs a font to cut names with";
  for (const float at : {0.0f, 3000.5f, 3001.0f}) {
    scene::Scene tree(rooms_view());
    auto& list = tree.root();
    const auto items = room_items(300);
    double now = 0.0;
    const auto frame = [&] {
      now += 16.0;
      list.read(items);
      tree.update(now);
      tree.layoutIfNeeded(window);
      return tree.finishFrame();
    };
    for (int i = 0; i < 5; ++i)
      (void)frame();
    list.setCurrent(at);
    for (int i = 0; i < 30; ++i)
      (void)frame();
    const auto before = scene::walkCounts();
    int wanted = 0;
    for (int i = 0; i < 10; ++i)
      wanted += frame().fWantsAnotherFrame ? 1 : 0;
    EXPECT_EQ(wanted, 0) << "at " << at;
    EXPECT_EQ(scene::walkCounts().laidOut - before.laidOut, 0u) << "at " << at;
  }
}

// A room changing -- above the view, below it, in it, made again -- with the
// list away from its top: frames until it is shown as it is now, then none.
TEST(VirtualRowsFrames, ARoomMadeAgainAwayFromTheTopAsksForFramesOnlyUntilItIsShown) {
  if (!haveFont()) GTEST_SKIP() << "Needs a font to cut names with";
  for (const int changed : {36, 44, 52, 60}) {
    scene::Scene tree(rooms_view());
    auto& list = tree.root();
    auto items = room_items(300);
    double now = 0.0;
    const auto frame = [&] {
      now += 16.0;
      list.read(items);
      tree.update(now);
      tree.layoutIfNeeded(window);
      return tree.finishFrame();
    };
    for (int i = 0; i < 5; ++i)
      (void)frame();
    list.setCurrent(3000.5f);
    for (int i = 0; i < 30; ++i)
      (void)frame();
    items[static_cast<std::size_t>(changed)].first += 1000;
    for (int i = 0; i < 10; ++i)
      (void)frame();
    const auto before = scene::walkCounts();
    int wanted = 0;
    for (int i = 0; i < 10; ++i)
      wanted += frame().fWantsAnotherFrame ? 1 : 0;
    EXPECT_EQ(wanted, 0) << "room " << changed << " from " << list.fFrom << " to " << list.fTo;
    EXPECT_EQ(scene::walkCounts().laidOut - before.laidOut, 0u) << "room " << changed;
  }
}

namespace {
// mux's sidebar around it: a lane of faces beside a column -- its head, a
// search field, the folders, the rooms growing into what is left, the panes
// under them hidden, a server line -- the rooms kept per place they show.
using rooms_list = decltype(rooms_view());
auto sidebar() {
  auto band = [](float height) { return c::styled({.fillX = true, .height = height}, skiff::nodes::Box<>(0xFFEEEEEEu)); };
  auto rooms = c::column(c::vbox(0.0f) | c::fill_x | c::grow_y,
      c::remembered_scroll<int, rooms_list>(rooms_view(true)));
  auto rest = c::column(c::vbox(0.0f) | c::grow_x | c::fill_y,
      band(52.0f), band(40.0f), band(36.0f), std::move(rooms),
      c::visible(false, c::column(c::vbox(0.0f) | c::fill_x | c::grow_y, band(80.0f))), band(30.0f));
  auto lane = c::column(c::vbox(8.0f) | c::fill_y | c::with({.width = 64.0f}), c::styled({.width = 48.0f, .height = 48.0f}, skiff::nodes::Box<>(0xFF888888u)));
  return ::skiff::compose::build(c::row(c::hbox(0.0f, {.fill = true}), std::move(lane), std::move(rest)));
}
}

// The rooms in mux's sidebar, standing still away from the top, read again
// at every frame: no frame asked for, nothing laid out.
TEST(VirtualRowsFrames, RoomsInTheSidebarStandingStillAskForNoFrames) {
  if (!haveFont()) GTEST_SKIP() << "Needs a font to cut names with";
  for (const float at : {0.0f, 3000.5f, 3001.0f}) {
    scene::Scene tree(sidebar());
    rooms_list* found = nullptr;
    scene::forEachNode<rooms_list>(tree.root(), [&](rooms_list& one) { if (!found) found = &one; });
    ASSERT_NE(found, nullptr);
    auto& list = *found;
    const auto items = room_items(300);
    double now = 0.0;
    skia::SkBitmap pixels;
    (void)pixels.tryAllocN32Pixels(300, 600);
    skia::SkCanvas canvas(pixels);
    scene::detail::PlainPaint plain;
    // As the window runs it: scroll views copied, every frame drawn.
    scene::blitScrolling() = true;
    const auto frame = [&] {
      now += 16.0;
      list.read(items);
      tree.update(now);
      tree.layoutIfNeeded(window);
      auto done = tree.finishFrame();
      tree.draw(plain, &canvas);
      return done;
    };
    for (int i = 0; i < 5; ++i)
      (void)frame();
    list.setCurrent(at);
    for (int i = 0; i < 30; ++i)
      (void)frame();
    const float settled = list.current();
    const auto before = scene::walkCounts();
    int wanted = 0;
    for (int i = 0; i < 10; ++i)
      wanted += frame().fWantsAnotherFrame ? 1 : 0;
    EXPECT_EQ(wanted, 0) << "at " << at;
    EXPECT_EQ(scene::walkCounts().laidOut - before.laidOut, 0u) << "at " << at;
    EXPECT_FLOAT_EQ(list.current(), settled) << "at " << at;
    EXPECT_GT(list.bounds().height(), 300.0f) << "at " << at;
    scene::blitScrolling() = false;
  }
}

namespace {
// The rooms in a column sized by what it holds: laid out first in the room
// it might have, then in its own -- a provisional box, then the real one, at
// every layout of the frame.
auto measuredColumn(float tall) {
  auto band = c::styled({.fillX = true, .height = 52.0f}, skiff::nodes::Box<>(0xFFEEEEEEu));
  auto list = c::styled(tall > 0.0f ? scene::Spec{.fillX = true, .height = tall} : scene::Spec{.fillX = true, .grow = scene::axes::kY}, rooms_view(true));
  return ::skiff::compose::build(c::column(c::vbox(0.0f, {.fill = true}),
      c::column(c::vbox(0.0f) | c::fill_x | c::content_y, std::move(band), std::move(list))));
}
}

// Away from the top, in such a column: its rows the same at every layout --
// none made, no frame asked for, nothing laid out once it stands still.
TEST(VirtualRowsFrames, RoomsInAColumnSizedByThemStandingStillAskForNoFrames) {
  if (!haveFont()) GTEST_SKIP() << "Needs a font to cut names with";
  for (const float tall : {500.0f})
  for (const float at : {0.0f, 3000.5f}) {
    scene::Scene tree(measuredColumn(tall));
    rooms_list* found = nullptr;
    scene::forEachNode<rooms_list>(tree.root(), [&](rooms_list& one) { if (!found) found = &one; });
    ASSERT_NE(found, nullptr);
    auto& list = *found;
    const auto items = room_items(300);
    double now = 0.0;
    const auto frame = [&] {
      now += 16.0;
      list.read(items);
      tree.update(now);
      tree.layoutIfNeeded(window);
      return tree.finishFrame();
    };
    for (int i = 0; i < 5; ++i)
      (void)frame();
    list.setCurrent(at);
    for (int i = 0; i < 30; ++i)
      (void)frame();
    const auto from = list.fFrom;
    const auto before = scene::walkCounts();
    int wanted = 0;
    for (int i = 0; i < 10; ++i)
      wanted += frame().fWantsAnotherFrame ? 1 : 0;
    EXPECT_EQ(wanted, 0) << "at " << at;
    EXPECT_EQ(scene::walkCounts().laidOut - before.laidOut, 0u) << "at " << at;
    EXPECT_EQ(list.fFrom, from) << "at " << at;
    if (tall > 0.0f) EXPECT_FLOAT_EQ(list.bounds().height(), tall) << "at " << at;
  }
}

// Measured with no limit -- a box ten million tall, as a parent measuring
// what it holds lays it out -- then laid out in its own, at every frame:
// away from its top, its rows stay the ones where it is, none made again.
// (It chose the rows at its top in the tall box, its offset clamped to 0
// there, then the ones where it is: 2,075 nodes made a frame.)
TEST(VirtualRowsFrames, RoomsMeasuredWithoutALimitKeepTheirRows) {
  if (!haveFont()) GTEST_SKIP() << "Needs a font to cut names with";
  for (const float at : {0.0f, 3000.5f}) {
    auto list = rooms_view();
    const auto items = room_items(300);
    list.read(items);
    scene::layout(list, window);
    list.setCurrent(at);
    scene::layout(list, window);
    const auto from = list.fFrom, to = list.fTo;
    const auto first = std::get<0>(list.fChildren).fRows.front().fState.fId;
    for (int i = 0; i < 5; ++i) {
      list.read(items);
      scene::layout(list, skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 1.0e7f));
      scene::layout(list, window);
    }
    EXPECT_EQ(list.fFrom, from) << "at " << at;
    EXPECT_EQ(list.fTo, to) << "at " << at;
    EXPECT_EQ(std::get<0>(list.fChildren).fRows.front().fState.fId, first) << "at " << at;
    EXPECT_FLOAT_EQ(list.current(), at) << "at " << at;
  }
}
