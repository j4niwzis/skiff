// skiff.model:places -- places, the walk along them, edits and the changes
// there are (the module is skiff.model: see model.cc).
export module skiff.model:places;

import std;
import skiff.aggregate;
import :core;

export namespace skiff::model {
// ---- the tracked places of a model, all of them, as types ------------------
//
// Every place whose revision moves -- a Tracked<>'s value, a keyed list, its
// elements, an External's value -- listed while the model is compiled: what
// a batch logs its changes by (Changes<Root>), so that what shows a part can
// be found from what changed without walking anything.
namespace detail {
template <class P, class T> struct AllTrackedIn {
  using type = Types<>;
};
template <class P, class T, class Members, class Indices> struct AllTrackedMembers;
template <class P, class T, class... M, std::size_t... I>
struct AllTrackedMembers<P, T, Types<M...>, std::index_sequence<I...>> {
  using type = typename Concat<
      typename AllTrackedIn<::skiff::model::Join<P, Path<Member<I>>>, M>::type...>::type;
};
template <class P, Decomposable T>
struct AllTrackedIn<P, T>
    : AllTrackedMembers<P, T, MemberTypes<T>,
                        std::make_index_sequence<MemberTypes<T>::size>> {};
template <class P, class T> struct AllTrackedIn<P, Tracked<T>> {
  using Inside = ::skiff::model::Join<P, Path<Into>>;
  using type =
      typename Concat<Types<Inside>, typename AllTrackedIn<Inside, T>::type>::type;
};
template <class P, class T> struct AllTrackedIn<P, External<T>> {
  using type = Types<::skiff::model::Join<P, Path<Through>>>;
};
template <class P, class K, class T> struct AllTrackedIn<P, Keyed<K, T>> {
  using Element = ::skiff::model::Join<P, Path<At<K>>>;
  using type = typename Concat<Types<P, Element>,
                               typename AllTrackedIn<Element, T>::type>::type;
};
template <class P, class T> struct AllTrackedIn<P, std::optional<T>> {
  using type = typename AllTrackedIn<::skiff::model::Join<P, Path<IfThere>>, T>::type;
};
template <class P, class Alternatives, class Indices> struct AllTrackedAlts;
template <class P, class... Ts, std::size_t... I>
struct AllTrackedAlts<P, Types<Ts...>, std::index_sequence<I...>> {
  using type = typename Concat<
      typename AllTrackedIn<::skiff::model::Join<P, Path<Alt<I>>>, Ts>::type...>::type;
};
template <class P, template <class...> class V, class... Ts>
  requires VariantLike<V<Ts...>>
struct AllTrackedIn<P, V<Ts...>>
    : AllTrackedAlts<P, Types<Ts...>, std::index_sequence_for<Ts...>> {};

template <class L> struct ChangesOfList;
template <class... P> struct ChangesOfList<Types<P...>> {
  using type = std::tuple<std::vector<KeysOf<P>>...>;
};
template <class P, class L> struct IndexOfPath;
template <class P, class... Ps> struct IndexOfPath<P, Types<Ps...>> {
  static constexpr std::size_t value = [] {
    constexpr std::array same{std::same_as<P, Ps>...};
    return static_cast<std::size_t>(std::ranges::find(same, true) - same.begin());
  }();
};
} // namespace detail

// Every tracked place in a Root, in order.
template <class Root>
using AllTracked = typename detail::AllTrackedIn<Path<>, Root>::type;
// What moved, by tracked place: the keys of each one that did.
// The first, Path<>, is the whole model: logged where an edit went through
// no tracked part at all -- what shows such a part is then refreshed whole.
template <class Root>
using ChangedPlaces =
    typename detail::Concat<Types<Path<>>, AllTracked<Root>>::type;
template <class Root>
using Changes = typename detail::ChangesOfList<ChangedPlaces<Root>>::type;
// Where a tracked place's changes are in a Changes<Root>.
template <class Root, class P>
inline constexpr std::size_t kTrackedIndex =
    detail::IndexOfPath<P, ChangedPlaces<Root>>::value;

namespace detail {
struct Copyable {};
// A place that borrows its keys is not copied: a copy kept past the walk it
// was made in would refer to keys that may be gone. Moved, or borrowed again.
struct MoveOnly {
  constexpr MoveOnly() = default;
  MoveOnly(const MoveOnly &) = delete;
  constexpr MoveOnly(MoveOnly &&) = default;
  MoveOnly &operator=(const MoveOnly &) = delete;
  constexpr MoveOnly &operator=(MoveOnly &&) = default;
};
} // namespace detail

// A place in a model of type Root: the path, a type, and its keys -- held
// (the default, what an edit kept for later holds) or borrowed (what a walk
// carries down, copying nothing, and not copyable itself).
template <class Root, class P, class Keys = KeysOf<P>> struct Place {
  using RootType = Root;
  using PathType = P;
  using Target = TypeAt<Root, P>;
  Keys fKeys{};
  [[no_unique_address]] std::conditional_t<std::same_as<Keys, KeysOf<P>>,
                                           detail::Copyable, detail::MoveOnly>
      fGuard{};

  // A key by its place along the path, or by its type where only one is.
  template <std::size_t I> constexpr const auto &key() const {
    return std::get<I>(fKeys);
  }
  template <class K>
    requires(!std::integral<K>)
  constexpr const K &key() const {
    return std::get<const K &>(borrowed().fKeys);
  }
  // The same place, borrowing these keys.
  constexpr auto borrowed() const {
    return Place<Root, P, BorrowedKeysOf<P>>{
        std::apply([](const auto &...k) { return BorrowedKeysOf<P>(k...); }, fKeys)};
  }
  // The same place, holding its keys.
  constexpr auto held() const {
    return Place<Root, P>{
        std::apply([](const auto &...k) { return KeysOf<P>(k...); }, fKeys)};
  }
  friend constexpr bool operator==(const Place &a, const Place &b) {
    return a.fKeys == b.fKeys;
  }
};

// The place of the one Want in a whole model, with the keys its path needs.
template <class Want, class Root, class... Keys>
constexpr auto placeOf(Keys... keys) {
  using P = PathTo<Want, Root>;
  static_assert(std::same_as<KeysOf<P>, std::tuple<Keys...>>,
                "skiff::model: the keys given are not the ones this place "
                "goes through, in order");
  return Place<Root, P>{{std::move(keys)...}};
}

// The place of the one Want below another place, with the keys the rest of
// the way needs: held.
template <class Want, class Root, class P, class K, class... Keys>
constexpr auto placeBelow(const Place<Root, P, K> &above, Keys... keys) {
  using Rest = PathTo<Want, TypeAt<Root, P>>;
  static_assert(std::same_as<KeysOf<Rest>, std::tuple<Keys...>>,
                "skiff::model: the keys given are not the ones the way below "
                "this place goes through, in order");
  return Place<Root, Join<P, Rest>>{
      std::tuple_cat(above.held().fKeys, std::tuple<Keys...>{std::move(keys)...})};
}
// The same, borrowing its keys from above and from those given -- for a walk,
// which copies nothing.
template <class Want, class Root, class P, class K, class... Keys>
constexpr auto borrowBelow(const Place<Root, P, K> &above, const Keys &...keys) {
  using Rest = PathTo<Want, TypeAt<Root, P>>;
  static_assert(std::same_as<KeysOf<Rest>, std::tuple<Keys...>>,
                "skiff::model: the keys given are not the ones the way below "
                "this place goes through, in order");
  using Whole = Join<P, Rest>;
  return Place<Root, Whole, BorrowedKeysOf<Whole>>{std::tuple_cat(
      above.borrowed().fKeys, std::tuple<const Keys &...>{keys...})};
}

namespace detail {
// Along a path from a part, the keys taken from index K on. At the end the
// visitor is given the part there (at), and on the way back up every part it
// went through (left), with the element of the innermost list at or above
// it -- its owner -- where there is one, and how many of the keys lead to
// it. False: a key was not in its list, or an external part pointed at
// nothing.
struct NoOwner {};

template <std::size_t K, class Keys, class V, class Owner, class N>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner, Path<>);
template <std::size_t K, class Keys, class V, class Owner, class N,
          std::size_t I, class... R>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Member<I>, R...>);
