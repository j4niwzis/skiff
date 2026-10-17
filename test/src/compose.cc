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

// A named view held as itself in a typed part (a Box of its type, as a
// variant of message pieces holds one): placed and measured by its content,
// as it is laid out -- by its own state, never set, it was nothing.
namespace typed_part {
namespace c = skiff::compose;
inline auto piece(float *wide) {
  return SKIFF_NAMED(code_piece, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                           c::styled({.fillX = true}, MeasuredWidth(wide, {.height = 10.0f}))));
}
}  // namespace typed_part
TEST(Compose, ANamedViewInATypedPartIsMeasuredByItsContent) {
  using namespace typed_part;
  float wide = 0;
  using piece_t = decltype(piece(nullptr));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::Box<piece_t>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), piece(&wide))));
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
  EXPECT_FLOAT_EQ(wide, 300.0f);
  EXPECT_TRUE(scene::overflows().empty());
  const auto rows = scene::laidOutTree(tree.root());
  EXPECT_EQ(std::ranges::count_if(rows, [](const scene::LaidOutNode &row) { return row.names && row.view == "code_piece"; }), 1);
  EXPECT_TRUE(std::ranges::none_of(rows, &scene::LaidOutNode::overflows));
}

// A mark made by the program around a named view (mux's code pieces,
// marked to be found): a wrapper no combinator erased, adopted where it is
// laid out -- unadopted, it was 0 by 0 and its content stuck out of it.
namespace marked_part {
struct code_kind {};
}  // namespace marked_part
TEST(Compose, AMarkMadeAroundANamedViewIsAsWideAsItsContent) {
  using namespace typed_part;
  float wide = 0;
  using piece_t = decltype(piece(nullptr));
  using marked_t = compose::Marked<marked_part::code_kind, piece_t>;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::Box<marked_t>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}), marked_t(piece(&wide)))));
  (void)std::exchange(scene::overflows(), {});
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
  EXPECT_FLOAT_EQ(wide, 300.0f);
  EXPECT_TRUE(scene::overflows().empty());
}

// A node that asks, as it ticks, on its own behalf (a window list at its
// top, asking for what is above it): its press found by its id, in a named
// view, and delivered to the program by its type.
namespace edge_press {
namespace c = skiff::compose;
struct AtTop {
  bool older = true;
};
struct Seen {};
struct Asker : scene::Node {
  using Answer = std::variant<AtTop, Seen>;
  int fTicks = 0;
  Asker() { this->apply({.fillX = true, .height = 40.0f}); }
  void update(double) {
    if (++fTicks == 2)
      scene::pressLater(this->fState);
  }
  [[nodiscard]] bool wantsTick() const { return fTicks < 3; }
  [[nodiscard]] bool settling() const { return fTicks < 3; }
  std::optional<Answer> onPress() { return Answer(AtTop{}); }
};
struct Asked {
  int asked = 0;
  void take(const AtTop &) { ++asked; }
  void take(const Seen &) {}
};
inline auto view() {
  return SKIFF_NAMED(timeline_list, c::column(c::vbox(0.0f, {.fill = true}), Asker()));
}
}  // namespace edge_press
TEST(Compose, ANodeAskingOnItsOwnBehalfIsHeard) {
  using namespace edge_press;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), view()));
  model::Model<side_tests::ShownRoot, bind::NoReactions> m;
  bind::Binding<decltype(m)> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  for (double at : {0.0, 16.0, 32.0, 48.0}) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
    tree.update(at);
  }
  std::println("presses kept: {}", scene::hostWork().pressed.size());
  ASSERT_EQ(scene::hostWork().pressed.size(), 1u);
  edge_press::Asked program;
  const auto path = scene::hostWork().pressed.front();
  scene::hostWork().pressed.clear();
  EXPECT_TRUE(bind::press(tree.root(), m, path, &program));
  EXPECT_EQ(program.asked, 1);
}

// A window over a list (mux's timeline), scrolled to its top with nothing
// above it held: it asks for what is older, and the program hears it.
namespace window_edge {
namespace c = skiff::compose;
struct FeedTold {
  std::optional<int> of;
  c::WindowAim<std::string> aim;
  friend bool operator==(const FeedTold &, const FeedTold &) = default;
};
struct Feed {
  int count = 30;
  int first = 0;  // the oldest's number: lower, history come in above
};
struct FeedRoot {
  model::Tracked<FeedTold> told;
  model::Tracked<Feed> feed;
};
using FeedModel = model::Model<FeedRoot, bind::NoReactions>;
using Item = std::tuple<std::string, int>;
struct Src {
  using Told = FeedTold;
  static std::size_t count(const FeedModel &m, const Told &) { return static_cast<std::size_t>(m.root().feed.fValue.count); }
  static std::string key(const FeedModel &m, const Told &, std::size_t i) { return std::format("m{}", m.root().feed.fValue.first + static_cast<int>(i)); }
  static std::optional<std::size_t> index(const FeedModel &m, const Told &t, const std::string &id) {
    for (std::size_t i = 0; i < count(m, t); ++i)
      if (key(m, t, i) == id)
        return i;
    return std::nullopt;
  }
  static bool counts(const FeedModel &, const Told &, std::size_t) { return false; }
  static model::Revision revision(const FeedModel &m, const Told &) { return m.revision(); }
  static std::vector<Item> items(const FeedModel &m, const Told &t, std::size_t from, std::size_t to, const std::optional<std::string> &) {
    return std::views::iota(from, to) | std::views::transform([&](std::size_t i) { return Item{key(m, t, i), static_cast<int>(i)}; }) |
           std::ranges::to<std::vector>();
  }
};
struct RowNode : scene::Node {
  RowNode() { this->apply({.fillX = true, .height = 40.0f}); }
};
struct MakeFeedRow {
  RowNode operator()(const Item &) const { return RowNode{}; }
};
struct Tag {};
using FeedWindow = c::WindowFrom<Src, FeedModel, std::string, int, RowNode, MakeFeedRow, c::NoLocate, Tag>;
struct Heard {
  int older = 0;
  int aimed = 0;
  void take(const c::ListEdge<Tag> &edge) { older += edge.older ? 1 : 0; }
  void take(const c::ListAimed<std::string, Tag> &) { ++aimed; }
  void take(const c::ListSeen<std::string, Tag> &) {}
};
}  // namespace window_edge
TEST(Compose, AWindowAtItsTopAsksForWhatIsOlder) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  std::println("heard after opening: {}", heard.older);
  // Scrolled up to its top, as by the wheel.
  for (int notch = 0; notch < 60; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 600.0; t < 2000.0; t += 16.0)
    frame(t);
  std::println("heard after scrolling up: {}", heard.older);
  EXPECT_GE(heard.older, 1);
}

