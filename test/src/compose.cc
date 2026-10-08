import std;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;

#include "gtest/gtest-macros.h"

// Screens as combinator expressions, bound to a model; local and hidden
// state; components wired by what they send (>>, +); a processor; time; a
// Binding doing only what can have happened; events whose types are
// deduced.

namespace {

using namespace skiff;
using namespace skiff::compose;

struct ReadReceipts {
  bool on = true;
};
struct DisplayName {
  std::string text;
};
struct Count {
  int n = 0;
};
struct AccountT {
  std::string who;
  DisplayName name;
  ReadReceipts receipts;
};
struct SettingsT {
  model::Keyed<std::string, AccountT> accounts;
  Count count;
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

Model twoAccounts() {
  Root root;
  auto &list = root.settings.fValue.accounts;
  list.put("@a:x.org", AccountT{"@a:x.org", {"A"}, {true}});
  list.put("@b:x.org", AccountT{"@b:x.org", {"B"}, {false}});
  return Model(std::move(root));
}

// Leaves, as a program's widgets would be.
struct Switch : scene::Node {
  bool fOn = false;
  int fReads = 0;
  void read(const ReadReceipts &now) {
    fOn = now.on;
    ++fReads;
  }
  auto onPress() { return bind::own(model::flip); }
};
struct Chevron : scene::Node {
  bool fOpen = false;
  template <class T> void read(const T &now) { fOpen = now.on; }
  auto onPress() { return bind::own(model::flip); }
};
struct Label : scene::Node {
  std::string fText;
  void read(const DisplayName &now) { fText = now.text; }
};
struct Number : scene::Node {
  int fValue = 0;
  template <class T> void read(const T &now) { fValue = now.n; }
};
struct Pad : scene::Node {};

struct Removed {};
inline auto accountEvents() {
  return handlers(handle<Removed>([](const auto &here) {
    return model::take<AccountT>(model::key<std::string>(here));
  }));
}
using AccountEvents = decltype(accountEvents());

// Local: which rows are open, each its own; nothing of it in the model.
struct Expanded {
  bool on = false;
};

auto accountRow() {
  return scoped<AccountT>(
      accountEvents(),
      local<Expanded>(
          column(row(bound<DisplayName>(Label{}),
                     bound<Expanded>(Chevron{})),
                 bound<ReadReceipts>(Switch{}),
                 onClick(Removed{}, Pad{}))));
}
auto accountsPage() {
  return column(vbox(6.0f), each<std::string, AccountT>([] { return accountRow(); }));
}
using Page = decltype(accountsPage());

// The parts of a row, by where they are in the expression: the scope, the
// local and the column are one node, the column's.
auto &rowColumn(auto &row) { return row; }
auto &nameOf(auto &row) {
  return std::get<0>(std::get<0>(rowColumn(row).fParts).fParts);
}
auto &chevronOf(auto &row) {
  return std::get<1>(std::get<0>(rowColumn(row).fParts).fParts);
}
auto &switchOf(auto &row) { return std::get<1>(rowColumn(row).fParts); }
auto &removeOf(auto &row) { return std::get<2>(rowColumn(row).fParts); }
auto &rowsOf(Page &page) { return std::get<0>(page.fParts).fRows; }
// Where they are, to press: the list, the row, then down its column.
scene::Path chevronAt(std::uint32_t row) { return {0, row, 0, 1}; }
scene::Path switchAt(std::uint32_t row) { return {0, row, 1}; }
scene::Path removeAt(std::uint32_t row) { return {0, row, 2}; }

TEST(Compose, APageWrittenAsAnExpressionShowsTheModel) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::refresh(page, m);
  auto &rows = rowsOf(page);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(nameOf(rows[0]).fText, "A");
  EXPECT_EQ(nameOf(rows[1]).fText, "B");
  EXPECT_TRUE(switchOf(rows[0]).fOn);
  EXPECT_FALSE(switchOf(rows[1]).fOn);
}

TEST(Compose, LocalStateIsEachRowsOwnAndTellsNoOne) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::refresh(page, m);
  auto &rows = rowsOf(page);
  const auto before = m.revision();
  EXPECT_TRUE(bind::press(page, m, chevronAt(1)));
  bind::refresh(page, m);
  EXPECT_FALSE(chevronOf(rows[0]).fOpen);
  EXPECT_TRUE(chevronOf(rows[1]).fOpen);
  EXPECT_EQ(m.revision(), before);
  EXPECT_EQ(m.outbox().size(), 0u);
}

