// skiff.model:core -- the parts of a model: shared values, tracked parts,
// lists, the steps of a path and the search for them (the module is
// skiff.model: see model.cc).
export module skiff.model:core;

import std;
import skiff.aggregate;

export namespace skiff::model {

using Revision = std::uint64_t;

// A value held by several, copied only when one of them changes it
// (copy-on-write): what lets a snapshot of the model share everything with
// it but what changed since. Counted plainly while compiling, atomically at
// run time -- a snapshot may be read on another thread.
template <class T> class Shared {
  struct Node {
    T fValue;
    std::size_t fCount = 1;
  };
  Node *fNode = nullptr;

  constexpr void hold() const {
    if (fNode == nullptr)
      return;
    if consteval {
      ++fNode->fCount;
    } else {
      std::atomic_ref<std::size_t>(fNode->fCount).fetch_add(1, std::memory_order_relaxed);
    }
  }
  constexpr void drop() {
    if (fNode == nullptr)
      return;
    bool last = false;
    if consteval {
      last = --fNode->fCount == 0;
    } else {
      last = std::atomic_ref<std::size_t>(fNode->fCount).fetch_sub(1, std::memory_order_acq_rel) == 1;
    }
    if (last)
      delete fNode;
    fNode = nullptr;
  }
  constexpr bool alone() const {
    if consteval {
      return fNode->fCount == 1;
    } else {
      return std::atomic_ref<std::size_t>(fNode->fCount).load(std::memory_order_acquire) == 1;
    }
  }

public:
  constexpr Shared() = default;
  constexpr explicit Shared(T value) : fNode(new Node{std::move(value)}) {}
  constexpr Shared(const Shared &other) : fNode(other.fNode) { hold(); }
  constexpr Shared(Shared &&other) noexcept : fNode(std::exchange(other.fNode, nullptr)) {}
  constexpr Shared &operator=(Shared other) noexcept {
    std::swap(fNode, other.fNode);
    return *this;
  }
  constexpr ~Shared() { drop(); }

  constexpr explicit operator bool() const { return fNode != nullptr; }
  constexpr const T &get() const { return fNode->fValue; }
  constexpr const T *operator->() const { return &fNode->fValue; }
  // Its own to change: copied first where another holds it too.
  constexpr T &mut() {
    if (!alone()) {
      Node *mine = new Node{fNode->fValue};
      drop();
      fNode = mine;
    }
    return fNode->fValue;
  }
};
template <class T> constexpr Shared<T> share(T value) {
  return Shared<T>(std::move(value));
}

// ---- less to write ----------------------------------------------------------
//
// A setting of its own type, in one line: Named<"read receipts", bool> is a
// type of its own -- found by it, flipped as a bool -- with no struct to
// write. Two names, two types.
template <std::size_t N> struct Name {
  char fText[N]{};
  constexpr Name(const char (&text)[N]) { std::ranges::copy(text, fText); }
  friend constexpr bool operator==(const Name &, const Name &) = default;
};
// Read and written as its value by knot (json_transparent: nothing of knot
// is needed to say it), so a setting given its type keeps its JSON.
template <Name N, class T> struct Named {
  T value{};
  using json_transparent = void;
  friend constexpr bool operator==(const Named &, const Named &) = default;
};

// A part whose changes are counted: its revision is the model's clock at the
// last edit at or below it.
template <class T> struct Tracked {
  T fValue{};
  Revision fRevision = 0;
};

// A part kept elsewhere, seen through: a value shared with where it is kept,
// never changed in place -- a new one is published (publish(share(v))) --
// so it can neither go away under the model nor change unseen; and the
// revision the model was at when it was published.
template <class T> struct External {
  Shared<T> fValue{};
  Revision fRevision = 0;
};

// A list of parts, each found by its key, in an order of its own. Each
// element tracked, and kept where it was put: a pointer to one stays good
// while it is in the list, whatever else comes or goes -- unless a snapshot
// shares it, when the first change copies it. Found by a sorted index, not
// by looking through the list. A copy shares its elements.
template <class Key, class T> struct Keyed {
  using Item = std::pair<Key, Shared<Tracked<T>>>;
  std::vector<Item> fItems;                         // in the list's order
  std::vector<std::pair<Key, std::size_t>> fIndex;  // by key: where in fItems
  // What was taken away since the walk that changed it last looked: its
  // reactions are told (Removed<T>), however it was taken.
  std::vector<Item> fTaken;
  // Elements put since the last edit's walk stamped them: those it stamps,
  // not the whole list.
  std::vector<Key> fFresh;
  // Which elements it has, or their order, changed since the batch that
  // changed it last told of it: what a targeted refresh hands to the rows
  // showing the list, and nothing else.
  mutable bool fReshaped = false;
  Revision fRevision = 0;

  constexpr const Tracked<T> *findItem(const Key &key) const {
    const auto at = slotOf(key);
    return at ? &fItems[*at].second.get() : nullptr;
  }
  constexpr Tracked<T> *findItem(const Key &key) {
    const auto at = slotOf(key);
    return at ? &fItems[*at].second.mut() : nullptr;
  }
  constexpr const T *find(const Key &key) const {
    const auto *item = findItem(key);
    return item == nullptr ? nullptr : &item->fValue;
  }
  constexpr T *find(const Key &key) {
    auto *item = findItem(key);
    return item == nullptr ? nullptr : &item->fValue;
  }
  constexpr std::size_t size() const { return fItems.size(); }
  constexpr bool contains(const Key &key) const { return slotOf(key).has_value(); }
  // The element of a key there is: one that is not is a mistake of the caller's.
  constexpr const T &at(const Key &key) const { return *find(key); }
  constexpr bool empty() const { return fItems.empty(); }
  constexpr const Key &keyAt(std::size_t i) const { return fItems[i].first; }
  constexpr const T &valueAt(std::size_t i) const { return fItems[i].second->fValue; }
  constexpr const Tracked<T> &elementAt(std::size_t i) const { return fItems[i].second.get(); }
  // Its elements' values, and their keys, in the list's order: seen, not
  // copied.
  constexpr auto values() const {
    return fItems | std::views::transform([](const Item &item) -> const T & { return item.second->fValue; });
  }
  constexpr auto keys() const {
    return fItems | std::views::transform([](const Item &item) -> const Key & { return item.first; });
  }
  // Walked as (key, value) pairs, in the list's order: for (auto &[k, v] : list).
  class Entry {
  public:
    using value_type = std::pair<Key, T>;
    using reference = std::pair<const Key &, const T &>;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::forward_iterator_tag;
    constexpr Entry() = default;
    constexpr explicit Entry(const Item *at) : fAt(at) {}
    constexpr reference operator*() const { return {fAt->first, fAt->second->fValue}; }
    constexpr Entry &operator++() {
      ++fAt;
      return *this;
    }
    constexpr Entry operator++(int) {
      Entry was = *this;
      ++fAt;
      return was;
    }
    friend constexpr bool operator==(const Entry &, const Entry &) = default;

  private:
    const Item *fAt = nullptr;
  };
  constexpr Entry begin() const { return Entry(fItems.data()); }
  constexpr Entry end() const { return Entry(fItems.data() + fItems.size()); }

  // An element put there: where its key is already, in its place; else at
  // the end, or at `position`.
  constexpr void put(Key key, T value,
                     std::size_t position = std::numeric_limits<std::size_t>::max()) {
    if (auto *item = findItem(key)) {
      item->fValue = std::move(value);
      fFresh.push_back(key);
      return;
    }
    position = std::min(position, fItems.size());
    fItems.emplace(fItems.begin() + static_cast<std::ptrdiff_t>(position),
                   key, Shared<Tracked<T>>(Tracked<T>{std::move(value)}));
    fFresh.push_back(key);
    fReshaped = true;
    // The index told, not made again: the places after it moved by one, and
    // the new one goes where its key sorts.
    for (auto &entry : fIndex)
      if (entry.second >= position)
        ++entry.second;
    fIndex.insert(std::ranges::lower_bound(fIndex, key, std::ranges::less{},
                                           &std::pair<Key, std::size_t>::first),
                  {std::move(key), position});
  }
  // Many at once, at the end, in their order: the index made once.
  template <std::ranges::input_range R> constexpr void putAll(R &&elements) {
    // Include keys inserted earlier in this range in subsequent lookups.
    // The list's sorted index is rebuilt only after all the puts.
    auto positions = std::ranges::to<std::map<Key, std::size_t>>(fIndex);
    for (auto &&[key, value] : elements) {
      const auto [at, added] = positions.try_emplace(key, fItems.size());
      if (!added)
        fItems[at->second].second.mut().fValue = std::forward<decltype(value)>(value);
      else {
        fItems.emplace_back(key, Shared<Tracked<T>>(
                                     Tracked<T>{std::forward<decltype(value)>(value)}));
        fReshaped = true;
      }
      fFresh.push_back(key);
    }
    reindex();
  }
  // An element taken away; whether it was there.
  constexpr bool take(const Key &key) {
    const auto found = std::ranges::lower_bound(
        fIndex, key, std::ranges::less{}, &std::pair<Key, std::size_t>::first);
    if (found == fIndex.end() || found->first != key)
      return false;
    const std::size_t position = found->second;
    fIndex.erase(found);
    for (auto &entry : fIndex)
      if (entry.second > position)
        --entry.second;
    fTaken.push_back(std::move(fItems[position]));
    fReshaped = true;
    fItems.erase(fItems.begin() + static_cast<std::ptrdiff_t>(position));
    return true;
  }
  // An element moved to another place in the order.
  constexpr bool move(const Key &key, std::size_t position) {
    const auto at = std::ranges::find(fItems, key, &Item::first);
    if (at == fItems.end())
      return false;
    Item item = std::move(*at);
    fItems.erase(at);
    position = std::min(position, fItems.size());
    fItems.insert(fItems.begin() + static_cast<std::ptrdiff_t>(position), std::move(item));
    reindex();
    fReshaped = true;
    return true;
  }
  // The order made by a projection of the elements.
  // Stable: equal ones keep their order (std::ranges::sort over the
  // places, ties broken by place -- stable_sort is constexpr only from C++26).
  template <class By> constexpr void sortBy(By by) {
    auto order = std::views::iota(std::size_t{0}, fItems.size()) |
                 std::ranges::to<std::vector>();
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
      const auto &x = by(fItems[a].second->fValue);
      const auto &y = by(fItems[b].second->fValue);
      // (Read through the shared value: sorting copies nothing.)
      return x < y || (!(y < x) && a < b);
    });
    std::vector<Item> sorted;
    sorted.reserve(fItems.size());
    for (const std::size_t at : order)
      sorted.push_back(std::move(fItems[at]));
    fItems = std::move(sorted);
    reindex();
    fReshaped = true;
  }

