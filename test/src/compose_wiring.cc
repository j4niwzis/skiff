// Scopes, locals, pipes and processors: how a page is wired to its model, and how a refresh goes only where a change is.
#include "skiff/named.h"
import std;
import skia;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;
import skiff.paint;

#include "gtest/gtest-macros.h"

// Screens as combinator expressions, bound to a model; local and hidden
// state; components wired by what they send (>>, +); a processor; time; a
// Binding doing only what can have happened; events whose types are
// deduced.

#include "compose_common.inc"

namespace {

// Where they are, to press: the list, the row, then down its column.
struct Rows {
  Page *page;
  [[nodiscard]] std::size_t size() const { return all_made_as<NameNode>(*page).size(); }
  RowParts operator[](std::size_t at) const {
    return {all_made_as<NameNode>(*page).at(at), all_made_as<ChevronNode>(*page).at(at), all_made_as<SwitchNode>(*page).at(at)};
  }
};
Rows rowsOf(Page &page) { return {&page}; }
using RemoveNode = OnClick<Removed, part_t<Pad>>;
scene::Path chevronAt(Page &page, std::uint32_t row) { return pressPathTo(page, chevronOf(rowsOf(page)[row]).fState); }
scene::Path switchAt(Page &page, std::uint32_t row) { return pressPathTo(page, switchOf(rowsOf(page)[row]).fState); }
scene::Path removeAt(Page &page, std::uint32_t row) { return pressPathTo(page, all_made_as<RemoveNode>(page).at(row)->fState); }

TEST(Compose, APageWrittenAsAnExpressionShowsTheModel) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::refresh(page, m);
  auto rows = rowsOf(page);
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
  auto rows = rowsOf(page);
  const auto before = m.revision();
  EXPECT_TRUE(bind::press(page, m, chevronAt(page, 1)));
  bind::refresh(page, m);
  EXPECT_FALSE(chevronOf(rows[0]).fOpen);
  EXPECT_TRUE(chevronOf(rows[1]).fOpen);
  EXPECT_EQ(m.revision(), before);
  EXPECT_EQ(m.reactions().log.size(), 0u);
}

TEST(Compose, ARowsEventIsTakenByItsScopeAndItsLocalStateGoesWithIt) {
  Model m = twoAccounts();
  Page page = accountsPage();
  bind::refresh(page, m);
  EXPECT_TRUE(bind::press(page, m, removeAt(page, 0)));
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
auto &numberOf(CounterPage &page) { return made_as<bind::Bound<Stepper::State, part_t<Number>>>(page); }

TEST(Compose, AProcessorStepsItsHiddenStateAndSendsWhatChanged) {
  Model m = twoAccounts();
  CounterPage page = counterPage();
  bind::refresh(page, m);
  // The process, its view, then the button in it.
  // The view's buttons, by where they are in it: -, the count, +, reset.
  const auto button = [&](std::uint32_t at) -> scene::Path {
    switch (at) {
      case 0: return pressPathTo(page, made_as<OnClick<Dec, part_t<Pad>>>(page).fState);
      case 2: return pressPathTo(page, made_as<OnClick<Inc, part_t<Pad>>>(page).fState);
      default: return pressPathTo(page, made_as<OnClick<Reset, part_t<Pad>>>(page).fState);
    }
  };
  EXPECT_TRUE(bind::press(page, m, button(2)));  // +
  EXPECT_TRUE(bind::press(page, m, button(2)));  // +
  EXPECT_TRUE(bind::press(page, m, button(0)));  // -
  bind::refresh(page, m);
  EXPECT_EQ(numberOf(page).fValue, 1);
  EXPECT_EQ(m.look<Count>()->n, 1);  // Changed, taken by the scope around it
  EXPECT_TRUE(bind::press(page, m, button(3)));  // reset, in place: nothing sent
  bind::refresh(page, m);
  EXPECT_EQ(numberOf(page).fValue, 0);
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
  auto &pipe = made_as<Pipe<SearchField, Filtered>>(page);
  // The pipe, then its field.
  pipe.fPipe.from.type("bo");
  EXPECT_TRUE(bind::press(page, m, pressPathTo(page, pipe.fPipe.from.fState)));
  deliverAnswers(page, m);
  pipe.fPipe.from.clear();
  EXPECT_TRUE(bind::press(page, m, pressPathTo(page, pipe.fPipe.from.fState)));
  deliverAnswers(page, m);
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
  // Typed where the combinators are concrete: a Held does not say what the
  // click under it gives.
  static_assert(scene::kErasedCombinators || std::same_as<OutOf<decltype(both)>, model::Types<Inc, Dec>>);
  using Placed = typename BoxOfPar<decltype(both)>::type;
  auto placed = place(hbox(4.0f), std::move(both));
  static_assert(std::tuple_size_v<decltype(made_as<Placed>(placed).fParts)> == 2);
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
  auto rows = rowsOf(page);
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  binding.refresh(page, m);
  EXPECT_EQ(switchOf(rows[0]).fReads, 1);
  EXPECT_TRUE(bind::press(page, m, switchAt(page, 0)));
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
  auto rows = rowsOf(page);
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
  auto rows = rowsOf(page);
  const int before0 = switchOf(rows[0]).fReads;
  EXPECT_TRUE(bind::press(page, m, chevronAt(page, 1)));
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
  EXPECT_TRUE(bind::press(page, m, pressPathTo(page, made_as<Pinger>(page).fState)));
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
  auto made = derived<Alpha, Beta>(SumOf{}, Total{});
  auto &total = made_as<bind::Derived<SumOf, part_t<Total>, Alpha, Beta>>(made);
  bind::Binding<SumModel> binding;
  binding.refresh(made, m);
  EXPECT_EQ(total.fTotal, 3);
  m.apply(model::over<Gamma>(model::setTo(Gamma{5})));
  binding.refresh(made, m);
  EXPECT_EQ(total.fComputed, 1);
  m.apply(model::over<Alpha>(model::setTo(Alpha{10})));
  binding.refresh(made, m);
  EXPECT_EQ(total.fTotal, 12);
  EXPECT_EQ(total.fComputed, 2);
}

}  // namespace
