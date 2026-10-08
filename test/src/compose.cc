import std;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;

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
};
struct Chevron : scene::Node {
  bool fOpen = false;
  template <class T> void read(const T &now) { fOpen = now.on; }
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
                     bound<Expanded, model::Flip>(Chevron{})),
                 bound<ReadReceipts, model::Flip>(Switch{}),
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
  chevronOf(rows[1]).change(model::flip);
  bind::drain(page, m);
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
  removeOf(rowsOf(page)[0]).onClick(0, 0);
  bind::drain(page, m);
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
  std::get<2>(view.fParts).onClick(0, 0);  // +
  std::get<2>(view.fParts).onClick(0, 0);  // +
  std::get<0>(view.fParts).onClick(0, 0);  // -
  bind::drain(page, m);
  bind::refresh(page, m);
  EXPECT_EQ(std::get<1>(view.fParts).fValue, 1);
  EXPECT_EQ(m.look<Count>()->n, 1);  // Changed, taken by the scope around it
  std::get<3>(view.fParts).onClick(0, 0);  // reset, in place: nothing sent
  bind::drain(page, m);
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
struct SearchField : scene::Node, bind::Emits<QueryChanged, Cleared> {
  void type(std::string text) { this->emit(QueryChanged{std::move(text)}); }
  void clear() { this->emit(Cleared{}); }
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
  pipe.fPipe.from.type("bo");
  pipe.fPipe.from.clear();
  bind::drain(page, m);
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
  EXPECT_EQ(ticks.fEmitted.size(), 2u);
  auto quiet = debounce<QueryChanged>(300.0);
  quiet.update(0.0);
  quiet.on(QueryChanged{"a"});
  quiet.update(100.0);
  quiet.on(QueryChanged{"ab"});
  quiet.update(350.0);
  EXPECT_TRUE(quiet.fEmitted.empty());
  quiet.update(500.0);
  ASSERT_EQ(quiet.fEmitted.size(), 1u);
}

// A Binding: a refresh with nothing moved walks nothing, a drain with
// nothing asked neither.
TEST(Compose, ABindingDoesOnlyWhatCanHaveHappened) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  auto &rows = rowsOf(page);
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  binding.refresh(page, m);
  binding.drain(page, m);
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  switchOf(rows[0]).change(model::flip);
  binding.drain(page, m);
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
  chevronOf(rows[1]).change(model::flip);
  binding.drain(page, m);
  binding.refresh(page, m);
  EXPECT_TRUE(chevronOf(rows[1]).fOpen);
  EXPECT_FALSE(chevronOf(rows[0]).fOpen);
  EXPECT_EQ(switchOf(rows[0]).fReads, before0);
  EXPECT_EQ(switchOf(rows[1]).fReads, 1);
}

// A node that says nothing of what it sends: deduced.
struct Ping {};
struct Pinger : scene::Node, bind::Emitter {
  void press() { bind::emitFrom(*this, Ping{}); }
};
struct PingEvents {
  auto on(const Ping &) const { return model::over<Count>(model::setTo(Count{42})); }
};
// Its sending code instantiated before its drain: here, by its use.
[[maybe_unused]] void pressIt(Pinger &p) { p.press(); }

TEST(Compose, WhatANodeSendsIsDeducedWhereItSaysNothing) {
  static_assert(std::same_as<OutOf<Pinger>, model::Types<Ping>>);
  Model m = twoAccounts();
  auto page = scoped<SettingsT>(PingEvents{}, column(Pinger{}));
  pressIt(std::get<0>(page.fParts));
  bind::drain(page, m);
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