private:
  constexpr std::optional<std::size_t> slotOf(const Key &key) const {
    const auto at = std::ranges::lower_bound(
        fIndex, key, std::ranges::less{}, &std::pair<Key, std::size_t>::first);
    if (at == fIndex.end() || at->first != key)
      return std::nullopt;
    return at->second;
  }
  constexpr void reindex() {
    fIndex = std::views::iota(std::size_t{0}, fItems.size()) |
             std::views::transform([&](std::size_t i) {
               return std::pair<Key, std::size_t>{fItems[i].first, i};
             }) |
             std::ranges::to<std::vector>();
    std::ranges::sort(fIndex, std::ranges::less{},
                      &std::pair<Key, std::size_t>::first);
  }
};

// The kinds of a step along a place: into an aggregate's member, by its
// index; into a tracked part's value; into an external part, through what it
// points at; into a keyed list's element, by a key.
template <std::size_t I> struct Member {};
struct Into {};
struct Through {};
template <class Key> struct At {};
// Into an optional part's value, where it has one; into a variant's
// alternative, while it holds that one (std::variant or spl::variant alike:
// anything with index() and valueless_by_exception(), reached by get<I>).
struct IfThere {};
template <std::size_t I> struct Alt {};
template <class... Steps> struct Path {};

// A field of an aggregate, named by its member pointer -- Field<&Chat::muted>
// -- where its type alone does not say which it is: placeOf<Field<...>>,
// look<Field<...>>, a reaction to Changed<Field<...>>. The aggregate is found
// by its type, as any part; the member by its place in it.
template <auto M> struct Field {};

