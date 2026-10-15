// skiff.bind:runtime -- the binding's walks made once, at run time: what a
// debug build binds a tree by (the module is skiff.bind: see bind.cc).
//
// A release build walks a tree with every node's type known: each walk made
// for every type of node, every place, every frame it can be in. Outside one
// that made the units that walk a program's window take minutes, and a tree
// whose node types nest deep could not be compiled at all. Here the same
// walks go through the scene's erased view of the tree (AnyNodeRef), and
// what a node binds is read from a table its type makes once
// (skiffBindTable, found by the scene for each node): a part shown, a scope,
// a Local, a list's rows. Places are skiff.model's runtime places.
export module skiff.bind:runtime;

import std;
import splice;
import skiff.model;
import skiff.scene;
import :kinds;
import :frames;
import :walk;
import :takes;
import :press;

export namespace skiff::bind::rt {
namespace mrt = model::rt;

// Where SKIFF_TRACE_BIND is set: what each runtime walk does, on stderr --
// a node read, a value derived, a scope or list passed by -- to find where
// a tree and its model part ways.
inline bool tracing() {
  static const bool on = std::getenv("SKIFF_TRACE_BIND") != nullptr;
  return on;
}

// ---- a model, erased ------------------------------------------------------------

// What the walks ask of a model: its shape, a part at a place, its revision.
struct AnyModel {
  const void *fModel = nullptr;
  const mrt::Shape *fRoot = nullptr;
  mrt::TypeKey fType = nullptr;     // the model's own type
  const void *fRootValue = nullptr;  // its root, as it is now
  mrt::Seen (*fLook)(const void *, const mrt::Place &) = nullptr;
  model::Revision (*fRevision)(const void *) = nullptr;
  // Where it may be changed (a press's model, a Local's state): the same
  // model, and how an erased change is made in it.
  void *fEditable = nullptr;
  bool (*fApply)(void *, const mrt::Place &, const mrt::Change &) = nullptr;
  // Its reactions' own events (an on(event) of theirs), taken: whether they
  // took it.
  bool (*fReact)(void *, mrt::TypeKey, const void *) = nullptr;
  template <class M> static AnyModel of(const M &m) {
    return {std::addressof(m), &mrt::shapeOf<typename M::RootType>(), mrt::keyOf<M>(), std::addressof(m.root()),
            +[](const void *self, const mrt::Place &place) { return static_cast<const M *>(self)->lookAt(place); },
            +[](const void *self) { return static_cast<const M *>(self)->revision(); }};
  }
  template <class M> static AnyModel editable(M &m) {
    AnyModel made = of(std::as_const(m));
    made.fEditable = std::addressof(m);
    made.fApply = +[](void *self, const mrt::Place &place, const mrt::Change &change) {
      return static_cast<M *>(self)->applyErased(place, change);
    };
    made.fReact = &reactWith<M>;
    return made;
  }
  template <class M> static bool reactWith(void *self, mrt::TypeKey type, const void *event) {
    return reactEach(*static_cast<M *>(self), type, event, takes::OnOneEvents<typename M::ReactionsType>{});
  }
  template <class M, class... E> static bool reactEach(M &model, mrt::TypeKey type, const void *event, model::Types<E...>) {
    return ((type == mrt::keyOf<E>() && (model.send(*static_cast<const E *>(event)), true)) || ...);
  }
  bool react(mrt::TypeKey type, const void *event) const {
    return fEditable != nullptr && fReact(fEditable, type, event);
  }
  bool apply(const mrt::Place &place, const mrt::Change &change) const {
    return fEditable != nullptr && fApply(fEditable, place, change);
  }
  [[nodiscard]] mrt::Seen look(const mrt::Place &place) const { return fLook(fModel, place); }
  [[nodiscard]] model::Revision revision() const { return fRevision(fModel); }
};

// ---- what a press and a handler answer, erased ----------------------------------------

// An event sent, held: its type and itself.
struct Sent {
  mrt::TypeKey fType = nullptr;
  std::shared_ptr<const void> fEvent;
  template <class E> static Sent of(E event) { return {mrt::keyOf<E>(), std::make_shared<const E>(std::move(event))}; }
};
// A change of the pressed node's own part.
struct OwnEdit {
  mrt::Change fChange;
  std::optional<mrt::Want> fWant;  // a bound node's part; else where it is
};
// A change of the one Want below where it is answered, with the keys the
// way there needs (model::Over).
struct OverEdit {
  mrt::Want fWant;
  std::vector<mrt::AnyKey> fKeys;
  mrt::Change fChange;
};
// An event passed on, up, from the frame that took it (model::Up).
struct UpSent {
  Sent fEvent;
};
// A change of the pressed node's own part that is another model's.
struct Missed {};
using Answer = std::variant<OwnEdit, Sent, OverEdit, UpSent, Missed>;
using Answers = std::vector<Answer>;

// The part an Over changes: the Want's own type, a field's member's.
template <class Want> struct OverValueT {
  using type = Want;
};
template <auto M> struct OverValueT<model::Field<M>> {
  using type = typename model::detail::MemberPointer<decltype(M)>::Type;
};
template <class Want> using OverValue = typename OverValueT<Want>::type;

// Answers, as their types say them, each made erased where it is answered.
namespace answers {
inline void add(Answers &, model::Nothing) {}
inline void add(Answers &, scene::Taken) {}
template <class... A> void add(Answers &out, const std::tuple<A...> &all);
template <class E> void add(Answers &out, const std::optional<E> &maybe);
template <class E> void add(Answers &out, const std::vector<E> &all);
template <class... E> void add(Answers &out, const std::variant<E...> &one);
template <class... E> void add(Answers &out, const spl::variant<E...> &one);
template <class E> void add(Answers &out, const model::Up<E> &up);
template <class E> void add(Answers &out, const model::Up<std::optional<E>> &up);
template <class Want, class C, class... Keys> void add(Answers &out, const model::Over<Want, C, Keys...> &over);
template <class E> void add(Answers &out, const E &event) { out.emplace_back(Sent::of(event)); }

template <class... A> void add(Answers &out, const std::tuple<A...> &all) {
  std::apply([&](const auto &...each) { (add(out, each), ...); }, all);
}
template <class E> void add(Answers &out, const std::optional<E> &maybe) {
  if (maybe)
    add(out, *maybe);
}
template <class E> void add(Answers &out, const std::vector<E> &all) {
  for (const E &each : all) { add(out, each); }
}
template <class... E> void add(Answers &out, const std::variant<E...> &one) {
  std::visit([&](const auto &each) { add(out, each); }, one);
}
template <class... E> void add(Answers &out, const spl::variant<E...> &one) {
  spl::visit([&](const auto &each) { add(out, each); }, one);
}
template <class E> void add(Answers &out, const model::Up<E> &up) { out.emplace_back(UpSent{Sent::of(up.fEvent)}); }
template <class E> void add(Answers &out, const model::Up<std::optional<E>> &up) {
  if (up.fEvent)
    out.emplace_back(UpSent{Sent::of(*up.fEvent)});
}
template <class Want, class C, class... Keys> void add(Answers &out, const model::Over<Want, C, Keys...> &over) {
  out.emplace_back(OverEdit{mrt::wantOf<Want>(),
                            std::apply([](const auto &...k) { return std::vector<mrt::AnyKey>{mrt::AnyKey(k)...}; }, over.fKeys),
                            mrt::Change::of<OverValue<Want>, C>(over.fChange)});
}
} // namespace answers

// Where a scope's handlers are told an event: its place, as a key is read
// off it (key<K>(here)).
struct Here {
  const mrt::Place &fPlace;
  template <class K>
    requires(!std::integral<K>)
  const K &key() const {
    for (const mrt::AnyKey &each : fPlace.fKeys)
      if (const K *found = each.as<K>())
        return *found;
    throw std::logic_error("skiff::bind: no key of this type on the way to this place");
  }
};

// A frame's handler of one event type: told the event, it answers.
struct HandlerEntry {
  mrt::TypeKey fEvent;
  // (handlers or the Local, the event, where, what is answered)
  void (*fTake)(void *, const void *, const Here &, Answers &);
};
template <class E, class T> void scopeTakes(void *n, const void *e, const Here &here, Answers &out) {
  const auto &handlers = detail::asScoped(*static_cast<T *>(n)).fHandlers;
  const E &event = *static_cast<const E *>(e);
  if constexpr (requires { handlers.on(event, here); })
    answers::add(out, handlers.on(event, here));
  else
    answers::add(out, handlers.on(event));
}
template <class T, class Events> struct ScopeHandlers;
template <class T, class... E> struct ScopeHandlers<T, model::Types<E...>> {
  static constexpr std::array<HandlerEntry, sizeof...(E)> kEntries{HandlerEntry{mrt::keyOf<E>(), &scopeTakes<E, T>}...};
};
template <class E, class T> void localTakes(void *n, const void *e, const Here &, Answers &out) {
  auto &local = detail::asLocal(*static_cast<T *>(n));
  using L = std::remove_reference_t<decltype(local)>;
  const E &event = *static_cast<const E *>(e);
  if constexpr (requires { local.fHandlers.on(event, *local.fModel.template look<typename L::LocalOf>()); })
    answers::add(out, local.fHandlers.on(event, *local.fModel.template look<typename L::LocalOf>()));
  else
    answers::add(out, local.fHandlers.on(event));
}
template <class T, class Events> struct LocalHandlers;
template <class T, class... E> struct LocalHandlers<T, model::Types<E...>> {
  static constexpr std::array<HandlerEntry, sizeof...(E)> kEntries{HandlerEntry{mrt::keyOf<E>(), &localTakes<E, T>}...};
};
[[nodiscard]] inline const HandlerEntry *entryFor(std::span<const HandlerEntry> entries, mrt::TypeKey event) {
  const auto found = std::ranges::find(entries, event, &HandlerEntry::fEvent);
  return found == entries.end() ? nullptr : &*found;
}

// What a pressed node answers -- its own part changed, events sent -- and,
// where it keeps an answer of its handler's (Answer), that answer.
struct PressPart {
  void (*fPress)(void *, Answers &);
  bool (*fKept)(void *, const scene::HostWork::KeptAnswer &, Answers &);
};

// ---- what a node binds: its type's table -------------------------------------------

// The value a bound node reads: the part's own type, a field's member's.
template <class Want> struct ValueOfT {
  using type = Want;
};
template <auto M> struct ValueOfT<model::Field<M>> {
  using type = typename model::detail::MemberPointer<decltype(M)>::Type;
};
template <class Want> using ValueOf = typename ValueOfT<Want>::type;

// A bound node: what it wants, how it reads it, how it is told it is gone,
// and what it last saw.
struct BoundPart {
  mrt::Want fWant;
  mrt::TypeKey fValue;
  void (*fRead)(void *, const void *);
  void (*fGone)(void *);
  model::Revision &(*fSeen)(void *);
  const void *&(*fSource)(void *);
  bool &(*fShown)(void *);
  bool &(*fIsGone)(void *);
};
// A scope: the part it is a scope of, the keys of the way there, what it
// last saw.
struct ScopedPart {
  const HandlerEntry *fOn;
  std::size_t fOnCount;
  mrt::Want fWithin;
  std::vector<mrt::AnyKey> (*fKeys)(void *);
  model::Revision &(*fSeen)(void *);
  std::uint64_t &(*fEpoch)(void *);
  bool &(*fShown)(void *);
};
// A Local: its state's model, and whether nothing outside it is seen from
// within.
struct LocalPart {
  const HandlerEntry *fOn;
  std::size_t fOnCount;
  AnyModel (*fModel)(void *);
  // What its state logged, let go: shown by the walk that comes upon it.
  void (*fForget)(void *);
  bool fHidden;
};
// A list's rows: the list it shows, its rows made again to be the list's
// elements (given the list and its revision), each row and its key.
struct EachPart {
  mrt::Want fList;
  void (*fRows)(void *, const void *, model::Revision);
  void (*fClear)(void *);
  std::size_t (*fCount)(void *);
  scene::AnyNodeRef (*fRow)(void *, std::size_t);
  mrt::AnyKey (*fKeyAt)(void *, std::size_t);
};
// A node that reads the model itself: a value derived from parts (Derived),
// or a node with its own refresh(const M &) for a model of one type.
struct ItselfPart {
  void (*fRefresh)(void *, const AnyModel &);
};
// A pipe: where what its first end sends goes first -- its second end: a
// Local's handlers, or its own on() -- and which child its first end is.
struct PipePart {
  void *(*fFrom)(void *);
  void *(*fTo)(void *);
  const HandlerEntry *fOn;  // the second end's, where it is no Local
  std::size_t fOnCount;
  const LocalPart *fLocal;  // the second end's, where it is one
};
// All a node binds; most bind nothing and have no table.
struct Table {
  const BoundPart *fBound = nullptr;
  const ScopedPart *fScoped = nullptr;
  const LocalPart *fLocal = nullptr;
  const EachPart *fEach = nullptr;
  const ItselfPart *fItself = nullptr;
  const PressPart *fPress = nullptr;
  const PipePart *fPipe = nullptr;
};

namespace made {
template <class T> constexpr const BoundPart *bound() { return nullptr; }
template <class T>
  requires IsBound<T>
constexpr const BoundPart *bound();
template <class T> constexpr const ScopedPart *scoped() { return nullptr; }
template <class T>
  requires IsScoped<T>
constexpr const ScopedPart *scoped();
template <class T> constexpr const LocalPart *local() { return nullptr; }
template <class T>
  requires IsLocal<T>
constexpr const LocalPart *local();
template <class T> constexpr const EachPart *each() { return nullptr; }
template <class T>
  requires IsEach<T>
constexpr const EachPart *each();

template <class T> struct BoundOf {
  using B = std::remove_reference_t<decltype(detail::asBound(std::declval<T &>()))>;
  using Want = decltype(detail::boundToOf(std::declval<B &>()));
  using V = ValueOf<Want>;
  static constexpr BoundPart kPart{
      mrt::wantOf<Want>(),
      mrt::keyOf<V>(),
      // (Through the read the node was made with: nothing of it made here.)
      +[](void *n, const void *value) {
        auto &bound = detail::asBound(*static_cast<T *>(n));
        bound.fReadErased(std::addressof(bound), value);
      },
      +[](void *n) { detail::gone(*static_cast<T *>(n)); },
      +[](void *n) -> model::Revision & { return detail::asBound(*static_cast<T *>(n)).fSeen; },
      +[](void *n) -> const void *& { return detail::asBound(*static_cast<T *>(n)).fSource; },
      +[](void *n) -> bool & { return detail::asBound(*static_cast<T *>(n)).fShown; },
      +[](void *n) -> bool & { return detail::asBound(*static_cast<T *>(n)).fGone; }};
};
template <class T>
  requires IsBound<T>
constexpr const BoundPart *bound() {
  return &BoundOf<T>::kPart;
}

template <class T> struct ScopedOf {
  using S = std::remove_reference_t<decltype(detail::asScoped(std::declval<T &>()))>;
  using Within = decltype(detail::scopeOfOf(std::declval<S &>()));
  using H = decltype(detail::handlersOf(std::declval<S &>()));
  using On = ScopeHandlers<T, takes::OnEvents<H, Here>>;
  static constexpr ScopedPart kPart{
      On::kEntries.data(), On::kEntries.size(),
      mrt::wantOf<Within>(),
      +[](void *n) {
        return std::apply([](const auto &...keys) { return std::vector<mrt::AnyKey>{mrt::AnyKey(keys)...}; },
                          detail::asScoped(*static_cast<T *>(n)).fKeys);
      },
      +[](void *n) -> model::Revision & { return detail::asScoped(*static_cast<T *>(n)).fSeen; },
      +[](void *n) -> std::uint64_t & { return detail::asScoped(*static_cast<T *>(n)).fEpoch; },
      +[](void *n) -> bool & { return detail::asScoped(*static_cast<T *>(n)).fShown; }};
};
template <class T>
  requires IsScoped<T>
constexpr const ScopedPart *scoped() {
  return &ScopedOf<T>::kPart;
}

template <class T> struct LocalOf {
  using L = std::remove_reference_t<decltype(detail::asLocal(std::declval<T &>()))>;
  using On = LocalHandlers<T, takes::OnEvents<decltype(std::declval<L &>().fHandlers), typename L::LocalOf>>;
  static constexpr LocalPart kPart{
      On::kEntries.data(), On::kEntries.size(),
      +[](void *n) { return AnyModel::editable(detail::asLocal(*static_cast<T *>(n)).fModel); },
      +[](void *n) { (void)detail::asLocal(*static_cast<T *>(n)).fModel.takeChanges(); }, L::kHidden};
};
template <class T>
  requires IsLocal<T>
constexpr const LocalPart *local() {
  return &LocalOf<T>::kPart;
}

template <class T> struct EachOf {
  using E = std::remove_reference_t<decltype(detail::asEach(std::declval<T &>()))>;
  using Key = decltype(detail::keyOf(std::declval<E &>()));
  using Element = decltype(detail::elementOf(std::declval<E &>()));
  using List = model::Keyed<Key, Element>;
  // The rows made to be the list's elements: through the row-making the
  // node was made with, nothing of the rows made here.
  static void rows(void *n, const void *seenList, model::Revision revision) {
    auto &list = detail::asEach(*static_cast<T *>(n));
    list.fRowsErased(std::addressof(list), seenList, revision);
  }
  static constexpr EachPart kPart{
      mrt::wantOf<List>(),
      &rows,
      +[](void *n) {
        auto &list = detail::asEach(*static_cast<T *>(n));
        if (list.fRows.empty())
          return;
        list.fRows.clear();
        list.fKeys.clear();
        list.fRowIndex.clear();
        list.invalidateLayout();
      },
      +[](void *n) { return detail::asEach(*static_cast<T *>(n)).fRows.size(); },
      +[](void *n, std::size_t i) { return scene::AnyNodeRef::of(detail::asEach(*static_cast<T *>(n)).fRows[i]); },
      +[](void *n, std::size_t i) { return mrt::AnyKey(detail::asEach(*static_cast<T *>(n)).fKeys[i]); }};
};
template <class T>
  requires IsEach<T>
constexpr const EachPart *each() {
  return &EachOf<T>::kPart;
}

// A Derived: its compute, its base, the parts it reads.
template <class C, class B, class... R> std::type_identity<Derived<C, B, R...>> derivedOf(const Derived<C, B, R...> &);
template <class T>
concept IsDerivedNode = requires(const T &t) { derivedOf(t); };
// The one argument a callable's own operator() takes: what it computes
// from, where it says (a generic one does not).
template <class F> struct ArgumentOf {};
template <class F>
  requires requires { &F::operator(); }
struct ArgumentOf<F> : ArgumentOf<decltype(&F::operator())> {};
template <class C, class R, class A> struct ArgumentOf<R (C::*)(A) const> {
  using type = std::remove_cvref_t<A>;
};
template <class C, class R, class A> struct ArgumentOf<R (C::*)(A)> {
  using type = std::remove_cvref_t<A>;
};
// What a node's own refresh() takes: the model it reads itself.
template <class T> struct RefreshesFrom {};
template <class T>
  requires requires { &T::refresh; }
struct RefreshesFrom<T> : RefreshesFrom<decltype(&T::refresh)> {};
template <class C, class A> struct RefreshesFrom<void (C::*)(A)> {
  using type = std::remove_cvref_t<A>;
};

// A Derived's refresh, made once: what each type gives it is the parts it
// reads, the root its compute takes where it reads none, and where its
// state and its compute are -- nothing of the walk made again for each.
struct DerivedState {
  model::Revision &fSeen;
  bool &fShown;
  void (*fCompute)(void *, const void *const *, const void *, mrt::TypeKey);
};
struct DerivedPart {
  const mrt::Want *fReads;
  std::size_t fCount;
  // Where it reads no part: the root its compute takes (none: never).
  mrt::TypeKey fRoot;
  DerivedState (*fState)(void *);
  const char *(*fName)();
};
void refreshDerived(void *node, const DerivedPart &part, const AnyModel &model) {
  // Computed from the whole root where it reads no part: only by the
  // model whose root its compute takes -- another model's walk would mark
  // it shown, computing nothing, and its own would then pass it by.
  if (part.fCount == 0 && (part.fRoot == nullptr || model.fRoot->fType != part.fRoot))
    return;
  // Its parts, each where it is in this model -- not this model's where
  // any is not in it -- and their newest revision.
  std::vector<const void *> values;
  values.reserve(part.fCount);
  model::Revision now = 0;
  for (const mrt::Want &want : std::span(part.fReads, part.fCount)) {
    const auto way = mrt::pathTo(*model.fRoot, want);
    if (!way)
      return;
    const mrt::Seen seen = model.look(mrt::Place{*way, {}});
    if (!seen)
      return;
    now = std::max(now, seen.fRevision);
    values.push_back(seen.fValue);
  }
  const DerivedState state = part.fState(node);
  if (tracing())
    std::println(std::cerr, "[bind] derived {} @{} at {} (seen {}, shown {})", part.fName(), node, now, state.fSeen, state.fShown);
  if (state.fShown && now == state.fSeen)
    return;
  state.fSeen = now;
  state.fShown = true;
  // Computed and read through what the node was made with: nothing of its
  // compute or read made here.
  state.fCompute(node, values.data(), model.fRootValue, model.fRoot->fType);
}
// The root a compute takes, where it says: none for a generic one.
template <class C> constexpr mrt::TypeKey rootTakenBy() { return nullptr; }
template <class C>
  requires requires { typename OnlyArgumentOf<C>::type; }
constexpr mrt::TypeKey rootTakenBy() {
  return mrt::keyOf<typename OnlyArgumentOf<C>::type>();
}
template <class D> struct DerivedReads;
template <class C, class B, class... R> struct DerivedReads<Derived<C, B, R...>> {
  static constexpr std::array<mrt::Want, sizeof...(R)> kReads{mrt::wantOf<R>()...};
  static constexpr mrt::TypeKey kRoot = rootTakenBy<C>();
};
template <class T> struct DerivedOf {
  using D = typename decltype(derivedOf(std::declval<const T &>()))::type;
  static constexpr DerivedPart kDerived{
      DerivedReads<D>::kReads.data(), DerivedReads<D>::kReads.size(), DerivedReads<D>::kRoot,
      +[](void *n) {
        D &node = *static_cast<D *>(n);
        return DerivedState{node.fSeen, node.fShown, node.fComputeErased};
      },
      +[]() { return typeid(D).name(); }};
  static constexpr ItselfPart kPart{+[](void *n, const AnyModel &model) {
    refreshDerived(std::addressof(static_cast<D &>(*static_cast<T *>(n))), kDerived, model);
  }};
};
template <class T> struct OwnRefreshOf {
  using M = typename RefreshesFrom<T>::type;
  static constexpr ItselfPart kPart{+[](void *n, const AnyModel &model) {
    if (model.fType == mrt::keyOf<M>())
      static_cast<T *>(n)->refresh(*static_cast<const M *>(model.fModel));
  }};
};
// A node that says which models it refreshes from (fRefreshes): through
// what it was made with.
// The class a member is declared in.
template <class P> struct MemberOwnerT {};
template <class M, class C> struct MemberOwnerT<M C::*> {
  using type = C;
};
template <class T> struct ListedRefreshOf {
  // The node as the class that lists them, which its entries are made for:
  // the node may be one made around it (a Bound of it), its own address
  // another.
  using Owner = typename MemberOwnerT<decltype(&T::fRefreshes)>::type;
  static constexpr ItselfPart kPart{+[](void *n, const AnyModel &model) {
    Owner &owner = *static_cast<T *>(n);
    for (const RefreshEntry &entry : owner.fRefreshes.fEntries)
      if (entry.fByRoot ? entry.fType == model.fRoot->fType : entry.fType == model.fType)
        entry.fRefresh(std::addressof(owner), entry.fByRoot ? model.fRootValue : model.fModel);
  }};
};
template <class T>
concept ListsRefreshes = requires(T &t) { t.fRefreshes.fEntries; };
// Whether a node has anything named refresh -- one, an overload set, a
// template: its name made ambiguous with a probe's own, where it has.
struct RefreshNameProbe {
  int refresh;
};
template <class T> struct RefreshNameOf : T, RefreshNameProbe {};
template <class T>
concept NamesRefresh = std::derived_from<T, scene::Node> && !std::is_final_v<T> && !requires { &RefreshNameOf<T>::refresh; };
// A node that reads the model itself where the walks cannot see how: its
// refresh overloaded or a template, and no list of its refreshes. The
// static walks call it; these would pass it by, and it would never be
// read -- refused here, not found broken on the screen.
template <class T>
concept RefreshUnseen = NamesRefresh<T> && !IsDerivedNode<T> && !ListsRefreshes<T> &&
                        !requires { typename RefreshesFrom<T>::type; };
template <class T> constexpr const ItselfPart *itself() { return nullptr; }
template <class T>
  requires ListsRefreshes<T>
constexpr const ItselfPart *itself() {
  return &ListedRefreshOf<T>::kPart;
}
template <class T>
  requires(IsDerivedNode<T> && !ListsRefreshes<T>)
constexpr const ItselfPart *itself() {
  return &DerivedOf<T>::kPart;
}
template <class T>
  requires(!IsDerivedNode<T> && !ListsRefreshes<T> && requires { typename RefreshesFrom<T>::type; })
constexpr const ItselfPart *itself() {
  return &OwnRefreshOf<T>::kPart;
}

// What a pressed node's own change changes: a bound node's part; else, a
// value set, the part of its type where the node is.
template <class T, class C> struct OwnValueT {};
template <class T, class C>
  requires IsBound<T>
struct OwnValueT<T, C> {
  using type = typename BoundOf<T>::V;
};
template <class T, class V>
  requires(!IsBound<T>)
struct OwnValueT<T, model::SetTo<V>> {
  using type = V;
};
template <class T> struct PressOf {
  static std::optional<mrt::Want> wanted() {
    if constexpr (IsBound<T>)
      return BoundOf<T>::kPart.fWant;
    else
      return std::nullopt;
  }
  template <class A> static void own(Answers &out, const A &answer) { answers::add(out, answer); }
  template <class A> static void own(Answers &out, const std::optional<A> &answer) {
    if (answer)
      own(out, *answer);
  }
  template <class C> static void own(Answers &out, const Own<C> &answer) {
    if constexpr (requires { typename OwnValueT<T, C>::type; })
      out.emplace_back(OwnEdit{mrt::Change::of<typename OwnValueT<T, C>::type, C>(answer.fChange), wanted()});
    else
      out.emplace_back(Missed{});
  }
  static void press(void *n, Answers &out) {
    T &node = *static_cast<T *>(n);
    if constexpr (detail::PressesNothing<T>)
      node.onPress();
    else if constexpr (detail::Presses<T>)
      own(out, node.onPress());
  }
  static bool kept(void *n, const scene::HostWork::KeptAnswer &answer, Answers &out) {
    (void)n;
    if constexpr (requires { typename T::Answer; }) {
      if (answer.type != &scene::kTypeKey<typename T::Answer>)
        return false;
      own(out, *static_cast<const typename T::Answer *>(answer.answer.get()));
      return true;
    } else {
      return false;
    }
  }
  static constexpr PressPart kPart{&press, &kept};
};
template <class T>
concept AnswersPress = detail::Presses<T> || detail::PressesNothing<T> || requires { typename T::Answer; };
template <class T> constexpr const PressPart *pressed() { return nullptr; }
template <class T>
  requires AnswersPress<T>
constexpr const PressPart *pressed() {
  return &PressOf<T>::kPart;
}

template <class E, class To> void pipeTakes(void *to, const void *e, const Here &, Answers &) {
  // What the end's on() answers is its own: nothing goes on from it.
  (void)static_cast<To *>(to)->on(*static_cast<const E *>(e));
}
template <class To, class Events> struct PipeHandlers;
template <class To, class... E> struct PipeHandlers<To, model::Types<E...>> {
  static constexpr std::array<HandlerEntry, sizeof...(E)> kEntries{HandlerEntry{mrt::keyOf<E>(), &pipeTakes<E, To>}...};
};
template <class T> struct PipeOf {
  using From = std::remove_cvref_t<decltype(std::declval<T &>().fPipe.from)>;
  using To = std::remove_cvref_t<decltype(std::declval<T &>().fPipe.to)>;
  using On = PipeHandlers<To, takes::OnOwnEvents<To>>;
  static constexpr PipePart kPart{+[](void *n) -> void * { return std::addressof(static_cast<T *>(n)->fPipe.from); },
                                  +[](void *n) -> void * { return std::addressof(static_cast<T *>(n)->fPipe.to); },
                                  On::kEntries.data(), On::kEntries.size(), local<To>()};
};
template <class T>
concept IsPipeNode = !IsEach<T> && requires(T &n) {
  n.fPipe.from;
  n.fPipe.to;
};
template <class T> constexpr const PipePart *piped() { return nullptr; }
template <class T>
  requires IsPipeNode<T>
constexpr const PipePart *piped() {
  return &PipeOf<T>::kPart;
}

template <class T> struct TableOf {
  static_assert(!RefreshUnseen<T>,
                "skiff::bind: this node's refresh() is overloaded or a template, which the runtime walks cannot "
                "call by themselves: list what it reads from as a member, fRefreshes = refreshes(refreshFrom<...>(), ...)");
  static constexpr Table value{bound<T>(), scoped<T>(), local<T>(), each<T>(), itself<T>(), pressed<T>(), piped<T>()};
};
template <class T>
inline constexpr const Table &kTable = TableOf<T>::value;
} // namespace made

// ---- the walk ------------------------------------------------------------------------

// Where the walk is: the model's place, and the frames around it -- the
// Locals, innermost first.
struct LocalFrame {
  AnyModel fModel;
  bool fHidden;
  const LocalFrame *fOuter;
};

// Every bound node below a node shown as its model now is -- or, given the
// places that changed, only those on their way -- as the static walks show
// them.
class Refresh {
public:
  explicit Refresh(AnyModel model, const std::vector<mrt::Place> *changed = nullptr)
      : fModel(model), fChanged(changed) {}