TEST(Compose, ARowsEventIsTakenByItsScopeAndItsLocalStateGoesWithIt) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::refresh(page, m);
  EXPECT_TRUE(bind::press(page, m, removeAt(0)));
  bind::refresh(page, m);
  ASSERT_EQ(rowsOf(page).size(), 1u);
  EXPECT_EQ(nameOf(rowsOf(page)[0]).fText, "B");
}

// A processor: its state hidden, its steps pure or in place, what it sends
// taken by the scope around it.
struct Inc {};
struct Dec {};
struct Reset {};
struct Changed {
  int n;
};
struct Stepper {
  struct State {
    int n = 0;
  };
  static auto step(State s, Inc) {
    ++s.n;
    return std::pair{s, Changed{s.n}};
  }
  static auto step(State s, Dec) {
    --s.n;
    return std::pair{s, Changed{s.n}};
  }
  static void step(State &s, const Reset &) { s.n = 0; }  // in place
  static auto view() {
    return row(onClick(Dec{}, Pad{}), bound<State>(Number{}),
               onClick(Inc{}, Pad{}), onClick(Reset{}, Pad{}));
  }
};
struct CountEvents {
  auto on(const Changed &c) const { return model::over<Count>(model::setTo(Count{c.n})); }
};
auto counterPage() {
  return scoped<SettingsT>(CountEvents{}, column(process<Stepper>()));
}
using CounterPage = decltype(counterPage());
auto &viewOf(CounterPage &page) {
  return std::get<0>(std::get<0>(page.fParts).fParts);
}

TEST(Compose, AProcessorStepsItsHiddenStateAndSendsWhatChanged) {
  Model m = twoAccounts();
  CounterPage page = counterPage();
  bind::refresh(page, m);
  auto &view = viewOf(page);
  // The process, its view, then the button in it.
  const auto button = [](std::uint32_t at) { return scene::Path{0, 0, at}; };
  EXPECT_TRUE(bind::press(page, m, button(2)));  // +
  EXPECT_TRUE(bind::press(page, m, button(2)));  // +
  EXPECT_TRUE(bind::press(page, m, button(0)));  // -
  bind::refresh(page, m);
  EXPECT_EQ(std::get<1>(view.fParts).fValue, 1);
  EXPECT_EQ(m.look<Count>()->n, 1);  // Changed, taken by the scope around it
  EXPECT_TRUE(bind::press(page, m, button(3)));  // reset, in place: nothing sent
  bind::refresh(page, m);
  EXPECT_EQ(std::get<1>(view.fParts).fValue, 0);
  EXPECT_EQ(m.look<Count>()->n, 1);
}

// Wiring by type: a field's queries go to the list after it; what the list
// does not take goes on up.
struct QueryChanged {
  std::string text;
};
struct Cleared {};
// What it was last given -- typed, cleared -- answered as it is pressed.
struct SearchField : scene::Node {
  using Out = model::Types<QueryChanged, Cleared>;
  using Said = spl::variant<QueryChanged, Cleared>;
  std::optional<Said> fSaid;
  void type(std::string text) { fSaid = Said(QueryChanged{std::move(text)}); }
  void clear() { fSaid = Said(Cleared{}); }
  std::optional<Said> onPress() { return std::exchange(fSaid, std::nullopt); }
  std::string fShown;
  void on(const Cleared &) { fShown.clear(); }
};
struct Filtered : scene::Node {
  using In = model::Types<QueryChanged>;
  std::string fQuery;
  void on(const QueryChanged &q) { fQuery = q.text; }
};
struct ClearedEvents {
  auto on(const Cleared &) const { return model::over<Count>(model::setTo(Count{-1})); }
};