template <class... T> struct Types {
  static constexpr std::size_t size = sizeof...(T);
  template <std::size_t I> using at = std::tuple_element_t<I, std::tuple<T...>>;
};

namespace detail {
template <class T> inline constexpr bool kTracked = false;
template <class T> inline constexpr bool kTracked<Tracked<T>> = true;
template <class T> inline constexpr bool kExternal = false;
template <class T> inline constexpr bool kExternal<External<T>> = true;
template <class T> inline constexpr bool kKeyed = false;
template <class K, class T> inline constexpr bool kKeyed<Keyed<K, T>> = true;
template <class T> inline constexpr bool kOptional = false;
template <class T> inline constexpr bool kOptional<std::optional<T>> = true;
} // namespace detail

template <class V>
concept VariantLike = requires(const V &v) {
  v.index();
  v.valueless_by_exception();
};

// A type the search does not go into: one that says so (using ModelOpaque),
// or one a program says so of (a specialisation of kOpaque) -- a library's
// own aggregates, a big value seen whole.
template <class T>
inline constexpr bool kOpaque = requires { typename T::ModelOpaque; };

// What the search goes through: an aggregate with members, not one of the
// model's own wrappers (each of which has a step of its own), not opaque.
template <class T>
concept Decomposable = std::is_class_v<T> && std::is_aggregate_v<T> &&
                       !std::is_empty_v<T> && !detail::kTracked<T> &&
                       !detail::kExternal<T> && !detail::kKeyed<T> &&
                       !kOpaque<T>;