// A picture fitted over a named view (mux's: Fitted over its picture_box):
// the wrapper's size its own, the view filling it -- with a Held& helper
// beside a catch-all, the named view took the catch-all, and the picture
// was laid out 0 by 0, the caption alone shown.
namespace fitted_named {
namespace c = skiff::compose;
struct NoRatio {
  std::optional<float> operator()(auto &) const { return std::nullopt; }
};
inline auto box(float *wide) {
  return SKIFF_NAMED(picture_box, c::column(c::vbox(0.0f, {}), c::styled({.fill = true}, MeasuredWidth(wide))));
}
}  // namespace fitted_named
TEST(Compose, AFittedNamedViewIsFilledByItsContent) {
  using namespace fitted_named;
  float wide = 0;
  using box_t = decltype(box(nullptr));
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::Fitted<NoRatio, box_t>(c::Fit{.width = 1100.0f, .height = 720.0f, .most = 430.0f}, NoRatio{}, box(&wide))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 600.0f));
  EXPECT_FLOAT_EQ(wide, 430.0f);
}

// Rows coming in moving (a message that has just come): made after the
// window is open, they fade in and are seen, whole, once they are still.
namespace window_arrive {
namespace c = skiff::compose;
using namespace window_edge;
struct MakeArriving {
  compose::Held operator()(const Item &) const { return compose::Held(c::appearing(true, RowNode{})); }
};
using ArrivingWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeArriving, c::NoLocate, Tag>;
struct Plain {
  static constexpr auto styles = scene::makeStyleSheet().rule(scene::selectAny(), {.alpha = 1.0f});
};
}  // namespace window_arrive
TEST(Compose, RowsComingInMovingAreSeenOnceStill) {
  using namespace window_arrive;
  ArrivingWindow window(MakeArriving{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  // Two more come.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  binding.refresh(tree.root(), m);
  for (double at = 600.0; at < 1600.0; at += 16.0)
    frame(at);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}


// A row that comes in moving, made after the frame's tick (as the bindings
// read a model that changed): the frame that lays it out asks for another,
// so that its coming plays rather than waiting for something else to wake
// the window.
TEST(Compose, AFrameAfterARowIsMadeAsksForAnother) {
  using namespace window_arrive;
  ArrivingWindow window(MakeArriving{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  for (double at = 0.0; at < 2000.0 && wants; at += 16.0) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    // What it answered, delivered as a program does after each frame.
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  }
  ASSERT_FALSE(wants) << "settled";
  // A message comes: read after the tick, as an event's change is.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 4})));
  tree.update(2100.0);
  binding.refresh(tree.root(), m);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
  EXPECT_TRUE(tree.finishFrame().fWantsAnotherFrame);
}

// A row swiped to answer it: it follows the finger as it is dragged -- its
// tick moves it, so a drag has to bring the walk to it -- and goes back
// when let go.
namespace swipe_follow {
namespace c = skiff::compose;
// What is pressed in the row (a message's text): a swipe is taken on its
// way down to it.
struct ShiftProbe : scene::Node {
  ShiftProbe() { this->apply({.fillX = true, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct Answered {};
}  // namespace swipe_follow
TEST(Compose, ASwipedRowFollowsTheFinger) {
  using namespace swipe_follow;
  auto row = c::swiped(48.0f, 72.0f, [] { return Answered{}; }, [](nodes::Text &, float) {}, nodes::Text("", 12.0f, 0u), ShiftProbe());
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(row)));
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f);
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  ShiftProbe *probe = scene::findNode<ShiftProbe>(tree.root());
  ASSERT_NE(probe, nullptr);
  (void)tree.dispatchPointer(scene::pointer::down{200.0f, 20.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::move{185.0f, 20.0f});
  frame(216.0);
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 20.0f});
  frame(232.0);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, -50.0f);
  (void)tree.dispatchPointer(scene::pointer::up{150.0f, 20.0f, 1});
  for (double at = 248.0; at < 800.0; at += 16.0)
    frame(at);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, 0.0f);
}

TEST(Compose, AWindowAimedAtARowSaysSoAndComesToRest) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                            model::setTo(FeedTold{.of = 1, .aim = {.key = std::string("m5"), .number = 1}})));
  binding.refresh(tree.root(), m);
  for (double t = 600.0; t < 4000.0; t += 16.0)
    frame(t);
  std::println("aimed heard: {}", heard.aimed);
  EXPECT_EQ(heard.aimed, 1);
}

// mux's timeline as it is put together: the window under a spanned_for,
// named, under with_way_down -- aimed at a row, it says so.
namespace timeline_like {
namespace c = skiff::compose;
using namespace window_edge;
struct Corner : scene::Node {
  Corner() { this->apply({.place = scene::anchor::kBottomRight, .width = 10.0f, .height = 10.0f}); }
};
struct Tell {
  bool operator()(bool, std::size_t) const { return true; }
};
inline auto timeline() {
  FeedWindow window(MakeFeedRow{});
  window.setCopiesOnScroll(false);
  window.apply({.fill = true});
  return SKIFF_NAMED(timeline_list, c::spanned_for<FeedTold>([](const FeedTold &) { return false; }, bind::Bound<FeedTold, FeedWindow>(std::move(window))));
}
}  // namespace timeline_like
struct HeardAll : window_edge::Heard {
  using Heard::take;
  void take(bool) {}
  void take(const compose::ListSpan<std::string, window_edge::Tag> &) {}
};
TEST(Compose, ATimelineAsMuxMakesItSaysItIsAimed) {
  using namespace timeline_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::projected<FeedTold>([](const FeedTold &) { return false; },
                                                     c::with_way_down(c::vbox(0.0f, {.fill = true}), timeline(), Corner(), Tell{}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 30}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  HeardAll heard;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                            model::setTo(FeedTold{.of = 1, .aim = {.key = std::string("m5"), .number = 1}})));
  binding.refresh(tree.root(), m);
  for (double t = 600.0; t < 4000.0; t += 16.0)
    frame(t);
  std::println("timeline aimed heard: {}", heard.aimed);
  EXPECT_EQ(heard.aimed, 1);
}

