// SPDX-License-Identifier: AGPL-3.0-only
// skiff.compose:builders -- Views described before they are built, so that a
// program may shape them first, where it sees everything that shapes them.
//
// A builder is what a view will be: a callable that makes the view's node,
// given the rules it is built under. A combinator given a builder for any of
// its parts gives a builder back (the lifting overloads below, one for each
// combinator, the same parameters as the eager one and preferred where a
// part is a builder): it makes the eager node when it is built, its parts
// built under the same rules. So a builder anywhere below makes its parents
// builders, up to where the program builds the whole -- its window, a list's
// row maker -- with its rules (build(rules)). Nothing is built twice, nothing
// is kept that the nodes would not have kept: what a builder captures is
// what the eager call would have been given, moved in when it is built.
//
// A piece (piece<Self>) is a function object making a view, keyed by its own
// type: what rules are on. Where it is built, the rules on its key are
// applied to what it made, in their order -- in a release build folded while
// compiling; outside one, the piece erased where it is made (as named views
// are) and its rules found in a table the program fills from the same rules
// as it starts (register_rules), so that debug builds keep their small types.
// The rules themselves (on<Piece>, before, after, around, instead, drop) and
// their verbs are the same code in both: only the fold differs.
//
// What a rule sees of a piece is its facts: a small value the piece says,
// from its own arguments (Self::facts(args...)), kept by the builder -- not
// the arguments, which may be gone by the time it is built.
export module skiff.compose:builders;

import std;
import skiff.scene;
import skiff.model;
import :boxes;
import :wiring;