TEST(Compose, APipeGivesWhatItsEndTakesAndPassesTheRestUp) {
  static_assert(std::same_as<OutOf<Pipe<SearchField, Filtered>>,
                             model::Types<Cleared>>);
  Model m = twoAccounts();
  auto page = scoped<SettingsT>(ClearedEvents{}, column(SearchField{} >> Filtered{}));
  auto &pipe = std::get<0>(page.fParts);
  // The pipe, then its field.
  pipe.fPipe.from.type("bo");
  EXPECT_TRUE(bind::press(page, m, scene::Path{0, 0}));
  pipe.fPipe.from.clear();
  EXPECT_TRUE(bind::press(page, m, scene::Path{0, 0}));
  EXPECT_EQ(pipe.fPipe.to.fQuery, "bo");
  EXPECT_EQ(m.look<Count>()->n, -1);
  // And told from outside, as Fudgets' input: send<>.
  pipe.fPipe.from.fShown = "x";
  bind::send(m, pipe.fPipe.from, Cleared{});
  EXPECT_EQ(pipe.fPipe.from.fShown, "");
}

TEST(Compose, APipeReadsEitherWayRound) {
  static_assert(std::same_as<decltype(Filtered{} << SearchField{}),
                             decltype(SearchField{} >> Filtered{})>);
  // + binds tighter: what the field sends goes to both, side by side.
  using Fan = decltype(SearchField{} >> (Filtered{} + Filtered{}));
  static_assert(std::same_as<Fan, decltype(SearchField{} >> Filtered{} + Filtered{})>);
}

TEST(Compose, SideBySideIsPlacedApartFromHowItIsWired) {
  auto both = onClick(Inc{}, Pad{}) + onClick(Dec{}, Pad{});
  static_assert(std::same_as<OutOf<decltype(both)>, model::Types<Inc, Dec>>);
  auto placed = place(hbox(4.0f), std::move(both));
  static_assert(std::tuple_size_v<decltype(placed.fParts)> == 2);
}

TEST(Compose, TimeIsEvents) {
  auto ticks = every<Inc>(100.0);
  ticks.update(0.0);
  ticks.update(250.0);
  EXPECT_EQ(ticks.onPress().size(), 2u);
  EXPECT_TRUE(ticks.onPress().empty());
  auto quiet = debounce<QueryChanged>(300.0);
  quiet.update(0.0);
  quiet.on(QueryChanged{"a"});
  quiet.update(100.0);
  quiet.on(QueryChanged{"ab"});
  quiet.update(350.0);
  EXPECT_FALSE(quiet.onPress());
  quiet.update(500.0);
  const auto said = quiet.onPress();
  ASSERT_TRUE(said);
  EXPECT_EQ(said->text, "ab");
}

// A Binding: a refresh with nothing moved walks nothing.
TEST(Compose, ABindingDoesOnlyWhatCanHaveHappened) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  auto &rows = rowsOf(page);
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  binding.refresh(page, m);
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  EXPECT_TRUE(bind::press(page, m, switchAt(0)));
  binding.refresh(page, m);
  EXPECT_EQ(switchOf(rows[0]).fReads, 2);
  EXPECT_EQ(switchOf(rows[1]).fReads, 1);  // its scope's part did not move
}

