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
  void markup(Markup &to) { to.listen("click"); }
  bool onDom(Part, std::string_view event, std::string_view) { return event == "click"; }
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

TEST(Web, APageWrittenBeforehandIsTakenOverNotMadeAgain) {
  dom::reset();
  Model m;
  // Written as a program does where it is built natively: mounted, dumped.
  Page written(counterPage(), m);
  written.mount();
  const std::string before = dom::dump(dom::root());
  // Taken over by another page of the same tree: the same elements, none made.
  Model live;
  (void)live.apply(model::over<Count>([](Count c) {
    c.n = 5;
    return c;
  }));
  Page page(counterPage(), live);
  page.hydrate();
  const std::string after = dom::dump(dom::root());
  EXPECT_EQ(dom::children(dom::root()), 1u) << after;
  auto &counter = std::get<1>(std::get<0>(page.root().fParts).fParts);
  EXPECT_EQ(counter.element(), std::get<1>(std::get<0>(written.root().fParts).fParts).element());
  EXPECT_NE(after.find(">5<"), std::string::npos) << after;
  EXPECT_EQ(before.size(), after.size()) << before << "\n" << after;
  // And it is the page's: its events go to its model.
  auto &button = std::get<1>(page.root().fParts);
  fire(page, button.element(), "click", "");
  EXPECT_EQ(live.look<Count>()->n, 6);
  EXPECT_NE(dom::dump(counter.element()).find(">6<"), std::string::npos);
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

TEST(Web, AGrownChildKeepsTheLeastWidthItSays) {
  dom::reset();
  Model m;
  Page page(row(hbox(), Box({.grow = scene::axes::kX, .minWidth = 300.0f}), Box({.grow = scene::axes::kX})), m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  // The one that says a least width keeps it; the other may shrink to nothing.
  EXPECT_NE(html.find("min-width:300px;"), std::string::npos) << html;
  EXPECT_NE(html.find("min-width:0;"), std::string::npos) << html;
}
TEST(Web, PlacementDepthAndTheLooksOfASpecAreCss) {
  dom::reset();
  Model m;
  Page page(column(vbox(),
                   Box({.place = scene::anchor::kBottomRight,
                        .depth = 2.0f,
                        .corners = scene::Corners{1.0f, 2.0f, 3.0f, 4.0f},
                        .tail = scene::Tail{},
                        .background = 0xff445566u,
                        .hoverBackground = 0xff112233u,
                        .gradient = scene::Gradient{0xff000000u, 0xffffffffu},
                        .border = scene::Border{0xff00ff00u, 2.0f},
                        .shadow = scene::Shadow{0x80000000u, 4.0f}}),
                   row(hbox(), Text("a", {.grow = scene::axes::kY}), Box({.grow = scene::axes::kX})), Text("b", {.grow = scene::axes::kX})),
            m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  // Placed at the stack's bottom right by its own bottom right, over its siblings.
  EXPECT_NE(html.find("position:absolute;left:100%;top:100%;"), std::string::npos) << html;
  EXPECT_NE(html.find("z-index:2;"), std::string::npos);
  EXPECT_NE(html.find("transform:translate(-100%,-100%);"), std::string::npos) << html;
  EXPECT_NE(html.find("border-radius:1px 2px 3px 4px;"), std::string::npos);
  EXPECT_NE(html.find("background-image:linear-gradient(rgba(0,0,0,1),rgba(255,255,255,1));"), std::string::npos);
  EXPECT_NE(html.find("border:2px solid rgba(0,255,0,1);"), std::string::npos);
  EXPECT_NE(html.find("box-shadow:0 4px 8px rgba(0,0,0,0.502);"), std::string::npos) << html;
  // Under the pointer: a variable and an attribute the page's rules select on; a tail likewise.
  EXPECT_NE(html.find("data-skiff-hover=\"\""), std::string::npos) << html;
  EXPECT_NE(html.find("--skiff-hover-bg:rgba(17,34,51,1);"), std::string::npos);
  EXPECT_NE(html.find("data-skiff-tail=\"right\""), std::string::npos);
  EXPECT_NE(html.find("--skiff-bg:rgba(68,85,102,1);"), std::string::npos);
  EXPECT_NE(pageCss().find("[data-skiff-hover]:hover"), std::string::npos);
  // A stack does not stretch its children across its way, as a scene's flow does not.
  EXPECT_NE(html.find("flex-direction:column;align-items:flex-start;"), std::string::npos) << html;
  // Grown across the stack's way: its breadth; along it: a share.
  auto &inner = std::get<1>(page.root().fParts);
  const std::string a = dom::dump(std::get<0>(inner.fParts).element());
  EXPECT_NE(a.find("align-self:stretch;"), std::string::npos) << a;
  EXPECT_EQ(a.find("flex:"), std::string::npos) << a;
  const std::string grown = dom::dump(std::get<1>(inner.fParts).element());
  EXPECT_NE(grown.find("flex:1 1 0%;"), std::string::npos) << grown;
  const std::string b = dom::dump(std::get<2>(page.root().fParts).element());
  EXPECT_NE(b.find("align-self:stretch;"), std::string::npos) << b;
  EXPECT_EQ(b.find("flex:"), std::string::npos) << b;
}

// A picture's source of the test's own: where the picture is, when it is.
struct Shot {
  std::shared_ptr<scene::Picture> fPicture;
  const scene::Picture *operator()() const { return fPicture.get(); }
};

TEST(Web, FlowsScrollingPicturesAndIconsAreTheBrowsersOwn) {
  dom::reset();
  Model m;
  auto shot = std::make_shared<scene::Picture>();
  scene::IconShape shape{.marks = {{.shape = scene::mark::line{-6.0f, 0.0f, 6.0f, 0.0f}},
                                   {.shape = scene::mark::circle{0.0f, 0.0f, 4.0f}, .filled = true, .colour = 0xffff0000u}},
                         .rotation = 45.0f};
  Page page(column(vbox(),
                   Flow(scene::FlowOptions{.direction = scene::direction::horizontal{}, .spacingX = 4.0f, .spacingY = 2.0f, .wrap = true,
                                           .justify = scene::justify::space_between{}},
                        Text("a"), Text("b")),
                   ScrollContainer(Text("c")), Image(Shot{shot}), Icon(shape, 0xff00ff00u, {.width = 24.0f, .height = 12.0f})),
            m);
  page.mount();
  std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("class=\"skiff-flow\" style=\"display:flex;flex-direction:row;flex-wrap:wrap;column-gap:4px;row-gap:2px;align-items:flex-start;"
                      "justify-content:space-between;"),
            std::string::npos)
      << html;
  EXPECT_NE(html.find("class=\"skiff-scroll\" style=\"display:flex;flex-direction:column;overflow-x:hidden;overflow-y:auto;"), std::string::npos) << html;
  // No picture yet: no src; the icon's marks about its middle, rotated.
  EXPECT_NE(html.find("<img class=\"skiff-image\" style=\"object-fit:cover;\">"), std::string::npos) << html;
  EXPECT_NE(html.find("<svg class=\"skiff-icon\" width=\"24\" height=\"12\" viewBox=\"-12 -6 24 12\""), std::string::npos) << html;
  EXPECT_NE(html.find("<g transform=\"rotate(45)\"><line x1=\"-6\" y1=\"0\" x2=\"6\" y2=\"0\" fill=\"none\" stroke=\"rgba(0,255,0,1)\" "
                      "stroke-width=\"1.8\""),
            std::string::npos)
      << html;
  EXPECT_NE(html.find("<circle cx=\"0\" cy=\"0\" r=\"4\" fill=\"rgba(255,0,0,1)\" stroke=\"none\">"), std::string::npos) << html;
  // The picture arrives: shown at the next sync, asked for until then.
  shot->url = "data:image/png;base64,AA==";
  page.sync();
  html = dom::dump(dom::root());
  EXPECT_NE(html.find("src=\"data:image/png;base64,AA==\""), std::string::npos) << html;
  // Scrolled to the end: told to the element.
  auto &scroll = std::get<1>(page.root().fParts);
  scroll.scrollToEnd();
  page.sync();
  EXPECT_NE(dom::dump(scroll.element()).find("data-skiff-scroll=\"end\""), std::string::npos) << dom::dump(scroll.element());
  EXPECT_FALSE(dom::objectUrl(std::span<const unsigned char>(), "image/png").empty());
}

// A text reading a number: for a derived value.
struct Doubled : Text {
  Doubled() : Text("") {}
  void read(int n) { setText(std::to_string(n)); }
};

TEST(Web, StyledProjectedAndDerivedNodesAreThePagesToo) {
  dom::reset();
  Model m;
  Page page(scoped<Root>(handlers(),
                         column(styled({.width = 50.0f}, Text("s")), projected<Count>([](const Count &c) { return std::to_string(c.n); }, TextOf("0")),
                                spec_for<Count>([](const Count &c) { return scene::Spec{.visible = c.n > 0}; }, Box()),
                                text_for<Count>([](const Count &c) { return std::format("n={}", c.n); }, Text("")),
                                derived<Count>([](const Count &c) { return c.n * 2; }, Doubled()))),
            m);
  page.mount();
  std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("width:50px;"), std::string::npos) << html;
  EXPECT_NE(html.find(">0</span>"), std::string::npos) << html;
  EXPECT_NE(html.find("display:none;"), std::string::npos) << html;  // n is 0: the box is hidden
  EXPECT_NE(html.find(">n=0</span>"), std::string::npos) << html;
  m.apply(model::over<Count>([](Count c) {
    c.n = 3;
    return c;
  }));
  page.refresh();
  html = dom::dump(dom::root());
  EXPECT_NE(html.find(">3</span>"), std::string::npos) << html;
  EXPECT_EQ(html.find("display:none;"), std::string::npos) << html;
  EXPECT_NE(html.find(">n=3</span>"), std::string::npos) << html;
  EXPECT_NE(html.find(">6</span>"), std::string::npos) << html;
}

TEST(Web, ATextAsTheSceneMakesItAndAColouredBoxWithChildren) {
  dom::reset();
  Model m;
  Text styled("hello world", 13.0f, 0xff112233u, true);
  styled.setStyles({{.first = 0, .last = 5, .emphasis = true}, {.first = 6, .last = 11, .code = true}}, 0xff445566u);
  styled.setLinks({{.first = 6, .last = 11, .target = "https://x.y"}}, 0xff8ab4f8u);
  Page page(column(std::move(styled), Box(0xff202020u, Text("in a box"))), m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("font-size:13px;color:rgba(17,34,51,1);font-weight:600;"), std::string::npos) << html;
  EXPECT_NE(html.find("<span style=\"font-style:italic;\">hello</span>"), std::string::npos) << html;
  EXPECT_NE(html.find("<a href=\"https://x.y\" style=\"color:rgba(138,180,248,1);font-family:ui-monospace, monospace;"), std::string::npos) << html;
  EXPECT_NE(html.find("background-color:rgba(32,32,32,1);\"><span class=\"skiff-text\">in a box</span>"), std::string::npos) << html;
}

TEST(Core, ARectSaysWhatSkiasDoes) {
  scene::Rect r = scene::Rect::MakeXYWH(10.0f, 20.0f, 30.0f, 40.0f);
  EXPECT_EQ(r.fRight, 40.0f);
  EXPECT_EQ(r.fBottom, 60.0f);
  EXPECT_EQ(r.width(), 30.0f);
  EXPECT_EQ(r.centerY(), 40.0f);
  EXPECT_TRUE(r.contains(10.0f, 20.0f));
  EXPECT_FALSE(r.contains(40.0f, 20.0f));
  EXPECT_TRUE(scene::Rect::Intersects(r, scene::Rect::MakeWH(20.0f, 30.0f)));
  scene::Rect cut = r;
  EXPECT_TRUE(cut.intersect(scene::Rect::MakeWH(20.0f, 30.0f)));
  EXPECT_EQ(cut, scene::Rect::MakeLTRB(10.0f, 20.0f, 20.0f, 30.0f));
  EXPECT_FALSE(cut.intersect(scene::Rect::MakeXYWH(100.0f, 100.0f, 1.0f, 1.0f)));
  scene::Rect all;
  EXPECT_TRUE(all.isEmpty());
  all.join(r);
  all.join(scene::Rect::MakeWH(5.0f, 5.0f));
  EXPECT_EQ(all, scene::Rect::MakeLTRB(0.0f, 0.0f, 40.0f, 60.0f));
  EXPECT_EQ(r.makeInset(1.0f, 2.0f), scene::Rect::MakeLTRB(11.0f, 22.0f, 39.0f, 58.0f));
}

TEST(Web, ANodeHiddenAndShownAgainIsShownAsItWas) {
  dom::reset();
  Model m;
  Page page(column(vbox(), Text("x"), row(hbox(), Text("y"))), m);
  page.mount();
  auto &text = std::get<0>(page.root().fParts);
  auto &inner = std::get<1>(page.root().fParts);
  text.setVisible(false);
  inner.setVisible(false);
  page.sync();
  EXPECT_NE(dom::dump(text.element()).find("display:none;"), std::string::npos) << dom::dump(text.element());
  EXPECT_NE(dom::dump(inner.element()).find("display:none;"), std::string::npos);
  text.setVisible(true);
  inner.setVisible(true);
  page.sync();
  EXPECT_EQ(dom::dump(text.element()).find("display:"), std::string::npos) << dom::dump(text.element());
  EXPECT_NE(dom::dump(inner.element()).find("display:flex;"), std::string::npos) << dom::dump(inner.element());
  EXPECT_EQ(dom::dump(inner.element()).find("display:none"), std::string::npos);
}

TEST(Web, AStylesheetIsMountedBeforeTheTree) {
  dom::reset();
  Model m;
  mountStylesheet(dom::root(), ".skiff-text{color:red}");
  Page page(column(Text("x")), m);
  page.mount();
  EXPECT_EQ(dom::dump(dom::root()).find("<style>.skiff-text{color:red}</style><div"), 6u) << dom::dump(dom::root());
}


// A canvas: drawn by the node's own hand when synced, and pressed where it
// was clicked.
struct Plot : Canvas {
  using Out = model::Types<Clicked>;
  Press fLast;
  void pressed(Press press) { fLast = press; }
  Clicked onPress() { return {}; }
  template <class P> void paint(P &p) {
    p.clear(0xff000000u);
    const scene::Point tri[] = {{0.0f, 0.0f}, {10.0f, 0.0f}, {5.0f, 8.0f}};
    p.fillPolygon(tri, 0xffffffffu);
    p.line({0.0f, 0.0f}, {10.0f, 10.0f}, 1.0f, 0xff00ff00u);
    p.circle({5.0f, 5.0f}, 2.0f, 0xffff0000u);
    p.text({5.0f, 5.0f}, "x", 8.0f, 0xffffffffu);
  }
};
auto plotPage() {
  return scoped<Root>(handlers(handle<Clicked>([](const Clicked &, const auto &) {
                        return model::over<Count>([](Count c) {
                          ++c.n;
                          return c;
                        });
                      })),
                      column(Plot()));
}

TEST(Web, ACanvasIsDrawnByItsNodeAndPressedWhereItWasClicked) {
  dom::reset();
  Model m;
  Page page(plotPage(), m);
  page.mount();
  auto &plot = std::get<0>(page.root().fParts);
  plot.setSize(120, 80);
  page.sync();
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("<canvas class=\"skiff-canvas\" width=\"120\" height=\"80\""), std::string::npos) << html;
  const std::string drawn = dom::drawn(plot.element());
  EXPECT_TRUE(drawn.ends_with("size clear begin moveto lineto lineto close fill begin moveto lineto stroke circle text")) << drawn;
  fire(page, plot.element(), "contextmenu", "12.5,34,2");
  EXPECT_EQ(m.look<Count>()->n, 1);
  EXPECT_FLOAT_EQ(plot.fLast.at.fX, 12.5f);
  EXPECT_FLOAT_EQ(plot.fLast.at.fY, 34.0f);
  EXPECT_EQ(plot.fLast.button, 2);
  fire(page, plot.element(), "click", "not a place");
  EXPECT_EQ(m.look<Count>()->n, 1);
}