export namespace skiff::compose {

// ---- builders -----------------------------------------------------------------------

template <class Make>
struct Builder {
  using is_builder = std::true_type;
  Make make;
  // Built once: what it captured moved into what it makes.
  template <class Rules>
  auto build(const Rules& rules) && {
    return std::move(make)(rules);
  }
};
template <class Make>
Builder(Make) -> Builder<Make>;

template <class T>
concept a_builder = requires { typename std::remove_cvref_t<T>::is_builder; };
// A part that holds a builder: one itself, or an optional or a list of them.
template <class T>
struct holds_builder : std::bool_constant<a_builder<T>> {};
template <class T>
struct holds_builder<std::optional<T>> : holds_builder<T> {};
template <class T>
struct holds_builder<std::vector<T>> : holds_builder<T> {};
template <class... T>
concept any_builder = (holds_builder<std::remove_cvref_t<T>>::value || ...);

// A part as the eager combinator takes it, under these rules: a builder
// built, an optional or a list of them each built; anything else as it is.
template <class T, class Rules>
T built(T part, const Rules&) {
  return part;
}
template <a_builder B, class Rules>
auto built(B part, const Rules& rules) {
  return std::move(part).build(rules);
}
template <class T, class Rules>
  requires a_builder<T>
auto built(std::optional<T> part, const Rules& rules) {
  using N = decltype(std::move(*part).build(rules));
  return part ? std::optional<N>(std::move(*part).build(rules)) : std::optional<N>();
}
template <class T, class Rules>
  requires a_builder<T>
auto built(std::vector<T> parts, const Rules& rules) {
  return parts | std::views::as_rvalue | std::views::transform([&](T&& one) { return std::move(one).build(rules); }) |
         std::ranges::to<std::vector>();
}

// A view named where it is written (SKIFF_NAMED) over a builder: a builder,
// built, of the view named so -- its name the macro's, on what was built.
// The handlers the macro brings to its level are none here: it is no node.
template <class Self, a_builder B>
struct Named<Self, B> {
  using is_builder = std::true_type;
  B inner;
  explicit Named(B made) : inner(std::move(made)) {}
  void onKey() = delete;
  void onPointer() = delete;
  void onText() = delete;
  void onSemantic() = delete;
  template <class Rules>
  auto build(const Rules& rules) && {
    auto node = std::move(inner).build(rules);
    return Named<Self, decltype(node)>(std::move(node));
  }
};

// An eager call made a builder: `make` called, when it is built, with every
// argument built under the same rules.
template <class Make, class... Args>
auto lifted(Make make, Args... args) {
  return Builder{[make = std::move(make), ... args = std::move(args)](const auto& rules) mutable {
    return make(built(std::move(args), rules)...);
  }};
}

// ---- rules --------------------------------------------------------------------------

// What is done with a piece: put something before it or after it (in a row,
// `gap` apart), wrap it (around: f(node, facts) -> anything), make something
// else in its place (instead: f(facts)), or drop it.
template <class F>
struct before_t {
  F f;
  float gap = 0.0f;
};
template <class F>
struct after_t {
  F f;
  float gap = 0.0f;
};
template <class F>
struct around_t {
  F f;
};
template <class F>
struct instead_t {
  F f;
};
struct drop_t {};
template <class F>
constexpr before_t<F> before(F f, float gap = 0.0f) {
  return {f, gap};
}
template <class F>
constexpr after_t<F> after(F f, float gap = 0.0f) {
  return {f, gap};
}
template <class F>
constexpr around_t<F> around(F f) {
  return {f};
}
template <class F>
constexpr instead_t<F> instead(F f) {
  return {f};
}
inline constexpr drop_t drop{};

// A rule: a piece, and what is done with it.
template <class Piece, class Verb>
struct rule {
  Verb verb;
};
template <class Piece>
struct on_t {
  template <class Verb>
  constexpr rule<Piece, Verb> operator()(Verb verb) const {
    return {verb};
  }
};
template <class Piece>
inline constexpr on_t<Piece> on{};
// Rules joined, in their order: a | b.
template <class... Rules>
struct rules {
  std::tuple<Rules...> all;
};
template <class Piece, class Verb>
constexpr std::tuple<rule<Piece, Verb>> rule_tuple(rule<Piece, Verb> one) {
  return {one};
}
template <class... Rules>
constexpr std::tuple<Rules...> rule_tuple(rules<Rules...> some) {
  return some.all;
}
template <class... Rules>
constexpr rules<Rules...> rules_of(std::tuple<Rules...> all) {
  return {all};
}
template <class T>
concept a_rule_set = requires(T one) { rule_tuple(one); };
template <a_rule_set A, a_rule_set B>
constexpr auto operator|(A a, B b) {
  return rules_of(std::tuple_cat(rule_tuple(a), rule_tuple(b)));
}
// No rules: what a builder built where nothing shapes it is built under.
inline constexpr rules<> no_rules{};
// The rules a program builds its views under -- every plugin's, gathered
// where all of them are seen. A piece is built only under them, never under
// no_rules: what a release build folds and what a debug build's table
// applies are then the same rules, and a piece built where they are not
// known does not compile.
template <class R>
struct program_rules {
  using is_program_rules = std::true_type;
  R rules;
};
template <class R>
constexpr auto rule_tuple(const program_rules<R>& all) {
  return rule_tuple(all.rules);
}
// Outside a release build, what a piece's own parts are built under: the
// table holds the program's rules.
struct table_rules {
  using is_program_rules = std::true_type;
};
template <class T>
concept the_program_rules = requires { typename T::is_program_rules; };

// A verb done on a node: one code for both builds -- the release fold calls
// it with the concrete node, the debug table with the held one.
template <class N, class Facts, class F>
auto apply_verb(const before_t<F>& verb, N node, const Facts& facts) {
  return row(hbox(verb.gap, {.autoSize = scene::axes::kBoth}), verb.f(facts), std::move(node));
}
template <class N, class Facts, class F>
auto apply_verb(const after_t<F>& verb, N node, const Facts& facts) {
  return row(hbox(verb.gap, {.autoSize = scene::axes::kBoth}), std::move(node), verb.f(facts));
}
template <class N, class Facts, class F>
auto apply_verb(const around_t<F>& verb, N node, const Facts& facts) {
  return verb.f(std::move(node), facts);
}
template <class N, class Facts, class F>
auto apply_verb(const instead_t<F>& verb, N, const Facts& facts) {
  return verb.f(facts);
}
template <class N, class Facts>
auto apply_verb(drop_t, N node, const Facts&) {
  node.setVisible(false);
  return node;
}

// ---- pieces -------------------------------------------------------------------------

// The facts of a piece that says none: nothing.
struct no_facts {};
// A piece's facts' type: what it says (facts_type), or none.
template <class Piece>
struct facts_type_of {
  using type = no_facts;
};
template <class Piece>
  requires requires { typename Piece::facts_type; }
struct facts_type_of<Piece> {
  using type = typename Piece::facts_type;
};
template <class Piece>
using facts_type_t = typename facts_type_of<Piece>::type;
// Its facts, from its arguments: Self::facts(args...), or none.
template <class Self, class... Args>
facts_type_t<Self> facts_of(const Args&...) {
  return {};
}
template <class Self, class... Args>
  requires requires(const Args&... args) { Self::facts(args...); }
facts_type_t<Self> facts_of(const Args&... args) {
  return Self::facts(args...);
}

// A rule on this piece: its verb done on what was made; on another: as it
// was. Told apart by overloading: the first is the closer match where the
// rule's piece is this one.
template <class Piece, class Verb, class N, class Facts>
auto rule_on(const rule<Piece, Verb>& one, std::type_identity<Piece>, N node, const Facts& facts) {
  return apply_verb(one.verb, std::move(node), facts);
}
template <class Other, class Verb, class Piece, class N, class Facts>
N rule_on(const rule<Other, Verb>&, std::type_identity<Piece>, N node, const Facts&) {
  return node;
}
// The rules on Piece, of a rule set, in order: applied to what it made.
template <class Piece, std::size_t I, class... Rules, class N, class Facts>
auto fold_rules(const std::tuple<Rules...>& all, N node, const Facts& facts) {
  if constexpr (I == sizeof...(Rules))
    return node;
  else
    return fold_rules<Piece, I + 1>(all, rule_on(std::get<I>(all), std::type_identity<Piece>{}, std::move(node), facts), facts);
}

// A callable kept by its type erased, outside a release build only: its
// state on the heap, called and freed through a pointer each -- what the
// table and an erased piece keep (std::move_only_function is not in every
// standard library skiff is built with).
template <class Sig>
struct erased_call;
template <class R, class... Args>
struct erased_call<R(Args...)> {
  std::unique_ptr<void, void (*)(void*)> state{nullptr, [](void*) {}};
  R (*call)(void*, Args...) = nullptr;
  erased_call() = default;
  template <class F>
    requires(!std::same_as<std::remove_cvref_t<F>, erased_call>)
  erased_call(F f)
      : state(new F(std::move(f)), [](void* at) { delete static_cast<F*>(at); }),
        call([](void* at, Args... args) -> R { return (*static_cast<F*>(at))(std::forward<Args>(args)...); }) {}
  R operator()(Args... args) const { return call(state.get(), std::forward<Args>(args)...); }
};

// The table a debug build applies a piece's rules from, filled by the
// program as it starts from its rules (register_rules): for each piece, by
// its key, the verbs on it in order, each over the held node.
struct table_verb {
  erased_call<Held(Held, const void*)> apply;
};
inline std::unordered_map<model::rt::TypeKey, std::vector<table_verb>>& rule_table() {
  static std::unordered_map<model::rt::TypeKey, std::vector<table_verb>> table;
  return table;
}
template <class Piece>
constexpr model::rt::TypeKey piece_key() {
  return model::rt::keyOf<Piece>();
}
template <class Piece, class Verb>
void register_rule(const rule<Piece, Verb>& one) {
  using Facts = facts_type_t<Piece>;
  rule_table()[piece_key<Piece>()].push_back(table_verb{[verb = one.verb](Held node, const void* facts) {
    return Held(apply_verb(verb, std::move(node), *static_cast<const Facts*>(facts)));
  }});
}
template <class... Rules>
void register_rules(const rules<Rules...>& all) {
  std::apply([](const auto&... each) { (register_rule(each), ...); }, all.all);
}
// The table's verbs for a piece, in order, over what it made.
inline Held apply_table(const std::vector<table_verb>& verbs, std::size_t at, Held node, const void* facts) {
  if (at == verbs.size())
    return node;
  return apply_table(verbs, at + 1, verbs[at].apply(std::move(node), facts), facts);
}
// A piece's builder outside a release build: erased where it is made, as a
// named view is -- its type the same whatever it makes -- its rules the
// table's; the rules it is built under not asked.
struct erased_piece {
  erased_call<Held()> make;
  template <class Rules>
  Held operator()(const Rules&) {
    static_assert(the_program_rules<Rules>, "a piece built where the program's rules are not known (under no_rules)");
    return make();
  }
};

// A piece: a function object making a view (Self::make(args...)), keyed by
// Self. Called, it is a builder: built, what it made with the rules on it
// applied -- folded in a release build; outside one, the made node held and
// the table's verbs run on it.
template <class Self>
struct piece {
  template <class... Args>
  auto operator()(const Args&... args) const {
    auto facts = facts_of<Self>(args...);
    auto made = static_cast<const Self&>(*this).make(args...);
    if constexpr (scene::kErasedCombinators) {
      return Builder{erased_piece{[made = std::move(made), facts = std::move(facts)]() mutable {
        Held node(built(std::move(made), table_rules{}));
        const auto found = rule_table().find(piece_key<Self>());
        return found == rule_table().end() ? std::move(node) : apply_table(found->second, 0, std::move(node), &facts);
      }}};
    } else {
      return Builder{[made = std::move(made), facts = std::move(facts)]<class Rules>(const Rules& rules) mutable {
        static_assert(the_program_rules<Rules>, "a piece built where the program's rules are not known (under no_rules)");
        return fold_rules<Self, 0>(rule_tuple(rules), built(std::move(made), rules), facts);
      }};
    }
  }
};

// ---- the combinators, lifted --------------------------------------------------------
// Each the eager one's parameters, where a part is a builder: a builder of it.

template <class... Parts>
  requires any_builder<Parts...>
auto column(Look look, Parts... parts) {
  return lifted([](Look l, auto... p) { return column(std::move(l), std::move(p)...); }, std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires any_builder<Parts...>
auto row(Look look, Parts... parts) {
  return lifted([](Look l, auto... p) { return row(std::move(l), std::move(p)...); }, std::move(look), std::move(parts)...);
}
template <a_builder B>
auto visible(bool on, B node) {
  return lifted([on](auto n) { return visible(on, std::move(n)); }, std::move(node));
}
template <a_builder B>
auto recorded(B node, bool on = true) {
  return lifted([on](auto n) { return recorded(std::move(n), on); }, std::move(node));
}
template <a_builder B>
auto with_cursor(scene::Cursor shape, B node) {
  return lifted([shape](auto n) { return with_cursor(shape, std::move(n)); }, std::move(node));
}
template <a_builder B>
auto styled(scene::Spec spec, B node) {
  return lifted([spec](auto n) { return styled(spec, std::move(n)); }, std::move(node));
}
template <class E, a_builder B>
auto onClick(E event, B node, std::string label = {}) {
  return lifted([event = std::move(event), label = std::move(label)](auto n) mutable {
    return onClick(std::move(event), std::move(n), std::move(label));
  }, std::move(node));
}
template <a_builder B>
auto said(std::optional<scene::SemanticRole> role, bool chosen, B node) {
  return lifted([role = std::move(role), chosen](auto n) mutable { return said(std::move(role), chosen, std::move(n)); }, std::move(node));
}

}  // namespace skiff::compose