// A targeted refresh: a change in one account reaches its row, and nothing
// in the other row is so much as looked at; a new account makes one row.
TEST(Compose, ARefreshGoesOnlyWhereTheChangeIs) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  auto &rows = rowsOf(page);
  const auto name = model::placeOf<DisplayName, Root>(std::string("@b:x.org"));
  using P = decltype(name)::PathType;
  m.apply(model::Edit<Root, P, model::SetTo<DisplayName>>{name, model::setTo(DisplayName{"Bee"})});
  binding.refresh(page, m);
  EXPECT_EQ(nameOf(rows[1]).fText, "Bee");
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  EXPECT_EQ(switchOf(rows[1]).fReads, 2);  // same element, tracked as a whole: read again
  m.apply(model::put<AccountT>(std::string("@c:x.org"), AccountT{"@c:x.org", {"C"}, {true}}));
  binding.refresh(page, m);
  ASSERT_EQ(rowsOf(page).size(), 3u);
  EXPECT_EQ(nameOf(rowsOf(page)[2]).fText, "C");
  EXPECT_EQ(switchOf(rowsOf(page)[0]).fReads, 1);
}

// A change of one row's Local state reaches that row's chevron only.
TEST(Compose, ALocalChangeGoesOnlyToItsLocal) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  auto &rows = rowsOf(page);
  const int before0 = switchOf(rows[0]).fReads;
  EXPECT_TRUE(bind::press(page, m, chevronAt(1)));
  binding.refresh(page, m);
  EXPECT_TRUE(chevronOf(rows[1]).fOpen);
  EXPECT_FALSE(chevronOf(rows[0]).fOpen);
  EXPECT_EQ(switchOf(rows[0]).fReads, before0);
  EXPECT_EQ(switchOf(rows[1]).fReads, 1);
}

// A node that says nothing of what it sends: what its onPress() answers.
struct Ping {};
struct Pinger : scene::Node {
  Ping onPress() const { return {}; }
};
struct PingEvents {
  auto on(const Ping &) const { return model::over<Count>(model::setTo(Count{42})); }
};

TEST(Compose, WhatANodeSendsIsWhatItsPressAnswers) {
  static_assert(std::same_as<OutOf<Pinger>, model::Types<Ping>>);
  Model m = twoAccounts();
  auto page = scoped<SettingsT>(PingEvents{}, column(Pinger{}));
  EXPECT_TRUE(bind::press(page, m, scene::Path{0}));
  EXPECT_EQ(m.look<Count>()->n, 42);
}

// A value computed from the model: computed again only where a part it
// reads has moved; and a scope moved to another part of its kind.
struct Alpha {
  int value = 1;
};
struct Beta {
  int value = 2;
};
struct Gamma {
  int value = 0;
};
struct SumRoot {
  model::Tracked<Alpha> alpha;
  model::Tracked<Beta> beta;
  model::Tracked<Gamma> gamma;
};
struct NoReactions {};
using SumModel = model::Model<SumRoot, NoReactions>;
struct Total : scene::Node {
  int fTotal = 0;
  int fComputed = 0;
  void read(int total) {
    fTotal = total;
    ++fComputed;
  }
};
struct SumOf {
  int operator()(const SumRoot &root) const { return root.alpha.fValue.value + root.beta.fValue.value; }
};
TEST(Compose, ADerivedValueIsComputedAgainOnlyWhereWhatItReadsMoved) {
  SumModel m{SumRoot{}};
  auto total = derived<Alpha, Beta>(SumOf{}, Total{});
  bind::Binding<SumModel> binding;
  binding.refresh(total, m);
  EXPECT_EQ(total.fTotal, 3);
  m.apply(model::over<Gamma>(model::setTo(Gamma{5})));
  binding.refresh(total, m);
  EXPECT_EQ(total.fComputed, 1);
  m.apply(model::over<Alpha>(model::setTo(Alpha{10})));
  binding.refresh(total, m);
  EXPECT_EQ(total.fTotal, 12);
  EXPECT_EQ(total.fComputed, 2);
}
} // namespace