  void walk(scene::AnyNodeRef node) {
    if (tracing())
      std::println(std::cerr, "[bind] refresh {} over {}", fChanged ? "targeted" : "whole", node.type().name());
    this->node(node, mrt::Place{}, nullptr);
  }

private:
  AnyModel fModel;
  const std::vector<mrt::Place> *fChanged;
  // The way to what is wanted below a shape, found once for each.
  std::map<std::pair<const mrt::Shape *, std::pair<mrt::TypeKey, std::optional<std::size_t>>>, std::optional<mrt::Steps>>
      fWays;
  std::map<std::pair<mrt::TypeKey, std::optional<std::size_t>>, bool> fOwned;

  const std::optional<mrt::Steps> &wayFrom(const mrt::Shape &from, const mrt::Want &want) {
    const auto key = std::pair{&from, std::pair{want.fType, want.fMember}};
    if (const auto found = fWays.find(key); found != fWays.end())
      return found->second;
    return fWays.emplace(key, mrt::pathTo(from, want)).first->second;
  }
  // Whether a part is anywhere in this model: its nodes, not another's.
  bool owns(const mrt::Want &want) {
    const auto key = std::pair{want.fType, want.fMember};
    if (const auto found = fOwned.find(key); found != fOwned.end())
      return found->second;
    return fOwned.emplace(key, !mrt::pathsTo(*fModel.fRoot, want).empty()).first->second;
  }
  bool onTheWay(const mrt::Place &place) const {
    return fChanged == nullptr ||
           std::ranges::any_of(*fChanged, [&](const mrt::Place &changed) { return mrt::related(place, changed); });
  }
  // A place below another: the way to what is wanted, and its keys.
  std::optional<mrt::Place> below(const mrt::Place &here, const mrt::Want &want, std::vector<mrt::AnyKey> keys = {}) {
    const mrt::Shape *at = mrt::shapeAt(*fModel.fRoot, here.fSteps);
    if (at == nullptr)
      return std::nullopt;
    const auto &way = wayFrom(*at, want);
    if (!way)
      return std::nullopt;
    mrt::Place place = here;
    place.fSteps.append_range(*way);
    place.fKeys.append_range(std::move(keys));
    return place;
  }

