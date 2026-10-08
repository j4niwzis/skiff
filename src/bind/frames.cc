// skiff.bind:frames -- frames around a node, where a part is found, and what
// is walked (the module is skiff.bind: see bind.cc).
export module skiff.bind:frames;

import std;
import splice;
import skiff.aggregate;
import skiff.model;
import skiff.scene;
import :kinds;

export namespace skiff::bind {
namespace detail {
// ---- frames: what is around a node, innermost first ----------------------

// A scope: its place in the model, and its handlers.
template <class Place, class Handlers> struct ScopeFrame {
  const Place *fPlace;
  const Handlers *fHandlers;
};
// A Local: its state and handlers, through the node.
template <class L> struct LocalFrame {
  L *fLocal;
};
// A pipe: the component that what is sent from within goes to first.
template <class To> struct PipeFrame {
  To *fTo;
};

template <class F> inline constexpr bool kLocalFrame = false;
template <class L> inline constexpr bool kLocalFrame<LocalFrame<L>> = true;

template <class Want, class F> inline constexpr bool kLocalHas = false;
template <class Want, class L>
inline constexpr bool kLocalHas<Want, LocalFrame<L>> =
    model::kFound<Want, typename L::LocalOf> == 1;
template <class F> inline constexpr bool kHiddenFrame = false;
template <class L>
inline constexpr bool kHiddenFrame<LocalFrame<L>> = L::kHidden;

// Where a part is found: in a Local's state, or in the model.
template <class Place> struct InModel {
  Place fPlace;
};
template <class L, class Place> struct InLocal {
  L *fLocal;
  Place fPlace;
};

template <class Want, class Here> auto where(const Here &here) {
  return InModel<decltype(model::borrowBelow<Want>(here))>{
      model::borrowBelow<Want>(here)};
}
template <class Want, class Here, class F, class... Rest>
  requires kLocalHas<Want, F>
auto where(const Here &, const F &frame, const Rest &...) {
  using L = std::remove_pointer_t<decltype(frame.fLocal)>;
  auto place = model::placeOf<Want, typename L::LocalOf>();
  return InLocal<L, decltype(place)>{frame.fLocal, place};
}
template <class Want, class Here, class F, class... Rest>
  requires(!kLocalHas<Want, F> && kHiddenFrame<F>)
auto where(const Here &here, const F &, const Rest &...) {
  static_assert(false, "skiff::bind: this part is not in the hidden state "
                       "around it, and nothing outside a hidden state is "
                       "seen from within it");
  return where<Want>(here);
}
template <class Want, class Here, class F, class... Rest>
auto where(const Here &here, const F &, const Rest &...rest) {
  return where<Want>(here, rest...);
}

// Whether a frame takes an event: a scope's or a Local's handlers with an
// on() for it, a pipe whose end accepts it.
template <class To, class E>
concept Accepts = requires(To &to, const E &e) { to.on(e); };

template <class F, class E> inline constexpr bool kTakes = false;
template <class P, class H, class E>
inline constexpr bool kTakes<ScopeFrame<P, H>, E> =
    requires(const H &h, const E &e, const P &p) { h.on(e, p); } ||
    requires(const H &h, const E &e) { h.on(e); };
template <class L, class E>
inline constexpr bool kTakes<LocalFrame<L>, E> =
    requires(const decltype(std::declval<L &>().fHandlers) &h, const E &e,
             const typename L::LocalOf &s) { h.on(e, s); } ||
    requires(const decltype(std::declval<L &>().fHandlers) &h, const E &e) {
      h.on(e);
    };
template <class To, class E>
inline constexpr bool kTakes<PipeFrame<To>, E> = Accepts<To, E>;

// ---- what is walked: types with nothing bound below are not ----------------

template <class T, class... Seen> struct Walks;
template <class T, class... Seen>
inline constexpr bool kWalks = Walks<T, Seen...>::value;

template <class Tuple, class... Seen> struct AnyWalks;
template <class... P, class... Seen>
struct AnyWalks<std::tuple<P...>, Seen...>
    : std::bool_constant<(kWalks<P, Seen...> || ...)> {};

// By rank, the first that applies: met already, walked where it was first
// met; a kind of its own, walked; said by the type itself (compose's nodes);
// a parts aggregate, walked where any part is; any other node, walked, as
// it names its children itself; anything else, not.
template <int N> struct Rank : Rank<N - 1> {};
template <> struct Rank<0> {};

template <class T, class... Seen>
  requires(std::same_as<T, Seen> || ...)
consteval bool walksOf(Rank<5>) {
  return false;
}
template <class T, class... Seen>
  requires(IsBound<T> || IsScoped<T> || IsEach<T> || IsLocal<T> ||
           EmitsEvents<T> || std::derived_from<T, Emitter> ||
           requires(T &t) { t.takeEvents(); })
consteval bool walksOf(Rank<4>) {
  return true;
}
template <class T, class... Seen>
  requires requires { typename T::Walked; }
consteval bool walksOf(Rank<3>) {
  return T::Walked::value;
}
template <class T, class... Seen>
  requires requires(T &t) { t.parts; }
consteval bool walksOf(Rank<2>) {
  return AnyWalks<aggregate::Members<decltype(std::declval<T &>().parts)>,
                  Seen..., T>::value;
}
template <class T, class... Seen>
  requires std::derived_from<T, scene::Node>
consteval bool walksOf(Rank<1>) {
  return true;
}
template <class T, class... Seen> consteval bool walksOf(Rank<0>) {
  return false;
}
template <class T, class... Seen>
struct Walks : std::bool_constant<walksOf<std::remove_cvref_t<T>, Seen...>(Rank<5>{})> {};

// Whether a Local is anywhere below a type: where a local change is looked
// for. Said by the type (compose's nodes), found in a parts aggregate, else
// -- a node naming its children itself -- taken to be, to be sure.
template <class T, class... Seen> struct HasLocal;
template <class T, class... Seen>
inline constexpr bool kHasLocal = HasLocal<T, Seen...>::value;
template <class Tuple, class... Seen> struct AnyHasLocal;
template <class... P, class... Seen>
struct AnyHasLocal<std::tuple<P...>, Seen...>
    : std::bool_constant<(kHasLocal<P, Seen...> || ...)> {};
template <class T, class... Seen>
  requires(std::same_as<T, Seen> || ...)
consteval bool hasLocalOf(Rank<5>) {
  return false;
}
template <class T, class... Seen>
  requires(IsLocal<T> || (IsEach<T> && kHasLocal<typename decltype(std::declval<T &>().fRows)::value_type, Seen..., T>))
consteval bool hasLocalOf(Rank<4>) {
  return true;
}
template <class T, class... Seen>
  requires requires { typename T::HasLocal; }
consteval bool hasLocalOf(Rank<3>) {
  return T::HasLocal::value;
}
template <class T, class... Seen>
  requires requires(T &t) { t.parts; }
consteval bool hasLocalOf(Rank<2>) {
  return AnyHasLocal<aggregate::Members<decltype(std::declval<T &>().parts)>, Seen...,
                     T>::value;
}
// A node holding its children in a tuple, as skiff's groups and flows do.
template <class T, class... Seen>
  requires requires(T &t) { std::tuple_size<std::remove_cvref_t<decltype(t.fChildren)>>::value; }
consteval bool hasLocalOf(Rank<1>) {
  return AnyHasLocal<std::remove_cvref_t<decltype(std::declval<T &>().fChildren)>, Seen..., T>::value;
}
// (A node naming its children some other way is taken to hold no Local: a
// program putting a Local inside one says so with `using HasLocal`.)
template <class T, class... Seen> consteval bool hasLocalOf(Rank<0>) {
  return false;
}
template <class T, class... Seen>
struct HasLocal : std::bool_constant<hasLocalOf<std::remove_cvref_t<T>, Seen...>(Rank<5>{})> {};

} // namespace detail
} // namespace skiff::bind
