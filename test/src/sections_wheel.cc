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

// A virtual Sections whose heights reckoned before its sections are made
// fall short of what they are laid out at -- as the emoji panel's did -- and
// a wheel turned down under it: the view goes down and never back up. Before
// the sections were measured as laid out, the list's extent was the
// reckoning's, and a wheel's glide, clamped to it every frame, was pulled
// back up the list.
namespace {
using namespace skiff;
using namespace skiff::compose;

// A section 200 high, whatever was reckoned; which one it is kept.
struct Block : scene::Node {
  int index = 0;
  explicit Block(int n) : index(n) { this->apply({.fillX = true, .height = 200.0f}); }
};

// Wheeled down 120 notches with each section reckoned at `reckoned`: where
// in the sections, as laid out, the top of the view is after each frame --
// the section there and how far into it -- and where the list's offset
// ends.
struct wheeled {
  float lowest_step = 0.0f;  // the most the content in view went back up in a frame
  float last = 0.0f;         // the content position at the top of the view at the end
};
wheeled wheel_down(float reckoned) {
  const auto tab = [](int n) { return nodes::Text(std::to_string(n), 13.0f, 0); };
  auto made = sections<int>(vbox(4.0f, {.fill = true}), vbox(0.0f), tab, [](int n) { return Block(n); },
                            wrapped<int>(vbox(0.0f), hbox(4.0f)), [](std::size_t) { return scene::Taken{}; },
                            [reckoned](int, float) { return reckoned; });
  using List = decltype(made);
  scene::Scene tree(column(vbox(0.0f, {.fill = true}), std::move(made)));
  List *found = nullptr;
  scene::forEachNode<List>(tree.root(), [&](List &one) { found = &one; });
  EXPECT_NE(found, nullptr);
  List &list = *found;
  std::vector<int> all(60);
  std::ranges::iota(all, 0);
  list.read(SectionsShown<int>{all, {}, true});
  const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f);
  double now = 0.0;
  const auto frame = [&] {
    now += 16.0;
    tree.update(now);
    tree.layoutIfNeeded(viewport);
    (void)tree.finishFrame();
  };
  // What is at the top of the view, as laid out: the section there, by its
  // index, and how far into it -- the section's own 200 a piece.
  const auto seen = [&] {
    const float offset = list.fList.current();
    const float top = list.fList.bounds().fTop;
    for (auto &part : list.parts()) {
      const float from = part.bounds().fTop - top, to = part.bounds().fBottom - top;
      if (offset >= from && offset < to)
        return static_cast<float>(part.index) * 200.0f + (offset - from);
    }
    return -1.0f;
  };
  for (int i = 0; i < 5; ++i)
    frame();
  wheeled out;
  out.last = seen();
  for (int notch = 0; notch < 120; ++notch) {
    (void)list.fList.onScroll(-1.0f);  // a notch down
    for (int i = 0; i < 6; ++i) {
      frame();
      const float now_at = seen();
      if (now_at >= 0.0f) {
        out.lowest_step = std::min(out.lowest_step, now_at - out.last);
        out.last = now_at;
      }
    }
  }
  return out;
}

TEST(SectionsWheel, AWheelDownGoesDownThroughSectionsReckonedShort) {
  const wheeled got = wheel_down(100.0f);
  std::println("reckoned 100 of 200: at {} after 120 notches, the content in view went up by {} at most", got.last, -got.lowest_step);
  EXPECT_GE(got.lowest_step, -0.5f) << "the content in view went back up while the wheel went down";
  EXPECT_GT(got.last, 7000.0f) << "the wheel stopped short of where it was turned to";
}
TEST(SectionsWheel, AWheelDownGoesDownThroughSectionsReckonedLong) {
  const wheeled got = wheel_down(300.0f);
  std::println("reckoned 300 of 200: at {} after 120 notches, the content in view went up by {} at most", got.last, -got.lowest_step);
  EXPECT_GE(got.lowest_step, -0.5f) << "the content in view went back up while the wheel went down";
  EXPECT_GT(got.last, 7000.0f) << "the wheel stopped short of where it was turned to";
}

