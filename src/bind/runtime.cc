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

export namespace skiff::bind::rt {
namespace mrt = model::rt;

// ---- a model, erased ------------------------------------------------------------

// What the walks ask of a model: its shape, a part at a place, its revision.
struct AnyModel {
  const void *fModel = nullptr;
  const mrt::Shape *fRoot = nullptr;
  mrt::TypeKey fType = nullptr;     // the model's own type
  const void *fRootValue = nullptr;  // its root, as it is now
  mrt::Seen (*fLook)(const void *, const mrt::Place &) = nullptr;
  model::Revision (*fRevision)(const void *) = nullptr;
  template <class M> static AnyModel of(const M &m) {
    return {std::addressof(m), &mrt::shapeOf<typename M::RootType>(), mrt::keyOf<M>(), std::addressof(m.root()),
            +[](const void *self, const mrt::Place &place) { return static_cast<const M *>(self)->lookAt(place); },
            +[](const void *self) { return static_cast<const M *>(self)->revision(); }};
  }
  [[nodiscard]] mrt::Seen look(const mrt::Place &place) const { return fLook(fModel, place); }
  [[nodiscard]] model::Revision revision() const { return fRevision(fModel); }
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
  bool &(*fShown)(void *);
  bool &(*fIsGone)(void *);
};
// A scope: the part it is a scope of, the keys of the way there, what it
// last saw.
struct ScopedPart {
  mrt::Want fWithin;
  std::vector<mrt::AnyKey> (*fKeys)(void *);
  model::Revision &(*fSeen)(void *);
  std::uint64_t &(*fEpoch)(void *);
  bool &(*fShown)(void *);
};
// A Local: its state's model, and whether nothing outside it is seen from
// within.
struct LocalPart {
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
// All a node binds; most bind nothing and have no table.
struct Table {
  const BoundPart *fBound = nullptr;
  const ScopedPart *fScoped = nullptr;
  const LocalPart *fLocal = nullptr;
  const EachPart *fEach = nullptr;
  const ItselfPart *fItself = nullptr;
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
      +[](void *n, const void *value) { detail::read(*static_cast<T *>(n), *static_cast<const V *>(value)); },
      +[](void *n) { detail::gone(*static_cast<T *>(n)); },
      +[](void *n) -> model::Revision & { return detail::asBound(*static_cast<T *>(n)).fSeen; },
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
  static constexpr ScopedPart kPart{
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
  static constexpr LocalPart kPart{
      +[](void *n) { return AnyModel::of(detail::asLocal(*static_cast<T *>(n)).fModel); },
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
  // The rows made to be the list's elements, in its order: a row kept for
  // a key still there, made for one new to it, gone with one gone -- as the
  // static refresh makes them.
  static void rows(void *n, const void *seenList, model::Revision revision) {
    auto &list = detail::asEach(*static_cast<T *>(n));
    const List &seen = *static_cast<const List *>(seenList);
    if (list.fShown && list.fSeen == revision)
      return;
    list.fSeen = revision;
    list.fShown = true;
    const bool same = seen.size() == list.fKeys.size() &&
                      std::ranges::equal(seen.keys(), list.fKeys);
    if (same)
      return;
    using Row = typename decltype(list.fRows)::value_type;
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

template <class T> struct DerivedOf {
  using D = typename decltype(derivedOf(std::declval<const T &>()))::type;
  template <class C, class B, class... R> static void refresh(Derived<C, B, R...> &node, const AnyModel &model) {
    // Its parts, each where it is in this model -- not this model's where
    // any is not in it -- and their newest revision.
    const std::array<std::optional<mrt::Steps>, sizeof...(R)> ways{mrt::pathTo(*model.fRoot, mrt::wantOf<R>())...};
    if (std::ranges::any_of(ways, [](const auto &way) { return !way.has_value(); }))
      return;
    std::array<mrt::Seen, sizeof...(R)> seen{};
    std::ranges::transform(ways, seen.begin(), [&](const auto &way) { return model.look(mrt::Place{*way, {}}); });
    if (std::ranges::any_of(seen, [](const mrt::Seen &one) { return !one; }))
      return;
    const model::Revision now = std::ranges::fold_left(
        seen, model::Revision{0}, [](model::Revision most, const mrt::Seen &one) { return std::max(most, one.fRevision); });
    if (node.fShown && now == node.fSeen)
      return;
    node.fSeen = now;
    node.fShown = true;
    using Reads = std::tuple<ValueOf<R>...>;
    if constexpr (requires(B &b, const C &c) { b.read(c(std::declval<const ValueOf<R> &>()...)); })
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        node.read(node.fCompute(*static_cast<const std::tuple_element_t<I, Reads> *>(seen[I].fValue)...));
      }(std::index_sequence_for<R...>{});
    else
      fromRoot(node, model);
  }
  // Computed from the whole root: where the compute names its root's type,
  // and this model's root is of it.
  template <class C, class B, class... R> static void fromRoot(Derived<C, B, R...> &node, const AnyModel &model) {
    if constexpr (requires { typename ArgumentOf<C>::type; }) {
      using Root = typename ArgumentOf<C>::type;
      if (model.fRoot->fType == mrt::keyOf<Root>())
        node.read(node.fCompute(*static_cast<const Root *>(model.fRootValue)));
    }
  }
  static constexpr ItselfPart kPart{+[](void *n, const AnyModel &model) {
    refresh(static_cast<D &>(*static_cast<T *>(n)), model);
  }};
};
template <class T> struct OwnRefreshOf {
  using M = typename RefreshesFrom<T>::type;
  static constexpr ItselfPart kPart{+[](void *n, const AnyModel &model) {
    if (model.fType == mrt::keyOf<M>())
      static_cast<T *>(n)->refresh(*static_cast<const M *>(model.fModel));
  }};
};
template <class T> constexpr const ItselfPart *itself() { return nullptr; }
template <class T>
  requires IsDerivedNode<T>
constexpr const ItselfPart *itself() {
  return &DerivedOf<T>::kPart;
}
template <class T>
  requires(!IsDerivedNode<T> && requires { typename RefreshesFrom<T>::type; })
constexpr const ItselfPart *itself() {
  return &OwnRefreshOf<T>::kPart;
}

template <class T> inline constexpr Table kTable{bound<T>(), scoped<T>(), local<T>(), each<T>(), itself<T>()};
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

  void walk(scene::AnyNodeRef node) { this->node(node, mrt::Place{}, nullptr); }

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
    if (!seen || seen.fShape->fType != part.fValue) {
      if (!std::exchange(part.fIsGone(n), true))
        part.fGone(n);
      return;
    }
    if (part.fShown(n) && !part.fIsGone(n) && part.fSeen(n) == seen.fRevision)
      return;
    part.fSeen(n) = seen.fRevision;
    part.fShown(n) = true;
    part.fIsGone(n) = false;
    if (!seen.fTracked)
      ++untrackedReads();
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

} // namespace skiff::bind::rt

// The scene's hook: a node's binding table, where it binds anything.
export namespace skiff::bind {
template <class T>
  requires(IsBound<T> || IsScoped<T> || IsLocal<T> || IsEach<T> || rt::made::IsDerivedNode<T> ||
           requires { typename rt::made::RefreshesFrom<T>::type; })
constexpr const void *skiffBindTable(T *) {
  return &rt::made::kTable<T>;
}
} // namespace skiff::bind
