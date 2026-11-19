// A list of rooms scrolled away from its top, read again at every frame:
// its rows hovered, lit and repainted, clicked -- also once a room came above
// them all -- and with mux's own row over each.
import std;
import skia;
import skiff.scene;
import skiff.nodes.scroll;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.box;
import skiff.compose;
import gtest;

#include "gtest/gtest-macros.h"

namespace {
namespace c = skiff::compose;
namespace scene = skiff::scene;
const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 200.0f, 100.0f);
using virtual_item = std::pair<int, int>;
std::vector<virtual_item> virtual_items(int count) {
  std::vector<virtual_item> out;
  for (int i = 0; i < count; ++i) out.emplace_back(i, i % 2 ? 82 : 62);
  return out;
}
}

namespace {
inline constexpr auto pressed_make = [](const virtual_item& item) {
  return ::skiff::compose::build(c::recorded(c::onClick(item.first, c::column(c::vbox(0.0f) | c::fill_x | c::with({.height = static_cast<float>(item.second),
      .background = 0xFFFFFFFFu, .hoverBackground = 0xFFFF0000u})), "Room")));
};
auto pressed_view() {
  using rows = skiff::nodes::MemoRows<int, int,
      std::invoke_result_t<decltype(pressed_make), const virtual_item&>,
      std::remove_cv_t<decltype(pressed_make)>>;
  auto contents = rows(pressed_make);
  contents.apply({.fillX = true, .autoSize = scene::axes::kY});
  auto scroll = skiff::nodes::ScrollContainer(std::move(contents));
  scroll.apply({.fillX = true, .height = 100.0f});
  return ::skiff::compose::build(c::virtual_rows<virtual_item>([](const virtual_item& item) { return item.second; }, std::move(scroll)));
}
// A list of rooms away from its top, read again at each frame as its model
// tells it, and drawn.
template <class View> struct pressed_list_of {
  scene::Scene<View> tree;
  std::vector<virtual_item> items = virtual_items(300);
  skia::SkBitmap pixels;
  std::optional<skia::SkCanvas> canvas;
  scene::detail::PlainPaint plain;
  double now = 0.0;
  pressed_list_of(View view, float at) : tree(std::move(view)) {
    scene::hostWork().answers.clear();
    scene::hostWork().pressed.clear();
    (void)pixels.tryAllocN32Pixels(200, 100);
    canvas.emplace(pixels);
    tree.root().read(items);
    (void)frame();
    tree.root().setCurrent(at);
    for (int i = 0; i < 5; ++i)
      (void)frame();
  }
  ~pressed_list_of() {
    scene::hostWork().answers.clear();
    scene::hostWork().pressed.clear();
  }
  scene::FrameResult frame() {
    now += 16.0;
    tree.root().read(items);
    tree.update(now);
    tree.layoutIfNeeded(viewport);
    auto done = tree.finishFrame();
    tree.draw(plain, &*canvas);
    return done;
  }
  // The frames up to the next still one, their damage joined.
  skia::SkRect settle() {
    skia::SkRect all = skia::SkRect::MakeEmpty();
    for (int i = 0; i < 4; ++i) {
      const auto done = frame();
      all.join(done.fDamage);
    }
    return all;
  }
  auto& rows() { return std::get<0>(tree.root().fChildren).fRows; }
  const auto* hovered() {
    const auto hit = std::ranges::find_if(rows(), [](const auto& row) { return row.fRow.fState.hovered(); });
    return hit == rows().end() ? nullptr : &*hit;
  }
};
}

