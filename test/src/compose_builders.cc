// Builders: a view described before it is built; a piece shaped by the rules
// on it -- folded while compiling in a release build, from the table outside
// one -- the same either way.
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

namespace {

namespace c = skiff::compose;
using namespace skiff;

struct who_facts {
  std::string name;
};
// A piece: a name, as a text; what a rule sees of it, the name.
inline constexpr struct name_piece_fn : c::piece<name_piece_fn> {
  using facts_type = who_facts;
  static who_facts facts(const std::string& name) { return {name}; }
  auto make(const std::string& name) const { return nodes::Text(name, 13.0f, 0xff000000u); }
} name_piece{};
// Another, no rule on it.
inline constexpr struct other_piece_fn : c::piece<other_piece_fn> {
  auto make(const std::string& name) const { return nodes::Text(name, 13.0f, 0xff000000u); }
} other_piece{};

// A third, with facts, that a rule puts something beside.
inline constexpr struct pet_piece_fn : c::piece<pet_piece_fn> {
  using facts_type = who_facts;
  static who_facts facts(const std::string& name) { return {name}; }
  auto make(const std::string& name) const { return nodes::Text(name, 13.0f, 0xff000000u); }
} pet_piece{};

struct initial {
  auto operator()(const who_facts& facts) const { return nodes::Text(facts.name.substr(0, 1), 13.0f, 0xff000000u); }
};

TEST(Builders, ACombinatorGivenABuilderIsOne) {
  auto made = c::column(c::vbox(0.0f), other_piece(std::string("x")), nodes::Text("y", 13.0f, 0xff000000u));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(c::program_rules{c::no_rules});
  EXPECT_TRUE(node.visible());
}

TEST(Builders, ARuleDropsItsPieceAndLeavesTheOthers) {
  constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::drop) | c::on<name_piece_fn>(c::around([](auto node, const who_facts&) { return node; }))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  auto dropped = name_piece(std::string("ann")).build(rules);
  auto kept = other_piece(std::string("bob")).build(rules);
  EXPECT_FALSE(dropped.visible());
  EXPECT_TRUE(kept.visible());
}

TEST(Builders, BeforePutsWhatItMakesFromTheFactsBeside) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<pet_piece_fn>(c::before(initial{}, 4.0f))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  auto made = c::row(c::hbox(0.0f), pet_piece(std::string("cat")));
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
}

TEST(Builders, AMakerOfBuildersMakesBuiltRows) {
  constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::drop) | c::on<name_piece_fn>(c::around([](auto node, const who_facts&) { return node; }))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  const auto make = [](const std::string& name) { return name_piece(name); };
  const c::built_maker<decltype(make), decltype(rules)> rows{make, rules};
  auto row = rows(std::string("dee"));
  static_assert(!c::a_builder<decltype(row)>);
  EXPECT_FALSE(row.visible());
}

TEST(Builders, WrappersOverAPieceAreBuildersToo) {
  constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::drop) | c::on<name_piece_fn>(c::around([](auto node, const who_facts&) { return node; }))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  auto made = c::appearing(true, c::onClickAt([](auto&, float, float) { return std::optional<int>(); }, c::column(c::vbox(0.0f), name_piece(std::string("eve")))));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
}

struct pressed {};
TEST(Builders, ManyAndPressOverPiecesAreBuilders) {
  constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::drop) | c::on<name_piece_fn>(c::around([](auto node, const who_facts&) { return node; }))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  std::vector<decltype(name_piece(std::string()))> parts;
  parts.push_back(name_piece(std::string("fay")));
  parts.push_back(name_piece(std::string("gus")));
  auto made = c::onPress([] { return pressed{}; }, c::many(c::vbox(0.0f), std::move(parts)));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
}

struct a_mark {};
TEST(Builders, MoreWrappersOverAPieceAreBuilders) {
  constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::drop) | c::on<name_piece_fn>(c::around([](auto node, const who_facts&) { return node; }))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  auto made = c::frozen_under(c::vbox(0.0f),
      c::clamped_width(c::WidthLimit{}, c::named("a", "b", c::keep_ticking(false, c::marked(a_mark{}, name_piece(std::string("hal")))))),
      c::appearing_across(true, c::place(c::vbox(0.0f), other_piece(std::string("ivy"))), 10.0f));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
}