// A list: a row per element, as the model has them -- made as elements
// come, gone as they go, in the list's order; a press in a row delivered
// to the row's scope with its key.
struct Item {
  std::string name;
  int n = 0;
};
struct Gone {};
struct ListRoot {
  model::Keyed<std::string, Item> items;
};
using ListModel = model::Model<ListRoot, Reactions>;
struct ItemText : Text {
  ItemText() : Text("") {}
  void read(const Item &item) { setText(item.name + ":" + std::to_string(item.n)); }
};
struct Remover : Clicker {
  using Out = model::Types<Gone>;
  Gone onPress() { return {}; }
};
auto itemRow() {
  return scoped<Item>(handlers(handle<Gone>([](const auto &here) { return model::take<Item>(model::key<std::string>(here)); })),
                      row(hbox(4.0f), bound<Item>(ItemText()), Remover()));
}
auto listPage() { return column(vbox(), Text("items"), each<std::string, Item>([] { return itemRow(); })); }

TEST(Web, AListIsARowPerElementMadeAndGoneWithThemInOrder) {
  dom::reset();
  ListModel m;
  m.apply(model::put<Item>(std::string("b"), Item{"bee", 2}));
  m.apply(model::put<Item>(std::string("a"), Item{"ay", 1}));
  Page page(listPage(), m);
  page.mount();
  std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("bee:2"), std::string::npos) << html;
  EXPECT_NE(html.find("ay:1"), std::string::npos) << html;
  EXPECT_LT(html.find("bee:2"), html.find("ay:1")) << html;  // the list's order
  auto &rows = std::get<1>(page.root().fParts);
  EXPECT_EQ(rows.fRows.size(), 2u);
  // An element changed: its row reads it; one put: a row made; one taken: its row gone.
  m.apply(model::put<Item>(std::string("b"), Item{"bee", 3}));
  m.apply(model::put<Item>(std::string("c"), Item{"sea", 0}));
  page.refresh();
  html = dom::dump(dom::root());
  EXPECT_NE(html.find("bee:3"), std::string::npos) << html;
  EXPECT_NE(html.find("sea:0"), std::string::npos) << html;
  EXPECT_EQ(rows.fRows.size(), 3u);
  m.apply(model::take<Item>(std::string("a")));
  page.refresh();
  html = dom::dump(dom::root());
  EXPECT_EQ(html.find("ay:1"), std::string::npos) << html;
  EXPECT_EQ(rows.fRows.size(), 2u);
  // A press in the second row: delivered along its path, with its key.
  auto &button = std::get<1>(rows.fRows[1].fParts);
  fire(page, button.element(), "click", "");
  html = dom::dump(dom::root());
  EXPECT_EQ(html.find("sea:0"), std::string::npos) << html;
  EXPECT_NE(html.find("bee:3"), std::string::npos) << html;
  EXPECT_EQ(rows.fRows.size(), 1u);
  EXPECT_EQ(rows.fKeys, std::vector<std::string>{"b"});
}