struct pressed_list : pressed_list_of<decltype(pressed_view())> {
  explicit pressed_list(float at) : pressed_list_of(pressed_view(), at) {}
};
// As mux's rooms: each row recorded, its cursor said, a list item, clicked,
// right pressed for its menu, its name a text, a strip beside some.
struct room_menu { int at = 0; };
struct room_ask {
  std::optional<room_menu> operator()(auto&, float, float) const { return room_menu{1}; }
};
inline auto room_row(const virtual_item& item) {
  skiff::nodes::Text name("room", 13.0f, 0xff000000u);
  name.apply({.width = 120.0f, .height = 20.0f});
  auto clicked = c::onClick(item.first, c::row(c::hbox(12.0f) | c::fill_x | c::with({.height = static_cast<float>(item.second), .padding = {0.0f, 12.0f, 0.0f, 10.0f},
                                                                       .hoverBackground = 0xFFFF0000u, .selectedBackground = 0xFF0000FFu, .focusBackground = 0xFFFF0000u, .selected = false}),
      c::column(c::vbox(0.0f) | c::content_x, std::move(name)),
      c::visible(item.first % 3 == 0, c::styled({.place = scene::anchor::kCentreLeft, .x = -8.0f, .width = 4.0f, .height = 38.0f, .cornerRadius = 2.0f},
                                                skiff::nodes::Box<>(0xFF00FF00u)))), "Room");
  return ::skiff::compose::build(c::onRightPressAt(room_ask{}, c::recorded(c::with_cursor(scene::cursor::arrow{},
      c::said(scene::SemanticRole{scene::semantic_role::list_item{}}, false, std::move(clicked))))));
}
inline constexpr auto room_make = [](const virtual_item& item) { return room_row(item); };
auto room_view() {
  auto line = c::styled({.fillX = true, .autoSize = scene::axes::kY},
      c::memo_rows<int, int, virtual_item>(room_make, skiff::nodes::ColumnStack{}));
  return ::skiff::compose::build(c::virtual_rows<virtual_item>([](const virtual_item& item) { return item.second; },
      c::styled({.fillX = true, .height = 100.0f}, c::scrolled(std::move(line)))));
}

// A row of a list of rooms away from its top, read again at every frame: a
// click on it is pressed, and is the row under the pointer.
TEST(Views, ARowOfAListAwayFromItsTopTakesItsClick) {
  for (const float at : {0.0f, 5000.5f}) {
    pressed_list list(at);
    // Rows of 62 and 82 alternate, 144 a pair: 5040 is row 70's top.
    list.tree.dispatchPointer(scene::pointer::move{20.0f, 50.0f});
    (void)list.settle();
    const auto* hit = list.hovered();
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->fKey, at == 0.0f ? 0 : 70);
    list.tree.dispatchPointer(scene::pointer::down{20.0f, 50.0f, 1});
    (void)list.frame();
    list.tree.dispatchPointer(scene::pointer::up{20.0f, 50.0f, 1});
    (void)list.settle();
    ASSERT_EQ(scene::hostWork().pressed.size(), 1u);
    EXPECT_FLOAT_EQ(list.tree.root().current(), at);
  }
}

// The pointer come onto a row there, and gone to the next: each row it
// lights or leaves recorded again and repainted where it is on the screen.
TEST(Views, ARowOfAListAwayFromItsTopLightsUnderThePointer) {
  // Two rows each: 0 and 1 at the top, 69 and 70 at 5000.
  for (const float at : {0.0f, 5000.5f}) {
    pressed_list list(at);
    (void)list.settle();
    const auto recordedAt = [&](float y) {
      for (auto& row : list.rows())
        if (row.fRow.fState.fBounds.fTop - at <= y && row.fRow.fState.fBounds.fBottom - at > y) return row.fRow.fState.fRecordedAt;
      return std::uint64_t{0};
    };
    const auto before = recordedAt(20.0f);
    list.tree.dispatchPointer(scene::pointer::move{20.0f, 20.0f});
    const auto onto = list.settle();
    // Recorded again, lit: not its old look played back.
    EXPECT_GT(recordedAt(20.0f), before) << "at " << at;
    EXPECT_TRUE(onto.contains(20.0f, 20.0f)) << "at " << at;
    list.tree.dispatchPointer(scene::pointer::move{20.0f, 70.0f});
    const auto along = list.settle();
    EXPECT_TRUE(along.contains(20.0f, 20.0f)) << "at " << at;
    EXPECT_TRUE(along.contains(20.0f, 70.0f)) << "at " << at;
    (void)list.settle();
    EXPECT_TRUE(list.settle().isEmpty()) << "at " << at;
  }
}

