// skiff.model:effects -- the outbox, and the effects a model's reactions can
// ask for (the module is skiff.model: see model.cc).
export module skiff.model:effects;

import std;
import skiff.aggregate;
import :places;

export namespace skiff::model {
// Where effects wait to be carried out, in the order they were asked for.
// One that replaces an older one takes its place: it says so
// (replaces(older)), or it says which one it is (key()) and an older one of
// its kind has the same.
template <class Effect> class Outbox {
public:
  constexpr void take(Nothing) {}
  template <class E> constexpr void take(std::optional<E> maybe) {
    if (maybe)
      take(std::move(*maybe));
  }
  template <class... E> constexpr void take(std::tuple<E...> all) {
    std::apply([&](auto &...each) { (take(std::move(each)), ...); }, all);
  }
  template <class E>
    requires std::constructible_from<Effect, E>
  constexpr void take(E effect) {
    constexpr std::size_t kind = detail::IndexIn<E, Effect>::value;
    for (const std::size_t at : fByKind[kind])
      if (replaces(effect, std::get<kind>(fItems[at]))) {
        fItems[at] = Effect(std::move(effect));
        return;
      }
    fByKind[kind].push_back(fItems.size());
    fItems.push_back(Effect(std::move(effect)));
  }

  // What is waiting, given to be carried out; the outbox left empty.
  constexpr std::vector<Effect> drain() {
    for (auto &each : fByKind)
      each.clear();
    return std::exchange(fItems, {});
  }
  constexpr std::size_t size() const { return fItems.size(); }

private:
  template <class E>
    requires requires(const E &e) {
      { e.replaces(e) } -> std::convertible_to<bool>;
    }
  static constexpr bool replaces(const E &effect, const E &older) {
    return effect.replaces(older);
  }
  template <class E>
    requires(requires(const E &e) { e.key() == e.key(); } &&
             !requires(const E &e) { e.replaces(e); })
  static constexpr bool replaces(const E &effect, const E &older) {
    return effect.key() == older.key();
  }
  template <class E> static constexpr bool replaces(const E &, const E &) {
    return false;
  }
  std::vector<Effect> fItems;
  std::array<std::vector<std::size_t>, std::variant_size_v<Effect>> fByKind{};
};

// ---- the effects a model's reactions can ask for, found ---------------------
//
// Model<Root, Reactions> without an effect type: the variant is made of what
// the reactions return, at every place in the model they can be told of --
// on(Changed<T>, at) and its short form, on(Removed<T>, last, key) -- an
// optional or a tuple of effects unwrapped, Nothing left out.
struct DeducedEffects {};

namespace detail {
template <int N> struct Rank : Rank<N - 1> {};
template <> struct Rank<0> {};

// Every place in a model reactions are told of: every part, but not the
// model's own wrappers (Tracked, a Keyed list itself, an External).
template <class P, class T> struct AllPlacesIn {
  using type = Types<P>;
};
template <class P, class T, class Members, class Indices> struct AllPlacesMembers;
template <class P, class T, class... M, std::size_t... I>
struct AllPlacesMembers<P, T, Types<M...>, std::index_sequence<I...>> {
  using type = typename Concat<
      Types<P>, typename AllPlacesIn<::skiff::model::Join<P, Path<Member<I>>>, M>::type...>::type;
};
template <class P, Decomposable T>
struct AllPlacesIn<P, T>
    : AllPlacesMembers<P, T, MemberTypes<T>, std::make_index_sequence<MemberTypes<T>::size>> {};
template <class P, class T> struct AllPlacesIn<P, Tracked<T>> {
  using type = typename AllPlacesIn<::skiff::model::Join<P, Path<Into>>, T>::type;
};
template <class P, class T> struct AllPlacesIn<P, External<T>> {
  using type = Types<>;
};
template <class P, class K, class T> struct AllPlacesIn<P, Keyed<K, T>> {
  using type = typename AllPlacesIn<::skiff::model::Join<P, Path<At<K>>>, T>::type;
};
template <class P, class T> struct AllPlacesIn<P, std::optional<T>> {
  using type = typename Concat<
      Types<P>, typename AllPlacesIn<::skiff::model::Join<P, Path<IfThere>>, T>::type>::type;
};
template <class P, class Alternatives, class Indices> struct AllPlacesAlts;
template <class P, class... Ts, std::size_t... I>
struct AllPlacesAlts<P, Types<Ts...>, std::index_sequence<I...>> {
  using type = typename Concat<
      Types<P>, typename AllPlacesIn<::skiff::model::Join<P, Path<Alt<I>>>, Ts>::type...>::type;
};
template <class P, template <class...> class V, class... Ts>
  requires VariantLike<V<Ts...>>
struct AllPlacesIn<P, V<Ts...>>
    : AllPlacesAlts<P, Types<Ts...>, std::index_sequence_for<Ts...>> {};

// The element of the innermost list on a path, or nothing (the part itself).
template <class In, class P, class Owner> struct OwnerAt {
  using type = Owner;
};
template <class In, std::size_t I, class... R, class O>
struct OwnerAt<In, Path<Member<I>, R...>, O>
    : OwnerAt<typename MemberTypes<In>::template at<I>, Path<R...>, O> {};
template <class T, class... R, class O>
struct OwnerAt<Tracked<T>, Path<Into, R...>, O> : OwnerAt<T, Path<R...>, O> {};
template <class K, class T, class... R, class O>
struct OwnerAt<Keyed<K, T>, Path<At<K>, R...>, O> : OwnerAt<T, Path<R...>, T> {};
template <class T, class... R, class O>
struct OwnerAt<std::optional<T>, Path<IfThere, R...>, O> : OwnerAt<T, Path<R...>, O> {};
template <template <class...> class V, class... Ts, std::size_t I, class... R, class O>
  requires VariantLike<V<Ts...>>
struct OwnerAt<V<Ts...>, Path<Alt<I>, R...>, O>
    : OwnerAt<std::tuple_element_t<I, std::tuple<Ts...>>, Path<R...>, O> {};

template <class R, class N, class O, class Keys>
concept TellsAt = requires(const R &r, const N &n, const O &o, const Keys &k) {
  r.on(Changed<N>{}, At_<N, O, Keys>{n, o, k});
};
template <class R, class N, class O>
concept TellsShort = requires(const R &r, const O &o) { r.on(Changed<N>{}, o); };

// (Partial specialisations, not overloads: a candidate overload's return
// type is formed before its constraints are looked at, and forming it would
// call a reaction that does not take what it is given.)
template <class R, class N, class O, class Keys> struct ReturnOf {
  using type = Nothing;
};
template <class R, class N, class O, class Keys>
  requires TellsAt<R, N, O, Keys>
struct ReturnOf<R, N, O, Keys> {
  using type = decltype(std::declval<const R &>().on(Changed<N>{},
                                                     std::declval<At_<N, O, Keys>>()));
};
template <class R, class N, class O, class Keys>
  requires(!TellsAt<R, N, O, Keys> && TellsShort<R, N, O>)
struct ReturnOf<R, N, O, Keys> {
  using type = decltype(std::declval<const R &>().on(Changed<N>{}, std::declval<const O &>()));
};

// What a reaction returns, as the effects in it.
template <class E> struct EffectsIn {
  using type = Types<E>;
};
template <> struct EffectsIn<Nothing> {
  using type = Types<>;
};
template <class E> struct EffectsIn<std::optional<E>> : EffectsIn<E> {};
template <class... E> struct EffectsIn<std::tuple<E...>> {
  using type = typename Concat<typename EffectsIn<E>::type...>::type;
};

template <class R, class Root, class P> struct EffectsAt {
  using N = ::skiff::model::TypeAt<Root, P>;
  using O = typename OwnerAt<Root, P, N>::type;
  using type = typename EffectsIn<std::remove_cvref_t<
      typename ReturnOf<R, N, O, BorrowedKeysOf<P>>::type>>::type;
};
// And what a list's elements going asks for.
template <class R, class T, class K> struct RemovedOf {
  using type = Nothing;
};
template <class R, class T, class K>
  requires requires(const R &r, const T &t, const K &k) { r.on(Removed<T>{}, t, k); }
struct RemovedOf<R, T, K> {
  using type = decltype(std::declval<const R &>().on(
      Removed<T>{}, std::declval<const T &>(), std::declval<const K &>()));
};
template <class P> struct LastStep {
  using type = void;
};
template <class S, class... Rest> struct LastStep<Path<S, Rest...>> {
  using type = std::tuple_element_t<sizeof...(Rest), std::tuple<S, Rest...>>;
};
template <class Step> struct KeyOfAt {
  using type = void;
};
template <class K> struct KeyOfAt<At<K>> {
  using type = K;
};
template <class R, class Root, class P,
          class K = typename KeyOfAt<typename LastStep<P>::type>::type>
struct RemovedAt {
  using T = ::skiff::model::TypeAt<Root, P>;
  using type = typename EffectsIn<std::remove_cvref_t<typename RemovedOf<R, T, K>::type>>::type;
};
template <class R, class Root, class P> struct RemovedAt<R, Root, P, void> {
  using type = Types<>;
};

// Each once, in the order first seen: a fold too, for the same reason.
template <class E> struct One {};
template <class... S, class E>
std::conditional_t<(std::same_as<E, S> || ...), Types<S...>, Types<S..., E>> operator*(Types<S...>, One<E>);
template <class L> struct Unique;
template <class... E> struct Unique<Types<E...>> {
  using type = decltype((Types<>{} * ... * One<E>{}));
};
template <class L> struct VariantOf;
template <> struct VariantOf<Types<>> {
  using type = std::variant<Nothing>;
};
template <class... E> struct VariantOf<Types<E...>> {
  using type = std::variant<E...>;
};

// And what a reaction to a member, as a field of its aggregate, returns.
template <class P> struct ParentOf;
template <class... S> struct ParentOf<Path<S...>> {
  using type = decltype([]<std::size_t... I>(std::index_sequence<I...>) {
    return Path<std::tuple_element_t<I, std::tuple<S...>>...>{};
  }(std::make_index_sequence<sizeof...(S) - 1>{}));
};
template <class R, class C, std::size_t I, class N, class O, class Keys>
concept FieldTellsAt = requires(const R &r, const N &n, const O &o, const Keys &k) {
  r.on(MemberChanged<C, I>{}, At_<N, O, Keys>{n, o, k});
};
template <class R, class C, std::size_t I, class O>
concept FieldTellsShort = requires(const R &r, const O &o) { r.on(MemberChanged<C, I>{}, o); };
template <class R, class C, std::size_t I, class N, class O, class Keys> struct FieldReturnOf {
  using type = Nothing;
};
template <class R, class C, std::size_t I, class N, class O, class Keys>
  requires FieldTellsAt<R, C, I, N, O, Keys>
struct FieldReturnOf<R, C, I, N, O, Keys> {
  using type = decltype(std::declval<const R &>().on(MemberChanged<C, I>{}, std::declval<At_<N, O, Keys>>()));
};
template <class R, class C, std::size_t I, class N, class O, class Keys>
  requires(!FieldTellsAt<R, C, I, N, O, Keys> && FieldTellsShort<R, C, I, O>)
struct FieldReturnOf<R, C, I, N, O, Keys> {
  using type = decltype(std::declval<const R &>().on(MemberChanged<C, I>{}, std::declval<const O &>()));
};
template <class R, class Root, class P, class Step = typename LastStep<P>::type> struct FieldEffectsAt {
  using type = Types<>;
};
template <class R, class Root, class P, std::size_t I> struct FieldEffectsAt<R, Root, P, Member<I>> {
  using C = ::skiff::model::TypeAt<Root, typename ParentOf<P>::type>;
  using N = ::skiff::model::TypeAt<Root, P>;
  using O = typename OwnerAt<Root, P, N>::type;
  using type = typename EffectsIn<std::remove_cvref_t<
      typename FieldReturnOf<R, C, I, N, O, BorrowedKeysOf<P>>::type>>::type;
};

template <class Root, class R, class Places> struct EffectsOfPlaces;
template <class Root, class R, class... P> struct EffectsOfPlaces<Root, R, Types<P...>> {
  using type = typename VariantOf<typename Unique<typename Concat<
      typename EffectsAt<R, Root, P>::type..., typename FieldEffectsAt<R, Root, P>::type...,
      typename RemovedAt<R, Root, P>::type...>::type>::type>::type;
};
template <class Root, class R, class E> struct EffectOf {
  using type = E;
};
template <class Root, class R> struct EffectOf<Root, R, DeducedEffects> {
  using type = typename EffectsOfPlaces<Root, R, typename AllPlacesIn<Path<>, Root>::type>::type;
};
} // namespace detail

// The effect type a Model<Root, Reactions> deduces.
template <class Root, class Reactions>
using EffectsOf = typename detail::EffectOf<Root, Reactions, DeducedEffects>::type;

} // namespace skiff::model
