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
// The program's part of a model's reactions, for a test: the requests they
// answer with taken -- kept as a program keeps what it is asked, an effect
// replacing an older one of its kind (replaces(), key()) -- to be looked at.
template <class Effect> struct Log {
  std::vector<Effect> fItems;
  template <class E> constexpr void take(const E &effect) {
    for (auto &each : fItems)
      if (std::holds_alternative<E>(each) && replaces(effect, std::get<E>(each))) {
        each = Effect(effect);
        return;
      }
    fItems.push_back(Effect(effect));
  }
  constexpr std::vector<Effect> drain() { return std::exchange(fItems, {}); }
  constexpr std::size_t size() const { return fItems.size(); }

private:
  template <class E>
    requires requires(const E &e) { e.replaces(e); }
  static constexpr bool replaces(const E &effect, const E &older) {
    return effect.replaces(older);
  }
  template <class E>
    requires(requires(const E &e) { e.key() == e.key(); } && !requires(const E &e) { e.replaces(e); })
  static constexpr bool replaces(const E &effect, const E &older) {
    return effect.key() == older.key();
  }
  template <class E> static constexpr bool replaces(const E &, const E &) { return false; }
};
// One on(request) for each request, not a template: the runtime layer finds
// the events reactions take by their on(event)s, one type each.
template <class Self, class E> struct Takes {
  constexpr skiff::model::Nothing on(const E &effect) const {
    static_cast<const Self &>(*this).log.take(effect);
    return {};
  }
};
template <class R, class Effect> struct Logged;
template <class R, class... E>
struct Logged<R, std::variant<E...>> : R, Takes<Logged<R, std::variant<E...>>, E>... {
  mutable Log<std::variant<E...>> log;
  using R::on;
  using Takes<Logged<R, std::variant<E...>>, E>::on...;
};

using Effect = std::variant<WriteSettings>;
struct Reactions {
  auto on(model::Changed<SettingsT>, const SettingsT &) const {
    return WriteSettings{};
  }
};
using Model = model::Model<Root, Logged<Reactions, Effect>>;

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
  EXPECT_EQ(m.reactions().log.size(), 0u);
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
struct ShortcutScreen : scene::Node {
  using Node::onKey;
  std::optional<Count> onKey(scene::phase::bubble, const scene::key::down& key,
                            scene::Reply& reply) {
    if (key.key != scene::keys::kUp || !key.modifiers.has<scene::modifier::control>())
      return std::nullopt;
    reply.handle();
    return Count{1};
  }
};
struct ShownShortcutScreen : ShortcutScreen {
  using ShortcutScreen::onKey;
};
template <class N>
concept BoundByValue = requires(N&& node) {
  bound<MountedFacts>(std::move(node));
};
// Value combinators must reject an immovable node before their bodies
// instantiate. Bound's in-place constructor still accepts its arguments.
static_assert(!BoundByValue<MountedControl>);
static_assert(!std::constructible_from<
              bind::Bound<MountedFacts, MountedControl>, MountedControl>);
static_assert(std::constructible_from<
              bind::Bound<MountedFacts, MountedControl>, std::in_place_t, int>);
static_assert(!std::constructible_from<scene::Scene<MountedControl>, MountedControl>);
static_assert(std::constructible_from<scene::Scene<MountedControl>, std::in_place_t, int>);
} // namespace
// Match a program that explicitly instantiates its scene in one module
// unit: unavailable value constructors must not instantiate their bodies.
template class skiff::scene::Scene<MountedControl>;

TEST(Compose, ExplicitlyInstantiatedSceneKeepsImmovableRootInPlace) {
  scene::Scene<MountedControl> tree(std::in_place, 7);
  EXPECT_EQ(tree.root().owner, &tree.root());
  EXPECT_EQ(tree.root().value, 7);
}

TEST(Compose, NestedBindingsKeepImmovableControlAtItsFinalAddress) {
  using Inner = bind::Bound<MountedFacts, MountedControl>;
  using Outer = bind::Bound<int, Inner>;
  Outer node(std::in_place, std::in_place, 7);
  EXPECT_EQ(node.owner, &node);
  EXPECT_EQ(node.value, 7);
  node.setVisible(false);
  EXPECT_FALSE(node.owner->visible());
}

TEST(Compose, BoundKeepsInheritedScreenShortcuts) {
  bind::Bound<Count, ShownShortcutScreen> screen;
  scene::key::down key{.key = scene::keys::kUp,
                       .modifiers = scene::modifiers::kControl};
  scene::Reply reply;
  const auto answer = screen.onKey(scene::phase::bubble{}, key, reply);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->n, 1);
  EXPECT_TRUE(reply.fHandled);
}