// A figure: shapes as nodes in an svg, each bound to its part of the model
// and written to its element as the part moves; a list of them as many as
// the model has.
struct Dot {
  float x = 0.0f, y = 0.0f;
  bool lit = false;
};
struct DotsRoot {
  model::Keyed<int, Dot> dots;
  model::Tracked<Count> count;
};
using DotsModel = model::Model<DotsRoot, Reactions>;
struct DotShape : Circle {
  explicit DotShape(const Dot &dot) : Circle({dot.x, dot.y}, 3.0f, 0xff808080u) {}
  void read(const Dot &dot) {
    setCentre({dot.x, dot.y});
    setFill(dot.lit ? 0xffff0000u : 0xff808080u);
  }
};
struct CountLine : Line {
  CountLine() : Line({0.0f, 0.0f}, {0.0f, 0.0f}, 0xff00ff00u, 2.0f) {}
  void read(const Count &c) { setEnds({0.0f, 0.0f}, {static_cast<float>(c.n), 0.0f}); }
};
auto dotsPage() {
  return column(figure(100.0f, 50.0f, Polygon({{0.0f, 0.0f}, {10.0f, 0.0f}, {5.0f, 8.0f}}, 0xff0000ffu), bound<Count>(CountLine()),
                       group<int, Dot>([](const Dot &dot) { return bound<Dot>(DotShape(dot)); })));
}