// A piece about a node its caller made: the node moved in, its facts given.
inline constexpr struct about_piece_fn : c::piece<about_piece_fn> {
  using facts_type = who_facts;
  static who_facts facts(const who_facts& who, const auto&) { return who; }
  template <class N>
  N make(const who_facts&, N node) const {
    return node;
  }
} about_piece{};
TEST(Builders, APieceAboutANodeTakesItMoved) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<about_piece_fn>(c::before(initial{}, 4.0f))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  auto made = about_piece(who_facts{"kim"}, nodes::Text("kim", 13.0f, 0xff000000u));
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
}
struct some_handlers {};
struct some_facts {
  std::string name;
  friend bool operator==(const some_facts&, const some_facts&) = default;
};
TEST(Builders, AMountOfABuilderIsOne) {
  constexpr auto rules = c::program_rules{c::on<name_piece_fn>(c::drop) | c::on<name_piece_fn>(c::around([](auto node, const who_facts&) { return node; }))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  using content = decltype(c::column(c::vbox(0.0f), name_piece(std::string())));
  auto made = c::mount<content, some_facts>([](const some_facts& now) { return std::tuple{c::column(c::vbox(0.0f), name_piece(now.name))}; });
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  node.read(std::optional<some_facts>(some_facts{"joy"}));
  EXPECT_TRUE(node.shown() != nullptr);
}
// derived and state over a builder: theirs chosen, not ambiguous with the
// eager ones (constraints said as concepts).
static_assert(requires(decltype(name_piece(std::string())) b) { c::derived<>([](auto&) { return 0; }, std::move(b)); });
static_assert(requires(decltype(name_piece(std::string())) b) { c::state<int>(some_handlers{}, std::move(b)); });
static_assert(c::a_builder<decltype(c::state<int>(some_handlers{}, name_piece(std::string())))>);

// ---- parity: the release fold and the debug table, the same results ----------------
// Each test below expects the same values in both builds, so that a build
// whose fold differs -- the rules' order, a rule run twice or on the wrong
// piece, facts not the piece's own -- fails in the build it differs in.

// What the verbs did, in the order they did it.
std::vector<std::string>& trace() {
  static std::vector<std::string> done;
  return done;
}
// A verb around a piece that only says it ran: its tag and the facts' name.
struct traced {
  const char* tag;
  template <class N>
  N operator()(N node, const who_facts& facts) const {
    trace().push_back(std::format("{}:{}", tag, facts.name));
    return node;
  }
};
// What a rule puts beside or instead of a piece, saying it was made.
struct traced_mark {
  const char* tag;
  auto operator()(const who_facts& facts) const {
    trace().push_back(std::format("{}:{}", tag, facts.name));
    return nodes::Text(facts.name, 13.0f, 0xff000000u);
  }
};
inline constexpr struct first_piece_fn : c::piece<first_piece_fn> {
  using facts_type = who_facts;
  static who_facts facts(const std::string& name) { return {name}; }
  auto make(const std::string& name) const { return nodes::Text(name, 13.0f, 0xff000000u); }
} first_piece{};
inline constexpr struct second_piece_fn : c::piece<second_piece_fn> {
  using facts_type = who_facts;
  static who_facts facts(const std::string& name) { return {name}; }
  auto make(const std::string& name) const { return nodes::Text(name, 13.0f, 0xff000000u); }
} second_piece{};
// A piece whose own make holds another piece: the inner one built first.
inline constexpr struct outer_piece_fn : c::piece<outer_piece_fn> {
  using facts_type = who_facts;
  static who_facts facts(const std::string& name) { return {"outer " + name}; }
  auto make(const std::string& name) const { return c::column(c::vbox(0.0f), second_piece(name)); }
} outer_piece{};

TEST(BuildersParity, RulesRunInTheirOrderOnTheirPieceOnly) {
  constexpr auto rules = c::program_rules{c::on<first_piece_fn>(c::around(traced{"one"})) |
                                          c::on<second_piece_fn>(c::around(traced{"other"})) |
                                          c::on<first_piece_fn>(c::around(traced{"two"})) |
                                          c::on<first_piece_fn>(c::before(traced_mark{"before"}, 2.0f)) |
                                          c::on<first_piece_fn>(c::after(traced_mark{"after"}, 2.0f))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto node = first_piece(std::string("ann")).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"one:ann", "two:ann", "before:ann", "after:ann"}));
}

TEST(BuildersParity, RegisteredTwiceTheRulesRunOnce) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"once"}))))};
  c::register_rules(rules.rules);
  c::register_rules(rules.rules);
  trace().clear();
  auto node = first_piece(std::string("bob")).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"once:bob"}));
}

TEST(BuildersParity, APieceInsideAPieceIsShapedFirst) {
  constexpr auto rules = c::program_rules{c::on<outer_piece_fn>(c::around(traced{"outside"})) |
                                          c::on<second_piece_fn>(c::around(traced{"inside"}))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto node = outer_piece(std::string("cy")).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"inside:cy", "outside:outer cy"}));
}

TEST(BuildersParity, DropThenAroundSeesTheDroppedNode) {
  constexpr auto rules = c::program_rules{c::on<first_piece_fn>(c::drop) | c::on<first_piece_fn>(c::around(traced{"seen"}))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto node = first_piece(std::string("dot")).build(rules);
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"seen:dot"}));
}

TEST(BuildersParity, InsteadMakesItsOwnFromTheFacts) {
  constexpr auto rules = c::program_rules{c::on<first_piece_fn>(c::instead(traced_mark{"instead"})) |
                                          c::on<first_piece_fn>(c::around(traced{"then"}))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto node = first_piece(std::string("eli")).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"instead:eli", "then:eli"}));
}

