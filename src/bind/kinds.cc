// skiff.bind:kinds -- the kinds of node the model knows: Bound, Scoped,
// Each, Local, Emits (the module is skiff.bind: see bind.cc).
export module skiff.bind:kinds;

import std;
import splice;
import skiff.aggregate;
import skiff.model;
import skiff.scene;

export namespace skiff::bind {

// Asked of the nodes since the last drain: what a Binding's drain looks at
// before it walks. Kept per thread, as the scene is.
inline std::uint64_t &pendingCount() {
  thread_local std::uint64_t count = 0;
  return count;
}
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

// A node's events, of the types it sends.
template <class... E> struct Emits {
  using Out = model::Types<E...>;
  std::vector<spl::variant<E...>> fEmitted;
  template <class X> void emit(X event) {
    fEmitted.emplace_back(std::move(event));
    ++pendingCount();
  }
};

// ---- events of types nobody listed: deduced -----------------------------
//
// A node that does not say what it sends -- no Emits<E...>, no Out -- may
// still send: emitFrom(*this, e). What types a node of its type sends is
// then deduced, by the loophole of friend injection: each emitFrom<C, E>
// instantiated records E against C, and the drain reads C's back. Nothing
// of this is instantiated for a node that lists what it sends. What is
// read is what was instantiated before the read, so a node's sending code
// has to be instantiated before the drain is (in the same translation
// unit, or by deduce<C, E...>() said there).
namespace loophole {
template <class C, int N> struct Slot {
  friend consteval auto typeAt(Slot);
};
template <class C, int N, class T> struct Fill {
  friend consteval auto typeAt(Slot<C, N>) { return std::type_identity<T>{}; }
};
template <class C, int N, auto Tag>
consteval int count() {
  if constexpr (requires { typeAt(Slot<C, N>{}); })
    return count<C, N + 1, Tag>();
  else
    return N;
}
template <class C, class T, int N, auto Tag> consteval bool has() {
  if constexpr (N == 0)
    return false;
  else
    return std::same_as<typename decltype(typeAt(Slot<C, N - 1>{}))::type, T> ||
           has<C, T, N - 1, Tag>();
}
template <class C, class T, auto Tag = [] {}> consteval bool record() {
  constexpr int n = count<C, 0, Tag>();
  if constexpr (!has<C, T, n, Tag>())
    (void)sizeof(Fill<C, n, T>);
  return true;
}
template <class C, class Indices> struct ReadAll;
template <class C, int... I>
struct ReadAll<C, std::integer_sequence<int, I...>> {
  using type =
      model::Types<typename decltype(typeAt(Slot<C, I>{}))::type...>;
};
// The types a C was seen to send, as many as were instantiated by now.
template <class C, auto Tag = [] {}>
using Deduced = typename ReadAll<
    C, std::make_integer_sequence<int, count<C, 0, Tag>()>>::type;
} // namespace loophole

// Marks a node whose events are deduced.
struct Emitter {};

// Where deduced events wait: per type, with the node that sent them.
template <class E> std::vector<std::pair<const void *, E>> &pendingOf() {
  thread_local std::vector<std::pair<const void *, E>> pending;
  return pending;
}
// Recorded in its signature, where it is called: a function template's
// body is instantiated at the end of the translation unit, and a read
// before that would see nothing.
template <class C, class E, auto Tag = [] {},
          bool = loophole::record<C, E, Tag>()>
void emitFrom(C &self, E event) {
  pendingOf<E>().emplace_back(&self, std::move(event));
  ++pendingCount();
}
// That a C sends these, said where its drain is instantiated, so that the
// deduction sees them whatever was instantiated first.
template <class C, class... E> consteval bool deduce() {
  return (loophole::record<C, E>() && ...);
}

// A node shown the one Want where it is found; what it changes of it, its
// answers to presses say (Own<change>).
template <class Want, class Base>
struct Bound : Base {
  using BoundTo = Want;
  Bound() = default;
  explicit Bound(Base base) : Base(std::move(base)) {}
  model::Revision fSeen = 0;
  bool fShown = false;
  bool fGone = false;
};

// A node shown a value computed from the model -- a list sorted and
// filtered, a header's facts put together -- by Compute, a callable type,
// from the root: computed again only where one of the parts it reads
// (Reads, each found once in the root) has moved since, and read as a
// bound node reads its part.
template <class Compute, class Base, class... Reads>
struct Derived : Base {
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
    if constexpr (requires(Base &b) { b.read(fCompute(model.root())); })
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
  using Store = model::Model<T, NoReactions, std::variant<model::Nothing>>;
  Local() = default;
  explicit Local(Handlers handlers, Base base, T initial = {})
      : Base(std::move(base)), fState(std::move(initial)),
        fHandlers(std::move(handlers)) {}
  Store fState{};
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
template <class N>
concept EmitsEvents = requires(N &n) { n.fEmitted.clear(); };
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