namespace {
// A row of a list fed views by key: how many were made tells which were
// kept.
struct Shown : skiff::scene::Node {
  Shown(int key, const std::string &view) : fKey(key), fText(view) {}
  int fKey;
  std::string fText;
};
struct MakeShown {
  int *fMade;
  Shown operator()(const std::pair<int, std::string> &item) const {
    ++*fMade;
    return Shown(item.first, item.second);
  }
};
} // namespace

TEST(Compose, RowsReadByKeyAreKeptWhereTheirViewIsTheSame) {
  int made = 0;
  skiff::nodes::MemoRows<int, std::string, Shown, MakeShown> rows(MakeShown{&made});
  using Item = std::pair<int, std::string>;
  EXPECT_TRUE(rows.read(std::vector<Item>{{1, "a"}, {2, "b"}, {3, "c"}}));
  EXPECT_EQ(made, 3);
  // The same: nothing made.
  EXPECT_FALSE(rows.read(std::vector<Item>{{1, "a"}, {2, "b"}, {3, "c"}}));
  EXPECT_EQ(made, 3);
  // One view changed, one gone, the order turned: one made again.
  EXPECT_TRUE(rows.read(std::vector<Item>{{3, "c"}, {1, "A"}}));
  EXPECT_EQ(made, 4);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows.fRows[0].fRow.fKey, 3);
  EXPECT_EQ(rows.fRows[1].fRow.fText, "A");
}

namespace {
struct MountedFacts {
  int value = 0;
  friend bool operator==(MountedFacts, MountedFacts) = default;
};
// A control can keep a reference to itself and cannot be moved.
struct MountedControl : scene::Node {
  const MountedControl *owner = this;
  int value;
  explicit MountedControl(int given) : value(given) {}
  MountedControl(const MountedControl &) = delete;
  MountedControl(MountedControl &&) = delete;
};
struct MountedArguments {
  int *made;
  auto operator()(const MountedFacts &facts) const {
    ++*made;
    return std::tuple{facts.value};
  }
};
} // namespace
TEST(Compose, MountedConstructsInPlaceAndKeepsEqualFacts) {
  int made = 0;
  auto node = mount<MountedControl, MountedFacts>(MountedArguments{&made});
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(node.shown(), nullptr);
  node.read(std::optional{MountedFacts{7}});
  ASSERT_NE(node.shown(), nullptr);
  EXPECT_EQ(node.shown()->owner, node.shown());
  EXPECT_EQ(node.shown()->value, 7);
  const auto id = node.shown()->fState.fId;
  node.read(std::optional{MountedFacts{7}});
  EXPECT_EQ(made, 1);
  EXPECT_EQ(node.shown()->fState.fId, id);
  node.read(std::optional{MountedFacts{9}});
  EXPECT_EQ(made, 2);
  EXPECT_EQ(node.shown()->value, 9);
  EXPECT_EQ(node.shown()->owner, node.shown());
  node.read(std::optional<MountedFacts>{});
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(node.shown(), nullptr);
}

TEST(Compose, ProjectionsReadFieldsAndRefreshFromModelEdits) {
  auto model = twoAccounts();
  auto label = text_for<model::Field<&Count::n>>(
      [](int n) { return std::format("{} items", n); },
      nodes::Text("", 12.0f, 0u));
  bind::Binding<Model> binding;
  binding.refresh(label, model);
  EXPECT_EQ(label.text(), "0 items");
  (void)model.apply(
      model::edit(model::placeOf<Count, Root>(), model::setTo(Count{8})));
  binding.refresh(label, model);
  EXPECT_EQ(label.text(), "8 items");
}

TEST(Compose, StyleProjectionPreservesItsChildren) {
  auto model = twoAccounts();
  auto node = spec_for<Count>(
      [](const Count &now) -> scene::Spec { return {.visible = now.n > 0}; },
      row(text_for<model::Field<&Count::n>>(
          [](int n) { return std::to_string(n); },
          nodes::Text("", 12.0f, 0u))));
  bind::Binding<Model> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  (void)model.apply(
      model::edit(model::placeOf<Count, Root>(), model::setTo(Count{4})));
  binding.refresh(node, model);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(std::get<0>(node.fParts).text(), "4");
}