TEST(Compose, BoundMountConstructsAndReopensImmovableContentInPlace) {
  int made = 0;
  using Content = Mounted<MountedControl, MountedFacts, MountedArguments>;
  bind::Bound<std::optional<MountedFacts>, Content> node(
      std::in_place, MountedArguments{&made}, scene::Spec{.fill = true}, true);
  for (int value : {7, 9}) {
    node.read(std::optional{MountedFacts{value}});
    ASSERT_NE(node.shown(), nullptr);
    EXPECT_EQ(node.shown()->owner, node.shown());
    EXPECT_EQ(node.shown()->value, value);
    node.shown()->setVisible(false);
    EXPECT_FALSE(node.shown()->owner->visible());
    node.read(std::optional<MountedFacts>{});
    EXPECT_EQ(node.shown(), nullptr);
  }
  EXPECT_EQ(made, 2);
}

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

TEST(Compose, AProjectionOverAProjectedNodeTellsItTheSamePart) {
  struct Flag {
    bool on = false;
    int width = 10;
  };
  struct Root {
    model::Tracked<Flag> flag;
  };
  model::Model<Root, bind::NoReactions> model;
  // Shown as one projection says, its width as the one under it does.
  auto node = shown_for<Flag>([](const Flag& now) { return now.on; },
                              spec_for<Flag>([](const Flag& now) { return scene::Spec{.width = static_cast<float>(now.width)}; }, Pad{}));
  bind::Binding<decltype(model)> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(node.fState.width(), 10.0f);
  model.apply(model::edit(model::placeOf<Flag, Root>(), model::setTo(Flag{true, 30})));
  binding.refresh(node, model);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(node.fState.width(), 30.0f);
}

TEST(Compose, ARefreshCombinesChangedPathsAndLeavesOtherBindingsAlone) {
  struct First { int value = 0; };
  struct Second { int value = 0; };
  struct Third { int value = 0; };
  struct Root {
    model::Tracked<First> first;
    model::Tracked<Second> second;
    model::Tracked<Third> third;
  };
  struct Probe : scene::Node {
    int reads = 0, value = 0;
    void read(const First& part) { value = part.value; ++reads; }
    void read(const Second& part) { value = part.value; ++reads; }
    void read(const Third& part) { value = part.value; ++reads; }
  };
  model::Model<Root, bind::NoReactions> model;
  auto page = column(bound<First>(Probe{}), bound<Second>(Probe{}), bound<Third>(Probe{}));
  bind::Binding<decltype(model)> binding;
  binding.refresh(page, model);
  model.applyBatch(model::over<First>(model::setTo(First{1})), model::over<Second>(model::setTo(Second{2})));
  binding.refresh(page, model);
  EXPECT_EQ(std::get<0>(page.fParts).reads, 2);
  EXPECT_EQ(std::get<0>(page.fParts).value, 1);
  EXPECT_EQ(std::get<1>(page.fParts).reads, 2);
  EXPECT_EQ(std::get<1>(page.fParts).value, 2);
  EXPECT_EQ(std::get<2>(page.fParts).reads, 1);
  binding.refresh(page, model);
  EXPECT_EQ(std::get<0>(page.fParts).reads, 2);
}

TEST(Compose, PointerHandlerRetainsClickOutputsAndReadsItsNode) {
  auto node = onPointer<std::string>([](auto& base, const scene::pointer::down& event, scene::PointerReply& reply) -> std::optional<std::string> {
    if (event.button != 3) return std::nullopt;
    reply.handle();
    return std::get<0>(base.fParts).text();
  }, onClick(42, column(vbox(), nodes::Text("event", 13.0f, 0))));
  static_assert(std::same_as<OutOf<decltype(node)>, model::Types<int, std::string>>);
  EXPECT_EQ(node.onPress(), 42);
  scene::PointerReply reply;
  EXPECT_EQ(node.onPointer(scene::phase::bubble{}, scene::pointer::down{.button = 3}, reply), "event");
  EXPECT_FALSE(node.onPointer(scene::phase::bubble{}, scene::pointer::down{.button = 1}, reply).has_value());
}