// Ctrl+Up in mux: at the end, the newest row aimed at as a bar opens under
// the list (the answer's) -- the view goes there and stays, not back and
// forth; and, the aim done, the top asks for what is older again.
TEST(Compose, AWindowAimedAsItsViewShrinksComesToRestOnce) {
  using namespace timeline_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::projected<FeedTold>([](const FeedTold &) { return false; },
                                                     c::with_way_down(c::vbox(0.0f, {.fill = true}), timeline(), Corner(), Tell{}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 30}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  HeardAll heard;
  float height = 400.0f;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, height));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  FeedWindow *list = scene::findNode<bind::Bound<FeedTold, FeedWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  // Back and forth: the offset's direction turned, within one aim.
  int turns = 0;
  const auto aim = [&](std::string key, std::uint64_t number, double from) {
    (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                              model::setTo(FeedTold{.of = 1, .aim = {.key = std::move(key), .number = number}})));
    binding.refresh(tree.root(), m);
    std::vector<float> offsets;
    for (double t = from; t < from + 1500.0; t += 16.0) {
      if (height > 350.0f)
        height -= 5.0f;
      frame(t);
      offsets.push_back(list->current());
    }
    // From the second step on: a jump there and the bottom then kept as
    // the bar opens is not a swing.
    for (std::size_t i = 3; i < offsets.size(); ++i)
      if ((offsets[i] - offsets[i - 1]) * (offsets[i - 1] - offsets[i - 2]) < -0.01f)
        ++turns;
    std::println("offsets: {}", offsets | std::views::take(16));
  };
  aim("m29", 1, 600.0);
  aim("m28", 2, 2100.0);
  // One in the middle, glided to as the bar opens again.
  height = 400.0f;
  for (double t = 3600.0; t < 4200.0; t += 16.0)
    frame(t);
  aim("m12", 3, 4200.0);
  // Near the end, glided to, as the bar opens: Ctrl+Up's.
  aim("m29", 4, 5700.0);
  height = 400.0f;
  for (double t = 7200.0; t < 7800.0; t += 16.0)
    frame(t);
  aim("m25", 5, 7800.0);
  // Further up, still glided to: a long glide's steps are large.
  aim("m29", 6, 9300.0);
  height = 400.0f;
  for (double t = 10800.0; t < 11400.0; t += 16.0)
    frame(t);
  aim("m9", 7, 11400.0);
  // One way: aimed from where the row is on the screen, the target was off
  // by each frame's step of the glide, and the view swung about the row.
  EXPECT_EQ(turns, 0);
  EXPECT_EQ(heard.aimed, 7);
  EXPECT_FALSE(list->settling());
}

// History paged in above a window at the list's start: made, and the next
// page asked for -- held by its first key, the window stayed where that key
// went and never asked again.
TEST(Compose, AWindowAtTheStartTakesInWhatComesAbove) {
  using namespace window_edge;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, FeedWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    tree.update(at);
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  for (int notch = 0; notch < 60; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 600.0; t < 2000.0; t += 16.0)
    frame(t);
  ASSERT_GE(heard.older, 1);
  const int asked = heard.older;
  // A page of the history come, above what is held.
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 50, .first = -20})));
  binding.refresh(tree.root(), m);
  for (double t = 2000.0; t < 3000.0; t += 16.0)
    frame(t);
  auto *list = scene::findNode<bind::Bound<FeedTold, FeedWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  EXPECT_NE(list->row("m-20"), nullptr);
  // Scrolled up to it: the next page asked for.
  for (int notch = 0; notch < 60; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  for (double t = 3000.0; t < 4500.0; t += 16.0)
    frame(t);
  EXPECT_GT(heard.older, asked);
}

// Rows measured taller once laid out -- a message's text, its picture --
// as the view glides to one: still one way, not past it and back.
namespace window_grow {
namespace c = skiff::compose;
using namespace window_edge;
struct GrowingRow : scene::Node {
  GrowingRow() { this->apply({.fillX = true, .height = 40.0f}); }
  int fLaid = 0;
  [[nodiscard]] bool wantsTick() const { return fLaid < 3; }
  [[nodiscard]] bool settling() const { return fLaid < 3; }
  void update(double) {
    if (fLaid >= 3 || this->bounds().isEmpty())
      return;
    if (++fLaid == 3) {
      this->apply({.fillX = true, .height = 70.0f});
      this->invalidateLayout();
    }
  }
};
struct MakeGrowing {
  GrowingRow operator()(const Item &) const { return GrowingRow{}; }
};
using GrowWindow = c::WindowFrom<Src, FeedModel, std::string, int, GrowingRow, MakeGrowing, c::NoLocate, Tag>;
}  // namespace window_grow
TEST(Compose, AWindowGlidingToARowAsRowsGrowGoesOneWay) {
  using namespace window_grow;
  GrowWindow window(MakeGrowing{});
  window.setCopiesOnScroll(false);
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, GrowWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 60}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  float height = 400.0f;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, height));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  auto *list = scene::findNode<bind::Bound<FeedTold, GrowWindow>>(tree.root());
  ASSERT_NE(list, nullptr);
  std::vector<float> offsets;
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                            model::setTo(FeedTold{.of = 1, .aim = {.key = std::string("m50"), .number = 1}})));
  binding.refresh(tree.root(), m);
  for (double t = 600.0; t < 2600.0; t += 16.0) {
    if (height > 350.0f)
      height -= 5.0f;
    frame(t);
    offsets.push_back(list->current());
  }
  int turns = 0;
  for (std::size_t i = 2; i < offsets.size(); ++i)
    if ((offsets[i] - offsets[i - 1]) * (offsets[i - 1] - offsets[i - 2]) < -0.01f)
      ++turns;
  std::println("growing offsets: {}", offsets | std::views::take(40));
  EXPECT_LE(turns, 1);
  EXPECT_EQ(heard.aimed, 1);
  EXPECT_FALSE(list->settling());
}

