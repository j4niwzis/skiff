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
import skia;
import splice;
import skiff.scene;
import skiff.model;
import skiff.nodes;
import :boxes;
import :wiring;
import :views;
import :reorder;

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

// A part that holds a builder: one itself, or an optional or a list of them,
// or a variant one of whose kinds is one (a message's blocks: a link's,
// which holds a piece, beside a code block's, which does not).
template <class T>
struct holds_builder : std::bool_constant<a_builder<T>> {};
template <class T>
struct holds_builder<std::optional<T>> : holds_builder<T> {};
template <class T>
struct holds_builder<std::vector<T>> : holds_builder<T> {};
template <class... T>
struct holds_builder<spl::variant<T...>> : std::bool_constant<(holds_builder<T>::value || ...)> {};
// And a tuple of parts, one of which holds one (a bubble's plugins' slots:
// each an optional of what its plugin made).
template <class... T>
struct holds_builder<std::tuple<T...>> : std::bool_constant<(holds_builder<T>::value || ...)> {};
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
// A variant: the kind it holds built, as the same kind of the variant of
// what each kind builds (by its index: two kinds may build alike).
template <std::size_t I, class Out, class In, class Rules>
Out built_kind(In& part, const Rules& rules) {
  if constexpr (I + 1 < spl::variant_size_v<In>)
    if (part.index() != I)
      return built_kind<I + 1, Out>(part, rules);
  return Out(std::in_place_index<I>, built(std::move(spl::get<I>(part)), rules));
}
template <class... T, class Rules>
  requires(holds_builder<T>::value || ...)
auto built(spl::variant<T...> part, const Rules& rules) {
  using Out = spl::variant<decltype(built(std::declval<T>(), rules))...>;
  return built_kind<0, Out>(part, rules);
}
template <class... T, class Rules>
  requires(holds_builder<T>::value || ...)
auto built(std::tuple<T...> parts, const Rules& rules) {
  return std::apply([&](T&... each) {
    return std::tuple<decltype(built(std::declval<T>(), rules))...>(built(std::move(each), rules)...);
  }, parts);
}
template <class T, class Rules>
  requires holds_builder<T>::value
auto built(std::optional<T> part, const Rules& rules) {
  using N = decltype(built(std::move(*part), rules));
  return part ? std::optional<N>(built(std::move(*part), rules)) : std::optional<N>();
}
template <class T, class Rules>
  requires holds_builder<T>::value