TEST(Compose, LocalRefreshCanEnterDynamicAndMemoizedChildren) {
  struct FlipLocal {};
  const auto makeLocal = [] {
    return local<Expanded>(
        handlers(handle<FlipLocal>([](const FlipLocal &) { return model::over<Expanded>(model::flip); })),
        onClick(FlipLocal{}, bound<Expanded>(Chevron{})));
  };
  using local_t = decltype(makeLocal());
  static_assert(bind::detail::kHasLocal<std::optional<local_t>>);
  static_assert(bind::detail::kHasLocal<std::vector<local_t>>);
  static_assert(bind::detail::kHasLocal<std::unique_ptr<local_t>>);
  static_assert(bind::detail::kHasLocal<std::shared_ptr<local_t>>);
  static_assert(bind::detail::kHasLocal<std::reference_wrapper<local_t>>);
  static_assert(bind::detail::kHasLocal<std::tuple<int, local_t>>);
  static_assert(bind::detail::kHasLocal<std::variant<int, local_t>>);
  static_assert(bind::detail::kHasLocal<spl::variant<int, local_t>>);
  using memo_t = nodes::Memo<int, local_t>;
  static_assert(bind::detail::kHasLocal<memo_t>);
  using mounted_t = Mounted<local_t, int, std::identity>;
  static_assert(bind::detail::kHasLocal<mounted_t>);
  using kept_row_t = nodes::KeptRow<int, int, local_t>;
  static_assert(bind::detail::kHasLocal<kept_row_t>);
  const auto makeItem = [makeLocal](const std::tuple<int, int> &) { return makeLocal(); };
  using memo_rows_t = nodes::MemoRows<int, int, local_t, decltype(makeItem)>;
  static_assert(bind::detail::kHasLocal<memo_rows_t>);
  using holding_t = Holding<std::variant<nodes::Text, local_t>>;
  static_assert(bind::detail::kHasLocal<holding_t>);
  using window_list_t = WindowList<int, int, local_t, decltype(makeItem)>;
  static_assert(bind::detail::kHasLocal<window_list_t>);
  const auto noopAct = [] { return scene::Taken{}; };
  const auto noopShow = [](nodes::Text &, float) {};
  using swiped_t = Swiped<decltype(noopAct), nodes::Text, decltype(noopShow), local_t>;
  static_assert(bind::detail::kHasLocal<swiped_t>);
  const auto noopRest = [](bool) { return scene::Taken{}; };
  using resting_t = Resting<decltype(noopRest), local_t>;
  static_assert(bind::detail::kHasLocal<resting_t>);
  static_assert(!bind::detail::kHasLocal<nodes::Memo<int, nodes::Text>>);
  static_assert(!bind::detail::kHasLocal<nodes::KeptRow<int, int, nodes::Text>>);
  static_assert(!bind::detail::kHasLocal<Holding<std::variant<nodes::Text>>>);
  static_assert(!bind::detail::kHasLocal<std::vector<int>>);
  static_assert(!bind::detail::kHasLocal<std::unique_ptr<nodes::Text>>);
  static_assert(!bind::detail::kHasLocal<std::tuple<int, nodes::Text>>);

  // Verify on the Release LocalPass path that editing a Local inside each
  // holder refreshes its bound child without an outer model change.
  using PlainModel = model::Model<int, bind::NoReactions>;
  const auto checkHolder = [](auto &&holder, auto &&findLocal) {
    PlainModel model(0);
    bind::Binding<PlainModel> binding;
    binding.refresh(holder, model);
    local_t &target = findLocal(holder);
    EXPECT_FALSE(target.fOpen);
    ASSERT_TRUE(bind::press(target, model, scene::Path{}));
    // Also exercise detail::LocalPass<PlainModel> directly (the Release path).
    bind::detail::LocalPass<PlainModel> pass{model};
    bind::detail::visitNode(pass, holder, model::Place<int, model::Path<>>{});
    binding.refresh(holder, model);
    EXPECT_TRUE(target.fOpen);
  };

  memo_rows_t rows(makeItem);
  rows.read(std::vector<std::tuple<int, int>>{{1, 10}});
  checkHolder(rows, [](memo_rows_t &h) -> local_t & { return h.fRows[0].fRow; });

  holding_t held(makeLocal());
  checkHolder(held, [](holding_t &h) -> local_t & { return std::get<local_t>(h.parts.shown); });

  window_list_t win(makeItem);
  win.read(WindowShown<std::tuple<int, int>, int>{.items = {{1, 10}}});
  checkHolder(win, [](window_list_t &h) -> local_t & { return h.rows().fRows[0].fRow; });

  swiped_t swipedNode(48.0f, 72.0f, noopAct, noopShow, nodes::Text("", 12.0f, 0), makeLocal());
  checkHolder(swipedNode, [](swiped_t &h) -> local_t & { return std::get<0>(h.fParts); });

  resting_t restingNode(300.0, noopRest, makeLocal());
  checkHolder(restingNode, [](resting_t &h) -> local_t & { return std::get<0>(h.fParts); });

  using page_t = spl::variant<local_t>;
  auto pages = page_view<int, page_t>(
      [makeLocal](page_t &page, int) { page.template emplace<0>(makeLocal()); },
      [](int a, int b) { return a == b; },
      [](int) { return 1.0f; },
      [](int) -> std::optional<scene::Spec> { return std::nullopt; },
      page_t(std::in_place_index<0>, makeLocal()), {});
  static_assert(bind::detail::kHasLocal<decltype(pages)>);
  checkHolder(pages, [](auto &h) -> local_t & { return spl::get<local_t>(h.page()); });
}

TEST(Compose, ListedIsMadeAgainFromWhatItReads) {
  auto list = listed<int>(vbox(2.0f), [](int n) { return nodes::Text(std::to_string(n), 13.0f, 0); });
  EXPECT_TRUE(list.fParts.empty());
  list.read(std::vector<int>{1, 2, 3});
  ASSERT_EQ(list.fParts.size(), 3u);
  EXPECT_EQ(list.fParts[1].text(), "2");
  list.read(std::vector<int>{7});
  ASSERT_EQ(list.fParts.size(), 1u);
  EXPECT_EQ(list.fParts[0].text(), "7");
}

