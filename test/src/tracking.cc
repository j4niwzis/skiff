import std;
import splice;
import gtest;
import skia;
import skiff.nodes;
import skiff.paint;
import skiff.scene;

#include "gtest/gtest-macros.h"

// What moves, shows or hides a node apart from its layout, tracked by the
// scene itself: where it was and where it is repainted, its parent laid out
// again, it drawn wherever its own area is repainted, it shown or hidden as
// its holder is hovered. Each was once left to whoever moved it to say, and
// what nobody said stayed on the screen as it had been.

namespace {

using namespace skiff::scene;
using skiff::nodes::Box;
using skiff::nodes::ScrollContainer;
using skiff::nodes::Stack;

const auto kView = skia::SkRect::MakeWH(200.0f, 120.0f);
inline constexpr skia::SkColor kFill = 0xff405060;

// A node that counts how often it is drawn.
struct DrawProbe : Node {
  int fDraws = 0;
  void drawSelf(skia::SkCanvas *, float) { ++fDraws; }
};

struct OneBox : Node {
  struct parts_t {
    Box<> box = make<Box>({.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f}, kFill);
  } parts;
};

TEST(Tracking, SetShiftRepaintsWhereItWasAndWhereItIs) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();

  scene.root().parts.box.fState.setShift(100.0f, 50.0f);
  const skia::SkRect damage = scene.finishFrame().fDamage;
  EXPECT_TRUE(damage.contains(skia::SkRect::MakeXYWH(10.0f, 10.0f, 40.0f, 20.0f)));
  EXPECT_TRUE(damage.contains(skia::SkRect::MakeXYWH(110.0f, 60.0f, 40.0f, 20.0f)));
}

TEST(Tracking, SetShiftToWhereItIsRepaintsNothing) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  scene.root().parts.box.fState.setShift(5.0f, 5.0f);
  (void)scene.finishFrame();

  scene.root().parts.box.fState.setShift(5.0f, 5.0f);
  EXPECT_TRUE(scene.finishFrame().fDamage.isEmpty());
}

TEST(Tracking, SetShiftQuietlyRepaintsNothing) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();

  scene.root().parts.box.fState.setShiftQuietly(30.0f, 0.0f);
  EXPECT_TRUE(scene.finishFrame().fDamage.isEmpty());
}

TEST(Tracking, ShownBoundsAreTheBoundsMovedByTheShift) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  auto &box = scene.root().parts.box;
  box.fState.setShift(7.0f, -3.0f);
  EXPECT_EQ(box.shownBounds(), box.bounds().makeOffset(7.0f, -3.0f));
}

struct Column : Stack {
  struct parts_t {
    Box<> first = make<Box>({.width = 40.0f, .height = 20.0f}, kFill);
    Box<> second = make<Box>({.width = 40.0f, .height = 20.0f}, kFill);
  } parts;
};

TEST(Tracking, SetOutOfFlowLaysTheParentOutAgain) {
  Scene<Column> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  const float before = scene.root().parts.second.bounds().fTop;

  scene.root().parts.first.fState.setOutOfFlow(true);
  EXPECT_TRUE(scene.layoutIfNeeded(kView));
  EXPECT_LT(scene.root().parts.second.bounds().fTop, before);

  scene.root().parts.first.fState.setOutOfFlow(false);
  EXPECT_TRUE(scene.layoutIfNeeded(kView));
  EXPECT_FLOAT_EQ(scene.root().parts.second.bounds().fTop, before);
}

TEST(Tracking, PlacedAtTheAnchorItHadLeavesTheFlowAtOnce) {
  Scene<Column> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  const float before = scene.root().parts.second.bounds().fTop;

  // The top left: the anchor every node has to begin with.
  scene.root().parts.first.apply({.place = anchor::kTopLeft});
  EXPECT_TRUE(scene.layoutIfNeeded(kView));
  EXPECT_LT(scene.root().parts.second.bounds().fTop, before);
}