  void node(scene::AnyNodeRef node, const mrt::Place &here, const LocalFrame *frames) {
    const auto *table = static_cast<const Table *>(node.bindTable());
    if (table == nullptr) {
      children(node, here, frames);
      return;
    }
    // A scope: what is inside it shown below its part -- only where that
    // moved, or a Local's state did, since it was last.
    std::optional<mrt::Place> inner;
    if (table->fScoped != nullptr && owns(table->fScoped->fWithin)) {
      inner = below(here, table->fScoped->fWithin, table->fScoped->fKeys(node.node()));
      if (!inner)
        return;
      if (tracing())
        std::println(std::cerr, "[bind] scope {} entered", node.type().name());
      if (fChanged == nullptr) {
        const auto seen = fModel.look(*inner);
        const model::Revision now = seen ? seen.fRevision : fModel.revision();
        auto &shown = table->fScoped->fShown(node.node());
        auto &was = table->fScoped->fSeen(node.node());
        auto &epoch = table->fScoped->fEpoch(node.node());
        if (shown && was == now && epoch == localEpoch())
          return;
        shown = true;
        was = now;
        epoch = localEpoch();
      } else if (!onTheWay(*inner)) {
        return;
      }
    }
    const mrt::Place &place = inner ? *inner : here;
    // A Local: a frame of its own, its state's parts found in it.
    std::optional<LocalFrame> local;
    if (table->fLocal != nullptr) {
      if (fChanged == nullptr)
        table->fLocal->fForget(node.node());
      local.emplace(LocalFrame{table->fLocal->fModel(node.node()), table->fLocal->fHidden, frames});
    }
    const LocalFrame *around = local ? &*local : frames;
    if (table->fBound != nullptr)
      bound(node, *table->fBound, place, around);
    if (table->fItself != nullptr)
      table->fItself->fRefresh(node.node(), fModel);
    if (table->fEach != nullptr && owns(table->fEach->fList)) {
      rows(node, *table->fEach, place, around);
      return;
    }
    children(node, place, around);
  }

