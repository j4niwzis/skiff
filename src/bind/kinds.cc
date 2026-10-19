// skiff.bind:kinds -- the kinds of node the model knows: Bound, Scoped,
// Each, Local (the module is skiff.bind: see bind.cc).
export module skiff.bind:kinds;

import std;
import splice;
import skiff.aggregate;
import skiff.model;
import skiff.scene;

export namespace skiff::bind {

// Bound parts read with no tracked part of their own above them: each is
// read again at every edit anywhere. Counted, for a test or a debug build to
// see where a Tracked<> is missing.
inline std::uint64_t &untrackedReads() {
  thread_local std::uint64_t count = 0;
  return count;
}
// Moved by every change of any Local's state: what a Binding's refresh
// looks at, beside the model's revision.
inline std::uint64_t &localEpoch() {
  thread_local std::uint64_t epoch = 0;
  return epoch;
}

// The value a node bound to Want reads: the part's own type, a field's
// member's.
template <class Want> struct BoundValueT {
  using type = Want;
};
template <auto M> struct BoundValueT<model::Field<M>> {
  using type = typename model::detail::MemberPointer<decltype(M)>::Type;
};
// A node's read of a value, where it has one.
template <class N, class V> void readErased(void *node, const void *value) {
  if constexpr (requires(N &n, const V &v) { n.read(v); })
    static_cast<N *>(node)->read(*static_cast<const V *>(value));
}

// A node shown the one Want where it is found; what it changes of it, its
// answers to presses say (Own<change>).
template <class Want, class Base>
struct Bound : Base {
  // Its read, made where the node is made, for the runtime walks (outside a
  // release build): a read may make a whole subtree -- a mount's content, a
  // list's rows -- which is that unit's to make, not the one that makes the
  // walks' table of the node.
  void (*fReadErased)(void *, const void *) = &readErased<Bound, typename BoundValueT<Want>::type>;
  using BoundTo = Want;
  // Keep the wrapped node's overloads at this wrapper's level. Node's
  // deduced-this defaults otherwise beat inherited event handlers.
  using Base::onKey;
  using Base::onPointer;
  using Base::onText;
  using Base::onSemantic;
  Bound() = default;
  explicit Bound(Base base)
    requires std::move_constructible<Base>
      : Base(std::move(base)) {}
  // Made in place, where it is held by what makes its parts from arguments.
  template <class... Args>
    requires std::constructible_from<Base, Args...>
  explicit Bound(std::in_place_t, Args &&...args) : Base(std::forward<Args>(args)...) {}
  model::Revision fSeen = 0;
  // Equal revisions in two keyed parts do not mean they are the same
  // value. Keep its identity too when a scope is moved to another key.
  const void *fSource = nullptr;
  bool fShown = false;
  bool fGone = false;
};

// A node shown a value computed from the model -- a list sorted and
// filtered, a header's facts put together -- by Compute, a callable type,
// from the root: computed again only where one of the parts it reads
// (Reads, each found once in the root) has moved since, and read as a
// bound node reads its part.
// A node that reads the model itself -- refresh(const M &) -- says which
// models, and how, as a member set as it is made (outside a release build,
// for the runtime walks): `Refreshes<N> fRefreshes = refreshes(
// refreshFrom<Self, chats_model>(), refreshFromRoot<Self, kept_root,
// &Self::refresh_kept>())`. The walks' table of the node then calls through
// it, and nothing of the node's refresh is made where the table is.
struct RefreshEntry {
  model::rt::TypeKey fType;  // the model's type, or its root's
  bool fByRoot;
  void (*fRefresh)(void *, const void *);
};
template <std::size_t N> struct Refreshes {
  std::array<RefreshEntry, N> fEntries;
};
template <class Self, class M> constexpr RefreshEntry refreshFrom() {
  return {model::rt::keyOf<M>(), false, +[](void *node, const void *m) { static_cast<Self *>(node)->refresh(*static_cast<const M *>(m)); }};
}
template <class Self, class Root, void (Self::*Refresh)(const Root &)> constexpr RefreshEntry refreshFromRoot() {
  return {model::rt::keyOf<Root>(), true, +[](void *node, const void *root) { (static_cast<Self *>(node)->*Refresh)(*static_cast<const Root *>(root)); }};
}
template <class... E> constexpr Refreshes<sizeof...(E)> refreshes(E... entries) { return {{entries...}}; }

// The one argument a callable's own operator() takes: what it computes
// from, where it says (a generic one does not).
template <class F> struct OnlyArgumentOf {};
template <class F>
  requires requires { &F::operator(); }
struct OnlyArgumentOf<F> : OnlyArgumentOf<decltype(&F::operator())> {};
template <class C, class R, class A> struct OnlyArgumentOf<R (C::*)(A) const> {
  using type = std::remove_cvref_t<A>;
};
template <class C, class R, class A> struct OnlyArgumentOf<R (C::*)(A)> {
  using type = std::remove_cvref_t<A>;
};

template <class Compute, class Base, class... Reads>
struct Derived : Base {
  using Computes = Compute;
  // Its compute and read, made where the node is made, for the runtime walks
  // (outside a release build): given the parts' values, or the model's root
  // and its type, computed and read -- a read may make a whole subtree (a
  // mount's content), which is that unit's to make, not the one that makes
  // the walks' table of the node.
  void (*fComputeErased)(void *, const void *const *, const void *, model::rt::TypeKey) = &computeErased;
  static void computeErased(void *self, const void *const *values, const void *root, model::rt::TypeKey rootType) {
    auto &derived = *static_cast<Derived *>(self);
    using Values = std::tuple<typename BoundValueT<Reads>::type...>;
    if constexpr (requires(Base &b, const Compute &c) { b.read(c(std::declval<const typename BoundValueT<Reads>::type &>()...)); }) {
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        derived.read(derived.fCompute(*static_cast<const std::tuple_element_t<I, Values> *>(values[I])...));
      }(std::index_sequence_for<Reads...>{});
    } else if constexpr (requires { typename OnlyArgumentOf<Compute>::type; }) {
      using Root = typename OnlyArgumentOf<Compute>::type;
      if (rootType == model::rt::keyOf<Root>())
        derived.read(derived.fCompute(*static_cast<const Root *>(root)));
    }
  }
  Derived() = default;
  explicit Derived(Base base, Compute compute = {}) : Base(std::move(base)), fCompute(std::move(compute)) {}
  Compute fCompute{};
  model::Revision fSeen = 0;
  bool fShown = false;