// mux's picture in a message, as it is put together: its preview and its
// picture -- marked, under what turns while it comes -- fill the fitted box,
// both ways: laid out 0 high, neither is drawn, so neither is asked for, and
// the box's plate shows alone, grey.
namespace picture_like {
namespace c = skiff::compose;
struct ShownImage : scene::Node {
  [[nodiscard]] const skia::Sp<skia::SkImage> *image() const { return nullptr; }
};
struct Turning : scene::Node {
  Turning() { this->apply({.width = 20.0f, .height = 20.0f}); }
};
struct PictureTag {};
struct NoRatio {
  std::optional<float> operator()(auto &) const { return std::nullopt; }
};
inline auto picture_box() {
  auto preview = c::visible(true, c::styled({.fill = true, .cornerRadius = 10.0f}, ShownImage()));
  auto picture = c::Marked<PictureTag, ShownImage>(c::visible(true, c::styled({.fill = true, .cornerRadius = 10.0f}, ShownImage())));
  auto loader = c::visible(false, c::styled({.place = scene::anchor::kCentre}, Turning()));
  return SKIFF_NAMED(picture_box, c::column(c::vbox(0.0f, {.cornerRadius = 10.0f, .background = 0xff808080u, .masking = true}),
                                            std::move(preview), c::while_coming(c::vbox(0.0f, {.fill = true}), std::move(picture), std::move(loader))));
}
using fitted_t = c::Fitted<NoRatio, decltype(picture_box())>;
struct Open {};
}  // namespace picture_like
TEST(Compose, APictureAsMuxMakesItIsFilledByItsImages) {
  using namespace picture_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::onClick(Open{}, fitted_t(c::Fit{.width = 1100.0f, .height = 720.0f, .most = 430.0f, .least = 100.0f}, NoRatio{}, picture_box()))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 600.0f));
  int seen = 0;
  const auto filled = [&](auto &one) {
    const auto box = one.bounds();
    std::println("image {}: {} x {}", seen, box.width(), box.height());
    EXPECT_FLOAT_EQ(box.width(), 430.0f);
    EXPECT_FLOAT_EQ(box.height(), 281.0f);
    ++seen;
  };
  scene::forEachNode<ShownImage>(tree.root(), filled);
  scene::forEachNode<c::Marked<PictureTag, ShownImage>>(tree.root(), filled);
  EXPECT_EQ(seen, 2);
}

// Rows come in moving, and the pointer moves before the next frame: the
// restyle after its event walks the new rows without ticking them. Taken
// as seen there, they were never ticked -- in no ticking set, their fade-in
// held at its start, a gap in the list where they are.
TEST(Compose, RowsComingInAsThePointerMovesAreTicked) {
  using namespace window_arrive;
  ArrivingWindow window(MakeArriving{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  binding.refresh(tree.root(), m);
  (void)tree.dispatchPointer(scene::pointer::move{150.0f, 170.0f});
  (void)tree.dispatchPointer(scene::pointer::move{151.0f, 185.0f});
  // And restyled whole, as a hover over a styled row's parent restyles all
  // under it.
  tree.setStyleSheet<window_arrive::Plain>();
  for (double at = 600.0; at < 1600.0; at += 16.0)
    frame(at);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}

// mux's message row: the avatar hangs at the row's bottom left, out of its
// flow -- the row as tall as its bubble, the avatar not making it taller.
namespace face_row {
namespace c = skiff::compose;
struct Face : scene::Node {
  Face() { this->apply({.width = 40.0f, .height = 40.0f}); }
};
struct Bubble : scene::Node {
  Bubble() { this->apply({.width = 100.0f, .height = 20.0f}); }
};
struct OpenFace {
  int operator()() const { return 1; }
};
inline auto row() {
  auto face = c::visible(true, c::styled({.place = scene::anchor::kBottomLeft, .x = -48.0f, .y = -1.0f, .alpha = 1.0f},
                                         c::onPress(OpenFace{}, Face(), "name")));
  return c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {1.0f, 0.0f, 1.0f, 48.0f}}),
                             nodes::justify::start{}),
                std::move(face), compose::Held(Bubble()));
}
}  // namespace face_row
TEST(Compose, AFaceOutOfARowsFlowLeavesItAsTallAsItsBubble) {
  using namespace face_row;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row())));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  std::print("{}", scene::laidOutText(tree.root()));
  const Bubble *bubble = scene::findNode<Bubble>(tree.root());
  ASSERT_NE(bubble, nullptr);
  EXPECT_FLOAT_EQ(bubble->bounds().fTop, 1.0f);
  EXPECT_FLOAT_EQ(bubble->bounds().fLeft, 48.0f);
  int faces = 0;
  scene::forEachNode<c::OnPress<OpenFace, c::part_t<Face>>>(tree.root(), [&](auto &face) {
    ++faces;
    EXPECT_FLOAT_EQ(face.bounds().fBottom, bubble->bounds().fBottom - 1.0f);
    EXPECT_FLOAT_EQ(face.bounds().fLeft, 0.0f);
  });
  EXPECT_EQ(faces, 1);
}

