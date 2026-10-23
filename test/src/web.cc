// SPDX-License-Identifier: AGPL-3.0-only
import std;
import gtest;
import skiff.core;
import skiff.model;
import skiff.bind;
import skiff.web;

#include "gtest/gtest-macros.h"

// A page: the tree mounted as the document's elements, each control showing
// its part of the model; what is done to a control sets its part, and what
// the model says is written back to the page.

namespace {
using namespace skiff;
using namespace skiff::web;

enum class Look { kSolid, kFrosted };
struct Settings {
  bool sound = false;
  float volume = 0.5f;
  int lines = 3;
  Look look = Look::kSolid;
  std::string name;
};
struct Root {
  model::Tracked<Settings> settings;
};
struct Reactions {};
using Model = model::Model<Root, Reactions>;

auto settingsPage() {
  return column(vbox(8.0f, {.padding = scene::Margin::all(12.0f)}),
                row(hbox(8.0f), Text("Sound"), bound<model::Field<&Settings::sound>>(ToggleField())),
                row(hbox(8.0f), Text("Volume"), bound<model::Field<&Settings::volume>>(SliderField<float>(0.0f, 1.0f))),
                bound<model::Field<&Settings::lines>>(SliderField<int>(1, 11)),
                bound<model::Field<&Settings::look>>(ChoiceTabs<Look>({{"Solid", Look::kSolid}, {"Frosted", Look::kFrosted}})),
                bound<model::Field<&Settings::name>>(TextField<std::string>("Name")),
                bound<model::Field<&Settings::look>>(Dropdown<Look>({{"Solid", Look::kSolid}, {"Frosted", Look::kFrosted}})));
}
using PageTree = decltype(settingsPage());

TEST(Web, TheTreeIsMountedAsElementsShowingTheModel) {
  dom::reset();
  Model m;
  Page page(settingsPage(), m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("<input class=\"skiff-toggle\" type=\"checkbox\""), std::string::npos) << html;
  EXPECT_NE(html.find("type=\"range\""), std::string::npos);
  EXPECT_NE(html.find("value=\"500\""), std::string::npos) << html;       // volume 0.5 of 1000
  EXPECT_NE(html.find("role=\"tablist\""), std::string::npos);
  EXPECT_NE(html.find("aria-selected=\"true\""), std::string::npos);
  EXPECT_NE(html.find("<select"), std::string::npos);
  EXPECT_NE(html.find("display:flex;"), std::string::npos);
  EXPECT_NE(html.find("padding:12px 12px 12px 12px;"), std::string::npos) << html;
  EXPECT_NE(html.find("<span class=\"skiff-text\">Sound</span>"), std::string::npos) << html;
  EXPECT_NE(html.find("<style>.skiff-text{"), std::string::npos) << html.substr(0, 200);
}

TEST(Web, WhatIsDoneToAControlSetsItsPartAndThePageShowsIt) {
  dom::reset();
  Model m;
  Page page(settingsPage(), m);
  page.mount();
  auto &sound = std::get<1>(std::get<0>(page.root().fParts).fParts);
  auto &volume = std::get<1>(std::get<1>(page.root().fParts).fParts);
  auto &look = std::get<3>(page.root().fParts);
  auto &name = std::get<4>(page.root().fParts);
  auto &pick = std::get<5>(page.root().fParts);

  fire(page, sound.element(), "change", "");
  EXPECT_TRUE(m.look<Settings>()->sound);
  EXPECT_TRUE(sound.on());

  fire(page, volume.element(), "input", "250");
  EXPECT_NEAR(m.look<Settings>()->volume, 0.25f, 0.001f);
  EXPECT_NE(dom::dump(volume.element()).find("value=\"250\""), std::string::npos);

  fire(page, look.tabs()[1], "click", "");
  EXPECT_EQ(m.look<Settings>()->look, Look::kFrosted);
  EXPECT_EQ(look.selected(), 1);
  EXPECT_EQ(pick.selected(), 1);  // the select shows the same part
  EXPECT_NE(dom::dump(look.tabs()[1]).find("aria-selected=\"true\""), std::string::npos);

  fire(page, name.element(), "input", "bob");
  EXPECT_EQ(m.look<Settings>()->name, "bob");

  fire(page, pick.element(), "change", "0");
  EXPECT_EQ(m.look<Settings>()->look, Look::kSolid);
  EXPECT_EQ(look.selected(), 0);
}

TEST(Web, AnEditMadeElsewhereIsWrittenToThePage) {
  dom::reset();
  Model m;
  Page page(settingsPage(), m);
  page.mount();
  auto &volume = std::get<1>(std::get<1>(page.root().fParts).fParts);
  m.apply(model::over<Settings>([](Settings s) {
    s.volume = 1.0f;
    s.name = "loud";
    return s;
  }));
  page.refresh();
  EXPECT_NE(dom::dump(volume.element()).find("value=\"1000\""), std::string::npos);
  auto &name = std::get<4>(page.root().fParts);
  EXPECT_EQ(name.text(), "loud");
  EXPECT_NE(dom::dump(name.element()).find("value=\"loud\""), std::string::npos);
}

struct Clicked {};
struct Count {
  int n = 0;
};
struct CountRoot {
  model::Tracked<Count> count;
};
using CountModel = model::Model<CountRoot, Reactions>;

TEST(Web, AButtonSendsItsEventToTheScopeAroundIt) {
  dom::reset();
  CountModel m;
  Page page(scoped<CountRoot>(handlers(handle<Clicked>([](const Clicked &, const auto &) {
                                return model::over<Count>([](Count c) {
                                  ++c.n;
                                  return c;
                                });
                              })),
                              column(SendButton<Clicked>("Count", Clicked{}))),
            m);
  page.mount();
  auto &button = std::get<0>(page.root().fParts);
  EXPECT_NE(dom::dump(button.element()).find("<button class=\"skiff-button\" type=\"button\">Count</button>"), std::string::npos) << dom::dump(dom::root());
  fire(page, button.element(), "click", "");
  fire(page, button.element(), "click", "");
  EXPECT_EQ(m.look<Count>()->n, 2);
}

struct SoundRow : Named<SoundRow, decltype(row(hbox(8.0f), Text("Sound"), bound<model::Field<&Settings::sound>>(ToggleField())))> {
  using Named::Named;
  static constexpr const char *viewName() { return "SoundRow"; }
};

TEST(Web, ANamedViewIsWrittenToThePageByItsName) {
  dom::reset();
  Model m;
  Page page(column(SoundRow(row(hbox(8.0f), Text("Sound"), bound<model::Field<&Settings::sound>>(ToggleField())))), m);
  page.mount();
  EXPECT_NE(dom::dump(dom::root()).find("data-view=\"SoundRow\""), std::string::npos) << dom::dump(dom::root());
}

TEST(Web, ASpecIsCssOnTheElement) {
  dom::reset();
  Model m;
  Page page(column(Box({.width = 120.0f, .height = 40.0f, .cornerRadius = 6.0f, .background = 0xff336699u, .alpha = 0.5f}),
                   Text("hidden", {.visible = false})),
            m);
  page.mount();
  const std::string html = dom::dump(dom::root());
  EXPECT_NE(html.find("width:120px;"), std::string::npos) << html;
  EXPECT_NE(html.find("height:40px;"), std::string::npos);
  EXPECT_NE(html.find("border-radius:6px;"), std::string::npos);
  EXPECT_NE(html.find("background-color:rgba(51,102,153,1);"), std::string::npos) << html;
  EXPECT_NE(html.find("opacity:0.5;"), std::string::npos);
  EXPECT_NE(html.find("display:none;"), std::string::npos);
}

} // namespace