  template <class M>
    requires((model::kFound<Reads, typename M::RootType> == 1) && ...)
  void refresh(const M &model) {
    const model::Revision now = std::max({model::Revision{0}, model.template look<Reads>().fRevision...});
    if (fShown && now == fSeen)
      return;
    fSeen = now;
    fShown = true;
    // Computed from the parts it reads, where Compute takes them; else from
    // the whole root.
    if constexpr (requires(Base &b) { b.read(fCompute(*model.template look<Reads>()...)); })
      this->read(fCompute(*model.template look<Reads>()...));
    else if constexpr (requires(Base &b) { b.read(fCompute(model.root())); })
      this->read(fCompute(model.root()));
  }
};

template <class Within, class Handlers, class Base, class... Keys>
struct Scoped : Base {
  using ScopeOf = Within;
  Scoped() = default;
  explicit Scoped(Handlers handlers, Base base, Keys... keys)
      : Base(std::move(base)), fKeys(std::move(keys)...),
        fHandlers(std::move(handlers)) {}
  std::tuple<Keys...> fKeys{};
  Handlers fHandlers{};
  // At another part of the same kind -- the chat chosen now: what is in it
  // is shown that one, by the next refresh that walks it (after the
  // binding's invalidate()).
  void rekey(Keys... keys) {
    fKeys = std::tuple<Keys...>(std::move(keys)...);
    fSeen = 0;
    fShown = false;
  }
  // Its part's revision when its subtree was last refreshed, and the local
  // epoch then: the same now, the subtree is not walked again.
  model::Revision fSeen = 0;
  std::uint64_t fEpoch = 0;
  bool fShown = false;
  // Something under it reads a part from beyond it -- from the root, a part
  // its own does not hold: walked whenever the model moved, not only where
  // its part did, or that read was never made again.
  bool fReadsBeyond = false;
};

// How a row is made for an element: from it where the row is made so, else
// default.
template <class Row> struct DefaultRow {
  template <class T>
    requires std::constructible_from<Row, const T &>
  Row operator()(const T &value) const {
    return Row(value);
  }
  template <class T> Row operator()(const T &) const { return Row{}; }
};

template <class Key, class T, class Row, class Container,
          class Make = DefaultRow<Row>>
struct Each : Container {
  Each() = default;
  explicit Each(Make make, Container container = {})
      : Container(std::move(container)), fMake(std::move(make)) {}
  Make fMake{};
  std::vector<Key> fKeys;
  std::vector<Row> fRows;
  // Its rows made to be a list's elements, made where the node is made, for
  // the runtime walks (outside a release build): making rows makes their
  // whole subtrees, which is that unit's to make.
  void (*fRowsErased)(void *, const void *, model::Revision) = &rowsErased;
  static void rowsErased(void *self, const void *seenList, model::Revision revision) {
    auto &list = *static_cast<Each *>(self);
    const auto &seen = *static_cast<const model::Keyed<Key, T> *>(seenList);
    if (list.fShown && list.fSeen == revision)
      return;
    list.fSeen = revision;
    list.fShown = true;
    if (seen.size() == list.fKeys.size() && std::ranges::equal(seen.keys(), list.fKeys))
      return;
    std::vector<Key> keys;
    std::vector<Row> rows;
    keys.reserve(seen.size());
    rows.reserve(seen.size());
    for (std::size_t i = 0; i < seen.size(); ++i) {
      const Key &key = seen.keyAt(i);
      keys.push_back(key);
      const auto was = std::ranges::find(list.fKeys, key);
      if (was != list.fKeys.end())
        rows.push_back(std::move(list.fRows[static_cast<std::size_t>(was - list.fKeys.begin())]));
      else
        rows.push_back(list.fMake(seen.valueAt(i)));
    }
    list.fKeys = std::move(keys);
    list.fRows = std::move(rows);
    list.indexRows();
    list.invalidateLayout();
  }
  // The rows by key, sorted: where a targeted refresh finds the one row a
  // change is in.
  std::vector<std::pair<Key, std::size_t>> fRowIndex;
  model::Revision fSeen = 0;
  bool fShown = false;