TEST(Compose, SectionsMakeATabAndAPartForEachAndLightTheFirst) {
  const auto text = [](std::string what) { return [what](int n) { return nodes::Text(what + std::to_string(n), 13.0f, 0); }; };
  auto list = sections<int>(vbox(4.0f), vbox(0.0f), text("tab "), text("part "), wrapped<int>(vbox(0.0f), hbox(4.0f)));
  list.read(SectionsShown<int>{{1, 2, 3}, {1, 2, 3}, true});
  ASSERT_EQ(list.parts().size(), 3u);
  ASSERT_EQ(list.fStrip.buttons().size(), 3u);
  EXPECT_EQ(list.parts()[2].text(), "part 3");
  EXPECT_TRUE(list.fStrip.visible());
  (void)list.fStrip.buttons()[1].onPress();  // brought once it is laid out
  EXPECT_TRUE(list.settling());
  list.update(0.0);
  EXPECT_TRUE(list.fStrip.buttons()[0].fState.selected());
  EXPECT_FALSE(list.fStrip.buttons()[1].fState.selected());
  // A search's results: every tab still there, none lit.
  list.read(SectionsShown<int>{{9}, {1, 2, 3}, false});
  EXPECT_EQ(list.parts().size(), 1u);
  EXPECT_EQ(list.fStrip.buttons().size(), 3u);
  list.update(0.0);
  EXPECT_FALSE(list.fStrip.buttons()[0].fState.selected());
}

TEST(Compose, SplitStripsPutEachTabWhereItsElementSaysAndPagedOnesPage) {
  const auto text = [](std::string what) { return [what](int n) { return nodes::Text(what + std::to_string(n), 13.0f, 0); }; };
  const auto arrow = [](bool next) { return nodes::Text(next ? ">" : "<", 13.0f, 0); };
  auto list = sections<int>(vbox(4.0f), vbox(0.0f), text("tab "), text("part "),
      split<int>(vbox(0.0f), [](int n) { return n >= 10; }, in_row<int>(hbox(0.0f)),
                 paged<int>(hbox(0.0f), hbox(0.0f), 30.0f, 60.0f, arrow)));
  list.read(SectionsShown<int>{{1, 2, 10, 11, 12}, {1, 2, 10, 11, 12}, true});
  ASSERT_EQ(list.fStrip.fFirst.fParts.size(), 2u);
  ASSERT_EQ(list.fStrip.fThen.buttons().size(), 3u);
  EXPECT_TRUE(list.fStrip.fThen.fBack.has_value());
  list.fStrip.light(3);  // the second of the packs
  EXPECT_TRUE(list.fStrip.fThen.buttons()[1].fState.selected());
  EXPECT_FALSE(list.fStrip.fFirst.fParts[0].fState.selected());
  list.fStrip.fThen.fHolds = 2;
  list.fStrip.fThen.page(true);
  EXPECT_EQ(list.fStrip.fThen.fFirst, 2u);
  list.fStrip.fThen.page(false);
  EXPECT_EQ(list.fStrip.fThen.fFirst, 0u);
}

TEST(Compose, PlacedAtIsShownWhereThereIsAPoint) {
  auto node = PlacedAt<nodes::Text>(nodes::Text("tones", 13.0f, 0));
  EXPECT_FALSE(node.visible());
  node.read(std::optional<skia::SkPoint>(skia::SkPoint{10.0f, 20.0f}));
  EXPECT_TRUE(node.visible());
  node.read(std::nullopt);
  EXPECT_FALSE(node.visible());
}

TEST(Compose, RestingSaysWhenThePointerHasRestedAndWhenItLeaves) {
  std::vector<bool> said;
  auto node = resting(300.0, [&](bool on) { said.push_back(on); }, nodes::Text("x", 13.0f, 0));
  node.fState.fHovered = true;
  node.update(1000.0);
  node.update(1200.0);
  EXPECT_TRUE(said.empty());
  EXPECT_TRUE(node.settling());
  // Said as a press: the node due, its act asked as the press is delivered.
  node.update(1300.0);
  EXPECT_EQ(scene::hostWork().pressedNow, &node.fState);
  node.onPress();
  EXPECT_EQ(said, std::vector<bool>{true});
  EXPECT_FALSE(node.settling());
  node.fState.fHovered = false;
  node.update(1400.0);
  node.onPress();
  EXPECT_EQ(said, (std::vector<bool>{true, false}));
  scene::hostWork().pressedNow = nullptr;
}

