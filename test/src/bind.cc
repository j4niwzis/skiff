import std;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;

#include "gtest/gtest-macros.h"

// A page of accounts, bound to a model: each row a scope of its account,
// its parts showing the account's parts and asking to change them, its
// button sending an event its scope takes.

namespace {

using namespace skiff;

struct ReadReceipts {
  bool on = true;
};
struct DisplayName {
  std::string text;
};
struct AccountT {
  std::string who;
  DisplayName name;
  ReadReceipts receipts;
};
struct SettingsT {
  model::Keyed<std::string, AccountT> accounts;
};
struct Root {
  model::Tracked<SettingsT> settings;
};

struct WriteSettings {
  bool replaces(const WriteSettings &) const { return true; }
};
using Effect = std::variant<WriteSettings>;
struct Reactions {
  auto on(model::Changed<SettingsT>, const SettingsT &) const {
    return WriteSettings{};
  }
};
using Model = model::Model<Root, Reactions, Effect>;

struct Switch : scene::Node {
  bool fOn = false;
  int fReads = 0;
  void read(const ReadReceipts &now) {
    fOn = now.on;
    ++fReads;
  }
};
struct Label : scene::Node {
  std::string fText;
  bool fGone = false;
  void read(const DisplayName &now) { fText = now.text; }
  void gone() { fGone = true; }
};
struct Removed {};
struct RemoveButton : scene::Node {
  std::vector<Removed> fEmitted;
};
// One that says what a press does: returned, not kept.
struct PressRemove : scene::Node {
  int fPressed = 0;
  Removed onPress() {
    ++fPressed;
    return {};
  }
};

struct AccountEvents {
  template <class P> auto on(const Removed &, const P &here) const {
    return model::take<AccountT>(here.template key<std::string>());
  }
};

struct AccountRow : bind::Scoped<AccountT, AccountEvents, scene::Node> {
  struct parts_t {
    bind::Bound<DisplayName, Label> name;
    bind::Bound<ReadReceipts, Switch, model::Flip> receipts;
    RemoveButton remove;
    PressRemove press;
  } parts;
};
struct Page : scene::Node {
  struct parts_t {
    bind::Each<std::string, AccountT, AccountRow, scene::Node> accounts;
  } parts;
};

Model twoAccounts() {
  Root root;
  auto &list = root.settings.fValue.accounts;
  list.put("@a:x.org", AccountT{"@a:x.org", {"A"}, {true}});
  list.put("@b:x.org", AccountT{"@b:x.org", {"B"}, {false}});
  return Model(std::move(root));
}

TEST(Bind, RowsAreTheListsElementsAndShowThem) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  auto &rows = page.parts.accounts.fRows;
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].parts.name.fText, "A");
  EXPECT_EQ(rows[1].parts.name.fText, "B");
  EXPECT_TRUE(rows[0].parts.receipts.fOn);
  EXPECT_FALSE(rows[1].parts.receipts.fOn);
}

TEST(Bind, AChangeIsMadeWhereTheNodeIsAndOnlyItsRowReadsAgain) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  auto &rows = page.parts.accounts.fRows;
  rows[0].parts.receipts.change(model::flip);
  bind::drain(page, m);
  const auto a = m.look(model::placeOf<ReadReceipts, Root>(std::string("@a:x.org")));
  ASSERT_TRUE(a);
  EXPECT_FALSE(a->on);
  bind::refresh(page, m);
  EXPECT_FALSE(rows[0].parts.receipts.fOn);
  EXPECT_EQ(rows[0].parts.receipts.fReads, 2);
  EXPECT_EQ(rows[1].parts.receipts.fReads, 1);
  // The settings changed: written, once.
  EXPECT_EQ(m.outbox().drain().size(), 1u);
}

TEST(Bind, AnEventIsTakenByTheRowsScope) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  page.parts.accounts.fRows[1].parts.remove.fEmitted.push_back(Removed{});
  bind::drain(page, m);
  EXPECT_EQ(m.root().settings.fValue.accounts.size(), 1u);
  bind::refresh(page, m);
  ASSERT_EQ(page.parts.accounts.fRows.size(), 1u);
  EXPECT_EQ(page.parts.accounts.fRows[0].parts.name.fText, "A");
}

// An event nothing in the tree nor the model takes: to the program's sink.
struct Opened {
  int which = 0;
};
struct OpenButton : scene::Node {
  std::vector<Opened> fEmitted;
};
struct Window : scene::Node {
  struct parts_t {
    OpenButton open;
  } parts;
};
struct Program {
  std::vector<int> opened;
  void take(const Opened &e) { opened.push_back(e.which); }
};
TEST(Bind, AnEventNothingTakesGoesToTheProgramsSink) {
  Model m = twoAccounts();
  Window window;
  Program program;
  window.parts.open.fEmitted.push_back(Opened{7});
  bind::drain(window, m, &program);
  ASSERT_EQ(program.opened.size(), 1u);
  EXPECT_EQ(program.opened[0], 7);
}

// A tree bound to two models: each binding binds its own nodes and passes
// the other's by.
struct Volume {
  int level = 3;
};
struct OtherRoot {
  model::Tracked<Volume> volume;
};
struct OtherReactions {};
using OtherModel = model::Model<OtherRoot, OtherReactions>;
struct VolumeLabel : scene::Node {
  int fLevel = 0;
  void read(const Volume &now) { fLevel = now.level; }
};
struct TwoModels : scene::Node {
  struct parts_t {
    bind::Bound<ReadReceipts, Switch, model::Flip> receipts;
    bind::Bound<Volume, VolumeLabel> volume;
  } parts;
};
TEST(Bind, ATreeIsBoundToTwoModelsEachItsOwn) {
  Root root;
  root.settings.fValue.accounts.put("@a:x.org", AccountT{"@a:x.org", {"A"}, {true}});
  OtherModel other{OtherRoot{}};
  TwoModels tree;
  bind::refresh(tree, other);
  EXPECT_EQ(tree.parts.volume.fLevel, 3);
  EXPECT_EQ(tree.parts.receipts.fReads, 0);
}

TEST(Bind, AnEditFromElsewhereIsShown) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  const auto name = model::placeOf<DisplayName, Root>(std::string("@b:x.org"));
  using P = decltype(name)::PathType;
  m.apply(model::Edit<Root, P, model::SetTo<DisplayName>>{
      name, model::setTo(DisplayName{"Bee"})});
  bind::refresh(page, m);
  EXPECT_EQ(page.parts.accounts.fRows[1].parts.name.fText, "Bee");
}

} // namespace

namespace {
TEST(Bind, APressIsDeliveredAlongItsPathAndTakenByTheRowsScope) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  // The page's list (0), its second row (1), the row's press (3): the path
  // the scene finds to it.
  EXPECT_TRUE(bind::press(page, m, scene::Path{0, 1, 3}));
  EXPECT_EQ(page.parts.accounts.fRows[1].parts.press.fPressed, 1);
  ASSERT_EQ(m.root().settings.fValue.accounts.size(), 1u);
  EXPECT_TRUE(m.root().settings.fValue.accounts.contains("@a:x.org"));
  // A path that leads nowhere: nothing pressed, nothing sent.
  EXPECT_FALSE(bind::press(page, m, scene::Path{0, 7, 3}));
  EXPECT_EQ(m.root().settings.fValue.accounts.size(), 1u);
}
}  // namespace