struct Small : Node {
  struct parts_t {
    DrawProbe probe = make<DrawProbe>({.width = 10.0f, .height = 10.0f});
  } parts;
};
struct Outer : Node {
  struct parts_t {
    Small small = make<Small>({.width = 20.0f, .height = 20.0f});
  } parts;
};

TEST(Tracking, AChildMovedPastItsParentIsDrawnWhereItIs) {
  Scene<Outer> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  auto &probe = scene.root().parts.small.parts.probe;
  probe.fState.setShift(100.0f, 60.0f);
  (void)scene.finishFrame();  // the damage walk works out each node's reach

  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(200, 120));
  skia::SkCanvas canvas(pixels);
  // Only where it is now repainted: its parent's own box is not.
  canvas.clipRect(skia::SkRect::MakeXYWH(100.0f, 60.0f, 10.0f, 10.0f));
  const int before = probe.fDraws;
  skiff::scene::detail::PlainPaint plain;
  scene.draw(plain, &canvas);
  EXPECT_EQ(probe.fDraws, before + 1);
}

struct Pushed : Stack {
  struct parts_t {
    Box<> spacer = make<Box>({.width = 40.0f, .height = 10.0f}, kFill);
    ScrollContainer<Box<>> view{make<Box>({.width = 40.0f, .height = 200.0f}, kFill)};
  } parts;
  Pushed() { parts.view.apply({.width = 100.0f, .height = 50.0f}); }
};

TEST(Tracking, AScrollViewPushedDownIsRepaintedWhereItIs) {
  Scene<Pushed> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();

  scene.root().parts.spacer.apply({.height = 40.0f});
  scene.layoutIfNeeded(kView);
  const skia::SkRect damage = scene.finishFrame().fDamage;
  EXPECT_TRUE(damage.contains(scene.root().parts.view.bounds()));
}

struct HolderBox : Node {
  struct parts_t {
    Box<> tip = make<Box>({.x = 50.0f, .width = 20.0f, .height = 10.0f, .visible = false, .revealOnHover = true}, kFill);
  } parts;
};
struct RevealScreen : Node {
  struct parts_t {
    HolderBox holder = make<HolderBox>({.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f});
  } parts;
};

TEST(Tracking, RevealOnHoverShowsWhileTheHolderIsHovered) {
  Scene<RevealScreen> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  auto &tip = scene.root().parts.holder.parts.tip;
  EXPECT_FALSE(tip.visible());

  scene.setHover(20.0f, 20.0f);
  EXPECT_TRUE(tip.visible());
  EXPECT_FALSE(scene.finishFrame().fDamage.isEmpty());

  scene.setHover(180.0f, 100.0f);
  EXPECT_FALSE(tip.visible());
}

struct PaddedScreen : Node {
  struct parts_t {
    Box<> box = make<Box>({.width = 40.0f, .height = 20.0f, .margin = {3.0f, 0.0f, 0.0f, 0.0f}, .padding = {0.0f, 0.0f, 12.0f, 0.0f}}, kFill);
  } parts;
};

// Given as all zeros, a padding or a margin is zero: it once read as "not
// mentioned", and a bottom padding applied for a docked panel stayed when
// the panel went.
TEST(Tracking, ZeroPaddingAndMarginApplied) {
  Scene<PaddedScreen> scene{std::in_place};
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  auto &box = scene.root().parts.box;
  EXPECT_EQ(box.fState.padding().fBottom, 12.0f);
  box.apply({.margin = {0.0f, 0.0f, 0.0f, 0.0f}, .padding = {0.0f, 0.0f, 0.0f, 0.0f}});
  EXPECT_EQ(box.fState.padding().fBottom, 0.0f);
  EXPECT_EQ(box.fState.margin().fTop, 0.0f);
  // And one not mentioned is left as it is.
  box.apply({.padding = {1.0f, 0.0f, 0.0f, 0.0f}});
  box.apply({.width = 41.0f});
  EXPECT_EQ(box.fState.padding().fTop, 1.0f);
}

