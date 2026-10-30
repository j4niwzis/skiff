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

}  // namespace