// A row the chat's settings hide, as mux makes one: hidden where it is made,
// then wrapped -- coming in, held off -- and still out of the list's flow,
// no gap where it is.
namespace hidden_row {
namespace c = skiff::compose;
struct Line : scene::Node {
  Line() { this->apply({.fillX = true, .height = 40.0f}); }
};
struct Asked {
  std::optional<int> operator()(auto &, float, float) const { return 1; }
};
inline auto row(bool shown) {
  auto inner = c::onRightPressAt(Asked{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Line()));
  inner.setVisible(shown);
  return compose::Held(c::appearing(false, std::move(inner)));
}
}  // namespace hidden_row
TEST(Compose, AHiddenRowWrappedTakesNoRoom) {
  using namespace hidden_row;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
                              c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), row(true), row(false), row(true))));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
  std::print("{}", scene::laidOutText(tree.root()));
  std::vector<float> tops;
  scene::forEachNode<Line>(tree.root(), [&](Line &one) { tops.push_back(one.bounds().fTop); });
  ASSERT_EQ(tops.size(), 3u);
  EXPECT_FLOAT_EQ(tops[2], 40.0f);
}

// As mux makes a row that comes in: its fade-in around a node that is held
// (held_off_for, erased outside a release build) -- the fade a wrapper over
// held content, ticked as itself all the same, and the row seen whole.
namespace window_arrive_held {
namespace c = skiff::compose;
using namespace window_edge;
struct MakeArrivingHeld {
  compose::Held operator()(const Item &) const {
    return compose::Held(c::appearing(true, compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), RowNode{}))));
  }
};
using ArrivingHeldWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeArrivingHeld, c::NoLocate, Tag>;
}  // namespace window_arrive_held
TEST(Compose, RowsComingInAroundHeldContentAreSeenOnceStill) {
  using namespace window_arrive_held;
  ArrivingHeldWindow window(MakeArrivingHeld{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, ArrivingHeldWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  binding.refresh(tree.root(), m);
  // Only the frames the window asks for, as a window at rest makes them:
  // the first, woken by what came.
  wants = true;
  for (double at = 600.0; at < 1600.0 && wants; at += 16.0)
    frame(at);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}

// A row that comes in as mux makes its message rows: named, washed for the
// marks it reads, fading in around what is held off while rows are being
// selected, that around held content -- seen whole once still, with only
// the frames the window asks for.
namespace window_arrive_mux {
namespace c = skiff::compose;
using namespace window_edge;
inline auto mux_row(bool arrives) {
  auto row = compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), RowNode{}));
  return SKIFF_NAMED(message_row, c::washed_for<FeedTold>(0xff3080ffu, 56.0f, 80.0f, [](const FeedTold &) { return c::Wash{}; },
      c::appearing(arrives, c::held_off_for<FeedTold>([](const FeedTold &) { return false; }, std::move(row)))));
}
using mux_row_t = decltype(mux_row(false));
struct MakeMuxRow {
  mux_row_t operator()(const Item &) const { return mux_row(true); }
};
using MuxRowWindow = c::WindowFrom<Src, FeedModel, std::string, int, mux_row_t, MakeMuxRow, c::NoLocate, Tag>;
}  // namespace window_arrive_mux
TEST(Compose, RowsComingInAsMuxMakesThemAreSeenOnceStill) {
  using namespace window_arrive_mux;
  MuxRowWindow window(MakeMuxRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, MuxRowWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<Feed, FeedRoot>(), model::setTo(Feed{.count = 5})));
  wants = true;
  int frames = 0;
  for (double at = 600.0; at < 1600.0 && wants; at += 16.0, ++frames)
    frame(at);
  std::println("frames after it came: {}", frames);
  const auto rows = scene::laidOutTree(tree.root());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_TRUE(std::ranges::none_of(rows, [](const scene::LaidOutNode &row) { return row.alpha < 1.0f; }));
}

// A message's row, as mux makes it: a right press anywhere in it -- on its
// text, which can be selected -- asks the row's menu; a click alone does
// not answer the swipe.
namespace row_menu {
namespace c = skiff::compose;
struct Menu {
  int at = 0;
};
struct Ask {
  std::optional<Menu> operator()(auto &, float, float) const { return Menu{1}; }
};
struct Reply {};
struct Show {
  void operator()(nodes::Text &, float) const {}
};
inline auto row() {
  nodes::Text words("hello there", 13.0f, 0xff000000u);
  words.setSelectable(true);
  // Its size said: this test's fonts measure nothing.
  words.apply({.width = 200.0f, .height = 20.0f});
  auto inner = c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(words)));
  auto mark = c::styled({.place = scene::anchor::kCentreRight, .width = 28.0f, .height = 28.0f}, nodes::Text("", 12.0f, 0u));
  return c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                            c::swiped(48.0f, 120.0f, [] { return Reply{}; }, Show{}, std::move(mark), std::move(inner))));
}
}  // namespace row_menu
TEST(Compose, ARightPressOnARowsTextAsksItsMenuAndAClickNoReply) {
  using namespace row_menu;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row()));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 3});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 3});
  const std::size_t menus = scene::hostWork().answers.size();
  std::println("answers after a right press: {}, presses: {}", menus, scene::hostWork().pressed.size());
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(menus, 1u);
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 1});
  std::println("after a click: answers {}, presses {}", scene::hostWork().answers.size(), scene::hostWork().pressed.size());
  EXPECT_TRUE(scene::hostWork().pressed.empty());
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
}

namespace row_menu_parts {
namespace c = skiff::compose;
struct Probe : scene::Node {
  Probe() { this->apply({.width = 100.0f, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
template <class N> std::size_t rightPressed(N node) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(node)));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 3});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 3});
  return std::exchange(scene::hostWork().answers, {}).size();
}
}  // namespace row_menu_parts
TEST(Compose, ARightPressIsAnsweredThroughEachLayerOfARow) {
  using namespace row_menu_parts;
  using row_menu::Ask;
  const auto alone = rightPressed(c::onRightPressAt(Ask{}, Probe()));
  const auto overColumn = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Probe())));
  const auto overSwipe = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::swiped(48.0f, 120.0f, [] { return row_menu::Reply{}; }, row_menu::Show{}, nodes::Text("", 12.0f, 0u), Probe()))));
  std::println("right press answered: alone {}, over a column {}, over a swipe {}", alone, overColumn, overSwipe);
  if constexpr (scene::kErasedWalks) {
    EXPECT_EQ(alone, 1u);
    EXPECT_EQ(overColumn, 1u);
    EXPECT_EQ(overSwipe, 1u);
  }
}

