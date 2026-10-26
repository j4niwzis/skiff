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

} // namespace