template <std::size_t K, class Keys, class V, class Owner, class N,
          class... R>
  requires kTracked<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Into, R...>);
template <std::size_t K, class Keys, class V, class Owner, class N,
          class... R>
  requires kExternal<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Through, R...>);
template <std::size_t K, class Keys, class V, class Owner, class N, class Key,
          class... R>
  requires kKeyed<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<At<Key>, R...>);
template <std::size_t K, class Keys, class V, class Owner, class N,
          class... R>
  requires kOptional<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<IfThere, R...>);
template <std::size_t K, class Keys, class V, class Owner, class N,
          std::size_t I, class... R>
  requires VariantLike<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Alt<I>, R...>);

template <std::size_t K, class Keys, class V, class Owner, class N>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner, Path<>) {
  v.at(node, owner, keys);
  v.template left<K>(node, owner, keys);
  return true;
}
template <std::size_t K, class Keys, class V, class Owner, class N,
          std::size_t I, class... R>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Member<I>, R...>) {
  if (!walk<K>(aggregate::get<I>(node), keys, v, owner, Path<R...>{}))
    return false;
  v.template left<K>(node, owner, keys);
  return true;
}
template <std::size_t K, class Keys, class V, class Owner, class N,
          class... R>
  requires kTracked<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Into, R...>) {
  if (!walk<K>(node.fValue, keys, v, owner, Path<R...>{}))
    return false;
  v.template left<K>(node, owner, keys);
  return true;
}
template <std::size_t K, class Keys, class V, class Owner, class N,
          class... R>
  requires kExternal<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Through, R...>) {
  if (!node.fValue)
    return false;
  if (!walk<K>(node.fValue.get(), keys, v, owner, Path<R...>{}))
    return false;
  v.template left<K>(node, owner, keys);
  return true;
}
template <std::size_t K, class Keys, class V, class Owner, class N, class Key,
          class... R>
  requires kKeyed<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<At<Key>, R...>) {
  auto *item = node.findItem(std::get<K>(keys));
  if (item == nullptr)
    return false;
  auto &element = item->fValue;
  if (!walk<K + 1>(element, keys, v, element, Path<R...>{}))
    return false;
  v.template left<K + 1>(*item, owner, keys);
  v.template left<K>(node, owner, keys);
  return true;
}