// A room come to the top of the list -- a message in it -- every row under
// it moved down: each row, and all in it, where it is shown; the row under
// the pointer lit and clicked there.
TEST(Views, RowsMovedDownByARowComeAboveTakeHoverAndClicksWhereTheyAre) {
  for (const float at : {0.0f, 5000.5f}) {
    pressed_list list(at);
    (void)list.settle();
    list.items.insert(list.items.begin(), virtual_item{1000, 62});
    (void)list.settle();
    for (auto& row : list.rows()) {
      const auto& own = row.fState.fBounds;
      const auto& inner = row.fRow.fState.fBounds;
      EXPECT_FLOAT_EQ(inner.fTop, own.fTop) << "row " << row.fKey << " at " << at;
      EXPECT_FLOAT_EQ(inner.fBottom, own.fBottom) << "row " << row.fKey << " at " << at;
    }
    const float shown = list.tree.root().current();
    list.tree.dispatchPointer(scene::pointer::move{20.0f, 20.0f});
    (void)list.settle();
    const auto* hit = list.hovered();
    ASSERT_NE(hit, nullptr) << "at " << at;
    EXPECT_LE(hit->fState.fBounds.fTop - shown, 20.0f) << "row " << hit->fKey << " at " << at;
    EXPECT_GT(hit->fState.fBounds.fBottom - shown, 20.0f) << "row " << hit->fKey << " at " << at;
    list.tree.dispatchPointer(scene::pointer::down{20.0f, 20.0f, 1});
    (void)list.frame();
    list.tree.dispatchPointer(scene::pointer::up{20.0f, 20.0f, 1});
    (void)list.settle();
    EXPECT_EQ(scene::hostWork().pressed.size(), 1u) << "at " << at;
  }
}

// The same, with mux's rooms -- each with its menu on a right press: hovered,
// lit and repainted; clicked, pressed -- away from the top as at it, and
// after a room came above them all. (Its menu hid every room's click.)
TEST(Views, RoomsAwayFromTheTopLightAndTakeClicks) {
  for (const float at : {0.0f, 5000.5f}) {
    pressed_list_of list(room_view(), at);
    for (const bool moved : {false, true}) {
      if (moved) {
        list.items.insert(list.items.begin(), virtual_item{1000 + static_cast<int>(at), 62});
        (void)list.settle();
      }
      (void)list.settle();
      // 86 apart, rows at most 82 tall: two rows, wherever the list stands.
      list.tree.dispatchPointer(scene::pointer::move{20.0f, 10.0f});
      EXPECT_TRUE(list.settle().contains(20.0f, 10.0f)) << "at " << at << " moved " << moved;
      list.tree.dispatchPointer(scene::pointer::move{20.0f, 96.0f});
      const auto along = list.settle();
      EXPECT_TRUE(along.contains(20.0f, 10.0f) && along.contains(20.0f, 96.0f)) << "at " << at << " moved " << moved;
      scene::hostWork().pressed.clear();
      list.tree.dispatchPointer(scene::pointer::down{20.0f, 96.0f, 1});
      (void)list.frame();
      list.tree.dispatchPointer(scene::pointer::up{20.0f, 96.0f, 1});
      (void)list.settle();
      EXPECT_EQ(scene::hostWork().pressed.size(), 1u) << "at " << at << " moved " << moved << " answers " << scene::hostWork().answers.size();
      EXPECT_TRUE(list.settle().isEmpty()) << "at " << at << " moved " << moved;
    }
  }
}
