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
// A builder made: see make_builder, below the rules (it builds under the
// table's outside a release build).
template <class F>
auto make_builder(F f);

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
  return make_builder([make = std::move(make), ... args = std::move(args)](const auto& rules) mutable {
    return make(built(std::move(args), rules)...);
  });
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
// What a builder is made as. In a release build, the callable itself, built
// where the program builds its views, under its rules folded. Outside one,
// built at once, where it is made -- its pieces' rules are the table's
// there, whatever it is built under -- and what it made kept, as its own
// type, until it is built: the nodes of a view made in the unit its code is
// emitted in, not all of them in the one the window is built in (scene.cc's
// half a thousand seconds, a debug build's).
template <class Made>
struct prebuilt {
  Made made;
  template <class Rules>
  Made operator()(const Rules&) {
    return std::move(made);
  }
};
template <class F>
auto make_builder(F f) {
  if constexpr (scene::kErasedCombinators)
    return Builder{prebuilt<decltype(f(table_rules{}))>{f(table_rules{})}};
  else
    return Builder{std::move(f)};
}
template <class T>
concept the_program_rules = requires { typename T::is_program_rules; };
// A builder as a scene's root (scene::Scene's buildRoot): built there, once,
// under no rules -- a program with rules builds its root itself, under its
// own (as mux's window is), and gives the scene what that made.
template <class B>
  requires a_builder<std::remove_cvref_t<B>>
auto buildRoot(B &&builder) {
  return std::remove_cvref_t<B>(std::forward<B>(builder)).build(program_rules{no_rules});
}

// What a verb makes, as a node: said with the combinators (a builder), built
// there and then -- under no rules: a verb's own making is not shaped again.
template <class T>
auto made_now(T made) {
  if constexpr (a_builder<T>)
    return std::move(made).build(program_rules{no_rules});
  else
    return made;
}
// A verb done on a node: one code for both builds -- the release fold calls
// it with the concrete node, the debug table with the held one.
template <class N, class Facts, class F>
auto apply_verb(const before_t<F>& verb, N node, const Facts& facts) {
  return made_now(row(hbox(verb.gap, {.autoSize = scene::axes::kBoth}), verb.f(facts), std::move(node)));
}
template <class N, class Facts, class F>
auto apply_verb(const after_t<F>& verb, N node, const Facts& facts) {
  return made_now(row(hbox(verb.gap, {.autoSize = scene::axes::kBoth}), std::move(node), verb.f(facts)));
}
template <class N, class Facts, class F>
auto apply_verb(const around_t<F>& verb, N node, const Facts& facts) {
  return made_now(verb.f(std::move(node), facts));
}
template <class N, class Facts, class F>
auto apply_verb(const instead_t<F>& verb, N, const Facts& facts) {
  return made_now(verb.f(facts));
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
      return make_builder(erased_piece{[made = std::move(made), facts = std::move(facts)]() mutable {
        Held node(built(std::move(made), table_rules{}));
        const auto found = rule_table().find(piece_key<Self>());
        return found == rule_table().end() ? std::move(node) : apply_table(found->second, 0, std::move(node), &facts);
      }});
    } else {
      return make_builder([made = std::move(made), facts = std::move(facts)]<class Rules>(const Rules& rules) mutable {
        static_assert(the_program_rules<Rules>, "a piece built where the program's rules are not known (under no_rules)");
        return fold_rules<Self, 0>(rule_tuple(rules), built(std::move(made), rules), facts);
      });
    }
  }
};

// ---- the combinators ------------------------------------------------------------------
// Each said once: a builder whose build makes its node -- what it is given
// where a node goes (a part: a builder, or a node made already) built under
// the same rules, the node made of them there. The node types (compose's
// boxes, views, wiring) are what is made; a view never makes one itself.
// A part: what a build makes a node of -- a node, a builder, or a list, an
// optional, a variant or a tuple of them (built, a node holding them).
template <class T>
struct part_like : std::bool_constant<std::derived_from<T, scene::NodeBase> || a_builder<T>> {};
template <class T>
struct part_like<std::optional<T>> : part_like<T> {};
template <class T>
struct part_like<std::vector<T>> : part_like<T> {};
template <class... T>
struct part_like<spl::variant<T...>> : std::bool_constant<(part_like<T>::value && ...)> {};
template <class... T>
struct part_like<std::tuple<T...>> : std::bool_constant<(part_like<T>::value && ...)> {};
template <class T>
concept a_part = part_like<std::remove_cvref_t<T>>::value;