// A node shown only while what it reads says so, holding a text made from
// the same part: hidden by the first refresh where it says no.
TEST(Compose, AShownIfHidesWhereItsReadSaysSo) {
  struct Count { int n = 0; };
  struct Root { model::Tracked<Count> count; };
  model::Model<Root, bind::NoReactions> model{Root{}};
  auto node = shown_if<Count>([](const Count &now) { return now.n > 0; },
                              row(text_of<Count>([](const Count &now) { return std::to_string(now.n); },
                                                 nodes::Text("", 12.0f, 0u))));
  bind::Binding<decltype(model)> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(std::get<0>(node.fParts).text(), "0");
}

// A node that reads the model itself, from more than one model: refreshed
// through what it lists, by the model's type or its root's.
namespace {
struct ItsRoot {
  model::Tracked<int> n;
};
struct OtherRoot {
  model::Tracked<long> m;
};
using ItsModel = model::Model<ItsRoot, bind::NoReactions>;
using OtherModel = model::Model<OtherRoot, bind::NoReactions>;
struct SelfReading : scene::Node {
  int fByModel = 0, fByRoot = 0;
  void refresh(const ItsModel &) { ++fByModel; }
  void refresh_other(const OtherRoot &) { ++fByRoot; }
  bind::Refreshes<2> fRefreshes = bind::refreshes(bind::refreshFrom<SelfReading, ItsModel>(),
                                                  bind::refreshFromRoot<SelfReading, OtherRoot, &SelfReading::refresh_other>());
};
} // namespace
namespace {
// As a program's window reads its models: refresh() overloaded, one a
// template over the model's reactions -- which the runtime walks cannot
// pick, so it lists them.
struct OverloadedReading : scene::Node {
  int fByModel = 0, fByRoot = 0;
  void refresh(const ItsModel &) { ++fByModel; }
  template <class M> void refresh(const M &model) { refresh_other(model.root()); }
  void refresh_other(const OtherRoot &) { ++fByRoot; }
  bind::Refreshes<2> fRefreshes = bind::refreshes(bind::refreshFrom<OverloadedReading, ItsModel>(),
                                                  bind::refreshFromRoot<OverloadedReading, OtherRoot, &OverloadedReading::refresh_other>());
};
// One made around it, its own address not the lister's.
struct Ahead {
  long fBytes[4] = {};
};
struct WrappedReading : Ahead, OverloadedReading {};
struct OverloadedUnlisted : scene::Node {
  void refresh(const ItsModel &) {}
  void refresh(const OtherModel &) {}
};
struct ButtonNamedRefresh {
  int refresh;
};
} // namespace
#ifdef SKIFF_ERASED_WALKS
// One the walks would pass by is refused; listed, or one refresh, or no
// node at all, it is not.
static_assert(bind::rt::made::RefreshUnseen<OverloadedUnlisted>);
static_assert(!bind::rt::made::RefreshUnseen<OverloadedReading>);
static_assert(!bind::rt::made::RefreshUnseen<SelfReading>);
static_assert(!bind::rt::made::RefreshUnseen<ButtonNamedRefresh>);
#endif
namespace {
// The root of the tree, and a part deep in boxes: read by each model, each
// time it moves.
TEST(Compose, AnOverloadedReaderIsRefreshedAsRootAndAsAPart) {
  ItsModel its{ItsRoot{}};
  OtherModel other{OtherRoot{}};
  OverloadedReading root;
  bind::Binding<ItsModel> first;
  bind::Binding<OtherModel> second;
  first.refresh(root, its);
  second.refresh(root, other);
  EXPECT_EQ(root.fByModel, 1);
  EXPECT_EQ(root.fByRoot, 1);
  auto tree = column(row(OverloadedReading{}), nodes::Box<>(0u));
  auto moved = std::move(tree);
  bind::Binding<ItsModel> third;
  bind::Binding<OtherModel> fourth;
  third.refresh(moved, its);
  fourth.refresh(moved, other);
  const auto &part = std::get<0>(std::get<0>(moved.fParts).fParts);
  EXPECT_EQ(part.fByModel, 1);
  EXPECT_EQ(part.fByRoot, 1);
  WrappedReading wrapped;
  bind::Binding<ItsModel> fifth;
  bind::Binding<OtherModel> sixth;
  fifth.refresh(wrapped, its);
  sixth.refresh(wrapped, other);
  EXPECT_EQ(wrapped.fByModel, 1);
  EXPECT_EQ(wrapped.fByRoot, 1);
}
} // namespace
TEST(Compose, ANodeReadingTwoModelsItselfIsRefreshedByEach) {
  ItsModel its{ItsRoot{}};
  OtherModel other{OtherRoot{}};
  SelfReading node;
  bind::Binding<ItsModel> first;
  bind::Binding<OtherModel> second;
  first.refresh(node, its);
  second.refresh(node, other);
  EXPECT_EQ(node.fByModel, 1);
  EXPECT_EQ(node.fByRoot, 1);
}