TEST(BuildersParity, RowsOfAMakerAreShapedOneByOneAsMade) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"row"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  const auto make = [](const std::string& name) { return first_piece(name); };
  const c::built_maker<decltype(make), decltype(rules)> rows{make, rules};
  auto first = rows(std::string("fin"));
  EXPECT_EQ(trace(), (std::vector<std::string>{"row:fin"}));
  auto second = rows(std::string("gil"));
  EXPECT_TRUE(first.visible() && second.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"row:fin", "row:gil"}));
}

TEST(BuildersParity, AMountedPieceIsShapedWhenItsFactsCome) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"mounted"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  using content = decltype(c::column(c::vbox(0.0f), first_piece(std::string())));
  auto made = c::mount<content, some_facts>([](const some_facts& now) { return std::tuple{c::column(c::vbox(0.0f), first_piece(now.name))}; });
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(trace().empty());
  node.read(std::optional<some_facts>(some_facts{"hap"}));
  EXPECT_EQ(trace(), (std::vector<std::string>{"mounted:hap"}));
}

TEST(BuildersParity, AScrolledPieceIsABuilderShapedOnce) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"scrolled"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto made = c::scrolled(c::column(c::vbox(0.0f), first_piece(std::string("ida"))));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"scrolled:ida"}));
}

// A message's blocks: each a variant, one kind of it holding a piece.
struct code_block_kind {};
TEST(BuildersParity, AVariantHoldingAPieceIsBuiltAsTheKindItHolds) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"kind"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  using piece_part = decltype(c::column(c::vbox(0.0f), first_piece(std::string())));
  using block = spl::variant<nodes::Text, piece_part>;
  std::vector<block> blocks;
  blocks.emplace_back(std::in_place_index<0>, nodes::Text("code", 13.0f, 0xff000000u));
  blocks.emplace_back(std::in_place_index<1>, c::column(c::vbox(0.0f), first_piece(std::string("jo"))));
  blocks.emplace_back(std::in_place_index<0>, nodes::Text("more", 13.0f, 0xff000000u));
  static_assert(c::holds_builder<block>::value);
  auto made = c::many(c::vbox(0.0f), std::move(blocks));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"kind:jo"}));
}

// A bubble's plugins' slots: a tuple of optionals, one holding a piece.
TEST(BuildersParity, ATupleOfSlotsHoldingAPieceIsBuilt) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"slot"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  using piece_part = decltype(first_piece(std::string()));
  std::tuple<std::optional<nodes::Text>, std::optional<piece_part>, std::optional<piece_part>> slots{
      nodes::Text("here", 13.0f, 0xff000000u), first_piece(std::string("kai")), std::nullopt};
  static_assert(c::holds_builder<decltype(slots)>::value);
  auto made = c::column(c::vbox(0.0f), std::move(slots));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"slot:kai"}));
}

// A policy laying out what it arranges: a card's, a bubble's.
struct still_policy {
  struct State {};
  static void prepare(auto&, State&) {}
  static void arrange(auto&, State&) {}
  static bool settling(const auto&, const State&) { return false; }
  static void settle(auto&, State&) {}
};
TEST(BuildersParity, ArrangedOverAPieceIsABuilderNotAmbiguous) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"arranged"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto made = c::arranged<still_policy>({}, c::column(c::vbox(0.0f), first_piece(std::string("lee"))));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"arranged:lee"}));
}

// Rows kept by their keys, made by a maker whose rows hold pieces.
TEST(BuildersParity, KeptRowsOfPiecesAreShapedAsEachIsMade) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"kept"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  using item = std::pair<std::string, std::string>;
  const auto make = [](const item& one) { return c::column(c::vbox(0.0f), first_piece(one.second)); };
  auto made = c::memo_rows<std::string, std::string, item>(make, c::Box<>(c::hbox(4.0f)));
  static_assert(c::a_builder<decltype(made)>);
  // Scrolled sideways, it is a builder too (side_scroll lifted).
  static_assert(c::a_builder<decltype(c::side_scroll(c::memo_rows<std::string, std::string, item>(make, c::Box<>(c::hbox(4.0f))), true))>);
  auto node = std::move(made).build(rules);
  node.read(std::vector<item>{{"a", "max"}, {"b", "ned"}});
  EXPECT_EQ(trace(), (std::vector<std::string>{"kept:max", "kept:ned"}));
}

// A press caught on its way down, over a piece: a builder (the window's).
struct caught_at {
  template <class N>
  std::optional<int> operator()(const N&, const scene::pointer::down&, scene::PointerReply&) const { return std::nullopt; }
};
TEST(BuildersParity, CaptureOverAPieceIsABuilder) {
  constexpr auto rules = c::program_rules{c::rules_of(c::rule_tuple(c::on<first_piece_fn>(c::around(traced{"caught"}))))};
  if constexpr (scene::kErasedCombinators)
    c::register_rules(rules.rules);
  trace().clear();
  auto made = c::onCapture<int>(caught_at{}, c::column(c::vbox(0.0f), first_piece(std::string("oz"))));
  static_assert(c::a_builder<decltype(made)>);
  auto node = std::move(made).build(rules);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(trace(), (std::vector<std::string>{"caught:oz"}));
}

}  // namespace