// A column or a row: what it is built into is a stack (Box) of its parts as
// built -- nodes as they are, builders under the same rules -- made there,
// erased where a build erases.
template <class... Parts>
auto column(Look look, Parts... parts) {
  look.fHorizontal = false;
  return lifted([](Look l, auto... p) { return erased(Box(std::move(l), as_part(std::move(p))...)); }, std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires(!std::same_as<Parts, Look> && ...)
auto column(Parts... parts) {
  return column(vbox(), std::move(parts)...);
}
template <class... Parts>
auto row(Look look, Parts... parts) {
  look.fHorizontal = true;
  return lifted([](Look l, auto... p) { return erased(Box(std::move(l), as_part(std::move(p))...)); }, std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires(!std::same_as<Parts, Look> && ...)
auto row(Parts... parts) {
  return row(hbox(), std::move(parts)...);
}
// Its parts in a view that scrolls.
template <class... Parts>
auto scrolled(Parts... parts) {
  return lifted([](auto... p) { return nodes::ScrollContainer<decltype(p)...>(std::move(p)...); }, std::move(parts)...);
}
// A node shown, or there but hidden.
template <a_part N>
auto visible(bool on, N node) {
  return lifted([on](auto n) {
    n.setVisible(on);
    return n;
  }, std::move(node));
}
// A subtree whose drawing can be replayed until it is damaged (on the node
// as the walks see it: a Held's, its node's).
template <a_part N>
auto recorded(N node, bool on = true) {
  return lifted([on](auto n) {
    state_of(n).setRecorded(on);
    return n;
  }, std::move(node));
}
// A node showing the pointer as `shape` over it.
template <a_part N>
auto with_cursor(scene::Cursor shape, N node) {
  return lifted([shape](auto n) {
    state_of(n).setCursor(shape);
    return n;
  }, std::move(node));
}
// A node with its spec applied: said as it is put in its place.
template <a_part N>
auto styled(scene::Spec spec, N node) {
  return lifted([spec](auto n) {
    n.apply(spec);
    return n;
  }, std::move(node));
}
template <class E, a_part B>
auto onClick(E event, B node, std::string label = {}) {
  return lifted([event = std::move(event), label = std::move(label)](auto n) mutable {
    return erased(OnClick<E, part_t<decltype(n)>>(std::move(event), as_part(std::move(n)), std::move(label)));
  }, std::move(node));
}
template <a_part B>
auto said(std::optional<scene::SemanticRole> role, bool chosen, B node) {
  return lifted([role = std::move(role), chosen](auto n) mutable { return erased(Said<part_t<decltype(n)>>(std::move(role), chosen, as_part(std::move(n)))); }, std::move(node));
}

template <class Ask, a_part B>
auto onClickAt(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return erased(OnClickAt<Ask, part_t<decltype(n)>>(std::move(ask), as_part(std::move(n)))); }, std::move(node));
}
template <class Ask, a_part B>
auto onRightPressAt(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return erased(OnRightPressAt<Ask, part_t<decltype(n)>>(std::move(ask), as_part(std::move(n)))); }, std::move(node));
}
template <a_part B>
auto appearing(bool now, B node, float from = 12.0f) {
  return lifted([now, from](auto n) { return erased(Appearing<part_t<decltype(n)>>(now, from, as_part(std::move(n)))); }, std::move(node));
}
template <class Want, class Take, a_part B>
auto held_off_for(Take take, B node) {
  return lifted([take = std::move(take)](auto n) mutable { return detail::projecting<Want>(std::move(take), HeldOff<decltype(n)>(std::move(n))); }, std::move(node));
}
template <class Want, class Compute, a_part B>
auto washed_for(skia::SkColor colour, float held, float flash, Compute compute, B node) {
  return lifted([colour, held, flash, compute = std::move(compute)](auto n) mutable {
    return detail::projecting<Want>(std::move(compute), Washed<part_t<decltype(n)>>(colour, held, flash, as_part(std::move(n))));
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

// A stack of a list of parts, as many as there are, in their order.
template <a_part N>
auto many(Look look, std::vector<N> parts) {
  return lifted([](Look l, auto p) {
    using Made = typename decltype(p)::value_type;
    return erased(Many<part_t<Made>>(std::move(l), as_parts(std::move(p))));
  }, std::move(look), std::move(parts));
}
template <class Make, a_part B>
  requires MakesPress<Make>
auto onPress(Make make, B node, std::string label = {}) {
  return lifted([make = std::move(make), label = std::move(label)](auto n) mutable {
    return erased(OnPress<Make, part_t<decltype(n)>>(std::move(make), as_part(std::move(n)), std::move(label)));
  }, std::move(node));
}
// Visibility projected from a bound part, a scope's or a Local's included.
template <class Want, class Compute, a_part N>
auto shown_for(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable {
    return detail::projecting<Want>(std::move(compute), ShownBy<part_t<decltype(n)>>(as_part(std::move(n))));
  }, std::move(node));
}
// A node shown what Compute makes of the part Want, as it moves.
template <class Want, class Compute, a_part N>
auto projected(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return detail::projecting<Want>(std::move(compute), std::move(n)); },
                std::move(node));
}
template <class List, class Corner, class Tell>
  requires any_builder<List, Corner>
