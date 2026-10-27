// skiff.mirror: a scene's tree written into the in-memory document -- its
// stacks as flex boxes, its texts as their words -- and a press on an element
// pressed on the node it is, the model and the document moving with it.
import std;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;
import skiff.web;
import skiff.mirror;
import skia;

#include "gtest/gtest-macros.h"

namespace {
using namespace skiff;
namespace c = skiff::compose;
namespace dom = skiff::web::dom;

struct Count {
  int n = 0;
};
struct Pressed {};
struct CountText : nodes::Text {
  CountText() : nodes::Text("0", 13.0f, 0xff000000u) {}
  void read(const Count &now) { setText(std::to_string(now.n)); }
};
inline auto page() {
  return c::state<Count>(c::handlers(c::handle<Pressed>([] {
                           return model::over<Count>([](Count now) {
                             ++now.n;
                             return now;
                           });
                         })),
                         c::column(c::vbox(8.0f, {.fill = true, .padding = scene::Margin::all(4.0f), .background = 0xffeeeeeeu}),
                                   c::bound<Count>(CountText()),
                                   c::row(c::hbox(4.0f, {.autoSize = scene::axes::kBoth}),
                                          c::onClick(Pressed{}, nodes::Text("+", 13.0f, 0xff0000ffu)))));
}

// The "+" as the tree has it: the first node of its type.
using Plus = c::OnClick<Pressed, c::part_t<nodes::Text>>;
template <class N> scene::State *plusOf(N &root) {
  scene::State *found = nullptr;
  scene::forEachNode<Plus>(root, [&](Plus &one) {
    if (found == nullptr) found = &one.fState;
  });
  return found;
}

TEST(Mirror, AStackIsAFlexBoxAndATextItsWords) {
  dom::reset();
  using Tree = decltype(page());
  bind::ModelOf<Tree> m;
  scene::Scene tree(page());
  bind::Binding<bind::ModelOf<Tree>> binding;
  binding.refresh(tree.root(), m);
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_TRUE(html.contains("display:flex;")) << html;
  EXPECT_TRUE(html.contains("flex-direction:column;")) << html;
  EXPECT_TRUE(html.contains("flex-direction:row;")) << html;
  EXPECT_TRUE(html.contains("gap:8px 8px;")) << html;
  EXPECT_TRUE(html.contains("padding:4px 4px 4px 4px;")) << html;
  EXPECT_TRUE(html.contains("background-color:rgba(238,238,238,1);")) << html;
  EXPECT_TRUE(html.contains(">0<")) << html;
  EXPECT_TRUE(html.contains(">+<")) << html;
}

TEST(Mirror, APressOnAnElementPressesItsNode) {
  dom::reset();
  using Tree = decltype(page());
  bind::ModelOf<Tree> m;
  scene::Scene tree(page());
  bind::Binding<bind::ModelOf<Tree>> binding;
  binding.refresh(tree.root(), m);
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::size_t made = shown.size();
  // The "+": the node that takes a press, under the column.
  scene::State *plus = plusOf(tree.root());
  ASSERT_NE(plus, nullptr);
  const dom::Handle element = shown.elementOf(*plus);
  ASSERT_NE(element, dom::kNone);
  EXPECT_TRUE(dom::dump(element).contains(">+<")) << dom::dump(element);
  EXPECT_TRUE(shown.press(tree.root(), m, element));
  EXPECT_EQ(m.look<Count>()->n, 1);
  binding.refresh(tree.root(), m);
  shown.sync(tree.root());
  EXPECT_TRUE(dom::dump(dom::root()).contains(">1<")) << dom::dump(dom::root());
  // The same elements, written to: none made again.
  EXPECT_EQ(shown.size(), made);
  EXPECT_EQ(shown.elementOf(*plus), element);
}

TEST(Mirror, AClickOnAnElementIsTheScenesClickOnItsNode) {
  dom::reset();
  using Tree = decltype(page());
  bind::ModelOf<Tree> m;
  scene::Scene tree(page());
  bind::Binding<bind::ModelOf<Tree>> binding;
  binding.refresh(tree.root(), m);
  mirror::Mirror shown;
  shown.sync(tree.root());
  scene::State *plus = plusOf(tree.root());
  ASSERT_NE(plus, nullptr);
  // Never laid out by skiff: no bounds, and clicked all the same.
  EXPECT_TRUE(shown.click(tree, shown.elementOf(*plus)));
  // The presses it made and what they answered, given as a host gives them
  // after an event (mux's after_event): each path pressed in the model,
  // then what was answered along the way.
  bool pressed = false;
  for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
    pressed = bind::press(tree.root(), m, way) || pressed;
  for (const auto &one : std::exchange(scene::hostWork().answers, {}))
    pressed = bind::answer(tree.root(), m, one) || pressed;
  EXPECT_TRUE(pressed);
  EXPECT_EQ(m.look<Count>()->n, 1);
  scene::hostWork().laidOutElsewhere = false;
}

// A widget painting itself as Button does: a rounded plate, its edge, its
// label centred.
struct Plate : scene::Node {
  Plate() { fState.apply({.width = 120.0f, .height = 32.0f}); }
  template <class P> void drawSelf(P &p, float alpha) {
    p.fillRounded(fState.fBounds, 6.0f, 0xff3366ccu, alpha);
    p.strokeRounded(fState.fBounds, 6.0f, 0xff112233u, 2.0f, alpha);
    p.textCentredIn(fState.fBounds, "Send", 13.0f, 0xffffffffu, alpha, true);
  }
};
// And one painting a path: SVG.
struct Mark : scene::Node {
  Mark() { fState.apply({.width = 16.0f, .height = 16.0f}); }
  template <class P> void drawSelf(P &p, float alpha) {
    typename P::Path path;
    path.moveTo(0.0f, 0.0f).lineTo(16.0f, 8.0f).lineTo(0.0f, 16.0f).close();
    p.fill(path, 0xff000000u, alpha);
  }
};

TEST(Mirror, AWidgetPaintingItselfIsWrittenAsElements) {
  // As its scene's root: seen by its type in every build (a column's parts
  // are seen through the walks' table outside a release build, and are
  // painted by skiff there).
  dom::reset();
  scene::Scene tree(Plate{});
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  // The plate and its edge: elements of its own, rounded; its label, text.
  EXPECT_TRUE(html.contains("background-color:rgba(51,102,204,1);border-radius:6px;")) << html;
  EXPECT_TRUE(html.contains("border:2px solid rgba(17,34,51,1);")) << html;
  EXPECT_TRUE(html.contains(">Send<")) << html;
  EXPECT_EQ(shown.recorded(), 1u);
}

TEST(Mirror, APathIsSvg) {
  dom::reset();
  scene::Scene tree(Mark{});
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_TRUE(html.contains("<svg")) << html;
  EXPECT_TRUE(html.contains("d=\"M0 0 L16 8 L0 16 Z\"")) << html;
  EXPECT_TRUE(html.contains("fill=\"rgba(0,0,0,1)\"")) << html;
  EXPECT_EQ(shown.recorded(), 1u);
  EXPECT_TRUE(shown.takeNotNative().empty());
}

// In a column -- seen through the walks' table outside a release build --
// what a node paints is written all the same: a plate, and a path as SVG.
TEST(Mirror, WhatAnErasedNodePaintsIsWrittenToo) {
  dom::reset();
  scene::Scene tree(c::column(c::vbox(4.0f, {.fill = true}), Plate{}, Mark{}));
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_TRUE(html.contains("background-color:rgba(51,102,204,1);border-radius:6px;")) << html;
  EXPECT_TRUE(html.contains(">Send<")) << html;
  EXPECT_TRUE(html.contains("<svg")) << html;
  EXPECT_TRUE(html.contains("d=\"M0 0 L16 8 L0 16 Z\"")) << html;
  EXPECT_EQ(shown.recorded(), 2u);
  EXPECT_TRUE(shown.takeNotNative().empty());
}

// Laid out by skiff first, as the page does every frame: a flow's parts
// carry the places it gave them, which the flex box gives them already.
TEST(Mirror, APartInAFlowIsNotMovedAgain) {
  dom::reset();
  scene::Scene tree(c::column(c::vbox(4.0f, {.fill = true}), Plate{}, Plate{}));
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_FALSE(html.contains("translate(0px,36px)")) << html;
}

TEST(Mirror, AScrollViewportStaysScrollableAndMovesItsContentsOnce) {
  dom::reset();
  scene::Scene tree(nodes::ScrollContainer(
      c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), Plate{}, Plate{}, Plate{}, Plate{})));
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 60.0f));
  tree.root().setCurrent(30.0f);
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 60.0f));
  ASSERT_GT(tree.root().current(), 0.0f);
  mirror::Mirror shown;
  shown.sync(tree.root());
  const dom::Handle viewport = shown.elementOf(tree.root().fState);
  const std::string html = dom::dump(viewport);
  const std::string viewportTag = html.substr(0, html.find('>'));
  EXPECT_TRUE(viewportTag.contains("overflow-y:auto;")) << html;
  EXPECT_FALSE(viewportTag.contains("overflow:hidden;")) << html;
  EXPECT_TRUE(html.contains("flex:0 0 auto;")) << html;
  EXPECT_FALSE(html.contains("translate(0px,-30px)")) << html;
  // A user scroll is read back into the scene, whose next layout shifts
  // its contents. Mirroring that layout must still leave movement to CSS.
  shown.readScrolled(tree.root(), [&](dom::Handle element) { return element == viewport ? 45.0f : 0.0f; });
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 60.0f));
  EXPECT_FLOAT_EQ(tree.root().current(), 45.0f);
  shown.sync(tree.root());
  EXPECT_FALSE(dom::dump(viewport).contains("translate(0px,-45px)"));
}