// Its height taken by a phone's keyboard come up under it: what was at its
// bottom stays at its bottom.
TEST(Tracking, ShrunkScrollViewKeepsItsBottom) {
  Scene<ScrollContainer<Box<>>> scene{std::in_place, make<Box>({.width = 100.0f, .height = 1000.0f}, kFill)};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 300.0f));
  scene.root().setCurrent(200.0f);
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 300.0f));
  ASSERT_FLOAT_EQ(scene.root().current(), 200.0f);
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 200.0f));
  EXPECT_FLOAT_EQ(scene.root().current(), 300.0f);
  // And back as it goes down.
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 300.0f));
  EXPECT_FLOAT_EQ(scene.root().current(), 200.0f);
}

} // namespace

#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
namespace {
// Both hooks must still run on the original node, even though the geometry
// around them is shared. An inherited default layout must see real children.
struct MeasuredForSharedLayout : Node {
  int measures = 0, layouts = 0;
  DrawProbe child = make<DrawProbe>({.width = 13.0f, .height = 7.0f});
  void forEachChild(auto&& f) { f(child); }
  void measure(const skia::SkRect&) { ++measures; }
  void layoutChildren() {
    ++layouts;
    layout(child, fState.contentBox());
  }
};

TEST(Tracking, SharedDebugLayoutKeepsMeasurementChildrenAndPlacement) {
  auto node = make<MeasuredForSharedLayout>({.autoSize = axes::kBoth});
  auto ref = AnyNodeRef::of(node);
  layout(ref, kView);
  EXPECT_EQ(node.measures, 1);
  EXPECT_GE(node.layouts, 2);
  EXPECT_EQ(node.bounds().width(), 13.0f);
  EXPECT_EQ(node.bounds().height(), 7.0f);
  const auto before = node.child.bounds();
  const int measured = node.measures;
  node.setPosition(10.0f, 5.0f);
  layout(ref, kView);
  EXPECT_EQ(node.measures, measured);
  EXPECT_EQ(node.child.bounds(), before.makeOffset(10.0f, 5.0f));
}

struct CustomDrawForSharedDefault : Node {
  int draws = 0;
  void draw(Painting& painting, skia::SkCanvas* canvas, float alpha) {
    ++draws;
    drawDefault(*this, painting, canvas, alpha);
  }
};

TEST(Tracking, SharedDebugDefaultDrawKeepsCustomDrawAndChildHooks) {
  auto root = make<Group<CustomDrawForSharedDefault, DrawProbe>>({.width = 100.0f, .height = 100.0f},
      make<CustomDrawForSharedDefault>({.width = 10.0f, .height = 10.0f}),
      make<DrawProbe>({.width = 10.0f, .height = 10.0f}));
  auto ref = AnyNodeRef::of(root);
  layout(ref, kView);
  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(200, 120));
  skia::SkCanvas canvas(pixels);
  detail::PlainPaint painting;
  draw(ref, painting, &canvas, 1.0f);
  EXPECT_EQ(std::get<0>(root.fChildren).draws, 1);
  EXPECT_EQ(std::get<1>(root.fChildren).fDraws, 1);
}

struct TextProbeForSharedEvents : Node {
  using Node::onText;
  std::vector<int> received;
  void onText(const phase::capture &, const text::commit &event, Reply &) {
    received.push_back(10 + static_cast<int>(event.text.size()));
  }
  void onText(const phase::target &, const text::compose &event, Reply &) {
    received.push_back(20 + static_cast<int>(event.text.size()) + event.start * event.length);
  }
  void onText(const phase::bubble &, const text::commit &event, Reply &) {
    received.push_back(30 + static_cast<int>(event.text.size()));
  }
};

TEST(Tracking, SharedDebugEventRowsKeepPhaseAndAlternative) {
  auto node = make<TextProbeForSharedEvents>({});
  constexpr auto entries = EventEntries<TextHandler, TextEvent, Reply>::of<TextProbeForSharedEvents>();
  const TextEvent committed = text::commit{"alpha"};
  const TextEvent composing = text::compose{"beta", 3, 2};
  Reply reply;
  auto deliver = [&](auto when) {
    entries.deliver(&node, when, committed, reply);
    entries.deliver(&node, when, composing, reply);
  };
  deliver(phase::capture{});
  deliver(phase::target{});
  deliver(phase::bubble{});
  EXPECT_EQ(node.received, (std::vector<int>{15, 30, 35}));
}
}
#endif