// A page view kept scrolled where the model says -- a settings box -- is
// still told its facts by what holds it (a dialog), in every build: the
// offset's read does not hide the view's.
namespace {
struct KeptPage : skiff::scene::Node {};
struct KeptScrolled {
  float at = 0.0f;
};
struct KeptRoot {
  skiff::model::Tracked<KeptScrolled> scrolled;
};
struct KeptSaidAt {
  int operator()(float) const { return 1; }
};
auto keptPages(int *shown) {
  using page_t = std::variant<KeptPage>;
  auto show = [shown](page_t &, const int &facts) { *shown = facts; };
  auto same = [](const int &a, const int &b) { return a == b; };
  auto side = [](const int &) { return 1.0f; };
  auto pin = [](const int &) { return std::optional<skiff::scene::Spec>(); };
  return skiff::compose::kept_scroll<skiff::model::Field<&KeptScrolled::at>>(KeptSaidAt{},
      skiff::compose::page_view<int, page_t>(show, same, side, pin, page_t(std::in_place_index<0>), {.fill = true}));
}
} // namespace

TEST(Compose, APageViewKeptScrolledIsStillToldItsFacts) {
  int shown = -1;
  auto view = keptPages(&shown);
  static_assert(requires { view.read(3); }, "told its facts, as a dialog tells them");
  view.read(3);
  EXPECT_EQ(shown, 3);
  view.read(7);
  EXPECT_EQ(shown, 7);
  using M = skiff::model::Model<KeptRoot, skiff::bind::NoReactions>;
  M m{KeptRoot{}};
  skiff::bind::Binding<M> binding;
  binding.refresh(view, m);
  EXPECT_EQ(shown, 7) << "its offset read tells it no facts";
}

// A column sliding in (appearing_across) keeps its fill: a row in it is as
// wide as the place it slides into, and a text growing in the row gets what
// the row leaves. Outside a release build the slide's starting spec reached
// the column before the wrapper was adopted, and it was laid out 0 wide.
namespace {
struct MeasuredWidth : skiff::scene::Node {
  float *fWidth = nullptr;
  explicit MeasuredWidth(float *width, skiff::scene::Spec spec = {}) : fWidth(width) { this->apply(spec); }
  void layoutChildren() { *fWidth = this->fState.bounds().width(); }
};
} // namespace

TEST(Compose, AColumnSlidingInKeepsItsFill) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  float icon = 0.0f, text = 0.0f;
  auto row = c::row(c::hbox(16.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
      MeasuredWidth(&icon, {.width = 24.0f, .height = 24.0f}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, MeasuredWidth(&text, {.height = 20.0f})));
  auto sliding = c::appearing_across(true,
      c::column(c::vbox(0.0f, {.fill = true}), c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(row))),
      36.0f, 220.0f);
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(sliding)));
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 280.0f, 600.0f);
  for (double at : {0.0, 300.0, 600.0}) {
    tree.layoutIfNeeded(view);
    tree.update(at);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  }
  EXPECT_FLOAT_EQ(icon, 24.0f);
  EXPECT_FLOAT_EQ(text, 280.0f - 40.0f - 24.0f - 16.0f);
}

// A column whose width is clamped to what is wanted, over content that fills
// it: the rows in it as wide as the clamp. Outside a release build, held
// content lost its fill to the clamp and was laid out 0 wide.
TEST(Compose, AClampedColumnsRowsAreAsWideAsTheClamp) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  float icon = 0.0f, text = 0.0f;
  auto row = c::row(c::hbox(16.0f, {.fillX = true, .height = 46.0f, .padding = {0.0f, 20.0f, 0.0f, 20.0f}}),
      MeasuredWidth(&icon, {.width = 24.0f, .height = 24.0f}),
      c::styled({.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}, MeasuredWidth(&text, {.height = 20.0f})));
  struct Open {
    bool on = false;
  };
  auto side = c::clamped_width(c::WidthLimit{.wanted = 300.0f},
      c::local<Open>(c::column(c::vbox(0.0f, {.fill = true}),
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(row)))));
  scene::Scene tree(c::row(c::hbox(0.0f, {.fill = true}), std::move(side),
                           c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 900.0f, 600.0f));
  EXPECT_FLOAT_EQ(icon, 24.0f);
  EXPECT_FLOAT_EQ(text, 300.0f - 40.0f - 24.0f - 16.0f);
}

// Names without RTTI: a type's last part, its arguments left out; a view
// named where it is written goes by its own name in what the layout tells.
namespace {
struct WideNode : skiff::scene::Node {
  WideNode() { this->apply({.width = 120.0f, .height = 10.0f}); }
};
} // namespace

TEST(Compose, NamesAreShortAndNeedNoRtti) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  static_assert(scene::nameOf<std::vector<int>>() == "vector");
  static_assert(scene::nameOf<WideNode>() == "WideNode");
  static_assert(scene::kindOf<int>() != scene::kindOf<long>());
  auto narrow = SKIFF_NAMED(narrow_box, c::row(c::hbox(0.0f, {.width = 50.0f, .height = 20.0f}), WideNode()));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(narrow)));
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
  const auto told = std::exchange(scene::overflows(), {});
  ASSERT_FALSE(told.empty());
  EXPECT_EQ(told.front().node, "WideNode");
  EXPECT_EQ(told.front().view, "narrow_box");
}