// As mux's emoji panel makes its groups (cell_section): a header 32 high
// over a wrapping flow of cells 37 square that light up under the pointer,
// each group's height reckoned as mux reckons it; the wheel turned through
// the scene, at a pointer resting over the cells, as a window gives it.
inline constexpr float kCell = 37.0f;
inline auto cell() {
  return onClick(scene::Taken{}, styled({.width = kCell, .height = kCell, .hoverBackground = 1u}, nodes::Text("x", 13.0f, 0)));
}
using Cell = decltype(cell());
inline auto group(int count) {
  return column(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                row(hbox(8.0f, {.fillX = true, .height = 32.0f, .padding = {0.0f, 7.0f, 0.0f, 7.0f}}),
                    styled({.grow = scene::axes::kX}, nodes::Text(std::to_string(count), 13.0f, 0))),
                styled({.fillX = true, .autoSize = scene::axes::kY},
                       nodes::Flow<std::vector<Cell>>({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true},
                                                      std::views::iota(0, count) | std::views::transform([](int) { return cell(); }) |
                                                          std::ranges::to<std::vector>())));
}
TEST(SectionsWheel, AnEmojiPanelsGroupsGoDownUnderAWheelThroughTheScene) {
  const auto tab = [](int n) { return nodes::Text(std::to_string(n), 13.0f, 0); };
  auto made = sections<int>(vbox(4.0f, {.fill = true}), vbox(0.0f), tab, [](int n) { return group(n); },
                            wrapped<int>(vbox(0.0f), hbox(4.0f)), [](std::size_t) { return scene::Taken{}; },
                            [](int count, float width) {
                              const auto across = std::max<std::size_t>(1, static_cast<std::size_t>(width / kCell));
                              const std::size_t rows = (static_cast<std::size_t>(count) + across - 1) / across;
                              return 32.0f + static_cast<float>(rows) * kCell;
                            });
  using List = decltype(made);
  scene::Scene tree(column(vbox(0.0f, {.fill = true}), std::move(made)));
  List *found = nullptr;
  scene::forEachNode<List>(tree.root(), [&](List &one) { found = &one; });
  ASSERT_NE(found, nullptr);
  List &list = *found;
  // Groups as an emoji panel has them: recent, the categories, a few packs.
  const std::vector<int> groups{30, 460, 400, 150, 130, 120, 250, 300, 260, 40, 80, 60, 120};
  list.read(SectionsShown<int>{groups, groups, true});
  const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 345.0f, 400.0f);
  double now = 0.0;
  const auto frame = [&] {
    now += 16.0;
    tree.update(now);
    tree.layoutIfNeeded(viewport);
    (void)tree.finishFrame();
  };
  for (int i = 0; i < 5; ++i)
    frame();
  (void)tree.dispatchPointer(scene::pointer::move{170.0f, 200.0f});
  float last = list.fList.current(), lowest = 0.0f;
  for (int notch = 0; notch < 200; ++notch) {
    (void)tree.dispatchPointer(scene::pointer::scroll{170.0f, 200.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      frame();
      lowest = std::min(lowest, list.fList.current() - last);
      last = list.fList.current();
    }
  }
  std::println("emoji-like groups: offset {} after 200 notches; the most it went back up in a frame {}", last, -lowest);
  EXPECT_GE(lowest, -0.5f);
  EXPECT_GT(last, 6000.0f);
}

// And as mux holds that panel (emoji_kept.cc, with_preview): each cell in a
// rest timer that, after 450 ms under the pointer, tells the page what it
// shows large; the page's own state, read by an overlay mounted over it.
struct Previewed {
  int what = 0;
  friend bool operator==(const Previewed &, const Previewed &) = default;
};
struct PreviewShown {
  std::optional<Previewed> now;
};
struct PreviewRested {
  Previewed what;
  bool on = false;
};
struct PreviewEvents {
  auto on(const PreviewRested &rested, const PreviewShown &shown) const {
    PreviewShown next = shown;
    if (rested.on)
      next.now = rested.what;
    else if (next.now == rested.what)
      next.now.reset();
    return model::over<PreviewShown>(model::setTo(std::move(next)));
  }
};
struct RestsOn {
  using Answer = PreviewRested;
  Previewed what;
  PreviewRested operator()(bool on) const { return {what, on}; }
};
inline auto resting_cell(int n) {
  return resting(450.0, RestsOn{{n}}, onClick(scene::Taken{}, styled({.width = kCell, .height = kCell, .hoverBackground = 1u}, nodes::Text("x", 13.0f, 0))));
}
using RestingCell = decltype(resting_cell(0));
inline auto resting_group(int count) {
  return column(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                row(hbox(8.0f, {.fillX = true, .height = 32.0f, .padding = {0.0f, 7.0f, 0.0f, 7.0f}}),
                    styled({.grow = scene::axes::kX}, nodes::Text(std::to_string(count), 13.0f, 0))),
                styled({.fillX = true, .autoSize = scene::axes::kY},
                       nodes::Flow<std::vector<RestingCell>>({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true},
                                                             std::views::iota(0, count) | std::views::transform([](int at) { return resting_cell(at); }) |
                                                                 std::ranges::to<std::vector>())));
}
inline auto preview_of(const Previewed &one) {
  return column(vbox(0.0f, {.place = scene::anchor::kCentre, .autoSize = scene::axes::kBoth, .padding = {16.0f, 16.0f, 16.0f, 16.0f}}),
                nodes::Text(std::to_string(one.what), 120.0f, 0));
}
using PreviewOf = decltype(preview_of(Previewed{}));
TEST(SectionsWheel, AnEmojiPanelWithItsPreviewGoesDownUnderAWheel) {
  const auto tab = [](int n) { return nodes::Text(std::to_string(n), 13.0f, 0); };
  auto made = sections<int>(vbox(4.0f, {.fill = true}), vbox(0.0f), tab, [](int n) { return resting_group(n); },
                            wrapped<int>(vbox(0.0f), hbox(4.0f)), [](std::size_t) { return scene::Taken{}; },
                            [](int count, float width) {
                              const auto across = std::max<std::size_t>(1, static_cast<std::size_t>(width / kCell));
                              const std::size_t rows = (static_cast<std::size_t>(count) + across - 1) / across;
                              return 32.0f + static_cast<float>(rows) * kCell;
                            });
  using List = decltype(made);
  scene::Scene tree(local<PreviewShown>(PreviewEvents{},
      column(vbox(0.0f, {.fill = true}),
             styled({.fill = true}, std::move(made)),
             bound<model::Field<&PreviewShown::now>>(mount<PreviewOf, Previewed>(
                 [](const Previewed &one) { return std::tuple{preview_of(one)}; }, {.place = scene::anchor::kTopLeft, .fill = true})))));
  List *found = nullptr;
  scene::forEachNode<List>(tree.root(), [&](List &one) { found = &one; });
  ASSERT_NE(found, nullptr);
  List &list = *found;
  const std::vector<int> groups{30, 460, 400, 150, 130, 120, 250, 300, 260, 40, 80, 60, 120};
  list.read(SectionsShown<int>{groups, groups, true});
  const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 345.0f, 400.0f);
  double now = 0.0;
  const auto frame = [&] {
    now += 16.0;
    tree.update(now);
    tree.layoutIfNeeded(viewport);
    (void)tree.finishFrame();
  };
  for (int i = 0; i < 5; ++i)
    frame();
  (void)tree.dispatchPointer(scene::pointer::move{170.0f, 200.0f});
  // Rested long enough for the preview first, as a hand on a mouse does.
  for (int i = 0; i < 40; ++i)
    frame();
  float last = list.fList.current(), lowest = 0.0f;
  for (int notch = 0; notch < 200; ++notch) {
    (void)tree.dispatchPointer(scene::pointer::scroll{170.0f, 200.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      frame();
      lowest = std::min(lowest, list.fList.current() - last);
      last = list.fList.current();
    }
  }
  std::println("with the preview: offset {} after 200 notches; the most it went back up in a frame {}", last, -lowest);
  EXPECT_GE(lowest, -0.5f);
  EXPECT_GT(last, 6000.0f);
}