  const Row *rowFor(const Key &key) const { return const_cast<Each &>(*this).rowFor(key); }
  Row *rowFor(const Key &key) {
    const auto at = std::ranges::lower_bound(fRowIndex, key, std::ranges::less{},
                                             &std::pair<Key, std::size_t>::first);
    return at == fRowIndex.end() || at->first != key ? nullptr : &fRows[at->second];
  }
  void indexRows() {
    fRowIndex = std::views::iota(std::size_t{0}, fKeys.size()) |
                std::views::transform([&](std::size_t i) {
                  return std::pair<Key, std::size_t>{fKeys[i], i};
                }) |
                std::ranges::to<std::vector>();
    std::ranges::sort(fRowIndex, std::ranges::less{}, &std::pair<Key, std::size_t>::first);
  }

  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fRows);
  }
};

// No reactions: a Local's state tells no one.
struct NoReactions {};
// No handlers.
struct NoHandlers {};

template <class T, class Handlers, class Base, bool Hidden = false>
struct Local : Base {
  using LocalOf = T;
  static constexpr bool kHidden = Hidden;
  using Store = model::Model<T, NoReactions>;
  Local() = default;
  explicit Local(Handlers handlers, Base base, T initial = {})
      : Base(std::move(base)), fModel(std::move(initial)),
        fHandlers(std::move(handlers)) {}
  Store fModel{};
  Handlers fHandlers{};
};

namespace detail {
template <class W, class B>
std::true_type boundTest(const Bound<W, B> *);
std::false_type boundTest(const void *);
template <class W, class H, class B, class... K>
std::true_type scopedTest(const Scoped<W, H, B, K...> *);
std::false_type scopedTest(const void *);
template <class K, class T, class R, class C, class M>
std::true_type eachTest(const Each<K, T, R, C, M> *);
std::false_type eachTest(const void *);
template <class T, class H, class B, bool D>
std::true_type localTest(const Local<T, H, B, D> *);
std::false_type localTest(const void *);
} // namespace detail

template <class N>
concept IsBound = decltype(detail::boundTest(static_cast<N *>(nullptr)))::value;
template <class N>
concept IsScoped =
    decltype(detail::scopedTest(static_cast<N *>(nullptr)))::value;

// Whether a node is one a model with this root binds: one bound to a part,
// or a scope at one, that is there to find -- a tree may be bound to more
// than one model, each binding what is its own and passing the rest by.
template <class Root, class N> inline constexpr bool kOfModel = true;
template <class Root, class N>
  requires IsBound<N>
inline constexpr bool kOfModel<Root, N> = model::kFound<typename N::BoundTo, Root> > 0;
template <class Root, class N>
  requires(IsScoped<N> && !IsBound<N>)
inline constexpr bool kOfModel<Root, N> = model::kFound<typename N::ScopeOf, Root> > 0;
template <class N>
concept IsEach = decltype(detail::eachTest(static_cast<N *>(nullptr)))::value;
template <class N>
concept IsLocal = decltype(detail::localTest(static_cast<N *>(nullptr)))::value;
// A node that reads the model itself, the imperative way.
template <class N, class M>
concept ReadsItself = requires(N &n, const M &m) { n.refresh(m); };

namespace detail {
// A node as the kind it derives from: deduced through the base.
template <class W, class B> Bound<W, B> &asBound(Bound<W, B> &node) {
  return node;
}
template <class W, class H, class B, class... K>
Scoped<W, H, B, K...> &asScoped(Scoped<W, H, B, K...> &node) {
  return node;
}
template <class K, class T, class R, class C, class M>
Each<K, T, R, C, M> &asEach(Each<K, T, R, C, M> &node) {
  return node;
}
template <class T, class H, class B, bool D>
Local<T, H, B, D> &asLocal(Local<T, H, B, D> &node) {
  return node;
}
template <class W, class B> W boundToOf(const Bound<W, B> &);
template <class W, class H, class B, class... K>
W scopeOfOf(const Scoped<W, H, B, K...> &);
template <class W, class H, class B, class... K>
H handlersOf(const Scoped<W, H, B, K...> &);
template <class K, class T, class R, class C, class M>
T elementOf(const Each<K, T, R, C, M> &);
template <class K, class T, class R, class C, class M>
K keyOf(const Each<K, T, R, C, M> &);

} // namespace detail
} // namespace skiff::bind
