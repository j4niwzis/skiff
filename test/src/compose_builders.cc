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

}  // namespace