// What a caller asks of a named view -- shown or not, where it is -- it
// answers as what it names does.
TEST(Compose, ANamedViewAnswersWhatItsContentDoes) {
  namespace c = skiff::compose;
  auto shown = SKIFF_NAMED(shown_row, c::row(c::hbox(0.0f, {.width = 10.0f, .height = 10.0f}), WideNode()));
  shown.setVisible(false);
  shown.setPosition(1.0f, 2.0f);
  EXPECT_FALSE(shown.visible());
}

// A box a combinator sizes by its content (hovering's), told to grow, takes
// what its column leaves: the newer grow replaces the sizing it was made
// with, where before the state dropped the grow as a second size.
TEST(Compose, ABoxSizedByItsContentToldToGrowGrows) {
  namespace c = skiff::compose;
  namespace scene = skiff::scene;
  struct Tall : scene::Node {
    float *fHeight;
    explicit Tall(float *h) : fHeight(h) { this->apply({.fill = true}); }
    void layoutChildren() { *fHeight = this->fState.bounds().height(); }
  };
  float height = 0.0f;
  auto grown = c::styled({.fillX = true, .grow = scene::axes::kY},
      c::hovering([](bool, const skia::SkRect &) { return 1; }, c::column(c::vbox(0.0f, {.fill = true}), Tall(&height))));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::column(c::vbox(0.0f, {.fillX = true, .height = 100.0f})), std::move(grown)));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 600.0f));
  EXPECT_FLOAT_EQ(height, 500.0f);
}

// A plain node pressed in a bound one -- a theme card, setting the theme
// its card binds -- changes the bound part: in a build that erases the
// bound node's content (the pressed node is then not the bound one) as in
// one that does not.
namespace theme_tests {
struct Look {
  int theme = 0;
};
struct Root {
  skiff::model::Tracked<Look> look;
};
using field = skiff::model::Field<&Look::theme>;
inline auto card(int theme) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(theme_card, c::bound<field>(c::onClick(skiff::bind::own(skiff::model::setTo(theme)),
                                                           c::column(c::vbox(0.0f, {.width = 10.0f, .height = 10.0f})))));
}
}  // namespace theme_tests
TEST(Compose, APlainPressInABoundNamedViewChangesTheBoundPart) {
  namespace c = skiff::compose;
  // The second card, at whatever depth the build holds its press: the
  // scope is its row where nothing is erased, holds it where it is.
  const std::array paths{scene::Path{1}, scene::Path{1, 0}, scene::Path{0, 1}, scene::Path{0, 1, 0}, scene::Path{0, 1, 0, 0}};
  const auto themes = paths | std::views::transform([](const scene::Path &path) {
    model::Model<theme_tests::Root, bind::NoReactions> m;
    auto page = c::scoped<theme_tests::Look>(c::handlers(), c::row(c::hbox(0.0f, {.width = 40.0f, .height = 10.0f}),
                                                                   theme_tests::card(1), theme_tests::card(2)));
    bind::refresh(page, m);
    (void)bind::press(page, m, path);
    return m.root().look.fValue.theme;
  }) | std::ranges::to<std::vector>();
  EXPECT_EQ(std::ranges::count(themes, 2), 1);
  EXPECT_EQ(std::ranges::count(themes, 1), 0);
}