TEST(Web, AFigureIsShapesAsNodesEachWrittenAsItsPartMoves) {
  dom::reset();
  DotsModel m;
  m.apply(model::put<Dot>(1, Dot{10.0f, 20.0f, false}));
  m.apply(model::put<Dot>(2, Dot{30.0f, 40.0f, true}));
  Page page(dotsPage(), m);
  page.mount();
  std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("<svg class=\"skiff-figure\" width=\"100\" height=\"50\" viewBox=\"0 0 100 50\">"), std::string::npos) << html;
  EXPECT_NE(html.find("<polygon points=\"0,0 10,0 5,8\" fill=\"#0000ff\" stroke=\"none\" stroke-width=\"0\">"), std::string::npos) << html;
  EXPECT_NE(html.find("<line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"0\" fill=\"none\" stroke=\"#00ff00\" stroke-width=\"2\">"), std::string::npos) << html;
  EXPECT_NE(html.find("<g><circle cx=\"10\" cy=\"20\" r=\"3\" fill=\"#808080\""), std::string::npos) << html;
  EXPECT_NE(html.find("<circle cx=\"30\" cy=\"40\" r=\"3\" fill=\"#ff0000\""), std::string::npos) << html;
  m.apply(model::put<Dot>(1, Dot{11.0f, 20.0f, true}));
  m.apply(model::take<Dot>(2));
  m.apply(model::over<Count>([](Count c) {
    c.n = 42;
    return c;
  }));
  page.refresh();
  html = dom::dump(dom::root());
  EXPECT_NE(html.find("<circle cx=\"11\" cy=\"20\" r=\"3\" fill=\"#ff0000\""), std::string::npos) << html;
  EXPECT_EQ(html.find("cx=\"30\""), std::string::npos) << html;
  EXPECT_NE(html.find("x2=\"42\""), std::string::npos) << html;
}