namespace detail {
template <class Tuple> struct TypesOf;
template <class... T> struct TypesOf<std::tuple<T...>> {
  using type = Types<T...>;
};
} // namespace detail

// An aggregate's member types, in order.
template <Decomposable T>
using MemberTypes = typename detail::TypesOf<aggregate::Members<T>>::type;

namespace detail {
template <class Step, class P> struct Prefix1;
template <class Step, class... S> struct Prefix1<Step, Path<S...>> {
  using type = Path<Step, S...>;
};
template <class Step, class L> struct Prefix;
template <class Step, class... P> struct Prefix<Step, Types<P...>> {
  using type = Types<typename Prefix1<Step, P>::type...>;
};
template <class Step, class P> struct Suffix1;
template <class Step, class... S> struct Suffix1<Step, Path<S...>> {
  using type = Path<S..., Step>;
};
template <class Step, class L> struct Suffix;
template <class Step, class... P> struct Suffix<Step, Types<P...>> {
  using type = Types<typename Suffix1<Step, P>::type...>;
};
// Lists joined end to end. A fold over a declared operator, not a template
// recursing once per list: a model with thousands of places joins as many
// lists, past any depth of instantiation.
template <class... A, class... B> Types<A..., B...> operator+(Types<A...>, Types<B...>);
template <class... L> struct Concat {
  using type = decltype((Types<>{} + ... + L{}));
};

template <class Want, class In> struct Search;
template <class Want, class In, class Members, class Indices>
struct SearchMembers;
template <class Want, class In, class... M, std::size_t... I>
struct SearchMembers<Want, In, Types<M...>, std::index_sequence<I...>> {
  using type = typename Concat<
      typename Prefix<Member<I>, typename Search<Want, M>::type>::type...>::type;
};
// Below a part that is not the one wanted: through an aggregate's members,
// a tracked part's value, an external part, a keyed list's elements -- and
// nowhere else.
template <class Want, class In> struct SearchBelow {
  using type = Types<>;
};
template <class Want, Decomposable In>
struct SearchBelow<Want, In>
    : SearchMembers<Want, In, MemberTypes<In>,
                    std::make_index_sequence<MemberTypes<In>::size>> {};
template <class Want, class T> struct SearchBelow<Want, Tracked<T>> {
  using type = typename Prefix<Into, typename Search<Want, T>::type>::type;
};
template <class Want, class T> struct SearchBelow<Want, External<T>> {
  using type = typename Prefix<Through, typename Search<Want, T>::type>::type;
};
template <class Want, class K, class T>
struct SearchBelow<Want, Keyed<K, T>> {
  using type = typename Prefix<At<K>, typename Search<Want, T>::type>::type;
};
template <class Want, class T> struct SearchBelow<Want, std::optional<T>> {
  using type = typename Prefix<IfThere, typename Search<Want, T>::type>::type;
};
template <class Want, class Alternatives, class Indices> struct SearchAlts;
template <class Want, class... Ts, std::size_t... I>
struct SearchAlts<Want, Types<Ts...>, std::index_sequence<I...>> {
  using type = typename Concat<
      typename Prefix<Alt<I>, typename Search<Want, Ts>::type>::type...>::type;
};
template <class Want, template <class...> class V, class... Ts>
  requires VariantLike<V<Ts...>>
struct SearchBelow<Want, V<Ts...>>
    : SearchAlts<Want, Types<Ts...>, std::index_sequence_for<Ts...>> {};
// Every path from In to a part of type Want; not on below one found.
template <class Want, class In> struct Search : SearchBelow<Want, In> {};
template <class Want> struct Search<Want, Want> {
  using type = Types<Path<>>;
};

// A member pointer's class and type.
template <class M> struct MemberPointer;
template <class C, class T> struct MemberPointer<T C::*> {
  using Class = C;
  using Type = T;
};
// Declared, never defined, never read: only where its members are is asked.
template <class T> extern const T kDeclaredOnly;
// Which member of its class a member pointer names: the one at the same
// address -- no reflection needed, nothing left for run time.
template <auto M, class C> consteval std::size_t indexIn(const C &object) {
  const void *want = std::addressof(object.*M);
  std::size_t found = MemberTypes<C>::size;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    ((static_cast<const void *>(std::addressof(aggregate::get<I>(object))) == want
          ? void(found = I)
          : void()),
     ...);
  }(std::make_index_sequence<MemberTypes<C>::size>{});
  return found;
}
// Asked of one made while compiling, where the class can be (a model's
// parts can: it is constexpr); else of one only declared, which a class of
// a type local to a file cannot be asked of.
template <class C>
concept MadeWhileCompiling = requires { typename std::integral_constant<int, (C{}, 0)>; };
template <auto M> consteval std::size_t indexOfMember() {
  using C = typename MemberPointer<decltype(M)>::Class;
  if constexpr (MadeWhileCompiling<C>) {
    const C object{};
    return indexIn<M>(object);
  } else {
    return indexIn<M>(kDeclaredOnly<C>);
  }
}
template <auto M> inline constexpr std::size_t kIndexOfMember = indexOfMember<M>();
// A field: where its aggregate is, and on, into the member.
template <auto M, class In> struct Search<Field<M>, In> {
  using type = typename Suffix<Member<kIndexOfMember<M>>,
                               typename Search<typename MemberPointer<decltype(M)>::Class, In>::type>::type;
};
} // namespace detail