// mux's appearance page: a style's text fields, in a padded column of a
// scope in a scope, each a named column of a label and a bound field.
namespace style_tests {
namespace c = skiff::compose;
struct Style {
  std::string font;
  std::string code;
};
struct Look {
  Style style;
  int theme = 0;
};
struct Root {
  skiff::model::Tracked<Look> look;
};
struct Field : scene::Node {
  float *fWidth = nullptr;
  explicit Field(float *width) : fWidth(width) {
    fState.fRelativeSizeAxes = scene::axes::kX;
    fState.fWidth = 1.0f;
    fState.fHeight = 30.0f;
  }
  void layoutChildren() { *fWidth = this->fState.bounds().width(); }
  void read(const std::string &) {}
};
template <auto Member> inline auto text_field(float *width) {
  return SKIFF_NAMED(style_text_field, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      nodes::Text("Label", 13.0f, 0xff000000u),
      c::bound<skiff::model::Field<Member>>(c::styled({.fillX = true, .height = 36.0f},
          Field(width)))));
}
inline auto controls(float *width) {
  return SKIFF_NAMED(style_controls, c::scoped<Style>(c::handlers(), c::column(
      c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}}),
      nodes::Text("FONTS AND THEME", 13.0f, 0xff000000u, true),
      text_field<&Style::font>(width), text_field<&Style::code>(width))));
}
}  // namespace style_tests
TEST(Compose, AStylesTextFieldsAreAsWideAsTheirColumn) {
  using namespace style_tests;
  float width = 0;
  auto page = c::Box(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      std::make_unique<decltype(c::scoped<style_tests::Look>(c::handlers(), controls(&width)))>(c::scoped<style_tests::Look>(c::handlers(), controls(&width))));
  scene::Scene tree(std::move(page));
  model::Model<style_tests::Root, bind::NoReactions> m;
  bind::Binding<decltype(m)> binding;
  binding.refresh(tree.root(), m);
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
  EXPECT_TRUE(scene::overflows().empty());
  (void)std::exchange(scene::overflows(), {});
  EXPECT_FLOAT_EQ(width, 560.0f);
  // The tree as laid out, told a row a node: each field's view, as wide as
  // its column's room.
  const auto rows = scene::laidOutTree(tree.root());
  EXPECT_EQ(std::ranges::count_if(rows, [](const scene::LaidOutNode &row) {
    return row.names && row.view == "style_text_field" && row.bounds.width() == 560.0f;
  }), 2);
  EXPECT_TRUE(std::ranges::none_of(rows, &scene::LaidOutNode::overflows));
  // Each field's view holds the field, its label empty or not: as tall as
  // the label, the gap and the field.
  const auto view = std::ranges::find_if(rows, [](const scene::LaidOutNode &row) { return row.names && row.view == "style_text_field"; });
  ASSERT_NE(view, rows.end());
  const auto field = std::ranges::find_if(view, rows.end(), [](const scene::LaidOutNode &row) { return row.kind.name == "Bound"; });
  ASSERT_NE(field, rows.end());
  EXPECT_LE(field->bounds.fBottom, view->bounds.fBottom);
  EXPECT_TRUE(scene::laidOutText(tree.root()).contains("[style_controls]"));
}

// A view named over a view of its own (a page whose content is a named view
// that closes it on Escape): the same Held, named again -- laid out as it,
// filling what it is put in, the outer name the one told.
namespace named_twice {
namespace c = skiff::compose;
inline auto inner(float *wide) {
  return SKIFF_NAMED(closes_on_escape, c::column(c::vbox(0.0f, {.fill = true}), c::styled({.fillX = true}, MeasuredWidth(wide, {.height = 10.0f}))));
}
inline auto outer(float *wide) { return SKIFF_NAMED(accounts_view, inner(wide)); }
}  // namespace named_twice
TEST(Compose, AViewNamedOverANamedViewIsTheSameHeld) {
  using namespace named_twice;
  float wide = 0;
  auto view = outer(&wide);
  static_assert(compose::NamedOverHeld<decltype(view)> == scene::kErasedCombinators);
  static_assert(std::same_as<compose::part_t<decltype(view)>, compose::Held> == scene::kErasedCombinators);
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(view)));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
  EXPECT_FLOAT_EQ(wide, 300.0f);
  const auto rows = scene::laidOutTree(tree.root());
  EXPECT_TRUE(std::ranges::any_of(rows, [](const scene::LaidOutNode &row) { return row.names && row.view == "accounts_view"; }));
}

// A clamped width in a column as wide as it, beside a growing part (mux's
// chats screen: the side, then the chat): as wide as the column it made --
// laid out a second time in that column's own width, it clamped itself to a
// share of it and came out narrower.
namespace side_tests {
namespace c = skiff::compose;
struct SideUp {
  bool up = true;
};
struct ShownRoot {
  skiff::model::Tracked<SideUp> shown;
};
inline auto side(float *wide) {
  return SKIFF_NAMED(side_column, c::projected<SideUp>([](const SideUp &) { return c::WidthLimit{.wanted = 253.0f}; },
      c::clamped_width({}, c::column(c::vbox(0.0f, {.fill = true}), c::styled({.fillX = true}, MeasuredWidth(wide, {.height = 20.0f}))))));
}
}  // namespace side_tests
TEST(Compose, AClampedSideIsAsWideAsItsColumn) {
  using namespace side_tests;
  float wide = 0, chat = 0;
  scene::Scene tree(c::row(c::hbox(0.0f, {.fill = true}),
      c::shown_for<SideUp>([](const SideUp &now) { return now.up; }, c::column(c::vbox(0.0f, {.fillY = true, .autoSize = scene::axes::kX}), side(&wide))),
      c::shown_for<SideUp>([](const SideUp &now) { return now.up; },
          c::column(c::vbox(0.0f, {.fillY = true, .grow = scene::axes::kX}), c::styled({.fillX = true}, MeasuredWidth(&chat, {.height = 20.0f}))))));
  model::Model<ShownRoot, bind::NoReactions> m;
  bind::Binding<decltype(m)> binding;
  binding.refresh(tree.root(), m);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 470.0f, 285.0f));
  EXPECT_FLOAT_EQ(wide, 253.0f);
  EXPECT_FLOAT_EQ(chat, 470.0f - 253.0f);
}