auto with_way_down(Look look, List list, Corner corner, Tell tell) {
  return lifted([tell = std::move(tell)](Look l, auto li, auto co) mutable {
    return with_way_down(std::move(l), std::move(li), std::move(co), std::move(tell));
  }, std::move(look), std::move(list), std::move(corner));
}

// ---- the rest of the combinators, lifted ----------------------------------------------

// A node marked where it is made, found by its mark (scene::findNode).
template <class Tag, a_part N>
auto marked(Tag tag, N node) {
  return lifted([tag = std::move(tag)](auto n) mutable {
    return erased(Marked<Tag, part_t<decltype(n)>>(as_part(std::move(n)), std::move(tag)));
  }, std::move(node));
}
// A subtree kept polling while an external deadline is pending.
template <a_part N>
auto keep_ticking(bool on, N node) {
  return lifted([on](auto n) { return erased(Ticking<part_t<decltype(n)>>(on, as_part(std::move(n)))); }, std::move(node));
}
// A node bound to the part Want: read as it moves.
template <class Want, a_part N>
auto bound(N node) {
  return lifted([](auto n) { return erased(bind::Bound<Want, part_t<decltype(n)>>(as_part(std::move(n)))); }, std::move(node));
}
// A node shown what Compute makes of the parts it reads, as they move.
template <class... Reads, class Compute, a_part N>
  requires reads_parts<Compute, Reads...>
auto derived(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable {
    return erased(bind::Derived<Compute, part_t<decltype(n)>, Reads...>(as_part(std::move(n)), std::move(compute)));
  }, std::move(node));
}
// What Compute makes of one part, the part taken: projected from it,
// wherever it is -- a page's own state (a Local) as well as the model.
template <class Read, class Compute, a_part N>
  requires reads_one_part<Compute, Read>
auto derived(Compute compute, N node) {
  return projected<Read>(std::move(compute), std::move(node));
}
// A part of a page's own (a Local): what it is made with, kept over its
// node; with the handlers that answer what is sent within, or none.
template <class T, a_part N>
auto local(N node, T initial = {}) {
  return lifted([initial = std::move(initial)](auto n) mutable {
    return erased(bind::Local<T, bind::NoHandlers, part_t<decltype(n)>>({}, as_part(std::move(n)), std::move(initial)));
  }, std::move(node));
}
template <class T, class Handlers, a_part N>
  requires(!a_part<Handlers>)
auto local(Handlers handlers, N node, T initial = {}) {
  return lifted([handlers = std::move(handlers), initial = std::move(initial)](auto n) mutable {
    return erased(bind::Local<T, Handlers, part_t<decltype(n)>>(std::move(handlers), as_part(std::move(n)), std::move(initial)));
  }, std::move(node));
}
// The same, its part kept out of what is shown (hidden state).
template <class T, class Handlers, a_part N>
auto hidden(Handlers handlers, N node, T initial = {}) {
  return lifted([handlers = std::move(handlers), initial = std::move(initial)](auto n) mutable {
    return erased(bind::Local<T, Handlers, part_t<decltype(n)>, true>(std::move(handlers), as_part(std::move(n)), std::move(initial)));
  }, std::move(node));
}
// A part of the model kept by the view it is made over -- the model the
// tree is bound to holds it because the view says so (bind::ModelOf) --
// with the handlers that answer what is sent within.
template <class T, class Handlers, a_part N>
  requires(!a_part<Handlers>)