  void children(scene::AnyNodeRef node, const mrt::Place &here, const LocalFrame *frames) {
    node.forEachChild([&](scene::AnyNodeRef &child) { this->node(child, here, frames); });
  }

  // A part shown: read again where it moved; told where it is gone. From the
  // innermost Local whose state has it, else from the model.
  void bound(scene::AnyNodeRef node, const BoundPart &part, const mrt::Place &here, const LocalFrame *frames) {
    for (const LocalFrame *frame = frames; frame != nullptr; frame = frame->fOuter) {
      if (const auto way = mrt::pathTo(*frame->fModel.fRoot, part.fWant)) {
        // A Local's: shown by the walk over the whole tree only.
        if (fChanged == nullptr)
          show(node, part, frame->fModel.look(mrt::Place{*way, {}}));
        return;
      }
      if (frame->fHidden)
        return;  // nothing outside a hidden state is seen from within it
    }
    if (!owns(part.fWant))
      return;
    const auto place = below(here, part.fWant);
    if (!place)
      return;
    if (fChanged == nullptr || onTheWay(*place))
      show(node, part, fModel.look(*place));
  }
  void show(scene::AnyNodeRef node, const BoundPart &part, const mrt::Seen &seen) {
    void *n = node.node();
    if (tracing())
      std::println(std::cerr, "[bind] bound {} @{} {} at {} (seen {}, shown {})", node.type().name(), n, seen ? "seen" : "gone",
                   seen.fRevision, part.fSeen(n), part.fShown(n));
    if (!seen || seen.fShape->fType != part.fValue) {
      if (!std::exchange(part.fIsGone(n), true))
        part.fGone(n);
      return;
    }
    if (part.fShown(n) && !part.fIsGone(n) && part.fSeen(n) == seen.fRevision && part.fSource(n) == seen.fValue)
      return;
    part.fSeen(n) = seen.fRevision;
    part.fSource(n) = seen.fValue;
    part.fShown(n) = true;
    part.fIsGone(n) = false;
    if (!seen.fTracked)
      ++untrackedReads();
    if (tracing())
      std::println(std::cerr, "[bind] read @{}", n);
    part.fRead(n, seen.fValue);
  }