// Every place below In that a Want is at.
template <class Want, class In>
using PathsTo = typename detail::Search<Want, In>::type;

// How many there are: what a part is looked for by, before it is taken.
template <class Want, class In>
inline constexpr std::size_t kFound = PathsTo<Want, In>::size;

// Named in an error where a type is in more than one place: the compiler
// prints the paths it was found at, which is where the fix is.
template <class Want, class FoundAt> struct ThisTypeIsInMoreThanOnePlace;

namespace detail {
template <class Want, class In, std::size_t N> struct PathToT {
  using type = typename PathsTo<Want, In>::template at<0>;
};
template <class Want, class In> struct PathToT<Want, In, 0> {
  static_assert(false, "skiff::model: this type is nowhere below the part "
                       "looked in");
};
template <class Want, class In, std::size_t N>
  requires(N > 1)
struct PathToT<Want, In, N> {
  static_assert(false, "skiff::model: this type is in more than one place "
                       "below the part looked in: give each its own type, or "
                       "look from a scope nearer to it (the paths are named "
                       "in the next error)");
  using type = typename ThisTypeIsInMoreThanOnePlace<Want, PathsTo<Want, In>>::type;
};
} // namespace detail

// The one place below In a Want is at.
template <class Want, class In>
using PathTo = typename detail::PathToT<Want, In, kFound<Want, In>>::type;