// Read again while it is wheeled down, with one group a cell longer each
// time -- what a program's state does as it changes under the panel (an
// emoji used, a pack come by the sync): browsing still, the view stays
// where the reader has it, not glided back to the top.
TEST(SectionsWheel, ReadAgainWithAGroupChangedWhileBrowsingKeepsTheViewGoingDown) {
  const auto tab = [](int n) { return nodes::Text(std::to_string(n), 13.0f, 0); };
  auto made = sections<int>(vbox(4.0f, {.fill = true}), vbox(0.0f), tab, [](int n) { return group(n); },
                            wrapped<int>(vbox(0.0f), hbox(4.0f)), [](std::size_t) { return scene::Taken{}; },
                            [](int count, float width) {
                              const auto across = std::max<std::size_t>(1, static_cast<std::size_t>(width / kCell));
                              const std::size_t rows = (static_cast<std::size_t>(count) + across - 1) / across;
                              return 32.0f + static_cast<float>(rows) * kCell;
                            });
  using List = decltype(made);
  scene::Scene tree(column(vbox(0.0f, {.fill = true}), std::move(made)));
  List *found = nullptr;
  scene::forEachNode<List>(tree.root(), [&](List &one) { found = &one; });
  ASSERT_NE(found, nullptr);
  List &list = *found;
  std::vector<int> groups{30, 460, 400, 150, 130, 120, 250, 300, 260, 40, 80, 60, 120};
  list.read(SectionsShown<int>{groups, groups, true});
  const auto viewport = skia::SkRect::MakeXYWH(0.0f, 0.0f, 345.0f, 400.0f);
  double now = 0.0;
  const auto frame = [&] {
    now += 16.0;
    tree.update(now);
    tree.layoutIfNeeded(viewport);
    (void)tree.finishFrame();
  };
  for (int i = 0; i < 5; ++i)
    frame();
  (void)tree.dispatchPointer(scene::pointer::move{170.0f, 200.0f});
  float last = list.fList.current(), lowest = 0.0f;
  for (int notch = 0; notch < 100; ++notch) {
    (void)tree.dispatchPointer(scene::pointer::scroll{170.0f, 200.0f, 0.0f, -1.0f});
    for (int i = 0; i < 6; ++i) {
      if (i == 3) {
        groups[0] += 9;  // a row more in the first group, every notch
        list.read(SectionsShown<int>{groups, groups, true});
      }
      frame();
      lowest = std::min(lowest, list.fList.current() - last);
      last = list.fList.current();
    }
  }
  std::println("read again while browsing: offset {} after 100 notches; the most it went back up in a frame {}", last, -lowest);
  EXPECT_GE(lowest, -0.5f) << "the view went back up while the wheel went down";
  EXPECT_GT(last, 3000.0f);
}
}  // namespace
