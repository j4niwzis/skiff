// Tabs, sections and menu cards; readers of more than one model; pages scrolled and pinned; the names of views.
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


TEST(Compose, ListedIsMadeAgainFromWhatItReads) {
  auto list = listed<int>(vbox(2.0f), [](int n) { return nodes::Text(std::to_string(n), 13.0f, 0); });
  EXPECT_TRUE(list.fParts.empty());
  list.read(std::vector<int>{1, 2, 3});
  ASSERT_EQ(list.fParts.size(), 3u);
  EXPECT_EQ(list.fParts[1].text(), "2");
  list.read(std::vector<int>{7});
  ASSERT_EQ(list.fParts.size(), 1u);
  EXPECT_EQ(list.fParts[0].text(), "7");
}

TEST(Compose, SectionsMakeATabAndAPartForEachAndLightTheFirst) {
  const auto text = [](std::string what) { return [what](int n) { return nodes::Text(what + std::to_string(n), 13.0f, 0); }; };
  auto list = sections<int>(vbox(4.0f), vbox(0.0f), text("tab "), text("part "), wrapped<int>(vbox(0.0f), hbox(4.0f)));
  list.read(SectionsShown<int>{{1, 2, 3}, {1, 2, 3}, true});
  ASSERT_EQ(list.parts().size(), 3u);
  ASSERT_EQ(list.fStrip.buttons().size(), 3u);
  EXPECT_EQ(list.parts()[2].text(), "part 3");
  EXPECT_TRUE(list.fStrip.visible());
  (void)list.fStrip.buttons()[1].onPress();  // brought once it is laid out
  EXPECT_TRUE(list.settling());
  list.update(0.0);
  EXPECT_TRUE(list.fStrip.buttons()[0].fState.selected());
  EXPECT_FALSE(list.fStrip.buttons()[1].fState.selected());
  // A search's results: every tab still there, none lit.
  list.read(SectionsShown<int>{{9}, {1, 2, 3}, false});
  EXPECT_EQ(list.parts().size(), 1u);
  EXPECT_EQ(list.fStrip.buttons().size(), 3u);
  list.update(0.0);
  EXPECT_FALSE(list.fStrip.buttons()[0].fState.selected());
}

TEST(Compose, SplitStripsPutEachTabWhereItsElementSaysAndPagedOnesPage) {
  const auto text = [](std::string what) { return [what](int n) { return nodes::Text(what + std::to_string(n), 13.0f, 0); }; };
  const auto arrow = [](bool next) { return nodes::Text(next ? ">" : "<", 13.0f, 0); };
  auto list = sections<int>(vbox(4.0f), vbox(0.0f), text("tab "), text("part "),
      split<int>(vbox(0.0f), [](int n) { return n >= 10; }, in_row<int>(hbox(0.0f)),
                 paged<int>(hbox(0.0f), hbox(0.0f), 30.0f, 60.0f, arrow)));
  list.read(SectionsShown<int>{{1, 2, 10, 11, 12}, {1, 2, 10, 11, 12}, true});
  ASSERT_EQ(list.fStrip.fFirst.fParts.size(), 2u);
  ASSERT_EQ(list.fStrip.fThen.buttons().size(), 3u);
  EXPECT_TRUE(list.fStrip.fThen.fBack.has_value());
  list.fStrip.light(3);  // the second of the packs
  EXPECT_TRUE(list.fStrip.fThen.buttons()[1].fState.selected());
  EXPECT_FALSE(list.fStrip.fFirst.fParts[0].fState.selected());
  list.fStrip.fThen.fHolds = 2;
  list.fStrip.fThen.page(true);
  EXPECT_EQ(list.fStrip.fThen.fFirst, 2u);
  list.fStrip.fThen.page(false);
  EXPECT_EQ(list.fStrip.fThen.fFirst, 0u);
}
TEST(Compose, AMenuCardWithItsHeadHiddenIsAsWideAsItsLeast) {
  using namespace menu_card_width;
  for (const bool head : {true, false}) {
    scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::styled({.place = scene::anchor::kTopLeft}, card(head))));
    const auto box = skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f);
    tree.layoutIfNeeded(box);
    (void)tree.finishFrame();
    tree.layoutIfNeeded(box);
    float item = -1.0f;
    scene::forEachNode<Item>(tree.root(), [&](auto &one) { item = one.bounds().width(); });
    std::println("head {}: an item {} wide", head, item);
    EXPECT_GE(item, 220.0f);
  }
}