namespace detail {
template <class A, class B> struct Join;
template <class... A, class... B> struct Join<Path<A...>, Path<B...>> {
  using type = Path<A..., B...>;
};

template <class In, class P> struct TypeAt;
template <class In> struct TypeAt<In, Path<>> {
  using type = In;
};
template <class In, std::size_t I, class... R>
struct TypeAt<In, Path<Member<I>, R...>> {
  using type = typename TypeAt<typename MemberTypes<In>::template at<I>,
                               Path<R...>>::type;
};
template <class T, class... R> struct TypeAt<Tracked<T>, Path<Into, R...>> {
  using type = typename TypeAt<T, Path<R...>>::type;
};
template <class T, class... R>
struct TypeAt<External<T>, Path<Through, R...>> {
  using type = typename TypeAt<T, Path<R...>>::type;
};
template <class K, class T, class... R>
struct TypeAt<Keyed<K, T>, Path<At<K>, R...>> {
  using type = typename TypeAt<T, Path<R...>>::type;
};
template <class T, class... R>
struct TypeAt<std::optional<T>, Path<IfThere, R...>> {
  using type = typename TypeAt<T, Path<R...>>::type;
};
template <template <class...> class V, class... Ts, std::size_t I, class... R>
  requires VariantLike<V<Ts...>>
struct TypeAt<V<Ts...>, Path<Alt<I>, R...>> {
  using type = typename TypeAt<std::tuple_element_t<I, std::tuple<Ts...>>,
                               Path<R...>>::type;
};

template <class P> struct KeyTypes;
template <> struct KeyTypes<Path<>> {
  using type = Types<>;
};
template <class S, class... R> struct KeyTypes<Path<S, R...>> {
  using type = typename KeyTypes<Path<R...>>::type;
};
template <class K, class... R> struct KeyTypes<Path<At<K>, R...>> {
  using type = typename Concat<Types<K>, typename KeyTypes<Path<R...>>::type>::type;
};
template <class L> struct TupleOf;
template <class... K> struct TupleOf<Types<K...>> {
  using type = std::tuple<K...>;
  using borrowed = std::tuple<const K &...>;
};
} // namespace detail

template <class A, class B> using Join = typename detail::Join<A, B>::type;
template <class In, class P> using TypeAt = typename detail::TypeAt<In, P>::type;
// The keys a path goes through, held, or borrowed for a walk.
template <class P>
using KeysOf = typename detail::TupleOf<typename detail::KeyTypes<P>::type>::type;
template <class P>
using BorrowedKeysOf =
    typename detail::TupleOf<typename detail::KeyTypes<P>::type>::borrowed;

} // namespace skiff::model