namespace {
struct MediaFrame {
  skia::Sp<skia::SkImage> image;
  double due = 40.0;
};
struct TimedPicture {
  std::shared_ptr<MediaFrame> frame;
  const skia::Sp<skia::SkImage>* operator()() const { return &frame->image; }
  bool animated() const { return true; }
  double wakeAt() const { return frame->due; }
};
template <class Image> struct TimedMedia : Node {
  struct parts_t { Image image; } parts;
  explicit TimedMedia(std::shared_ptr<MediaFrame> frame)
      : parts{make<Image>({.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f}, TimedPicture{frame})} {
    parts.image.keepBox();
  }
};

TEST(Tracking, MediaDeadlinesPreserveLocalDamageWithoutContinuousFrames) {
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  ASSERT_TRUE(surface);
  const auto check = [&]<class Image>() {
    auto frame = std::make_shared<MediaFrame>();
    frame->image = surface->makeImageSnapshot();
    Scene<TimedMedia<Image>> scene{std::in_place, frame};
    scene.update(0.0);
    scene.layoutIfNeeded(kView);
    const auto first = scene.finishFrame();
    EXPECT_DOUBLE_EQ(first.fWakeAtMs, 40.0);
    EXPECT_FALSE(first.fWantsAnotherFrame);
    scene.update(20.0);
    EXPECT_TRUE(scene.finishFrame().fDamage.isEmpty());
    surface->getCanvas()->clear(kFill);
    frame->image = surface->makeImageSnapshot();
    frame->due = 140.0;
    scene.update(40.0);
    const auto next = scene.finishFrame();
    EXPECT_DOUBLE_EQ(next.fWakeAtMs, 140.0);
    EXPECT_FALSE(next.fWantsAnotherFrame);
    EXPECT_EQ(next.fDamage, scene.root().parts.image.bounds());
  };
  check.template operator()<skiff::nodes::internal::Image<TimedPicture>>();
  check.template operator()<skiff::nodes::Image<TimedPicture>>();
}
} // namespace

namespace {
struct CachedPicture {
  std::shared_ptr<MediaFrame> frame;
  std::shared_ptr<Waiters> waiting;
  const skia::Sp<skia::SkImage>* operator()() const { return frame->image ? &frame->image : nullptr; }
  Waiters& waiters() const { return *waiting; }
};
struct CachedMedia : Node {
  struct parts_t { skiff::nodes::Image<CachedPicture> image; } parts;
  explicit CachedMedia(CachedPicture source)
      : parts{make<skiff::nodes::Image<CachedPicture>>(
            {.x = 10.0f, .y = 10.0f, .width = 40.0f, .height = 20.0f}, source)} {
    parts.image.keepBox();
  }
};
TEST(Tracking, CacheReplacementWakesAnAlreadyLoadedImageAndDamagesOnlyItsBox) {
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  ASSERT_TRUE(surface);
  auto frame = std::make_shared<MediaFrame>();
  frame->image = surface->makeImageSnapshot();
  auto waiting = std::make_shared<Waiters>();
  Scene<CachedMedia> scene{std::in_place, CachedPicture{frame, waiting}};
  scene.update(0.0);
  scene.layoutIfNeeded(kView);
  (void)scene.finishFrame();
  EXPECT_FALSE(scene.root().parts.image.wantsTick());
  frame->image.reset();
  waiting->wake();
  scene.update(40.0);
  EXPECT_EQ(scene.finishFrame().fDamage, scene.root().parts.image.bounds());
  frame->image = surface->makeImageSnapshot();
  waiting->wake();
  scene.update(100.0);
  EXPECT_EQ(scene.finishFrame().fDamage, scene.root().parts.image.bounds());
  EXPECT_FALSE(scene.root().parts.image.wantsTick());
}
} // namespace