auto built(std::vector<T> parts, const Rules& rules) {
  return parts | std::views::as_rvalue | std::views::transform([&](T&& one) { return built(std::move(one), rules); }) |
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

// A maker whose rows are builders -- a list's, a window's, a mount's -- made
// one whose rows are built, under the rules the list itself is built under:
// what a node that keeps a maker (WindowFrom, MemoRows, listed) is given when
// it is made in a builder's build. The row's type is then what is built.
template <class Make, class Rules>
struct built_maker {
  Make make;
  Rules rules;
  template <class... Items>
  auto operator()(const Items&... items) const {
    return built(make(items...), rules);
  }
};
template <class Make, class Rules, class... Items>
using built_row_t = decltype(std::declval<const built_maker<Make, Rules>&>()(std::declval<const Items&>()...));

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
// The program's rules, as the table's: what was there before replaced, so
// that registering them again does not run each verb twice.
template <class... Rules>
void register_rules(const rules<Rules...>& all) {
  rule_table().clear();
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
  // Its arguments forwarded: a node given it (what a row is about, made by
  // its caller) moved in, not copied; its facts read from them first.
  template <class... Args>
  auto operator()(Args&&... args) const {
    auto facts = facts_of<Self>(std::as_const(args)...);
    auto made = static_cast<const Self&>(*this).make(std::forward<Args>(args)...);
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
template <class... Parts>
  requires any_builder<Parts...>
auto scrolled(Parts... parts) {
  return lifted([](auto... p) { return scrolled(std::move(p)...); }, std::move(parts)...);
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

template <class Ask, a_builder B>
auto onClickAt(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return onClickAt(std::move(ask), std::move(n)); }, std::move(node));
}
template <class Ask, a_builder B>
auto onRightPressAt(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return onRightPressAt(std::move(ask), std::move(n)); }, std::move(node));
}
template <a_builder B>
auto appearing(bool now, B node, float from = 12.0f) {
  return lifted([now, from](auto n) { return appearing(now, std::move(n), from); }, std::move(node));
}
template <class Want, class Take, a_builder B>
auto held_off_for(Take take, B node) {
  return lifted([take = std::move(take)](auto n) mutable { return held_off_for<Want>(std::move(take), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto washed_for(skia::SkColor colour, float held, float flash, Compute compute, B node) {
  return lifted([colour, held, flash, compute = std::move(compute)](auto n) mutable {
    return washed_for<Want>(colour, held, flash, std::move(compute), std::move(n));
  }, std::move(node));
}
template <class Act, class Mark, class Show, class N>
  requires any_builder<Mark, N>
auto swiped(float reach, float most, Act act, Show show, Mark mark, N node) {
  return lifted([reach, most, act = std::move(act), show = std::move(show)](auto m, auto n) mutable {
    return swiped(reach, most, std::move(act), std::move(show), std::move(m), std::move(n));
  }, std::move(mark), std::move(node));
}
template <class Want, class On, a_builder B>
auto spanned_for(On on, B list) {
  return lifted([on = std::move(on)](auto l) mutable { return spanned_for<Want>(std::move(on), std::move(l)); }, std::move(list));
}

template <class B>
  requires holds_builder<B>::value
auto many(Look look, std::vector<B> parts) {
  return lifted([](Look l, auto p) { return many(std::move(l), std::move(p)); }, std::move(look), std::move(parts));
}
template <class Make, a_builder B>
  requires MakesPress<Make>
auto onPress(Make make, B node, std::string label = {}) {
  return lifted([make = std::move(make), label = std::move(label)](auto n) mutable {
    return onPress(std::move(make), std::move(n), std::move(label));
  }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto shown_for(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return shown_for<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto projected(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return projected<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class List, class Corner, class Tell>
  requires any_builder<List, Corner>
auto with_way_down(Look look, List list, Corner corner, Tell tell) {
  return lifted([tell = std::move(tell)](Look l, auto li, auto co) mutable {
    return with_way_down(std::move(l), std::move(li), std::move(co), std::move(tell));
  }, std::move(look), std::move(list), std::move(corner));
}

// ---- the rest of the combinators, lifted ----------------------------------------------

template <class Tag, a_builder B>
auto marked(Tag tag, B node) {
  return lifted([tag = std::move(tag)](auto n) mutable { return marked(std::move(tag), std::move(n)); }, std::move(node));
}
template <a_builder B>
auto keep_ticking(bool on, B node) {
  return lifted([on](auto n) { return keep_ticking(on, std::move(n)); }, std::move(node));
}
template <class Want, a_builder B>
  requires std::move_constructible<B>
auto bound(B node) {
  return lifted([](auto n) { return bound<Want>(std::move(n)); }, std::move(node));
}
template <class... Reads, class Compute, a_builder B>
  requires reads_parts<Compute, Reads...>
auto derived(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return derived<Reads...>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Read, class Compute, a_builder B>
  requires reads_one_part<Compute, Read>
auto derived(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return derived<Read>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class T, a_builder B>
auto local(B node, T initial = {}) {
  return lifted([initial = std::move(initial)](auto n) mutable { return local<T>(std::move(n), std::move(initial)); }, std::move(node));
}
template <class T, class Handlers, a_builder B>
auto local(Handlers handlers, B node, T initial = {}) {
  return lifted([handlers = std::move(handlers), initial = std::move(initial)](auto n) mutable {
    return local<T>(std::move(handlers), std::move(n), std::move(initial));
  }, std::move(node));
}
template <class T, class Handlers, a_builder B>
auto hidden(Handlers handlers, B node, T initial = {}) {
  return lifted([handlers = std::move(handlers), initial = std::move(initial)](auto n) mutable {
    return hidden<T>(std::move(handlers), std::move(n), std::move(initial));
  }, std::move(node));
}
template <class T, class Handlers, a_builder B>
  requires handlers_not_node<Handlers>
auto state(Handlers handlers, B node) {
  return lifted([handlers = std::move(handlers)](auto n) mutable { return state<T>(std::move(handlers), std::move(n)); }, std::move(node));
}
template <class Within, class Handlers, a_builder B, class... Keys>
auto scoped(Handlers handlers, B node, Keys... keys) {
  return lifted([handlers = std::move(handlers), ... keys = std::move(keys)](auto n) mutable {
    return scoped<Within>(std::move(handlers), std::move(n), std::move(keys)...);
  }, std::move(node));
}
template <a_builder B>
auto named(std::string label, std::string hint, B node) {
  return lifted([label = std::move(label), hint = std::move(hint)](auto n) mutable { return named(std::move(label), std::move(hint), std::move(n)); },
                std::move(node));
}
template <class Want, class Compute, a_builder B>
auto chosen_for(Compute compute, std::optional<scene::SemanticRole> role, B node) {
  return lifted([compute = std::move(compute), role = std::move(role)](auto n) mutable {
    return chosen_for<Want>(std::move(compute), std::move(role), std::move(n));
  }, std::move(node));
}
template <class K, class E, a_builder B>
auto onKey(K key, E event, B node) {
  return lifted([key = std::move(key), event = std::move(event)](auto n) mutable { return onKey(std::move(key), std::move(event), std::move(n)); },
                std::move(node));
}
// A list that asks for more near its end, over a scroll that is a builder
// (c::scrolled of what holds pieces): a builder of it.
template <class Want, class Key, class Compute, class Ask, a_builder B>
auto fetching_for(Compute compute, Ask ask, B scroll) {
  return lifted([compute = std::move(compute), ask = std::move(ask)](auto s) mutable {
    return fetching_for<Want, Key>(std::move(compute), std::move(ask), std::move(s));
  }, std::move(scroll));
}
template <class E, class Ask, a_builder B>
auto onCapture(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return onCapture<E>(std::move(ask), std::move(n)); }, std::move(node));
}
template <class Ask, a_builder B>
auto onKeys(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return onKeys(std::move(ask), std::move(n)); }, std::move(node));
}
template <class... Reads, class Compute, a_builder B>
auto shown_if(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return shown_if<Reads...>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Want, class Phase, class Compute, class Ask, a_builder B>
auto keys_for(Compute compute, Ask ask, B node) {
  return lifted([compute = std::move(compute), ask = std::move(ask)](auto n) mutable {
    return keys_for<Want, Phase>(std::move(compute), std::move(ask), std::move(n));
  }, std::move(node));
}
template <class Want, class Compute, class Apply, a_builder B>
auto applied(Compute compute, Apply apply, B node) {
  return lifted([compute = std::move(compute), apply = std::move(apply)](auto n) mutable {
    return applied<Want>(std::move(compute), std::move(apply), std::move(n));
  }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto focused_by(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return focused_by<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto spec_for(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return spec_for<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto rekeyed_by(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return rekeyed_by<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Ratio, a_builder B>
auto fitted(Fit fit, Ratio ratio, B node) {
  return lifted([fit, ratio = std::move(ratio)](auto n) mutable { return fitted(fit, std::move(ratio), std::move(n)); }, std::move(node));
}
template <class Fit_, a_builder B>
auto shrunk(float wide, float gap, Fit_ fit, B node) {
  return lifted([wide, gap, fit = std::move(fit)](auto n) mutable { return shrunk(wide, gap, std::move(fit), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto placed_at(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return placed_at<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto popped_at(Compute compute, float edge, B node) {
  return lifted([compute = std::move(compute), edge](auto n) mutable { return popped_at<Want>(std::move(compute), edge, std::move(n)); }, std::move(node));
}
template <class E, class Make, a_builder B>
auto onPointer(Make make, B node) {
  return lifted([make = std::move(make)](auto n) mutable { return onPointer<E>(std::move(make), std::move(n)); }, std::move(node));
}
template <class Act, a_builder B>
auto onDragAcross(Act act, B node) {
  return lifted([act = std::move(act)](auto n) mutable { return onDragAcross(std::move(act), std::move(n)); }, std::move(node));
}
template <class E, a_builder B>
auto onRightPress(E event, B node) {
  return lifted([event = std::move(event)](auto n) mutable { return onRightPress(std::move(event), std::move(n)); }, std::move(node));
}
template <class Act, a_builder B>
auto resting(double ms, Act act, B node) {
  return lifted([ms, act = std::move(act)](auto n) mutable { return resting(ms, std::move(act), std::move(n)); }, std::move(node));
}
template <class Act, a_builder B>
auto hovering(Act act, B node) {
  return lifted([act = std::move(act)](auto n) mutable { return hovering(std::move(act), std::move(n)); }, std::move(node));
}
template <class Act, a_builder B>
auto hovering(Look look, Act act, B node) {
  return lifted([look = std::move(look), act = std::move(act)](auto n) mutable { return hovering(std::move(look), std::move(act), std::move(n)); },
                std::move(node));
}
template <class Ask, a_builder B>
auto moves_focus(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return moves_focus(std::move(ask), std::move(n)); }, std::move(node));
}
template <class Want, class Make, a_builder B>
auto asking_for(Make make, B node) {
  return lifted([make = std::move(make)](auto n) mutable { return asking_for<Want>(std::move(make), std::move(n)); }, std::move(node));
}
template <class Act, a_builder B>
auto flicked(float reach, Act act, B node) {
  return lifted([reach, act = std::move(act)](auto n) mutable { return flicked(reach, std::move(act), std::move(n)); }, std::move(node));
}
template <class Act, a_builder B>
auto flicked_holding(float reach, Act act, B node) {
  return lifted([reach, act = std::move(act)](auto n) mutable { return flicked_holding(reach, std::move(act), std::move(n)); }, std::move(node));
}
template <class Want, class Compute, a_builder B>
auto fading_for(Compute compute, double hold, B node) {
  return lifted([compute = std::move(compute), hold](auto n) mutable { return fading_for<Want>(std::move(compute), hold, std::move(n)); },
                std::move(node));
}
template <a_builder B>
auto appearing_across(bool now, B node, float from, float duration = 220.0f) {
  return lifted([now, from, duration](auto n) { return appearing_across(now, std::move(n), from, duration); }, std::move(node));
}
template <class Policy, a_builder B>
auto arranged(typename Policy::State state, B node) {
  return lifted([state = std::move(state)](auto n) mutable { return arranged<Policy>(std::move(state), std::move(n)); }, std::move(node));
}
template <a_builder B>
auto place(Look look, B node) {
  return lifted([](Look l, auto n) { return place(std::move(l), std::move(n)); }, std::move(look), std::move(node));
}
template <class M, class Told, class Compute, a_builder B>
auto with_model(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return with_model<M, Told>(std::move(compute), std::move(n)); }, std::move(node));
}
template <a_builder B>
auto focus_frame(Look look, B field) {
  return lifted([](Look l, auto f) { return focus_frame(std::move(l), std::move(f)); }, std::move(look), std::move(field));
}
template <class Item, class Height, a_builder B>
auto virtual_rows(Height height, B node, VirtualRowsOptions options = {}) {
  return lifted([height = std::move(height), options](auto n) mutable { return virtual_rows<Item>(std::move(height), std::move(n), options); },
                std::move(node));
}
template <class Want, class Compute, a_builder B>
auto slid_for(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return slid_for<Want>(std::move(compute), std::move(n)); }, std::move(node));
}
template <class Under, class Over>
  requires any_builder<Under, Over>
auto frozen_under(Look look, Under under, Over over) {
  return lifted([](Look l, auto u, auto o) { return frozen_under(std::move(l), std::move(u), std::move(o)); }, std::move(look), std::move(under),
                std::move(over));
}
template <class Make, a_builder B>
auto tapped_off(Make make, B node) {
  return lifted([make = std::move(make)](auto n) mutable { return tapped_off(std::move(make), std::move(n)); }, std::move(node));
}
template <class Want, class Tell, a_builder B>
auto kept_scroll(Tell tell, B node) {
  return lifted([tell = std::move(tell)](auto n) mutable { return kept_scroll<Want>(std::move(tell), std::move(n)); }, std::move(node));
}
// What a view named by its type is, built: a builder's what it builds
// under the rules; a node's itself; void, none named.
template <class View, class Rules>
struct built_view {
  using type = decltype(built(std::declval<View>(), std::declval<const Rules&>()));
};
template <class Rules>
struct built_view<void, Rules> {
  using type = void;
};
// Over a builder: the view it scrolls named as built under the same rules
// -- a View that is itself a builder (a marked part of what it holds) is
// found as what that builds.
template <class Key, class View = void, a_builder B>
auto remembered_scroll(B node) {
  return Builder{[node = std::move(node)]<class Rules>(const Rules& rules) mutable {
    return remembered_scroll<Key, typename built_view<View, Rules>::type>(built(std::move(node), rules));
  }};
}
template <class Key, a_builder B>
auto restarted_scroll(B node) {
  return lifted([](auto n) { return restarted_scroll<Key>(std::move(n)); }, std::move(node));
}
template <a_builder B>
auto clamped_width(WidthLimit limit, B node) {
  return lifted([limit](auto n) { return clamped_width(limit, std::move(n)); }, std::move(node));
}
template <class Tag, a_builder B>
auto reorder_lane(B node) {
  return lifted([](auto n) { return reorder_lane<Tag>(std::move(n)); }, std::move(node));
}
// A mount whose content is a builder: built when the mount is; its maker's
// arguments built under the same rules as it makes them, the content then
// what they build.
template <class Make, class Rules>
struct built_arguments {
  Make make;
  Rules rules;
  template <class Facts>
  auto operator()(const Facts& now) {
    return std::apply([&](auto&&... made) { return std::tuple{built(std::forward<decltype(made)>(made), rules)...}; }, make(now));
  }
};
template <a_builder Content, class Facts, class Make>
auto mount(Make make, scene::Spec spec = {}, bool floats = false) {
  return Builder{[make = std::move(make), spec, floats]<class Rules>(const Rules& rules) mutable {
    using made_t = decltype(built(std::declval<Content>(), rules));
    return mount<made_t, Facts>(built_arguments<Make, Rules>{std::move(make), rules}, spec, floats);
  }};
}
template <a_builder Content, class Facts, class Make>
auto mount_now(Make make, const Facts& first, scene::Spec spec = {}) {
  return Builder{[make = std::move(make), first, spec]<class Rules>(const Rules& rules) mutable {
    using made_t = decltype(built(std::declval<Content>(), rules));
    return mount_now<made_t, Facts>(built_arguments<Make, Rules>{std::move(make), rules}, first, spec);
  }};
}

// A list whose maker makes builders: built when the list is, its maker one
// of built rows under the same rules.
template <class Element, class Make>
  requires a_builder<std::invoke_result_t<Make&, const Element&>>
auto listed(Look look, Make make) {
  return Builder{[look = std::move(look), make = std::move(make)]<class Rules>(const Rules& rules) mutable {
    return listed<Element>(std::move(look), built_maker<Make, Rules>{std::move(make), rules});
  }};
}


// ---- nodes that keep a maker, made by combinators --------------------------------------

// Rows kept by their keys (MemoRows): each made by Make from its item
// (Item: a key, its view, and the rest). Where a row is a builder, a
// builder of the rows: their maker made one of built rows (built_maker)
// under the rules the rows are built under.
template <class Key, class View, class Item, class Make, class Holder>
  requires(!a_builder<std::invoke_result_t<Make&, const Item&>>)
auto memo_rows(Make make, Holder holder, scene::Spec spec = {.fillX = true, .autoSize = scene::axes::kY}) {
  return nodes::MemoRows<Key, View, std::invoke_result_t<Make&, const Item&>, Make, Holder>(std::move(make), std::move(holder), std::move(spec));
}
template <class Key, class View, class Item, class Make, class Holder>
  requires a_builder<std::invoke_result_t<Make&, const Item&>>
auto memo_rows(Make make, Holder holder, scene::Spec spec = {.fillX = true, .autoSize = scene::axes::kY}) {
  return Builder{[make = std::move(make), holder = std::move(holder), spec = std::move(spec)]<class Rules>(const Rules& rules) mutable {
    using maker = built_maker<Make, Rules>;
    return nodes::MemoRows<Key, View, std::invoke_result_t<maker&, const Item&>, maker, Holder>(maker{std::move(make), rules}, std::move(holder),
                                                                                               std::move(spec));
  }};
}
template <a_builder B>
auto side_scroll(B line, bool verticalWheel = false) {
  return lifted([verticalWheel](auto l) { return side_scroll(std::move(l), verticalWheel); }, std::move(line));
}
// A drag between bars whose rows are builders (its maker's): built under
// the rules, its maker made one of built rows. Its lanes function is asked
// with the rules' type -- lanes.operator()<Rules>(found) -- so that it may
// name the lanes as they were built.
// A part marked by Tag, found by its type as the walks see it: concrete in
// every build (marked() erases it outside a release one), so that what
// names it -- a scroll kept for it -- finds it as the type it names. Over a
// builder, a builder of it.
template <class Tag, class N>
  requires(!a_builder<N>)
Marked<Tag, N> marked_as(N node) {
  return Marked<Tag, N>(std::move(node));
}
template <class Tag, a_builder B>
auto marked_as(B node) {
  return lifted([](auto n) { return marked_as<Tag>(std::move(n)); }, std::move(node));
}
template <class Lanes, class Rules>
struct lanes_under {
  Lanes lanes;
  template <class Found>
  auto operator()(const Found& found) const {
    return lanes.template operator()<Rules>(found);
  }
};
template <class Key, class View, class Make, class Surface, class Lanes>
  requires(a_builder<std::invoke_result_t<Make&, const Key&, const View&, bool>> || holds_builder<Surface>::value)
auto reorder_bars(Surface surface, Make make, Lanes lanes, skia::SkColor highlight = 0) {
  return Builder{[surface = std::move(surface), make = std::move(make), lanes = std::move(lanes), highlight]<class Rules>(const Rules& rules) mutable {
    return reorder_bars<Key, View>(built(std::move(surface), rules), built_maker<Make, Rules>{std::move(make), rules},
                                   lanes_under<Lanes, Rules>{std::move(lanes)}, highlight);
  }};
}

}  // namespace skiff::compose
