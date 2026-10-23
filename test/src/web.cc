// SPDX-License-Identifier: AGPL-3.0-only
import std;
import gtest;
import skiff.core;
import skiff.model;
import skiff.bind;
import skiff.web;

#include "gtest/gtest-macros.h"

// A page: the tree mounted as the document's elements in the order a press
// walks them, a Spec written as CSS, a named view named in the document, and
// an event on an element delivered down the tree as a press -- with nodes
// made here, as a program makes its own.

namespace {
using namespace skiff;
using namespace skiff::web;

struct Clicked {};
// A button of the test's own: a press sends its event.
struct Clicker : Node {
  using Out = model::Types<Clicked>;
  [[nodiscard]] static constexpr std::string_view tag() { return "button"; }
  void mounted() { dom::listen(fState.fHandle, "click"); }
  bool onDom(dom::Handle, std::string_view event, std::string_view) { return event == "click"; }
  Clicked onPress() { return {}; }
};
struct Count {
  int n = 0;
};
// A text showing a count.
struct Counter : Text {
  Counter() : Text("0") {}
  void read(const Count &now) { setText(std::to_string(now.n)); }
};
struct Root {
  model::Tracked<Count> count;
};
struct Reactions {};
using Model = model::Model<Root, Reactions>;

auto counterPage() {
  return scoped<Root>(handlers(handle<Clicked>([](const Clicked &, const auto &) {
                        return model::over<Count>([](Count c) {
                          ++c.n;
                          return c;
                        });
                      })),
                      column(vbox(8.0f, {.padding = scene::Margin::all(12.0f)}), row(hbox(8.0f), Text("Clicks"), bound<Count>(Counter())), Clicker()));
}

TEST(Web, TheTreeIsMountedAsElementsInTheOrderAPressWalksThem) {
  dom::reset();
  Model m;
  Page page(counterPage(), m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("<div class=\"skiff-stack\" style=\"display:flex;flex-direction:column;gap:8px;"), std::string::npos) << html;
  EXPECT_NE(html.find("<span class=\"skiff-text\">Clicks</span><span class=\"skiff-text\">0</span>"), std::string::npos) << html;
  EXPECT_NE(html.find("<button>"), std::string::npos) << html;
}

TEST(Web, AnEventOnAnElementIsAPressDownTheTreeAndThePageFollows) {
  dom::reset();
  Model m;
  Page page(counterPage(), m);
  page.mount();
  auto &button = std::get<1>(page.root().fParts);
  fire(page, button.element(), "click", "");
  fire(page, button.element(), "click", "");
  EXPECT_EQ(m.look<Count>()->n, 2);
  auto &counter = std::get<1>(std::get<0>(page.root().fParts).fParts);
  EXPECT_EQ(counter.text(), "2");
  EXPECT_NE(dom::dump(counter.element()).find(">2<"), std::string::npos);
}

TEST(Web, AnEditMadeElsewhereIsWrittenToThePage) {
  dom::reset();
  Model m;
  Page page(counterPage(), m);
  page.mount();
  m.apply(model::over<Count>([](Count c) {
    c.n = 41;
    return c;
  }));
  page.refresh();
  auto &counter = std::get<1>(std::get<0>(page.root().fParts).fParts);
  EXPECT_EQ(counter.text(), "41");
}

struct CountRow : Named<CountRow, decltype(row(hbox(8.0f), Text("Clicks"), bound<Count>(Counter())))> {
  using Named::Named;
  static constexpr const char *viewName() { return "CountRow"; }
};

TEST(Web, ANamedViewIsWrittenToThePageByItsName) {
  dom::reset();
  Model m;
  Page page(column(CountRow(row(hbox(8.0f), Text("Clicks"), bound<Count>(Counter())))), m);
  page.mount();
  EXPECT_NE(dom::dump(dom::root()).find("data-view=\"CountRow\""), std::string::npos) << dom::dump(dom::root());
}

TEST(Web, ASpecIsCssOnTheElement) {
  dom::reset();
  Model m;
  Page page(column(Box({.width = 120.0f, .height = 40.0f, .cornerRadius = 6.0f, .background = 0xff336699u, .alpha = 0.5f}),
                   Text("hidden", {.visible = false}), row(hbox(), Box({.fill = true}), Box({.fill = true}))),
            m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("width:120px;"), std::string::npos) << html;
  EXPECT_NE(html.find("height:40px;"), std::string::npos);
  EXPECT_NE(html.find("border-radius:6px;"), std::string::npos);
  EXPECT_NE(html.find("background-color:rgba(51,102,153,1);"), std::string::npos) << html;
  EXPECT_NE(html.find("opacity:0.5;"), std::string::npos);
  EXPECT_NE(html.find("display:none;"), std::string::npos);
  EXPECT_NE(html.find("flex:1 1 0%;"), std::string::npos);
}

TEST(Web, AStylesheetIsMountedBeforeTheTree) {
  dom::reset();
  Model m;
  mountStylesheet(dom::root(), ".skiff-text{color:red}");
  Page page(column(Text("x")), m);
  page.mount();
  EXPECT_EQ(dom::dump(dom::root()).find("<style>.skiff-text{color:red}</style><div"), 6u) << dom::dump(dom::root());
}

} // namespace