template <std::size_t K, class Keys, class V, class Owner, class N,
          class... R>
  requires kOptional<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<IfThere, R...>) {
  if (!node)
    return false;
  if (!walk<K>(*node, keys, v, owner, Path<R...>{}))
    return false;
  v.template left<K>(node, owner, keys);
  return true;
}
template <std::size_t K, class Keys, class V, class Owner, class N,
          std::size_t I, class... R>
  requires VariantLike<std::remove_const_t<N>>
constexpr bool walk(N &node, const Keys &keys, V &v, Owner &owner,
                    Path<Alt<I>, R...>) {
  if (node.index() != I)
    return false;
  using std::get;
  if (!walk<K>(get<I>(node), keys, v, owner, Path<R...>{}))
    return false;
  v.template left<K>(node, owner, keys);
  return true;
}

// Whether a change gives a whole new value (rather than changing in place).
template <class F, class T>
inline constexpr bool kReplaces = requires(const F &f, T &&t) {
  { f(std::move(t)) } -> std::same_as<T>;
};

// The tracked parts of a value given a revision: every one, or those at 0.
// Settling -- a model taking a root put together before it -- also forgets
// what its lists say changed: nothing has, for the model.
template <class T> constexpr void stamp(T &, Revision, bool, bool = false) {}
template <class T> constexpr void stamp(Tracked<T> &, Revision, bool, bool = false);
template <class K, class T> constexpr void stamp(Keyed<K, T> &, Revision, bool, bool = false);
template <class T> constexpr void stamp(std::optional<T> &, Revision, bool, bool = false);
template <VariantLike V> constexpr void stamp(V &, Revision, bool, bool = false);
template <class... Ts> constexpr void stamp(std::variant<Ts...> &, Revision, bool, bool = false);
template <Decomposable T> constexpr void stamp(T &, Revision, bool, bool = false);
template <class T> constexpr void stamp(Tracked<T> &part, Revision now, bool all, bool settle) {
  if (all || part.fRevision == 0)
    part.fRevision = now;
  stamp(part.fValue, now, all, settle);
}
// A list: every element where it was made whole; else only those put since
// (fFresh) -- a put stamps what it made, not the whole list.
template <class K, class T> constexpr void stamp(Keyed<K, T> &list, Revision now, bool all, bool settle) {
  if (all || list.fRevision == 0)
    list.fRevision = now;
  if (settle)
    list.fReshaped = false;
  if (all) {
    list.fFresh.clear();
    for (auto &[key, element] : list.fItems)
      stamp(element.mut(), now, all, settle);
    return;
  }
  for (const K &key : std::exchange(list.fFresh, {}))
    if (Tracked<T> *element = list.findItem(key))
      stamp(*element, now, all, settle);
}
template <class T> constexpr void stamp(std::optional<T> &part, Revision now, bool all, bool settle) {
  if (part)
    stamp(*part, now, all, settle);
}
template <VariantLike V> constexpr void stamp(V &part, Revision now, bool all, bool settle) {
  part.visit([&](auto &alternative) { stamp(alternative, now, all, settle); });
}
template <class... Ts> constexpr void stamp(std::variant<Ts...> &part, Revision now, bool all, bool settle) {
  std::visit([&](auto &alternative) { stamp(alternative, now, all, settle); }, part);
}
template <Decomposable T> constexpr void stamp(T &part, Revision now, bool all, bool settle) {
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    (stamp(aggregate::get<I>(part), now, all, settle), ...);
  }(std::make_index_sequence<MemberTypes<T>::size>{});
}