// A model said by its tree, on the page as on the scene: each view keeps
// its part (state<T>), the model is the parts the views say.
struct Shown {
  bool on = false;
};
auto clicks() {
  return state<Count>(handlers(handle<Clicked>([](const Clicked &, const auto &) {
                        return model::over<Count>([](Count c) {
                          ++c.n;
                          return c;
                        });
                      })),
                      row(hbox(8.0f), bound<Count>(Counter()), Clicker()));
}
auto statedPage() { return column(vbox(8.0f), clicks(), state<Shown>(Text("shown")), state<Count>(Text("again"))); }

TEST(Web, AModelIsThePartsItsTreesViewsSayTheyKeep) {
  using Tree = decltype(statedPage());
  static_assert(std::same_as<bind::PartsOf<Tree>, model::Types<model::Tracked<Count>, model::Tracked<Shown>>>);
  dom::reset();
  bind::ModelOf<Tree> m;
  Page page(statedPage(), m);
  page.mount();
  auto &clicked = std::get<0>(page.root().fParts);
  auto &button = std::get<1>(clicked.fParts);
  fire(page, button.element(), "click", "");
  fire(page, button.element(), "click", "");
  EXPECT_EQ(m.look<Count>()->n, 2);
  EXPECT_EQ(std::get<0>(clicked.fParts).text(), "2");
}

} // namespace