TEST(Compose, PlacedAtIsShownWhereThereIsAPoint) {
  auto node = PlacedAt<nodes::Text>(nodes::Text("tones", 13.0f, 0));
  EXPECT_FALSE(node.visible());
  node.read(std::optional<skia::SkPoint>(skia::SkPoint{10.0f, 20.0f}));
  EXPECT_TRUE(node.visible());
  node.read(std::nullopt);
  EXPECT_FALSE(node.visible());
}

TEST(Compose, RestingSaysWhenThePointerHasRestedAndWhenItLeaves) {
  std::vector<bool> said;
  const auto rest = [&](bool on) { said.push_back(on); };
  auto made = resting(300.0, rest, nodes::Text("x", 13.0f, 0));
  auto &node = made_as<Resting<std::remove_cvref_t<decltype(rest)>, part_t<nodes::Text>>>(made);
  node.fState.fHovered = true;
  node.update(1000.0);
  node.update(1200.0);
  EXPECT_TRUE(said.empty());
  EXPECT_TRUE(node.settling());
  // Said as a press: the node due, its act asked as the press is delivered.
  node.update(1300.0);
  EXPECT_EQ(scene::hostWork().pressedNow, &node.fState);
  node.onPress();
  EXPECT_EQ(said, std::vector<bool>{true});
  EXPECT_FALSE(node.settling());
  node.fState.fHovered = false;
  node.update(1400.0);
  node.onPress();
  EXPECT_EQ(said, (std::vector<bool>{true, false}));
  scene::hostWork().pressedNow = nullptr;
}

// A node shown only while what it reads says so, holding a text made from
// the same part: hidden by the first refresh where it says no.
TEST(Compose, AShownIfHidesWhereItsReadSaysSo) {
  struct Count { int n = 0; };
  struct Root { model::Tracked<Count> count; };
  model::Model<Root, bind::NoReactions> model{Root{}};
  const auto digits = [](const Count &now) { return std::to_string(now.n); };
  auto node = shown_if<Count>([](const Count &now) { return now.n > 0; },
                              row(text_of<Count>(digits, nodes::Text("", 12.0f, 0u))));
  bind::Binding<decltype(model)> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  EXPECT_EQ((made_as<bind::Derived<std::remove_cvref_t<decltype(digits)>, TextOf, Count>>(node).text()), "0");
}

// A node that reads the model itself, from more than one model: refreshed
// through what it lists, by the model's type or its root's.
namespace {
struct ItsRoot {
  model::Tracked<int> n;
};
struct OtherRoot {
  model::Tracked<long> m;
};
using ItsModel = model::Model<ItsRoot, bind::NoReactions>;
using OtherModel = model::Model<OtherRoot, bind::NoReactions>;
struct SelfReading : scene::Node {
  int fByModel = 0, fByRoot = 0;
  void refresh(const ItsModel &) { ++fByModel; }
  void refresh_other(const OtherRoot &) { ++fByRoot; }
  bind::Refreshes<2> fRefreshes = bind::refreshes(bind::refreshFrom<SelfReading, ItsModel>(),
                                                  bind::refreshFromRoot<SelfReading, OtherRoot, &SelfReading::refresh_other>());
};
} // namespace
namespace {
// As a program's window reads its models: refresh() overloaded, one a
// template over the model's reactions -- which the runtime walks cannot
// pick, so it lists them.
struct OverloadedReading : scene::Node {
  int fByModel = 0, fByRoot = 0;
  void refresh(const ItsModel &) { ++fByModel; }
  template <class M> void refresh(const M &model) { refresh_other(model.root()); }
  void refresh_other(const OtherRoot &) { ++fByRoot; }
  bind::Refreshes<2> fRefreshes = bind::refreshes(bind::refreshFrom<OverloadedReading, ItsModel>(),
                                                  bind::refreshFromRoot<OverloadedReading, OtherRoot, &OverloadedReading::refresh_other>());
};
// One made around it, its own address not the lister's.
struct Ahead {
  long fBytes[4] = {};
};
struct WrappedReading : Ahead, OverloadedReading {};
struct OverloadedUnlisted : scene::Node {
  void refresh(const ItsModel &) {}
  void refresh(const OtherModel &) {}
};
struct ButtonNamedRefresh {
  int refresh;
};
} // namespace
#ifdef SKIFF_ERASED_WALKS
// One the walks would pass by is refused; listed, or one refresh, or no
// node at all, it is not.
static_assert(bind::rt::made::RefreshUnseen<OverloadedUnlisted>);
static_assert(!bind::rt::made::RefreshUnseen<OverloadedReading>);
static_assert(!bind::rt::made::RefreshUnseen<SelfReading>);
static_assert(!bind::rt::made::RefreshUnseen<ButtonNamedRefresh>);
#endif
namespace {
// The root of the tree, and a part deep in boxes: read by each model, each
// time it moves.
TEST(Compose, AnOverloadedReaderIsRefreshedAsRootAndAsAPart) {
  ItsModel its{ItsRoot{}};
  OtherModel other{OtherRoot{}};
  OverloadedReading root;
  bind::Binding<ItsModel> first;
  bind::Binding<OtherModel> second;
  first.refresh(root, its);
  second.refresh(root, other);
  EXPECT_EQ(root.fByModel, 1);
  EXPECT_EQ(root.fByRoot, 1);
  auto tree = column(row(OverloadedReading{}), nodes::Box<>(0u));
  auto moved = std::move(tree);
  bind::Binding<ItsModel> third;
  bind::Binding<OtherModel> fourth;
  third.refresh(moved, its);
  fourth.refresh(moved, other);
  const auto &part = made_as<OverloadedReading>(moved);
  EXPECT_EQ(part.fByModel, 1);
  EXPECT_EQ(part.fByRoot, 1);
  WrappedReading wrapped;
  bind::Binding<ItsModel> fifth;
  bind::Binding<OtherModel> sixth;
  fifth.refresh(wrapped, its);
  sixth.refresh(wrapped, other);
  EXPECT_EQ(wrapped.fByModel, 1);
  EXPECT_EQ(wrapped.fByRoot, 1);
}
} // namespace
TEST(Compose, ANodeReadingTwoModelsItselfIsRefreshedByEach) {
  ItsModel its{ItsRoot{}};
  OtherModel other{OtherRoot{}};
  SelfReading node;
  bind::Binding<ItsModel> first;
  bind::Binding<OtherModel> second;
  first.refresh(node, its);
  second.refresh(node, other);
  EXPECT_EQ(node.fByModel, 1);
  EXPECT_EQ(node.fByRoot, 1);
}