auto state(Handlers handlers, N node) {
  return lifted([handlers = std::move(handlers)](auto n) mutable {
    bind::Stated<T, Handlers, part_t<decltype(n)>> made(std::move(handlers), as_part(std::move(n)));
    over_held(made);
    return made;
  }, std::move(node));
}
template <class T, a_part N>
auto state(N node) {
  return state<T>(Handlers<>{}, std::move(node));
}
// A scope over a part of Within, by its keys: what is sent within answered
// by its handlers there. Concrete in every build: what moves it
// (rekeyed_by) reads it as itself.
template <class Within, class Handlers, a_part N, class... Keys>
auto scoped(Handlers handlers, N node, Keys... keys) {
  return lifted([handlers = std::move(handlers), ... keys = std::move(keys)](auto n) mutable {
    bind::Scoped<Within, Handlers, decltype(n), Keys...> made(std::move(handlers), std::move(n), std::move(keys)...);
    over_held(made);
    return made;
  }, std::move(node));
}
template <a_part B>
auto named(std::string label, std::string hint, B node) {
  return lifted([label = std::move(label), hint = std::move(hint)](auto n) mutable { Said<part_t<decltype(n)>> out(std::nullopt, false, as_part(std::move(n)));
    out.fLabel = std::move(label);
    out.fHint = std::move(hint);
    return erased(std::move(out));
  },
                std::move(node));
}
// Said to be chosen (a tab, a choice) as Compute says of the part Want.
template <class Want, class Compute, a_part N>
auto chosen_for(Compute compute, std::optional<scene::SemanticRole> role, N node) {
  return lifted([compute = std::move(compute), role = std::move(role)](auto n) mutable {
    return detail::projecting<Want>(std::move(compute), Said<part_t<decltype(n)>>(std::move(role), false, as_part(std::move(n))));
  }, std::move(node));
}
// A key answered with an event.
template <class K, class E, a_part N>
auto onKey(K key, E event, N node) {
  return lifted([key = std::move(key), event = std::move(event)](auto n) mutable {
    return erased(OnKey<E, part_t<decltype(n)>, K>(key, std::move(event), as_part(std::move(n))));
  }, std::move(node));
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
// Keys answered as `ask(press)` says.
template <class Ask, a_part N>
auto onKeys(Ask ask, N node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return erased(OnKeys<Ask, part_t<decltype(n)>>(std::move(ask), as_part(std::move(n)))); },
                std::move(node));
}
// Shown or hidden as Compute says of the parts it reads.
template <class... Reads, class Compute, a_part N>
auto shown_if(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable {
    using Shown = ShownBy<part_t<decltype(n)>>;
    return erased(bind::Derived<Compute, Shown, Reads...>(Shown(as_part(std::move(n))), std::move(compute)));
  }, std::move(node));
}
// A text that reads what Compute makes of the parts it reads.
template <class... Reads, class Compute>
auto text_of(Compute compute, nodes::Text text) {
  return lifted([compute = std::move(compute)](nodes::Text t) mutable {
    return erased(bind::Derived<Compute, TextOf, Reads...>(TextOf(std::move(t)), std::move(compute)));
  }, std::move(text));
}
// And of the one part Want.
template <class Want, class Compute>
auto text_for(Compute compute, nodes::Text text) {
  return lifted([compute = std::move(compute)](nodes::Text t) mutable { return detail::projecting<Want>(std::move(compute), TextOf(std::move(t))); },
                std::move(text));
}
// Keys answered as `ask(now, press)` says, with what Compute makes of the
// part Want as last read -- in the phase given.
template <class Want, class Phase, class Compute, class Ask, a_part N>
auto keys_for(Compute compute, Ask ask, N node) {
  return lifted([compute = std::move(compute), ask = std::move(ask)](auto n) mutable {
    using K = std::remove_cvref_t<std::invoke_result_t<Compute &, const typename bind::BoundValueT<Want>::type &>>;
    return detail::projecting<Want>(std::move(compute), KeysFor<Phase, K, Ask, part_t<decltype(n)>>(std::move(ask), as_part(std::move(n))));
  }, std::move(node));
}
// A node shown what it reads by `apply(node, value)`: a leaf with no read
// of its own (an icon's shape, an avatar's person).
template <class Want, class Compute, class Apply, a_part N>
auto applied(Compute compute, Apply apply, N node) {
  return lifted([compute = std::move(compute), apply = std::move(apply)](auto n) mutable {
    using Made = decltype(n);
    using In = typename bind::BoundValueT<Want>::type;
    using Out = std::remove_cvref_t<std::invoke_result_t<Compute &, const In &>>;
    auto fn = erased_fn<void(Made &, const Out &)>(std::move(apply));
    return detail::projecting<Want>(std::move(compute), Applied<decltype(fn), Made>(std::move(fn), std::move(n)));
  }, std::move(node));
}
// Focused where what Compute makes of the part Want asks it anew.
template <class Want, class Compute, a_part N>
auto focused_by(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable {
    return detail::projecting<Want>(std::move(compute), FocusedBy<part_t<decltype(n)>>(as_part(std::move(n))));
  }, std::move(node));
}
// Its spec as Compute makes it of the part Want.
template <class Want, class Compute, a_part N>
auto spec_for(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable {
    return detail::projecting<Want>(std::move(compute), SpecOf<part_t<decltype(n)>>(as_part(std::move(n))));
  }, std::move(node));
}
// A scope moved to the keys Compute makes of the part Want.
template <class Want, class Compute, a_part N>
auto rekeyed_by(Compute compute, N node) {
  return lifted([compute = std::move(compute)](auto n) mutable {
    return detail::bound_to<Want>(Rekeyed<Compute, part_t<decltype(n)>>(std::move(compute), as_part(std::move(n))));
  }, std::move(node));
}
template <class Ratio, a_part B>
auto fitted(Fit fit, Ratio ratio, B node) {
  return lifted([fit, ratio = std::move(ratio)](auto n) mutable { return erased(Fitted<Ratio, decltype(n)>(fit, std::move(ratio), std::move(n))); }, std::move(node));
}
template <class Fit_, a_part B>
auto shrunk(float wide, float gap, Fit_ fit, B node) {
  return lifted([wide, gap, fit = std::move(fit)](auto n) mutable { return erased(Shrunk<Fit_, decltype(n)>(wide, gap, std::move(fit), std::move(n))); }, std::move(node));
}
template <class Want, class Compute, a_part B>
auto placed_at(Compute compute, B node) {
  return lifted([compute = std::move(compute)](auto n) mutable { return detail::projecting<Want>(std::move(compute), PlacedAt<part_t<decltype(n)>>(as_part(std::move(n)))); }, std::move(node));
}
template <class Want, class Compute, a_part B>
auto popped_at(Compute compute, float edge, B node) {
  return lifted([compute = std::move(compute), edge](auto n) mutable { return detail::projecting<Want>(std::move(compute), Popped<part_t<decltype(n)>>(edge, as_part(std::move(n)))); }, std::move(node));
}
template <class E, class Make, a_part B>
auto onPointer(Make make, B node) {
  return lifted([make = std::move(make)](auto n) mutable { return erased(OnPointer<E, Make, part_t<decltype(n)>>(std::move(make), as_part(std::move(n)))); }, std::move(node));
}
template <class Act, a_part B>
auto onDragAcross(Act act, B node) {
  return lifted([act = std::move(act)](auto n) mutable { return erased(OnDragAcross<Act, part_t<decltype(n)>>(std::move(act), as_part(std::move(n)))); }, std::move(node));
}
template <class E, a_part B>
auto onRightPress(E event, B node) {
  return lifted([event = std::move(event)](auto n) mutable { return erased(OnRightPress<E, part_t<decltype(n)>>(std::move(event), as_part(std::move(n)))); }, std::move(node));
}
template <class Act, a_part B>
auto resting(double ms, Act act, B node) {
  return lifted([ms, act = std::move(act)](auto n) mutable { return erased(Resting<Act, part_t<decltype(n)>>(ms, std::move(act), as_part(std::move(n)))); }, std::move(node));
}
template <class Act, a_part B>
auto hovering(Act act, B node) {
  return lifted([act = std::move(act)](auto n) mutable { return erased(Hovering<Act, part_t<decltype(n)>>(std::move(act), as_part(std::move(n)))); }, std::move(node));
}
template <class Act, a_part B>
auto hovering(Look look, Act act, B node) {
  return lifted([look = std::move(look), act = std::move(act)](auto n) mutable { return erased(Hovering<Act, part_t<decltype(n)>>(std::move(look), std::move(act), as_part(std::move(n)))); },
                std::move(node));
}
template <class Ask, a_part B>
auto moves_focus(Ask ask, B node) {
  return lifted([ask = std::move(ask)](auto n) mutable { return erased(MovesFocus<Ask, part_t<decltype(n)>>(std::move(ask), as_part(std::move(n)))); }, std::move(node));
}
template <class Want, class Make, a_part B>
auto asking_for(Make make, B node) {
  return lifted([make = std::move(make)](auto n) mutable { return erased(bind::Bound<Want, AskingFor<Want, Make, part_t<decltype(n)>>>(AskingFor<Want, Make, part_t<decltype(n)>>(std::move(make), as_part(std::move(n))))); }, std::move(node));
}
template <class Act, a_part B>
auto flicked(float reach, Act act, B node) {
  return lifted([reach, act = std::move(act)](auto n) mutable { return erased(Flicked<Act, part_t<decltype(n)>>(reach, std::move(act), as_part(std::move(n)))); }, std::move(node));
}
template <class Act, a_part B>
auto flicked_holding(float reach, Act act, B node) {
  return lifted([reach, act = std::move(act)](auto n) mutable { return erased(Flicked<Act, part_t<decltype(n)>>(reach, std::move(act), as_part(std::move(n)), true)); }, std::move(node));
}
template <class Want, class Compute, a_part B>
auto fading_for(Compute compute, double hold, B node) {
  return lifted([compute = std::move(compute), hold](auto n) mutable { return detail::projecting<Want>(std::move(compute), Fading<part_t<decltype(n)>>(hold, as_part(std::move(n)))); },
                std::move(node));
}
template <a_part B>
auto appearing_across(bool now, B node, float from, float duration = 220.0f) {
  return lifted([now, from, duration](auto n) { return erased(Appearing<part_t<decltype(n)>, appear::across>(now, from, as_part(std::move(n)), duration)); }, std::move(node));
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
  return make_builder([node = std::move(node)]<class Rules>(const Rules& rules) mutable {
    return remembered_scroll<Key, typename built_view<View, Rules>::type>(built(std::move(node), rules));
  });
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
  return make_builder([make = std::move(make), spec, floats]<class Rules>(const Rules& rules) mutable {
    using made_t = decltype(built(std::declval<Content>(), rules));
    return mount<made_t, Facts>(built_arguments<Make, Rules>{std::move(make), rules}, spec, floats);
  });
}
template <a_builder Content, class Facts, class Make>
auto mount_now(Make make, const Facts& first, scene::Spec spec = {}) {
  return make_builder([make = std::move(make), first, spec]<class Rules>(const Rules& rules) mutable {
    using made_t = decltype(built(std::declval<Content>(), rules));
    return mount_now<made_t, Facts>(built_arguments<Make, Rules>{std::move(make), rules}, first, spec);
  });
}

// A list whose maker makes builders: built when the list is, its maker one
// of built rows under the same rules.
template <class Element, class Make>
  requires a_builder<std::invoke_result_t<Make&, const Element&>>
auto listed(Look look, Make make) {
  return make_builder([look = std::move(look), make = std::move(make)]<class Rules>(const Rules& rules) mutable {
    return listed<Element>(std::move(look), built_maker<Make, Rules>{std::move(make), rules});
  });
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
  return make_builder([make = std::move(make), holder = std::move(holder), spec = std::move(spec)]<class Rules>(const Rules& rules) mutable {
    using maker = built_maker<Make, Rules>;
    return nodes::MemoRows<Key, View, std::invoke_result_t<maker&, const Item&>, maker, Holder>(maker{std::move(make), rules}, std::move(holder),
                                                                                               std::move(spec));
  });
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
  return make_builder([surface = std::move(surface), make = std::move(make), lanes = std::move(lanes), highlight]<class Rules>(const Rules& rules) mutable {
    return reorder_bars<Key, View>(built(std::move(surface), rules), built_maker<Make, Rules>{std::move(make), rules},
                                   lanes_under<Lanes, Rules>{std::move(lanes)}, highlight);
  });
}

}  // namespace skiff::compose
