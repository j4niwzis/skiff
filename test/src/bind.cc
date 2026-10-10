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
  // Pressed: flipped, where it is bound.
  auto onPress() { return bind::own(model::flip); }
};
struct Label : scene::Node {
  std::string fText;
  bool fGone = false;
  void read(const DisplayName &now) { fText = now.text; }
  void gone() { fGone = true; }
};
struct Removed {};
struct RemoveButton : scene::Node {
  Removed onPress() { return {}; }
};
// One whose key handler returns what it asks for.
struct KeyRemove : scene::Node {
  using Answer = Removed;
  std::optional<Removed> onKey(scene::phase::target, const scene::key::down &press, scene::Reply &reply) {
    if (press.key != scene::keys::kDelete)
      return std::nullopt;
    reply.handle();
    return Removed{};
  }
  using Node::onKey;
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
    bind::Bound<ReadReceipts, Switch> receipts;
    RemoveButton remove;
    PressRemove press;
    KeyRemove key;
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

template <class Held> struct HeldPage : scene::Node {
  struct Parts { Held page; } parts;
};

template <class Variant>
void checkHeldPage() {
  Model m = twoAccounts();
  HeldPage<Variant> page;
  auto& active = page.parts.page.template emplace<Page>();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  ASSERT_EQ(active.parts.accounts.fRows.size(), 2u);
  EXPECT_EQ(active.parts.accounts.fRows[0].parts.name.fText, "A");
  m.apply(model::put<AccountT>(std::string("@a:x.org"),
                              AccountT{"@a:x.org", {"Changed"}, {false}}));
  binding.refresh(page, m);
  EXPECT_EQ(active.parts.accounts.fRows[0].parts.name.fText, "Changed");
  EXPECT_FALSE(active.parts.accounts.fRows[0].parts.receipts.fOn);
}

TEST(Bind, PartsHoldingVariantPagesRefreshTheirActiveBindings) {
  checkHeldPage<spl::variant<std::monostate, Page>>();
  checkHeldPage<std::variant<std::monostate, Page>>();
}

TEST(Bind, AWholeListReplacementRefreshesRetainedRows) {
  Model m = twoAccounts();
  Page page;
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  const auto id = page.parts.accounts.fRows[0].fState.fId;
  m.apply(model::put<AccountT>(std::string("@a:x.org"),
                              AccountT{"@a:x.org", {"Changed"}, {false}}));
  binding.refresh(page, m);
  ASSERT_EQ(page.parts.accounts.fRows.size(), 2u);
  EXPECT_EQ(page.parts.accounts.fRows[0].fState.fId, id);
  EXPECT_EQ(page.parts.accounts.fRows[0].parts.name.fText, "Changed");
  EXPECT_FALSE(page.parts.accounts.fRows[0].parts.receipts.fOn);
}

TEST(Bind, ADisappearingListClearsItsRowIndex) {
  struct OptionalRoot {
    model::Tracked<std::optional<SettingsT>> settings;
  };
  OptionalRoot root{{std::optional(twoAccounts().snapshot().settings.fValue)}};
  model::Model<OptionalRoot, Reactions, Effect> m(std::move(root));
  Page page;
  bind::Binding<decltype(m)> binding;
  binding.refresh(page, m);
  ASSERT_NE(page.parts.accounts.rowFor("@a:x.org"), nullptr);
  m.apply(model::over<std::optional<SettingsT>>(model::setTo(std::optional<SettingsT>{})));
  binding.refresh(page, m);
  EXPECT_TRUE(page.parts.accounts.fRows.empty());
  EXPECT_TRUE(page.parts.accounts.fRowIndex.empty());
  EXPECT_EQ(page.parts.accounts.rowFor("@a:x.org"), nullptr);
}

TEST(Bind, AChangeIsMadeWhereTheNodeIsAndOnlyItsRowReadsAgain) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  auto &rows = page.parts.accounts.fRows;
  EXPECT_TRUE(bind::press(page, m, scene::Path{0, 0, 1}));
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
  EXPECT_TRUE(bind::press(page, m, scene::Path{0, 1, 2}));
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
  Opened onPress() { return {7}; }
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
  EXPECT_TRUE(bind::press(window, m, scene::Path{0}, &program));
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
    bind::Bound<ReadReceipts, Switch> receipts;
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
  // the scene routes the press along.
  EXPECT_TRUE(bind::press(page, m, scene::Path{0, 1, 3}));
  EXPECT_EQ(page.parts.accounts.fRows[1].parts.press.fPressed, 1);
  ASSERT_EQ(m.root().settings.fValue.accounts.size(), 1u);
  EXPECT_TRUE(m.root().settings.fValue.accounts.contains("@a:x.org"));
  // A path that leads nowhere: nothing pressed, nothing sent.
  EXPECT_FALSE(bind::press(page, m, scene::Path{0, 7, 3}));
  EXPECT_EQ(m.root().settings.fValue.accounts.size(), 1u);
}
}  // namespace