TEST(Compose, ARightPressOnSelectableTextIsAnsweredAbove) {
  using namespace row_menu_parts;
  using row_menu::Ask;
  const auto text = [](bool selectable) {
    nodes::Text words("hello there", 13.0f, 0xff000000u);
    words.setSelectable(selectable);
    words.apply({.width = 200.0f, .height = 20.0f});
    return words;
  };
  const auto plain = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), text(false))));
  const auto selectable = rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), text(true))));
  std::println("right press on text answered: plain {}, selectable {}", plain, selectable);
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(selectable, 1u);
}

// A button hidden as it is made and shown by what it reads -- mux's call
// button, shown_if(callable, visible(false, button)): shown once the model
// says so, and pressed.
namespace shown_later {
namespace c = skiff::compose;
using namespace window_edge;
struct Pressed {};
struct Probe : scene::Node {
  Probe() { this->apply({.width = 40.0f, .height = 40.0f}); }
};
inline auto button() {
  return c::shown_if<FeedTold>([](const FeedTold &now) { return now.of == 2; },
                               c::visible(false, c::styled({.alignSelf = scene::align::kStart}, c::onClick(Pressed{}, Probe()))));
}
}  // namespace shown_later
TEST(Compose, AButtonHiddenAsItIsMadeIsShownByWhatItReads) {
  using namespace shown_later;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), button()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  const auto pressedAt = [&] {
    tree.update(0.0);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    (void)tree.finishFrame();
    scene::hostWork().pressed.clear();
    (void)tree.dispatchPointer(scene::pointer::down{10.0f, 10.0f, 1});
    (void)tree.dispatchPointer(scene::pointer::up{10.0f, 10.0f, 1});
    return std::exchange(scene::hostWork().pressed, {}).size();
  };
  EXPECT_EQ(pressedAt(), 0u);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  binding.refresh(tree.root(), m);
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_EQ(pressedAt(), 1u);
}

// mux's chats screen and its keys: named, its keys asked on their way down
// (keys_for, capture) and on their way up (onKeys) -- a key the field with
// the focus does not take answered once, not twice (Ctrl+Up stepped two
// messages).
namespace screen_keys_once {
namespace c = skiff::compose;
using namespace window_edge;
struct Field : scene::Node {
  Field() { this->apply({.width = 200.0f, .height = 30.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool focusable() const { return true; }
  [[nodiscard]] bool takesFocusOnPress() const { return true; }
};
struct Up {};
struct Caught {};
struct UpKeys {
  std::optional<Up> operator()(const scene::key::down &press) const {
    return press.key == scene::keys::kUp ? std::optional<Up>(Up{}) : std::nullopt;
  }
};
struct CaptureKeys {
  std::optional<Caught> operator()(const int &, const scene::key::down &) const { return std::nullopt; }
};
inline auto screen() {
  return SKIFF_NAMED(keys_screen, c::keys_for<FeedTold, scene::phase::capture>([](const FeedTold &) { return 0; }, CaptureKeys{},
      c::onKeys(UpKeys{}, c::column(c::vbox(0.0f, {.fill = true}), Field()))));
}
}  // namespace screen_keys_once
TEST(Compose, AKeyNoFieldTakesIsAnsweredOnceByTheScreen) {
  using namespace screen_keys_once;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), screen()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  tree.update(0.0);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
  (void)tree.finishFrame();
  (void)tree.dispatchPointer(scene::pointer::down{10.0f, 10.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{10.0f, 10.0f, 1});
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchKey(scene::key::down{scene::keys::kUp, scene::modifiers::kControl});
  const std::size_t answered = scene::hostWork().answers.size() + scene::hostWork().pressed.size();
  std::println("focused {}, a key answered {} times", tree.focusedId(), answered);
  // Kept to be delivered outside a release build; sent at once, carried,
  // in one -- never twice.
  EXPECT_LE(answered, 1u);
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
}

// The row's menu, asked by a right press, delivered: taken by the program
// (the sink given), not only kept.
namespace row_menu_delivered {
struct Heard {
  int menus = 0;
  void take(const row_menu::Menu &) { ++menus; }
  void take(const row_menu::Reply &) {}
};
}  // namespace row_menu_delivered
TEST(Compose, ARowsMenuAskedByARightPressIsDelivered) {
  using namespace row_menu_delivered;
  using namespace window_edge;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row_menu::row()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  tree.update(0.0);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 3});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 3});
  row_menu_delivered::Heard heard;
  for (const auto &one : std::exchange(scene::hostWork().answers, {}))
    std::println("delivered: {}", bind::answer(tree.root(), m, one, &heard));
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(heard.menus, 1);
}