// A page view kept scrolled where the model says -- a settings box -- is
// still told its facts by what holds it (a dialog), in every build: the
// offset's read does not hide the view's.
namespace {
struct KeptPage : skiff::scene::Node {};
struct KeptScrolled {
  float at = 0.0f;
};
struct KeptRoot {
  skiff::model::Tracked<KeptScrolled> scrolled;
};
struct KeptSaidAt {
  int operator()(float) const { return 1; }
};
auto keptPages(int *shown) {
  using page_t = std::variant<KeptPage>;
  auto show = [shown](page_t &, const int &facts) { *shown = facts; };
  auto same = [](const int &a, const int &b) { return a == b; };
  auto side = [](const int &) { return 1.0f; };
  auto pin = [](const int &) { return std::optional<skiff::scene::Spec>(); };
  return skiff::compose::kept_scroll<skiff::model::Field<&KeptScrolled::at>>(KeptSaidAt{},
      skiff::compose::page_view<int, page_t>(show, same, side, pin, page_t(std::in_place_index<0>), {.fill = true}));
}
} // namespace

TEST(Compose, APageViewKeptScrolledIsStillToldItsFacts) {
  int shown = -1;
  auto view = keptPages(&shown);
  static_assert(requires { view.read(3); }, "told its facts, as a dialog tells them");
  view.read(3);
  EXPECT_EQ(shown, 3);
  view.read(7);
  EXPECT_EQ(shown, 7);
  using M = skiff::model::Model<KeptRoot, skiff::bind::NoReactions>;
  M m{KeptRoot{}};
  skiff::bind::Binding<M> binding;
  binding.refresh(view, m);
  EXPECT_EQ(shown, 7) << "its offset read tells it no facts";
}
TEST(Compose, APageHeldWholeByItsStateScrollsUnderItsHeader) {
  using namespace pinned_local_page;
  scene::Scene tree(view());
  auto &pages = tree.root();
  pages.read(1);
  const auto box = skia::SkRect::MakeXYWH(0.0f, 0.0f, 200.0f, 300.0f);
  for (double at = 0.0; at < 400.0; at += 16.0) {
    tree.update(at);
    tree.layoutIfNeeded(box);
    (void)tree.finishFrame();
  }
  pages.setCurrent(300.0f);
  tree.layoutIfNeeded(box);
  (void)tree.finishFrame();
  skia::SkBitmap pixels;
  ASSERT_TRUE(pixels.tryAllocN32Pixels(200, 300));
  skia::SkCanvas canvas(pixels);
  scene::detail::PlainPaint plain;
  tree.draw(plain, &canvas);
  auto *header = scene::findNode<PinnedHeader>(tree.root());
  auto *body = scene::findNode<PageBody>(tree.root());
  ASSERT_NE(header, nullptr);
  ASSERT_NE(body, nullptr);
  const float scrolled = pages.fScroll.contentsShift();
  std::println("scrolled {}: header shifted {}, body {}", scrolled, header->fState.fShiftY, body->fState.fShiftY);
  EXPECT_NE(scrolled, 0.0f);
  EXPECT_FLOAT_EQ(header->fState.fShiftY, -scrolled);
  EXPECT_FLOAT_EQ(body->fState.fShiftY, 0.0f);
}

