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

} // namespace