// A page written out as HTML from its tree alone -- no document made, not an
// element -- as the model says: what a server writes for a browser.
TEST(Web, APageIsWrittenAsHtmlFromItsTreeWithNoDocument) {
  dom::reset();
  Model m;
  (void)m.apply(model::over<Count>([](Count c) {
    c.n = 3;
    return c;
  }));
  Page page(counterPage(), m);
  const std::string html = page.write();
  EXPECT_EQ(dom::children(dom::root()), 0u);
  EXPECT_NE(html.find("<span class=\"skiff-text\">Clicks</span><span class=\"skiff-text\">3</span>"), std::string::npos) << html;
  EXPECT_NE(html.find("<button></button>"), std::string::npos) << html;
  EXPECT_NE(html.find("<div class=\"skiff-stack\" style=\"display:flex;flex-direction:column;gap:8px;"), std::string::npos) << html;
}

// What a node makes in itself -- runs of a text, here -- is written with it,
// and an event on one of them comes back to the node by its Part.
namespace {
struct Picked {
  int part = -1;
};
struct TwoButtons : Node {
  int fPicked = -1;
  void markup(Markup &to) {
    to.attr("class", "two");
    for (std::uint32_t i = 1; i <= 2; ++i)
      to.inner("button", Part{i}, [&](Markup &b) {
        b.text(i == 1 ? "one" : "two");
        b.listen("click");
      });
  }
  bool onDom(Part where, std::string_view event, std::string_view) {
    if (event != "click") return false;
    fPicked = static_cast<int>(where.index);
    return false;
  }
};
} // namespace
TEST(Web, AnEventOnAnElementANodeMadeInItselfComesBackByItsPart) {
  dom::reset();
  Model m;
  Page page(TwoButtons(), m);
  const std::string written = page.write();
  EXPECT_NE(written.find("<div class=\"two\"><button>one</button><button>two</button></div>"), std::string::npos) << written;
  page.mount();
  const dom::Handle second = dom::child(page.root().element(), 1);
  ASSERT_NE(second, dom::kNone);
  fire(page, second, "click", "");
  EXPECT_EQ(page.root().fPicked, 2);
  // Written again where nothing changed: the same elements.
  page.root().markDirty();
  page.sync();
  EXPECT_EQ(dom::child(page.root().element(), 1), second);
}

// A page kept on the server: sent as one stream only ever appended to. Its
// containers a generation's shadow root, its leaves islands at their slots;
// what changed sent again as a new version, the old one hidden; a press and
// what a field holds posted as a form.
namespace {
// A field of the test's own: a text input whose value is the model's name.
struct Name {
  std::string text;
};
struct NamedRoot {
  model::Tracked<Name> name;
  model::Tracked<Count> count;
};
using NamedModel = model::Model<NamedRoot, Reactions>;
struct Typed {
  std::string text;
};
struct NameField : Node {
  using Out = model::Types<Typed>;
  std::string fText;
  [[nodiscard]] static constexpr std::string_view tag() { return "input"; }
  void read(const Name &now) {
    if (now.text != fText) {
      fText = now.text;
      markDirty();
    }
  }
  void markup(Markup &to) {
    to.attr("type", "text");
    to.listen("input");
    to.value(fText);
  }
  bool onDom(Part, std::string_view event, std::string_view value) {
    if (event != "input" && event != "change") return false;
    fText = std::string(value);
    return true;
  }
  Typed onPress() { return {fText}; }
};
struct NameText : Text {
  NameText() : Text("") {}
  void read(const Name &now) { setText("hello " + now.text); }
};
auto namedPage() {
  return scoped<NamedRoot>(handlers(handle<Typed>([](const Typed &t, const auto &) { return model::over<Name>([&](Name) { return Name{t.text}; }); }),
                                    handle<Clicked>([](const Clicked &, const auto &) {
                                      return model::over<Count>([](Count c) {
                                        ++c.n;
                                        return c;
                                      });
                                    })),
                           column(vbox(8.0f), bound<Name>(NameText()), bound<Name>(NameField()), bound<Count>(Counter()), Clicker()));
}
// The id of the island a node is, as the page names it ("nID").
template <class N> std::string island(const N &node) { return "n" + std::to_string(node.element()); }
} // namespace

