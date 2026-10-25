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
}  // namespace