template <class N> constexpr const N &ownerOf(const N &node, const NoOwner &) {
  return node;
}
template <class N, class O>
constexpr const O &ownerOf(const N &, const O &owner) {
  return owner;
}

// The first K of a place's keys.
template <std::size_t K, class Keys> constexpr auto firstKeys(const Keys &keys) {
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple<const std::remove_cvref_t<std::tuple_element_t<I, Keys>> &...>(
        std::get<I>(keys)...);
  }(std::make_index_sequence<K>{});
}
} // namespace detail

// A part as it is now, and the revision it was seen at: the innermost
// tracked part's at or above it, else the whole model's -- and whether it
// was one of its own (a part with none is read again at every edit).
template <class T> struct Seen {
  const T *fValue = nullptr;
  Revision fRevision = 0;
  bool fTracked = false;
  constexpr explicit operator bool() const { return fValue != nullptr; }
  constexpr const T &operator*() const { return *fValue; }
  constexpr const T *operator->() const { return fValue; }
};

// That a part of type T changed, or an element of a list of T's went: what a
// reaction is chosen by.
template <class T> struct Changed {};
template <class T> struct Removed {};
// That the member I of a C changed: what a reaction to a field,
// Changed<Field<&C::m>>, is chosen by -- it takes this one only where m is
// that member.
template <class C, std::size_t I> struct MemberChanged {};
template <auto M> struct Changed<Field<M>> {
  constexpr Changed() = default;
  template <class C, std::size_t I>
    requires(std::same_as<C, typename detail::MemberPointer<decltype(M)>::Class> &&
             I == detail::kIndexOfMember<M>)
  constexpr Changed(MemberChanged<C, I>) {}
};

// Where a reaction's part is: the part, the element of the innermost list
// it is in (or itself), and the keys that lead to it -- by place, or by type
// where only one is of it.
template <class Part, class Owner, class Keys> struct At_ {
  const Part &fPart;
  const Owner &fOwner;
  Keys fKeys;
  constexpr const Part &part() const { return fPart; }
  constexpr const Owner &owner() const { return fOwner; }
  template <std::size_t I> constexpr const auto &key() const {
    return std::get<I>(fKeys);
  }
  template <class K>
    requires(!std::integral<K>)
  constexpr const K &key() const {
    return std::get<const K &>(fKeys);
  }
};

// A key of a place, or of where a reaction's part is, without `template`:
// key<0>(at), key<AccountId>(here); and a reaction's part, part(at).
template <std::size_t I, class A>
  requires requires(const A &a) { a.template key<I>(); }
constexpr const auto &key(const A &at) {
  return at.template key<I>();
}
template <class K, class A>
  requires(!std::integral<K> && requires(const A &a) { a.template key<K>(); })