  // A list's rows: made again to be its elements, each a scope of its
  // element -- walked where they are on a change's way.
  void rows(scene::AnyNodeRef node, const EachPart &part, const mrt::Place &here, const LocalFrame *frames) {
    const auto place = below(here, part.fList);
    if (!place)
      return;
    if (!onTheWay(*place))
      return;
    void *n = node.node();
    const auto seen = fModel.look(*place);
    if (!seen)
      part.fClear(n);
    else
      part.fRows(n, seen.fValue, seen.fRevision);
    for (std::size_t i = 0; i < part.fCount(n); ++i) {
      mrt::Place row = *place;
      row.fSteps.push_back(mrt::step::at{});
      row.fKeys.push_back(part.fKeyAt(n, i));
      if (onTheWay(row))
        this->node(part.fRow(n, i), row, frames);
    }
  }
};

// ---- requests: applied by the parts that take them -------------------------------------

// A program's parts, as the last of the frames around a press: an event no
// frame nor the model's reactions take goes to the first part whose apply()
// takes its type (takes::Applied). Statically, in a release build -- this
// is the press's sink there -- and through a table made from the same
// parts at run time outside one.
template <class... P> struct Parts {
  std::tuple<P *...> fParts;
  template <class E>
    requires(takes::kIn<E, takes::Applied<P>> || ...)
  void take(const E &event) const {
    (void)((takes::kIn<E, takes::Applied<P>> && (std::get<P *>(fParts)->apply(event), true)) || ...);
  }
};
template <class... P> Parts<P...> parts(P &...each) { return {{std::addressof(each)...}}; }

// One part's apply() of one request type, erased.
struct Served {
  mrt::TypeKey fType;
  void *fOwner;
  void (*fTake)(void *, const void *);
};
template <class Part, class... R> void servedOne(std::vector<Served> &out, Part *part, model::Types<R...>) {
  (out.push_back(Served{mrt::keyOf<R>(), part, +[](void *owner, const void *request) {
     static_cast<Part *>(owner)->apply(*static_cast<const R *>(request));
   }}),
   ...);
}
// The table of a press's parts; none for a sink of another kind.
template <class... P> std::vector<Served> servedBy(const Parts<P...> *parts) {
  std::vector<Served> out;
  if (parts != nullptr)
    std::apply([&](auto *...each) { (servedOne(out, each, takes::Applied<std::remove_pointer_t<decltype(each)>>{}), ...); },
               parts->fParts);
  return out;
}
template <class S, class... E> void takenBy(std::vector<Served> &out, S *sink, model::Types<E...>) {
  (out.push_back(Served{mrt::keyOf<E>(), sink, +[](void *owner, const void *event) {
     static_cast<S *>(owner)->take(*static_cast<const E *>(event));
   }}),
   ...);
}
// A sink that says itself what it takes, and how (servedTable()): a program
// whose requests go to its parts by its own routing.
template <class S>
  requires requires(S &sink) {
    { sink.servedTable() } -> std::convertible_to<std::vector<Served>>;
  }
std::vector<Served> servedBy(S *sink) {
  return sink != nullptr ? sink->servedTable() : std::vector<Served>{};
}
// Any other sink: by what its take() takes.
template <class S> std::vector<Served> servedBy(S *sink) {
  std::vector<Served> out;
  if (sink != nullptr)
    takenBy(out, sink, takes::Taken<S>{});
  return out;
}

// ---- a press, at run time ---------------------------------------------------------------

// The frames around a pressed node, innermost first: a scope's place and
// handlers, a Local's state and handlers.
struct ScopeAt {
  mrt::Place fPlace;
  std::span<const HandlerEntry> fOn;
  void *fHandlers;
};
struct LocalAt {
  AnyModel fModel;
  std::span<const HandlerEntry> fOn;
  void *fNode;
  bool fHidden;
};
// A pipe's second end, where what its first end sends goes first.
struct PipeAt {
  std::span<const HandlerEntry> fOn;
  void *fTo;
  const LocalPart *fLocal;
};
struct PressFrame {
  std::variant<ScopeAt, LocalAt, PipeAt> fKind;
  const PressFrame *fOuter;
};

// A press, or a handler's kept answer, delivered down its path to its node,
// and what it answers done there and sent up the frames: as the static
// press does it.
class Pressing {
public:
  Pressing(AnyModel model, std::vector<Served> parts) : fModel(model), fParts(std::move(parts)) {}
  [[nodiscard]] bool reached() const { return fReached; }
  [[nodiscard]] bool missed() const { return fMissed; }