TEST(Live, ThePageIsAGenerationOfContainersWithItsLeavesAtTheirSlots) {
  NamedModel m;
  Live page(namedPage(), m, {.action = "/e?s=1", .css = ".x{}"});
  const std::string html = page.begin();
  EXPECT_NE(html.find("<form id=\"skiff-f\" method=\"post\" action=\"/e?s=1\" target=\"skiff-sink\">"), std::string::npos) << html;
  EXPECT_NE(html.find("<skiff-live id=\"skiff-g1\"><template shadowrootmode=\"open\"><style>.x{}"), std::string::npos) << html;
  auto &parts = page.root().fParts;
  const auto &text = std::get<0>(parts);
  // In the shadow root, the column and a slot for each leaf; after it, each leaf at its slot.
  const std::string slot = "<slot name=\"" + island(text) + "\"></slot>";
  EXPECT_NE(html.find("<div class=\"skiff-stack\""), std::string::npos) << html;
  EXPECT_LT(html.find(slot), html.find("</template>")) << html;
  EXPECT_NE(html.find("slot=\"" + island(text) + "\" id=\"" + island(text) + "v"), std::string::npos) << html;
  EXPECT_GT(html.find(">hello </span>"), html.find("</template>")) << html;
  // The field is the form's, the button a submit of its press.
  const auto &field = std::get<1>(parts);
  EXPECT_NE(html.find("form=\"skiff-f\" name=\"v." + std::to_string(field.element()) + ".0."), std::string::npos) << html;
  const auto &button = std::get<3>(parts);
  EXPECT_NE(html.find("type=\"submit\" form=\"skiff-f\" name=\"e\" value=\"" + std::to_string(button.element()) + ".0.click\""), std::string::npos) << html;
  EXPECT_EQ(page.update(), "");  // nothing moved: nothing sent
  // The script that makes it better where there is script, in the head.
  EXPECT_LT(html.find("<script>"), html.find("</head>")) << html;
  NamedModel bare;
  Live without(namedPage(), bare, {.script = false});
  EXPECT_EQ(without.begin().find("<script>"), std::string::npos);
  // A container's own element changed: the tree sent again, the old generation hidden.
  page.root().setVisible(false);
  const std::string again = page.update();
  EXPECT_NE(again.find("</skiff-live>\n<style>#skiff-g1{display:none!important}</style>"), std::string::npos) << again;
  EXPECT_NE(again.find("<skiff-live id=\"skiff-g2\">"), std::string::npos) << again;
  EXPECT_NE(again.find(">hello </span>"), std::string::npos) << again;
}

TEST(Live, APostedFormIsWhatItsFieldsHoldThenItsPressAndWhatMovedIsSentAgain) {
  NamedModel m;
  Live page(namedPage(), m, {});
  const std::string first = page.begin();
  auto &parts = page.root().fParts;
  const auto &text = std::get<0>(parts);
  const auto &field = std::get<1>(parts);
  const auto &counter = std::get<2>(parts);
  const auto &button = std::get<3>(parts);
  // The field's name as the page has it, its version among it.
  const std::string prefix = "name=\"v." + std::to_string(field.element()) + ".0.";
  const std::size_t at = first.find(prefix) + prefix.size();
  const std::string version = first.substr(at, first.find('"', at) - at);
  page.post("v." + std::to_string(field.element()) + ".0." + version + "=ann+%26+bo&e=" + std::to_string(button.element()) + ".0.click");
  EXPECT_EQ(m.look<Name>()->text, "ann & bo");
  EXPECT_EQ(m.look<Count>()->n, 1);
  const std::string next = page.update();
  // The text and the count sent again, each a new version, the old ones hidden.
  EXPECT_NE(next.find("hello ann &amp; bo"), std::string::npos) << next;
  EXPECT_NE(next.find("<style>#" + island(text) + "v"), std::string::npos) << next;
  EXPECT_NE(next.find(">1</span>"), std::string::npos) << next;
  EXPECT_NE(next.find("slot=\"" + island(counter) + "\""), std::string::npos) << next;
  EXPECT_EQ(next.find("skiff-live"), std::string::npos) << next;  // no new generation
  // The field holds what was typed: taken as the page shows it, not sent
  // again under the hand typing.
  EXPECT_EQ(next.find("slot=\"" + island(field) + "\""), std::string::npos) << next;
  // The field's version is still the one shown, and takes what is typed
  // next; one posted from a version not shown is not taken.
  page.post("v." + std::to_string(field.element()) + ".0." + version + "=cy");
  EXPECT_EQ(m.look<Name>()->text, "cy");
  page.post("v." + std::to_string(field.element()) + ".0.0=stale");
  EXPECT_EQ(m.look<Name>()->text, "cy");
}