TEST(Compose, AColumnSlidingInKeepsItsFill) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  float icon = 0.0f, text = 0.0f;
  auto row = c::row(c::hbox(16.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
      MeasuredWidth(&icon, {.width = 24.0f, .height = 24.0f}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, MeasuredWidth(&text, {.height = 20.0f})));
  auto sliding = c::appearing_across(true,
      c::column(c::vbox(0.0f, {.fill = true}), c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(row))),
      36.0f, 220.0f);
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(sliding)));
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 280.0f, 600.0f);
  for (double at : {0.0, 300.0, 600.0}) {
    tree.layoutIfNeeded(view);
    tree.update(at);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  }
  EXPECT_FLOAT_EQ(icon, 24.0f);
  EXPECT_FLOAT_EQ(text, 280.0f - 40.0f - 24.0f - 16.0f);
}

// A column whose width is clamped to what is wanted, over content that fills
// it: the rows in it as wide as the clamp. Outside a release build, held
// content lost its fill to the clamp and was laid out 0 wide.
TEST(Compose, AClampedColumnsRowsAreAsWideAsTheClamp) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  float icon = 0.0f, text = 0.0f;
  auto row = c::row(c::hbox(16.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
      MeasuredWidth(&icon, {.width = 24.0f, .height = 24.0f}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, MeasuredWidth(&text, {.height = 20.0f})));
  struct Open {
    bool on = false;
  };
  auto side = c::clamped_width(c::WidthLimit{.wanted = 300.0f},
      c::local<Open>(c::column(c::vbox(0.0f, {.fill = true}),
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(row)))));
  scene::Scene tree(c::row(c::hbox(0.0f, {.fill = true}), std::move(side),
                           c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 900.0f, 600.0f));
  EXPECT_FLOAT_EQ(icon, 24.0f);
  EXPECT_FLOAT_EQ(text, 300.0f - 40.0f - 24.0f - 16.0f);
}

// Names without RTTI: a type's last part, its arguments left out; a view
// named where it is written goes by its own name in what the layout tells.
namespace {
struct WideNode : skiff::scene::Node {
  WideNode() { this->apply({.width = 120.0f, .height = 10.0f}); }
};
} // namespace

TEST(Compose, NamesAreShortAndNeedNoRtti) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  static_assert(scene::nameOf<std::vector<int>>() == "vector");
  static_assert(scene::nameOf<WideNode>() == "WideNode");
  static_assert(scene::kindOf<int>() != scene::kindOf<long>());
  auto narrow = SKIFF_NAMED(narrow_box, c::row(c::hbox(0.0f, {.width = 50.0f, .height = 20.0f}), WideNode()));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(narrow)));
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
  const auto told = std::exchange(scene::overflows(), {});
  ASSERT_FALSE(told.empty());
  EXPECT_EQ(told.front().node, "WideNode");
  EXPECT_EQ(told.front().view, "narrow_box");
}

// What a caller asks of a named view -- shown or not, where it is -- it
// answers as what it names does.
TEST(Compose, ANamedViewAnswersWhatItsContentDoes) {
  namespace c = skiff::compose;
  auto shown = SKIFF_NAMED(shown_row, c::row(c::hbox(0.0f, {.width = 10.0f, .height = 10.0f}), WideNode()));
  shown.setVisible(false);
  shown.setPosition(1.0f, 2.0f);
  EXPECT_FALSE(shown.visible());
}

// A box a combinator sizes by its content (hovering's), told to grow, takes
// what its column leaves: the newer grow replaces the sizing it was made
// with, where before the state dropped the grow as a second size.
TEST(Compose, ABoxSizedByItsContentToldToGrowGrows) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  struct Tall : scene::Node {
    float *fHeight;
    explicit Tall(float *h) : fHeight(h) { this->apply({.fill = true}); }
    void layoutChildren() { *fHeight = this->fState.bounds().height(); }
  };
  float height = 0.0f;
  auto grown = c::styled({.fillX = true, .grow = scene::axes::kY},
      c::hovering([](bool, const skia::SkRect &) { return 1; }, c::column(c::vbox(0.0f, {.fill = true}), Tall(&height))));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::column(c::vbox(0.0f, {.fillX = true, .height = 100.0f})), std::move(grown)));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 600.0f));
  EXPECT_FLOAT_EQ(height, 500.0f);
}

}  // namespace