TEST(Mirror, AScrollInsideHeldContentIsReadBackToo) {
  dom::reset();
  auto scroll = c::styled({.fillX = true, .height = 60.0f}, nodes::ScrollContainer(
      c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), Plate{}, Plate{}, Plate{}, Plate{})));
  using Scroll = decltype(scroll);
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(scroll), Plate{}));
  Scroll *view = nullptr;
  scene::forEachNode<Scroll>(tree.root(), [&](Scroll &node) { view = &node; });
  ASSERT_NE(view, nullptr);
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 100.0f));
  view->setCurrent(30.0f);
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 100.0f));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const dom::Handle viewport = shown.elementOf(view->fState);
  ASSERT_NE(viewport, dom::kNone);
  EXPECT_TRUE(dom::dump(viewport).contains("data-skiff-scroll=\"30\""));
  shown.readScrolled(tree.root(), [&](dom::Handle element) { return element == viewport ? 45.0f : 0.0f; });
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 100.0f));
  EXPECT_FLOAT_EQ(view->current(), 45.0f);
}

// The root the whole page, whatever it is: a column, or a wrapper over held
// content (a debug build's root), whose element is the wrapper's.
TEST(Mirror, TheRootIsTheWholePage) {
  dom::reset();
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), Plate{}));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  const auto first = html.find("data-skiff-h");
  ASSERT_NE(first, std::string::npos) << html;
  const std::string root = html.substr(first, html.find('>', first) - first);
  EXPECT_TRUE(root.contains("width:100%;") && root.contains("height:100%;")) << root;
}