TEST(Live, AListsRowsComeAndGoAtItsSlotAndARowBetweenOthersIsANewGeneration) {
  ListModel m;
  m.apply(model::put<Item>(std::string("b"), Item{"bee", 2}));
  Live page(listPage(), m, {});
  const std::string first = page.begin();
  auto &rows = std::get<1>(page.root().fParts);
  const std::string slot = "<slot name=\"" + island(rows) + "\"></slot>";
  EXPECT_LT(first.find(slot), first.find("</template>")) << first;
  EXPECT_NE(first.find("slot=\"" + island(rows.fRows[0]) + "\""), std::string::npos) << first;
  EXPECT_NE(first.find("bee:2"), std::string::npos) << first;
  // One come after: sent at the list's slot, by its place.
  m.apply(model::put<Item>(std::string("c"), Item{"sea", 0}));
  std::string next = page.update();
  EXPECT_NE(next.find("sea:0"), std::string::npos) << next;
  EXPECT_NE(next.find("order:1;"), std::string::npos) << next;
  EXPECT_EQ(next.find("bee:2"), std::string::npos) << next;
  EXPECT_EQ(next.find("skiff-live"), std::string::npos) << next;
  // One gone: hidden.
  const std::string gone = island(rows.fRows[0]);
  m.apply(model::take<Item>(std::string("b")));
  next = page.update();
  EXPECT_NE(next.find("<style>#" + gone + "v"), std::string::npos) << next;
  EXPECT_EQ(next.find("skiff-live"), std::string::npos) << next;
  // One put after the rest (a list keeps what it was given in its order):
  // sent at the slot, after the one there.
  m.apply(model::put<Item>(std::string("a"), Item{"ay", 1}));
  next = page.update();
  EXPECT_NE(next.find("ay:1"), std::string::npos) << next;
  EXPECT_EQ(next.find("skiff-live"), std::string::npos) << next;
}

TEST(Live, AFormsBodyIsReadOnceIntoPressesAndFields) {
  const std::vector<live::Posted> all = live::form("e=12.3.click&v.4.0.7=a+b%2Bc%25&other=1&v.x=2");
  ASSERT_EQ(all.size(), 2u);
  // What the test made, in the order it made them: each one alternative, known.
  const auto &press = std::get<live::Press>(all[0]);
  EXPECT_EQ(press.node, 12u);
  EXPECT_EQ(press.part.index, 3u);
  EXPECT_EQ(press.kind, "click");
  const auto &typed = std::get<live::Typed>(all[1]);
  EXPECT_EQ(typed.node, 4u);
  EXPECT_EQ(typed.version, 7u);
  EXPECT_EQ(typed.value, "a b+c%");
}

// A part that is the browser's own (client): a model of its own, not the
// tree's; a press in it its own page's; on a page kept on the server, written
// once for the browser to take over.
namespace {
auto hybridPage() { return column(vbox(), state<Shown>(Text("server")), client(clicks())); }
} // namespace

TEST(Client, APartWithAModelOfItsOwnIsNotTheTreesAndAPressInItIsItsOwn) {
  using Tree = decltype(hybridPage());
  static_assert(std::same_as<bind::PartsOf<Tree>, model::Types<model::Tracked<Shown>>>);
  dom::reset();
  bind::ModelOf<Tree> m;
  Page page(hybridPage(), m);
  page.mount();
  auto &part = std::get<1>(page.root().fParts);
  auto &clicked = part.client().root();
  auto &button = std::get<1>(clicked.fParts);
  fire(page, button.element(), "click", "");
  EXPECT_EQ(part.model().look<Count>()->n, 1);
  EXPECT_EQ(std::get<0>(clicked.fParts).text(), "1");
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("<skiff-client data-skiff-client=\"0\""), std::string::npos) << html;
  EXPECT_NE(html.find(">1</span>"), std::string::npos) << html;
  EXPECT_TRUE(page.owns(button.element()));
}

TEST(Client, OnAPageKeptOnTheServerAClientIsWrittenOnceForTheBrowser) {
  bind::ModelOf<decltype(hybridPage())> m;
  Live page(hybridPage(), m, {});
  const std::string html = page.begin();
  const std::size_t at = html.find("<skiff-client data-skiff-client=\"0\"");
  ASSERT_NE(at, std::string::npos) << html;
  EXPECT_GT(at, html.find("</template>")) << html;  // an island, at its slot
  const std::string inside = html.substr(at, html.find("</skiff-client>", at) - at);
  EXPECT_NE(inside.find(">0</span>"), std::string::npos) << inside;
  EXPECT_EQ(inside.find("form=\"skiff-f\""), std::string::npos) << inside;  // the browser's, not the form's
  EXPECT_EQ(page.update(), "");
  // Found by the browser by its index, where the server wrote it.
  dom::reset();
  const dom::Handle written = dom::create("skiff-client");
  dom::attr(written, "data-skiff-client", "0");
  dom::append(dom::root(), written);
  EXPECT_EQ(dom::marked("data-skiff-client", "0"), written);
  EXPECT_EQ(dom::marked("data-skiff-client", "1"), dom::kNone);
}