// A key's answer delivered as mux delivers what was kept: to each model in
// turn until one says it took it -- taken once, and said so by the first.
namespace screen_keys_delivered {
struct Heard {
  int ups = 0;
  void take(const screen_keys_once::Up &) { ++ups; }
  void take(const screen_keys_once::Caught &) {}
};
}  // namespace screen_keys_delivered
TEST(Compose, AScreensKeyAnswerIsTakenOnceAcrossModels) {
  using namespace screen_keys_once;
  using namespace screen_keys_delivered;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), screen()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  FeedModel other{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  tree.update(0.0);
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
  (void)tree.finishFrame();
  (void)tree.dispatchPointer(scene::pointer::down{10.0f, 10.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{10.0f, 10.0f, 1});
  scene::hostWork().answers.clear();
  scene::hostWork().pressed.clear();
  (void)tree.dispatchKey(scene::key::down{scene::keys::kUp, scene::modifiers::kControl});
  screen_keys_delivered::Heard heard;
  for (const auto &one : std::exchange(scene::hostWork().answers, {})) {
    const bool first = bind::answer(tree.root(), m, one, &heard);
    const bool second = !first && bind::answer(tree.root(), other, one, &heard);
    std::println("taken by the first model: {}, the second: {}, heard {}", first, second, heard.ups);
  }
  EXPECT_LE(heard.ups, 1);
}

// A message's picture whose thumbnail comes after it was first drawn: woken
// where it waits, it finds it -- laid out again, repainted.
namespace waiting_picture {
namespace c = skiff::compose;
using namespace window_edge;
struct Store {
  static inline skia::Sp<skia::SkImage> image;
  static inline scene::Waiters waiting;
};
struct FromStore {
  std::string key;
  const skia::Sp<skia::SkImage> *operator()() const { return &Store::image; }
  scene::Waiters &waiters() const { return Store::waiting; }
};
using Picture = nodes::Image<FromStore>;
struct PictureTag {};
struct MakePictureRow {
  compose::Held operator()(const Item &) const {
    return compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                   c::Marked<PictureTag, Picture>(c::visible(true, c::styled({.width = 100.0f, .height = 80.0f}, Picture({"k"}))))));
  }
};
using PictureWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakePictureRow, c::NoLocate, Tag>;
}  // namespace waiting_picture
TEST(Compose, APictureThatComesLaterIsDrawn) {
  using namespace waiting_picture;
  PictureWindow window(MakePictureRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, PictureWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 2}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  skia::SkRect damage = skia::SkRect::MakeEmpty();
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    const auto done = tree.finishFrame();
    wants = done.fWantsAnotherFrame;
    damage = scene::joined(damage, done.fDamage);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  std::println("waiting before it came: {}", !Store::waiting.empty());
  // It comes: put where it is looked up, those waiting woken -- as mux's
  // cache does -- and the frames the window then asks for.
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  Store::image = surface->makeImageSnapshot();
  Store::waiting.wake();
  damage = skia::SkRect::MakeEmpty();
  wants = true;
  int frames = 0;
  for (double at = 600.0; at < 1600.0 && (wants || frames < 1); at += 16.0, ++frames)
    frame(at);
  std::println("frames after it came: {}, repainted {} x {}", frames, damage.width(), damage.height());
  EXPECT_FALSE(damage.isEmpty());
  Store::image = nullptr;
}

// The same, the picture as mux's picture_box holds it: fitted, pressed to
// open, its preview under it, the picture marked under what turns while it
// comes.
namespace waiting_picture_box {
namespace c = skiff::compose;
using namespace window_edge;
using waiting_picture::Picture;
using waiting_picture::PictureTag;
struct Polled {
  std::string key;
  const skia::Sp<skia::SkImage> *operator()() const { return nullptr; }
};
struct Turning : scene::Node {
  Turning() { this->apply({.width = 20.0f, .height = 20.0f}); }
  [[nodiscard]] const skia::Sp<skia::SkImage> *image() const { return nullptr; }
};
struct NoRatio {
  std::optional<float> operator()(auto &) const { return std::nullopt; }
};
inline auto box() {
  // Over the box, out of its flow, as mux now places it: in the flow, the
  // picture went under it, past the box's bottom, cut off.
  auto preview = c::visible(true, c::styled({.place = scene::anchor::kTopLeft, .fill = true, .cornerRadius = 10.0f}, nodes::Image<Polled>({"k"})));
  auto picture = c::Marked<PictureTag, Picture>(c::visible(true, c::styled({.fill = true, .cornerRadius = 10.0f}, Picture({"k"}))));
  auto loader = c::visible(false, c::styled({.place = scene::anchor::kCentre}, Turning()));
  return SKIFF_NAMED(picture_box, c::column(c::vbox(0.0f, {.cornerRadius = 10.0f, .background = 0xff808080u, .masking = true}),
                                            std::move(preview), c::while_coming(c::vbox(0.0f, {.fill = true}), std::move(picture), std::move(loader))));
}
using fitted_t = c::Fitted<NoRatio, decltype(box())>;
struct Open {};
struct MakeBoxRow {
  compose::Held operator()(const Item &) const {
    return compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        c::onClick(Open{}, fitted_t(c::Fit{.width = 100.0f, .height = 80.0f, .most = 430.0f, .least = 100.0f}, NoRatio{}, box()))));
  }
};
using BoxWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeBoxRow, c::NoLocate, Tag>;
}  // namespace waiting_picture_box
TEST(Compose, APictureInMuxsBoxThatComesLaterIsDrawn) {
  using namespace waiting_picture_box;
  using waiting_picture::Store;
  BoxWindow window(MakeBoxRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), bind::Bound<FeedTold, BoxWindow>(std::move(window))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 2}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  Heard heard;
  bool wants = true;
  skia::SkRect damage = skia::SkRect::MakeEmpty();
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    const auto done = tree.finishFrame();
    wants = done.fWantsAnotherFrame;
    damage = scene::joined(damage, done.fDamage);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  std::println("box: waiting before it came: {}", !Store::waiting.empty());
  auto surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(8, 8));
  Store::image = surface->makeImageSnapshot();
  Store::waiting.wake();
  damage = skia::SkRect::MakeEmpty();
  wants = true;
  int frames = 0;
  for (double at = 600.0; at < 1600.0 && (wants || frames < 1); at += 16.0, ++frames)
    frame(at);
  std::println("box: frames after it came: {}, repainted {} x {}", frames, damage.width(), damage.height());
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_FALSE(damage.isEmpty());
  Store::image = nullptr;
}

// A card popped at the pointer -- a message's menu: at the pointer once laid
// out, with only the frames it asks for -- not left where it was first laid
// out, in the window's top left corner.
namespace popped_menu {
namespace c = skiff::compose;
using namespace window_edge;
struct Card : scene::Node {
  Card() { this->apply({.width = 120.0f, .height = 90.0f}); }
};
inline auto menu() {
  return c::column(c::vbox(0.0f, {.fill = true}),
                   c::popped_at<FeedTold>([](const FeedTold &now) -> std::optional<c::PopAt> {
                     if (now.of != 2)
                       return std::nullopt;
                     return c::PopAt{.from = skia::SkRect::MakeXYWH(150.0f, 120.0f, 0.0f, 0.0f)};
                   }, 8.0f, c::styled({}, Card())));
}
}  // namespace popped_menu
TEST(Compose, ACardPoppedAtThePointerGoesThere) {
  using namespace popped_menu;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), menu()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  bool wants = true;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
    wants = tree.finishFrame().fWantsAnotherFrame;
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  wants = true;
  for (double at = 200.0; at < 1200.0 && wants; at += 16.0)
    frame(at);
  float left = -1.0f, top = -1.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.outOfFlow) {
      left = one.bounds.fLeft;
      top = one.bounds.fTop;
    }
  std::print("{}", scene::laidOutText(tree.root()));
  EXPECT_FLOAT_EQ(left, 150.0f);
  EXPECT_FLOAT_EQ(top, 120.0f);
}