  void press(scene::AnyNodeRef root, const scene::Path &path) {
    descend(root, path, 0, mrt::Place{}, nullptr, [&](scene::AnyNodeRef node, const mrt::Place &here, const PressFrame *frames) {
      const auto *table = static_cast<const Table *>(node.bindTable());
      if (table == nullptr || table->fPress == nullptr)
        return;
      Answers out;
      table->fPress->fPress(node.node(), out);
      done(out, here, frames);
    });
  }
  void answer(scene::AnyNodeRef root, const scene::HostWork::KeptAnswer &kept) {
    descend(root, kept.path, 0, mrt::Place{}, nullptr, [&](scene::AnyNodeRef node, const mrt::Place &here, const PressFrame *frames) {
      const auto *table = static_cast<const Table *>(node.bindTable());
      if (table == nullptr || table->fPress == nullptr)
        return;
      Answers out;
      if (table->fPress->fKept(node.node(), kept, out))
        done(out, here, frames);
    });
  }

private:
  AnyModel fModel;
  std::vector<Served> fParts;
  bool fReached = false;
  bool fMissed = false;

  // A part's way below a place, where it is in this model once.
  std::optional<mrt::Place> below(const mrt::Place &here, const mrt::Want &want, std::vector<mrt::AnyKey> keys = {}) const {
    const mrt::Shape *at = mrt::shapeAt(*fModel.fRoot, here.fSteps);
    if (at == nullptr)
      return std::nullopt;
    const auto way = mrt::pathTo(*at, want);
    if (!way)
      return std::nullopt;
    mrt::Place place = here;
    place.fSteps.append_range(*way);
    place.fKeys.append_range(std::move(keys));
    return place;
  }
  bool owns(const mrt::Want &want) const { return !mrt::pathsTo(*fModel.fRoot, want).empty(); }