constexpr const K &key(const A &at) {
  return at.template key<K>();
}
template <class A> constexpr const auto &part(const A &at) { return at.part(); }

// A change of a part, in its place: a function from its old value to its
// new, or a change made in place (a list's element put or taken away).
template <class Root, class P, class F, class Keys = KeysOf<P>> struct Edit {
  Place<Root, P, Keys> fPlace;
  F fChange;
};
// The same, its types read off the place and the change: edit(place, flip).
template <class Root, class P, class K, class F>
constexpr Edit<Root, P, F, K> edit(Place<Root, P, K> place, F change) {
  return {std::move(place), std::move(change)};
}

// A change of the one Want below where it is applied -- a scope, or the
// whole model -- with the keys the way there needs: what handlers answer.
template <class Want, class F, class... Keys> struct Over {
  F fChange;
  std::tuple<Keys...> fKeys;
};
template <class Want, class F, class... Keys>
constexpr Over<Want, F, Keys...> over(F change, Keys... keys) {
  return {std::move(change), {std::move(keys)...}};
}

// An event passed on, up, from the scope that took it.
template <class E> struct Up {
  E fEvent;
};
// Nothing to do.
struct Nothing {};
// Handlers that take these events and do nothing with them: where a scope
// means them to stop there.
template <class... E> struct Ignore {
  template <class X>
    requires(std::same_as<X, E> || ...)
  constexpr Nothing on(const X &) const {
    return {};
  }
};

// The changes there are for every part, as values.
//
// Flip: a bool, or an aggregate of one bool (a setting's own type) turned.
struct Flip {
  constexpr bool operator()(bool on) const { return !on; }
  template <Decomposable T>
    requires(MemberTypes<T>::size == 1 &&
             std::same_as<typename MemberTypes<T>::template at<0>, bool>)
  constexpr T operator()(T value) const {
    auto &on = aggregate::get<0>(value);
    on = !on;
    return value;
  }
};
inline constexpr Flip flip{};

// SetTo: the part made the value given.
template <class T> struct SetTo {
  T fValue;
  constexpr T operator()(const T &) const { return fValue; }
};
template <class T> constexpr SetTo<T> setTo(T value) {
  return {std::move(value)};
}

// Put, Take, MoveTo: an element of a list put there (in its place where its
// key is already), taken away, moved in the order.
template <class K, class T> struct Put {
  K fKey;
  T fValue;
  std::size_t fPosition = std::numeric_limits<std::size_t>::max();
  constexpr void operator()(Keyed<K, T> &list) const {
    list.put(fKey, fValue, fPosition);
  }
};
template <class K, class T> struct Take {
  K fKey;
  constexpr void operator()(Keyed<K, T> &list) const { list.take(fKey); }
};
template <class K, class T> struct MoveTo {
  K fKey;
  std::size_t fPosition;
  constexpr void operator()(Keyed<K, T> &list) const { list.move(fKey, fPosition); }
};
// An element put into the one list of T's, taken from it, moved in it.
template <class T, class K> constexpr auto put(K key, T value) {
  return over<Keyed<K, T>>(Put<K, T>{std::move(key), std::move(value)});
}
template <class T, class K> constexpr auto take(K key) {
  return over<Keyed<K, T>>(Take<K, T>{std::move(key)});
}
template <class T, class K> constexpr auto moveTo(K key, std::size_t position) {
  return over<Keyed<K, T>>(MoveTo<K, T>{std::move(key), position});
}

namespace detail {
// A change made: assigned where it gives a new value, else made in place.
template <class F, class T>
  requires std::same_as<std::invoke_result_t<const F &, T &&>, T>
constexpr void change(const F &f, T &part) {
  part = f(std::move(part));
}
template <class F, class T>
  requires std::is_void_v<std::invoke_result_t<const F &, T &>>
constexpr void change(const F &f, T &part) {
  f(part);
}

template <class E, class V> struct IndexIn;
template <class E, class... Ts> struct IndexIn<E, std::variant<Ts...>> {
  static constexpr std::size_t value = [] {
    constexpr std::array same{std::same_as<E, Ts>...};
    return static_cast<std::size_t>(std::ranges::find(same, true) - same.begin());
  }();
};
} // namespace detail

} // namespace skiff::model