namespace {
// A release build answers a press where it is routed: what binds the tree
// carried down with it, no path kept.
TEST(Bind, APressIsAnsweredWhereItIsRoutedWithWhatWasCarriedDown) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  auto &list = page.parts.accounts;
  auto &row = list.fRows[1];
  // By hand, level by level.
  const auto atPage = bind::carryFrom(&m);
  const auto &atList = carryInto(page, list, atPage);
  const auto atRow = carryInto(list, row, atList);
  const auto &atPress = carryInto(row, row.parts.press, atRow);
  EXPECT_TRUE(answerPress(row.parts.press, atPress));
  ASSERT_EQ(m.root().settings.fValue.accounts.size(), 1u);
  EXPECT_TRUE(m.root().settings.fValue.accounts.contains("@a:x.org"));
  // And as the scene routes a press: answered at the node, nothing kept.
  bind::refresh(page, m);
  auto &left = page.parts.accounts.fRows[0];
  scene::PointerReply reply;
  scene::Routed routed;
  scene::walk::routePointer(page, scene::Path{0, 0, 3}, 0, scene::PointerEvent{scene::pointer::down{1.0f, 1.0f, 1}}, reply,
                            routed, false, bind::carryFrom(&m));
  if constexpr (scene::kErasedWalks) {
    // Erased walks carry nothing: the press is kept, for the program to
    // deliver along its path.
    EXPECT_EQ(std::exchange(scene::hostWork().pressedNow, nullptr), &left.parts.press.fState);
    EXPECT_TRUE(bind::press(page, m, scene::Path{0, 0, 3}));
  } else {
    EXPECT_EQ(scene::hostWork().pressedNow, nullptr);
  }
  EXPECT_EQ(left.parts.press.fPressed, 1);
  EXPECT_EQ(m.root().settings.fValue.accounts.size(), 0u);
}
}  // namespace

namespace {
// A key handler returns what it asks for: sent up the frames its node is
// in, as the routing goes -- or, erased, kept with its path and sent from
// there once the dispatch is over.
TEST(Bind, WhatAKeyHandlerReturnsIsSentUpItsScopes) {
  Model m = twoAccounts();
  Page page;
  bind::refresh(page, m);
  scene::Reply reply;
  scene::walk::routeKey(page, scene::Path{0, 1, 4}, 0, scene::KeyEvent{scene::key::down{scene::keys::kDelete, {}}}, reply,
                        bind::carryFrom(&m));
  EXPECT_TRUE(reply.fHandled);
  if constexpr (scene::kErasedWalks) {
    auto kept = std::exchange(scene::hostWork().answers, {});
    ASSERT_EQ(kept.size(), 1u);
    EXPECT_TRUE(bind::answer(page, m, kept[0]));
  }
  ASSERT_EQ(m.root().settings.fValue.accounts.size(), 1u);
  EXPECT_TRUE(m.root().settings.fValue.accounts.contains("@a:x.org"));
}
}  // namespace