TEST(Compose, PollingKeepsAnExternalDeadlineAndTheNodesOwnTimer) {
  auto quiet = keep_ticking(false, column(nodes::Text("Quiet", 14.0f, 0u)));
  EXPECT_FALSE(quiet.wantsTick());
  auto deadline = keep_ticking(true, column(nodes::Text("Pending", 14.0f, 0u)));
  EXPECT_TRUE(deadline.wantsTick());
  auto timer = keep_ticking(false, every<model::Nothing>(1000.0));
  EXPECT_TRUE(timer.wantsTick());
}

TEST(Compose, NamedPressKeepsItsAccessibleLabelAndActions) {
  auto button = onClick(model::Nothing{}, nodes::Text("Save", 14.0f, 0u), "Save settings");
  const auto info = button.semantics();
  EXPECT_EQ(info.fLabel, "Save settings");
  EXPECT_EQ(info.fActions.size(), 2u);
  EXPECT_TRUE(button.acceptsInput());
  EXPECT_EQ(button.fState.cursorShape().index(), scene::Cursor(scene::cursor::hand{}).index());
}

TEST(Compose, LocalModelDoesNotHideTheNodesSceneState) {
  auto node = local<int>(nodes::Text("Local", 14.0f, 0u), 3);
  EXPECT_EQ(&scene::stateOf(node), &static_cast<scene::Node&>(node).fState);
  EXPECT_EQ(node.fModel.root(), 3);
  node.apply({.width = 80.0f});
}

TEST(Compose, NamedClickKeepsTheWrappedToggleRoleAndValue) {
  struct Toggle : scene::Node {
    scene::Semantics semantics() const {
      scene::Semantics result;
      result.fRole = scene::semantic_role::toggle{};
      result.fValue = "on";
      return result;
    }
  };
  auto node = onClick(model::Nothing{}, Toggle{}, "Encrypt local data");
  const auto info = node.semantics();
  EXPECT_EQ(info.fRole.index(), scene::SemanticRole(scene::semantic_role::toggle{}).index());
  EXPECT_EQ(info.fValue, "on");
  EXPECT_EQ(info.fLabel, "Encrypt local data");
}

TEST(Compose, TextSelectionIsDeclaredAtConstruction) {
  const nodes::Text selectable("Selectable", 14.0f, 0u, false, true);
  const nodes::Text ordinary("Ordinary", 14.0f, 0u);
  EXPECT_TRUE(selectable.selectable());
  EXPECT_FALSE(ordinary.selectable());
}

TEST(Compose, PressFactoryReadsCurrentDataWithoutRebuildingTheNode) {
  struct Event { int value; };
  struct Sink {
    std::vector<int> values;
    void take(const Event& event) { values.push_back(event.value); }
  } sink;
  model::Model<int, bind::NoReactions> model(0);
  int current = 1;
  auto button = onPress([&] { return Event{current}; }, Pad{}, "Current value");
  const auto id = button.fState.id();
  ASSERT_TRUE(bind::press(button, model, scene::Path{}, &sink));
  current = 2;
  ASSERT_TRUE(bind::press(button, model, scene::Path{}, &sink));
  EXPECT_EQ(sink.values, (std::vector<int>{1, 2}));
  EXPECT_EQ(button.fState.id(), id);
}

TEST(Compose, VisibilityProjectionReadsLocalModelFields) {
  struct Flag { bool on = false; };
  model::Model<int, bind::NoReactions> model(0);
  auto node = local<Flag>(shown_for<model::Field<&Flag::on>>(
      [](bool on) { return on; }, Pad{}), Flag{});
  bind::Binding<decltype(model)> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  node.fModel.apply(model::over<model::Field<&Flag::on>>(model::setTo(true)));
  binding.invalidate();
  binding.refresh(node, model);
  EXPECT_TRUE(node.visible());
}