TEST(Mirror, RichTextIsRuns) {
  dom::reset();
  nodes::Text said("Hello world, see this", 14.0f, 0xff000000u);
  said.setStyles({scene::TextStyled{.first = 0, .last = 5, .strong = true}}, 0xff888888u);
  said.setLinks({scene::TextLink{.first = 13, .last = 21, .target = "https://example.org"}}, 0xff2266ccu);
  scene::Scene tree(std::move(said));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_TRUE(html.contains("<span style=\"color:rgba(0,0,0,1);font-weight:bold;\">Hello</span>")) << html;
  EXPECT_TRUE(html.contains("<a href=\"https://example.org\" style=\"color:rgba(34,102,204,1);\">see this</a>")) << html;
  EXPECT_TRUE(html.contains(">, <") || html.contains("> world, <")) << html;
}

// An avatar, as one paints itself: its picture through a rounded box.
struct Face : scene::Node {
  skia::Sp<skia::SkImage> fPicture;
  Face() {
    fState.apply({.width = 40.0f, .height = 40.0f});
    const std::vector<std::uint8_t> red(4 * 4 * 4, 0xff);
    fPicture = skia::imageFromRGBA(4, 4, red.data());
  }
  template <class P> void drawSelf(P &p, float alpha) {
    p.imageThrough(skia::SkRRect::MakeRectXY(fState.fBounds, 20.0f, 20.0f), fPicture.get(), fState.fBounds, alpha);
  }
};