// A row flashed, then another: the first does not flash again as the flash
// leaves it (Ctrl+Up lit two messages at each step).
namespace wash_once {
namespace c = skiff::compose;
using namespace window_edge;
// Which row was painted with what wash, as it was: the rows' own record.
inline std::vector<std::pair<int, skia::SkColor>> &painted() {
  static std::vector<std::pair<int, skia::SkColor>> kept;
  return kept;
}
struct Probe : scene::Node {
  int fWhich = 0;
  explicit Probe(int which) : fWhich(which) { scene::Node::apply({.width = 100.0f, .height = 40.0f}); }
  void apply(const scene::Spec &spec) {
    if (spec.background)
      painted().emplace_back(fWhich, *spec.background);
    scene::Node::apply(spec);
  }
};
inline auto row(int which) {
  return c::washed_for<FeedTold>(0xff3080ffu, 56.0f, 80.0f,
                                 [which](const FeedTold &now) { return c::Wash{.flash = now.aim.key == std::to_string(which) ? now.aim.number : 0}; },
                                 Probe(which));
}
}  // namespace wash_once
TEST(Compose, AFlashLeavingARowDoesNotFlashItAgain) {
  using namespace wash_once;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), row(1), row(2)));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  double at = 0.0;
  const auto frames = [&](double until) {
    for (; at < until; at += 16.0) {
      binding.refresh(tree.root(), m);
      tree.update(at);
      tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
      (void)tree.finishFrame();
    }
  };
  const auto aim = [&](std::string key, std::uint64_t number) {
    (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(),
                              model::setTo(FeedTold{.of = 1, .aim = {.key = std::move(key), .number = number}})));
  };
  frames(200.0);
  aim("1", 1);
  frames(2000.0);  // its flash done
  painted().clear();
  aim("2", 2);
  frames(2100.0);
  // Flashing now: the second only -- not the first, flashed again as the
  // flash left it.
  const auto lit = [](int which) {
    return std::ranges::count_if(painted(), [&](const auto &one) { return one.first == which && one.second != 0; });
  };
  std::println("painted lit: first {}, second {}", lit(1), lit(2));
  EXPECT_EQ(lit(1), 0);
  EXPECT_GT(lit(2), 0);
}

namespace waiting_picture_box {
struct NoRules {
  struct State {
    friend bool operator==(const State &, const State &) = default;
  };
  static void prepare(auto &, State &) {}
  static void arrange(auto &, State &) {}
  static bool settling(const auto &, const State &) { return false; }
  static void settle(auto &, State &) {}
};
}  // namespace waiting_picture_box
// A right press on a message's picture -- pressed to open it in the viewer:
// asks the row's menu, as anywhere else in the row.
TEST(Compose, ARightPressOnARowsPictureAsksItsMenu) {
  using namespace waiting_picture_box;
  using row_menu::Ask;
  auto picture = SKIFF_NAMED(picture_view, c::onClick(Open{}, fitted_t(c::Fit{.width = 200.0f, .height = 100.0f, .most = 430.0f, .least = 100.0f}, NoRatio{}, box()), "Open picture"));
  picture.apply({.alpha = 0.9f});
  const auto answered = row_menu_parts::rightPressed(c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::swiped(48.0f, 120.0f, [] { return row_menu::Reply{}; }, row_menu::Show{}, nodes::Text("", 12.0f, 0u),
                c::onClickAt([](auto &, float, float) { return std::optional<int>(); },
                             c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                    // The bubble, held as mux holds it (made in its own unit).
                                    compose::Held(c::arranged<NoRules>(NoRules::State{}, c::column(c::vbox(2.0f, {.autoSize = scene::axes::kBoth}), std::move(picture))))))))));
  std::println("right press on a picture answered: {}", answered);
  if constexpr (scene::kErasedWalks)
    EXPECT_EQ(answered, 1u);
}

// A row swiped far, under what watches the whole screen for a flick (mux's
// chats screen): let go, it goes back -- its lift its own, not taken by the
// flick on its way down.
namespace swipe_under_flick {
struct Flick {
  std::optional<int> operator()(bool) const { return 1; }
};
}  // namespace swipe_under_flick
TEST(Compose, ARowSwipedFarUnderAFlickIsLetGo) {
  using namespace swipe_follow;
  auto row = c::swiped(48.0f, 120.0f, [] { return Answered{}; }, [](nodes::Text &, float) {}, nodes::Text("", 12.0f, 0u), ShiftProbe());
  scene::Scene tree(c::flicked(90.0f, swipe_under_flick::Flick{}, c::column(c::vbox(0.0f, {.fill = true}), std::move(row))));
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f);
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(view);
    (void)tree.finishFrame();
  };
  for (double at = 0.0; at < 200.0; at += 16.0)
    frame(at);
  ShiftProbe *probe = scene::findNode<ShiftProbe>(tree.root());
  ASSERT_NE(probe, nullptr);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{250.0f, 20.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::move{235.0f, 20.0f});
  frame(216.0);
  (void)tree.dispatchPointer(scene::pointer::move{140.0f, 20.0f});
  frame(232.0);
  (void)tree.dispatchPointer(scene::pointer::up{140.0f, 20.0f, 1});
  const auto replied = scene::hostWork().pressed.size();
  for (double at = 248.0; at < 900.0; at += 16.0)
    frame(at);
  std::println("swiped far: replied {}, shift after {}", replied, probe->fState.fShiftX);
  EXPECT_EQ(replied, 1u);
  EXPECT_FLOAT_EQ(probe->fState.fShiftX, 0.0f);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
}