  template <class End>
  void descend(scene::AnyNodeRef node, const scene::Path &path, std::size_t at, const mrt::Place &here, const PressFrame *frames,
               const End &end) {
    const auto *table = static_cast<const Table *>(node.bindTable());
    std::optional<mrt::Place> inner;
    std::optional<PressFrame> scope;
    if (table != nullptr && table->fScoped != nullptr && owns(table->fScoped->fWithin)) {
      inner = below(here, table->fScoped->fWithin, table->fScoped->fKeys(node.node()));
      if (!inner)
        return;
      // (Its handlers reached through the node, which holds them.)
      scope.emplace(PressFrame{ScopeAt{*inner, {table->fScoped->fOn, table->fScoped->fOnCount}, node.node()}, frames});
    }
    const mrt::Place &place = inner ? *inner : here;
    const PressFrame *around = scope ? &*scope : frames;
    std::optional<PressFrame> local;
    if (table != nullptr && table->fLocal != nullptr) {
      local.emplace(PressFrame{LocalAt{table->fLocal->fModel(node.node()), {table->fLocal->fOn, table->fLocal->fOnCount},
                                       node.node(), table->fLocal->fHidden},
                               around});
      around = &*local;
    }
    if (at == path.size()) {
      fReached = true;
      end(node, place, around);
      return;
    }
    if (table != nullptr && table->fEach != nullptr && owns(table->fEach->fList)) {
      const std::size_t i = path[at];
      if (i >= table->fEach->fCount(node.node()))
        return;
      auto list = below(place, table->fEach->fList);
      if (!list)
        return;
      list->fSteps.push_back(mrt::step::at{});
      list->fKeys.push_back(table->fEach->fKeyAt(node.node(), i));
      descend(table->fEach->fRow(node.node(), i), path, at + 1, *list, around, end);
      return;
    }
    std::optional<PressFrame> pipe;
    if (table != nullptr && table->fPipe != nullptr)
      pipe.emplace(PressFrame{PipeAt{{table->fPipe->fOn, table->fPipe->fOnCount}, table->fPipe->fTo(node.node()), table->fPipe->fLocal},
                              around});
    struct Context {
      Pressing *fSelf;
      const scene::Path *fPath;
      std::size_t fAt;
      const mrt::Place *fPlace;
      const PressFrame *fFrames;
      const PressFrame *fPiped;  // its first end's frames: the pipe's
      void *fFrom;
      const End *fEnd;
    } context{this, &path, at, &place, around, pipe ? &*pipe : nullptr,
              pipe ? table->fPipe->fFrom(node.node()) : nullptr, &end};
    (void)node.ops().fChildAt(node.node(), path[at], &context, +[](void *c, scene::AnyNodeRef &child) {
      auto &go = *static_cast<Context *>(c);
      const bool first = go.fPiped != nullptr && child.node() == go.fFrom;
      go.fSelf->descend(child, *go.fPath, go.fAt + 1, *go.fPlace, first ? go.fPiped : go.fFrames, *go.fEnd);
    });
  }