TEST(Mirror, APictureIsAnImg) {
  dom::reset();
  scene::Scene tree(Face{});
  (void)tree.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_TRUE(html.contains("<img")) << html;
  EXPECT_TRUE(html.contains("object-fit:cover;border-radius:20px;")) << html;
  EXPECT_TRUE(html.contains("data-skiff-picture=")) << html;
  EXPECT_TRUE(shown.takeNotNative().empty());
}

// A field, as a widget says it is one.
struct Field : scene::Node {
  std::string fText = "hello world";
  std::tuple<std::size_t, std::size_t, std::string> fLastEdit;
  [[nodiscard]] scene::Look look() const { return scene::look::field{.text = fText, .placeholder = "Say", .size = 14.0f, .colour = 0xff000000u}; }
  [[nodiscard]] std::string_view fieldText() const noexcept { return fText; }
  void replaceRange(std::size_t from, std::size_t to, std::string_view with) {
    fLastEdit = {from, to, std::string(with)};
    fText.replace(from, to - from, with);
  }
};

TEST(Mirror, AFieldIsTheEnginesAndItsEditsComeBack) {
  dom::reset();
  scene::Scene tree(Field{});
  mirror::Mirror shown;
  shown.sync(tree.root());
  const std::string html = dom::dump(dom::root());
  EXPECT_TRUE(html.contains("<input")) << html;
  EXPECT_TRUE(html.contains("value=\"hello world\"")) << html;
  EXPECT_TRUE(html.contains("placeholder=\"Say\"")) << html;
  // The engine's field edited: given back as the one edit between them.
  EXPECT_TRUE(shown.typed(tree, shown.elementOf(tree.root().fState), "hello brave world"));
  EXPECT_EQ(tree.root().fText, "hello brave world");
  EXPECT_EQ(tree.root().fLastEdit, (std::tuple<std::size_t, std::size_t, std::string>{6, 6, "brave "}));
  EXPECT_TRUE(shown.typed(tree, shown.elementOf(tree.root().fState), "hello world"));
  EXPECT_EQ(tree.root().fLastEdit, (std::tuple<std::size_t, std::size_t, std::string>{6, 12, ""}));
}

// A field that sends on Enter, as mux's composer does.
struct Sends : Field {
  int fSent = 0;
  [[nodiscard]] bool focusable() const { return true; }
  using Field::onKey;
  void onKey(scene::phase::target, const scene::key::down &press, scene::Reply &reply) {
    if (press.key == scene::keys::kEnter) {
      ++fSent;
      reply.handle();
    }
  }
};

TEST(Mirror, EnterIsTheProgramsFirst) {
  dom::reset();
  scene::Scene tree(Sends{});
  mirror::Mirror shown;
  shown.sync(tree.root());
  const dom::Handle field = shown.elementOf(tree.root().fState);
  EXPECT_TRUE(shown.keyed(tree, field, scene::key::down{scene::keys::kEnter, {}}));
  EXPECT_EQ(tree.root().fSent, 1);
  EXPECT_FALSE(shown.keyed(tree, field, scene::key::down{scene::keys::kA, {}}));
}

} // namespace