  // What was answered, done: own changes where the part is, events up the
  // frames, edits where their part is found.
  void done(const Answers &out, const mrt::Place &here, const PressFrame *frames) {
    for (const Answer &each : out)
      std::visit([&](const auto &one) { this->take(one, here, frames); }, each);
  }
  void take(const OwnEdit &edit, const mrt::Place &here, const PressFrame *frames) {
    if (!edit.fWant) {
      if (!fModel.apply(here, edit.fChange))
        fMissed = true;
      return;
    }
    // A Local's state, where it has the part.
    for (const PressFrame *frame = frames; frame != nullptr; frame = frame->fOuter)
      if (std::visit([&](const auto &one) { return this->ownInLocal(one, edit); }, frame->fKind))
        return;
    const auto place = owns(*edit.fWant) ? below(here, *edit.fWant) : std::nullopt;
    if (!place || !fModel.apply(*place, edit.fChange))
      fMissed = true;
  }
  bool intoLocalEnd(const ScopeAt &, const Sent &, const PressFrame *) { return false; }
  bool intoLocalEnd(const LocalAt &, const Sent &, const PressFrame *) { return false; }
  bool intoLocalEnd(const PipeAt &pipe, const Sent &event, const PressFrame *frame) {
    if (pipe.fLocal == nullptr)
      return false;
    const PressFrame local{LocalAt{pipe.fLocal->fModel(pipe.fTo), {pipe.fLocal->fOn, pipe.fLocal->fOnCount}, pipe.fTo,
                                   pipe.fLocal->fHidden},
                           frame->fOuter};
    route(event, &local);
    return true;
  }
  bool ownInLocal(const PipeAt &, const OwnEdit &) { return false; }
  bool ownInLocal(const ScopeAt &, const OwnEdit &) { return false; }
  bool ownInLocal(const LocalAt &local, const OwnEdit &edit) {
    const auto way = mrt::pathTo(*local.fModel.fRoot, *edit.fWant);
    if (!way)
      return false;
    (void)local.fModel.apply(mrt::Place{*way, {}}, edit.fChange);
    ++localEpoch();
    return true;
  }
  void take(const Sent &event, const mrt::Place &, const PressFrame *frames) { route(event, frames); }
  void take(const UpSent &up, const mrt::Place &, const PressFrame *frames) { route(up.fEvent, frames); }
  void take(const OverEdit &over, const mrt::Place &, const PressFrame *frames) { edit(over, frames); }
  void take(Missed, const mrt::Place &, const PressFrame *) { fMissed = true; }

  // An event sent from within frames: taken by the first with a handler for
  // it, whose answer is done there; else applied by the part that serves
  // its type.
  void route(const Sent &event, const PressFrame *frames) {
    for (const PressFrame *frame = frames; frame != nullptr; frame = frame->fOuter) {
      // A pipe whose second end is a Local: taken there, as a frame of its
      // own around what is outside the pipe.
      if (std::visit([&](const auto &one) { return this->intoLocalEnd(one, event, frame); }, frame->fKind))
        return;
      const auto [entries, held, where] = std::visit(
          [](const auto &one) {
            return std::tuple<std::span<const HandlerEntry>, void *, const mrt::Place *>{one.fOn, Pressing::ownerOf(one),
                                                                                       Pressing::placeOf(one)};
          },
          frame->fKind);
      if (const HandlerEntry *entry = entryFor(entries, event.fType)) {
        static const mrt::Place nowhere{};
        Answers out;
        entry->fTake(held, event.fEvent.get(), Here{where != nullptr ? *where : nowhere}, out);
        answered(out, frame);
        return;
      }
    }
    // The model's reactions, then the parts.
    if (fModel.react(event.fType, event.fEvent.get()))
      return;
    const auto found = std::ranges::find(fParts, event.fType, &Served::fType);
    if (found == fParts.end()) {
      std::println(std::cerr, "[skiff::bind] nothing takes an event of this type: no frame around it, nor a part applying it");
      return;
    }
    found->fTake(found->fOwner, event.fEvent.get());
  }
  static void *ownerOf(const PipeAt &pipe) { return pipe.fTo; }
  static const mrt::Place *placeOf(const PipeAt &) { return nullptr; }
  static void *ownerOf(const ScopeAt &scope) { return scope.fHandlers; }
  static void *ownerOf(const LocalAt &local) { return local.fNode; }
  static const mrt::Place *placeOf(const ScopeAt &scope) { return &scope.fPlace; }
  static const mrt::Place *placeOf(const LocalAt &) { return nullptr; }

  // A handler's answer, done at its frame: events passed up from the frames
  // above it, edits where their part is found from it.
  void answered(const Answers &out, const PressFrame *frame) {
    for (const Answer &each : out)
      std::visit([&](const auto &one) { this->answeredOne(one, frame); }, each);
  }
  void answeredOne(const Sent &event, const PressFrame *frame) { route(event, frame->fOuter); }
  void answeredOne(const UpSent &up, const PressFrame *frame) { route(up.fEvent, frame->fOuter); }
  void answeredOne(const OverEdit &over, const PressFrame *frame) { edit(over, frame); }
  void answeredOne(const OwnEdit &, const PressFrame *) {}
  void answeredOne(Missed, const PressFrame *) {}
  // An edit of the one Want below the innermost frame where it is found
  // once; else in the whole model.
  void edit(const OverEdit &over, const PressFrame *frames) {
    for (const PressFrame *frame = frames; frame != nullptr; frame = frame->fOuter)
      if (std::visit([&](const auto &one) { return this->editAt(one, over); }, frame->fKind))
        return;
    if (auto place = below(mrt::Place{}, over.fWant, over.fKeys))
      (void)fModel.apply(*place, over.fChange);
  }
  // Made at a frame, where its part is there: whether the search stops.
  bool editAt(const PipeAt &, const OverEdit &) { return false; }
  bool editAt(const ScopeAt &scope, const OverEdit &over) {
    auto place = below(scope.fPlace, over.fWant, over.fKeys);
    if (!place)
      return false;
    (void)fModel.apply(*place, over.fChange);
    return true;
  }
  bool editAt(const LocalAt &local, const OverEdit &over) {
    if (const auto way = mrt::pathTo(*local.fModel.fRoot, over.fWant)) {
      (void)local.fModel.apply(mrt::Place{*way, over.fKeys}, over.fChange);
      ++localEpoch();
      return true;
    }
    if (local.fHidden)
      std::println(std::cerr, "[skiff::bind] a hidden state's handler edits only that state, and this part is not in it");
    return local.fHidden;
  }
};

} // namespace skiff::bind::rt

// The scene's hook: a node's binding table, where it binds anything -- a
// partial specialization of the scene's BindTableOf, which counts wherever
// this module is reachable.
export namespace skiff::scene {
template <class T>
  requires(bind::IsBound<T> || bind::IsScoped<T> || bind::IsLocal<T> || bind::IsEach<T> ||
           bind::rt::made::IsDerivedNode<T> || requires { typename bind::rt::made::RefreshesFrom<T>::type; } ||
           bind::rt::made::AnswersPress<T> || bind::rt::made::IsPipeNode<T> || bind::rt::made::ListsRefreshes<T> ||
           bind::rt::made::RefreshUnseen<T>)
struct BindTableOf<T> {
  static constexpr const void *value = &bind::rt::made::kTable<T>;
};
} // namespace skiff::scene
