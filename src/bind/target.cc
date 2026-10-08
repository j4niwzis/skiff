// skiff.bind:target -- targeted refreshes, and the Binding (the module is
// skiff.bind: see bind.cc).
export module skiff.bind:target;

import std;
import splice;
import skiff.aggregate;
import skiff.model;
import skiff.scene;
import :ops;

export namespace skiff::bind {
namespace detail {
// ---- targeting: one change, followed down its path -----------------------

// Path A is where path B begins.
template <class A, class B> struct IsPrefix : std::false_type {};
template <class... B>
struct IsPrefix<model::Path<>, model::Path<B...>> : std::true_type {};
template <class S, class... A, class... B>
struct IsPrefix<model::Path<S, A...>, model::Path<S, B...>>
    : IsPrefix<model::Path<A...>, model::Path<B...>> {};
// A change at one can be seen at the other: one is within the other.
template <class A, class B>
inline constexpr bool kRelated = IsPrefix<A, B>::value || IsPrefix<B, A>::value;

// The keys two related places share -- the first of the shorter's -- the
// same.
template <class A, class B> bool sameKeys(const A &a, const B &b) {
  constexpr std::size_t n = std::min(std::tuple_size_v<A>, std::tuple_size_v<B>);
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return ((std::get<I>(a) == std::get<I>(b)) && ...);
  }(std::make_index_sequence<n>{});
}

// One change -- a tracked place C, with its keys -- shown: only what is on
// its way is gone into. A subtree whose place has nothing to do with C is
// left by its type; a scope or a row of other keys, by them.
template <class M, class C> struct Targeting {
  const M &fModel;
  const model::KeysOf<C> &fKeys;
  Refreshing<M> fFull{fModel};

  template <class N, class Here, class... Frames>
    requires IsBound<N>
  void bound(N &node, const Here &here, const Frames &...frames) {
    showIfOnTheWay(node, here, where<decltype(boundToOf(asBound(node)))>(here, frames...),
                   frames...);
  }
  template <class N, class Here, class... Frames>
  void bound(N &, const Here &, const Frames &...) {}

  template <class N>
    requires ReadsItself<N, M>
  void itself(N &node) {
    node.refresh(fModel);
  }
  template <class N> void itself(N &) {}
  template <class N, class Here, class... Frames>
  void emitted(N &, const Here &, const Frames &...) {}
  template <class N> static constexpr bool kWants = true;
  template <class N, class Here, class... Frames>
  bool local(N &, const Here &, const Frames &...) {
    return true;
  }

  template <class S, class Root, class P, class K>
  bool enter(S &, const model::Place<Root, P, K> &place) {
    if constexpr (!kRelated<P, C>)
      return false;
    else
      return sameKeys(place.fKeys, fKeys);
  }

  // A list: its members changed -- its rows made again, the new ones shown
  // whole; a change in one element -- that one row only; a change above it
  // -- all of it, whole.
  template <class N, class Root, class P, class K, class VisitRow>
  void eachRows(N &node, const model::Place<Root, P, K> &place, const VisitRow &visitRow) {
    auto &list = asEach(node);
    using Key = decltype(keyOf(node));
    using Element = model::Join<P, model::Path<model::At<Key>>>;
    if constexpr (std::same_as<P, C>) {
      if (!sameKeys(place.fKeys, fKeys))
        return;
      const std::vector<Key> had = list.fKeys;
      fFull.rows(node, place);
      for (std::size_t i = 0; i < list.fRows.size(); ++i)
        if (!std::ranges::contains(had, list.fKeys[i]))
          visitRow(fFull, i);
    } else if constexpr (IsPrefix<Element, C>::value) {
      if (!sameKeys(place.fKeys, fKeys))
        return;
      constexpr std::size_t at = std::tuple_size_v<model::KeysOf<P>>;
      if (auto *row = list.rowFor(std::get<at>(fKeys)))
        visitRow(*this, static_cast<std::size_t>(row - list.fRows.data()));
    } else if constexpr (IsPrefix<C, P>::value) {
      if (sameKeys(place.fKeys, fKeys))
        fFull.eachRows(node, place, visitRow);
    }
  }
  template <class N, class Place> void rows(N &, const Place &) {}

private:
  template <class N, class Here, class Root, class P, class K, class... Frames>
  void showIfOnTheWay(N &node, const Here &here,
                      const InModel<model::Place<Root, P, K>> &at, const Frames &...frames) {
    if constexpr (kRelated<P, C>)
      if (sameKeys(at.fPlace.fKeys, fKeys))
        fFull.bound(node, here, frames...);
  }
  // Shown from a Local's state: not this change's.
  template <class N, class Here, class L, class Place, class... Frames>
  void showIfOnTheWay(N &, const Here &, const InLocal<L, Place> &, const Frames &...) {}
};
// One change of one Local's state shown: inside that Local, only what is on
// its way; what shows the model's parts, or another Local's, left.
template <class M, class L, class C> struct LocalTargeting {
  const M &fModel;
  const L *fLocal;
  const model::KeysOf<C> &fKeys;
  Refreshing<M> fFull{fModel};

  template <class N, class Here, class... Frames>
    requires IsBound<N>
  void bound(N &node, const Here &here, const Frames &...frames) {
    showIfOnTheWay(node, here, where<decltype(boundToOf(asBound(node)))>(here, frames...),
                   frames...);
  }
  template <class N, class Here, class... Frames>
  void bound(N &, const Here &, const Frames &...) {}
  template <class N> void itself(N &) {}
  template <class N, class Here, class... Frames>
  void emitted(N &, const Here &, const Frames &...) {}
  template <class N> static constexpr bool kWants = true;
  // Another Local inside: its own changes are its own pass's.
  template <class N, class Here, class... Frames>
  bool local(N &node, const Here &, const Frames &...) {
    return static_cast<const void *>(&asLocal(node)) == static_cast<const void *>(fLocal);
  }
  template <class S, class Place> bool enter(S &, const Place &) { return true; }
  template <class N, class Place> void rows(N &, const Place &) {}
  template <class N, class Place, class VisitRow>
  void eachRows(N &node, const Place &, const VisitRow &visitRow) {
    for (std::size_t i = 0; i < asEach(node).fRows.size(); ++i)
      visitRow(*this, i);
  }

private:
  template <class N, class Here, class Place, class... Frames>
  void showIfOnTheWay(N &, const Here &, const InModel<Place> &, const Frames &...) {}
  template <class N, class Here, class Other, class Root, class P, class K, class... Frames>
  void showIfOnTheWay(N &node, const Here &here,
                      const InLocal<Other, model::Place<Root, P, K>> &at,
                      const Frames &...frames) {
    if constexpr (std::same_as<std::remove_const_t<Other>, std::remove_const_t<L>> &&
                  kRelated<P, C>)
      if (static_cast<const void *>(at.fLocal) == static_cast<const void *>(fLocal) &&
          sameKeys(at.fPlace.fKeys, fKeys))
        fFull.bound(node, here, frames...);
  }
};

// The Locals whose state changed, found by going only where a Local can be
// (by type), each shown along its own changes.
template <class M> struct LocalPass {
  const M &fModel;

  template <class N, class Here, class... Frames>
  void bound(N &, const Here &, const Frames &...) {}
  template <class N> void itself(N &) {}
  template <class N, class Here, class... Frames>
  void emitted(N &, const Here &, const Frames &...) {}
  template <class N> static constexpr bool kWants = kHasLocal<N>;
  template <class S, class Place> bool enter(S &, const Place &) { return true; }
  template <class N, class Place> void rows(N &, const Place &) {}
  template <class N, class Place, class VisitRow>
  void eachRows(N &node, const Place &, const VisitRow &visitRow) {
    for (std::size_t i = 0; i < asEach(node).fRows.size(); ++i)
      visitRow(*this, i);
  }

  template <class N, class Here, class F, class... Frames>
  bool local(N &node, const Here &here, const F &frame, const Frames &...frames) {
    auto &local = asLocal(node);
    using L = std::remove_reference_t<decltype(local)>;
    using T = typename L::LocalOf;
    auto changes = local.fState.takeChanges();
    if (!std::get<0>(changes).empty()) {
      // An edit of nothing tracked in it: the Local shown whole.
      Refreshing<M> full{fModel};
      visitBody(full, node, here, frame, frames...);
    } else {
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        using Places = model::ChangedPlaces<T>;
        (each<typename Places::template at<I>>(node, here, &local, std::get<I>(changes), frame,
                                              frames...),
         ...);
      }(std::make_index_sequence<std::tuple_size_v<decltype(changes)>>{});
    }
    return true;  // and on, into what is inside, for Locals within it
  }

private:
  template <class C, class N, class Here, class L, class Keys, class... Frames>
  void each(N &node, const Here &here, const L *local, const std::vector<Keys> &changed,
            const Frames &...frames) {
    for (const Keys &keys : changed) {
      LocalTargeting<M, L, C> op{fModel, local, keys};
      visitBody(op, node, here, frames...);
    }
  }
};
} // namespace detail

// Every node bound below `node` shown as the model now is: those whose part
// moved read it again, those whose part is gone told so, every Each's rows
// made the list's elements.
template <class M, class N> void refresh(N &node, const M &model) {
  detail::Refreshing<M> op{model};
  detail::visitNode(op, node,
                    model::Place<typename M::RootType, model::Path<>>{});
}

// What the nodes below `node` asked for, done: their changes applied where
// they are bound, their events sent up the frames they are in.
template <class M, class N, class S = detail::NoSink> void drain(N &node, M &model, S *sink = nullptr) {
  // One batch: the reactions told once, at the end, of what the whole drain
  // left -- not once per node's change.
  model.beginBatch();
  detail::Draining<M, S> op{model, sink};
  detail::visitNode(op, node,
                    model::Place<typename M::RootType, model::Path<>>{});
  model.endBatch();
  pendingCount() = 0;
}

// A message given to a component from outside it -- send<Clear>(field),
// Fudgets' high-level input -- taken as from a pipe: its on(), or a
// processor's step.
template <class M, class To, class E> void send(M &model, To &to, const E &event) {
  model.beginBatch();
  detail::Draining<M> op{model};
  op.deliver(to, event);
  model.endBatch();
}

// Both walks, done only where something can have happened.
template <class M> class Binding {
public:
  // The first time, after invalidate(), or where a Local's state moved: the
  // whole tree, walked. Else only what changed: each change the model
  // logged since, followed down its own path -- nothing else is visited.
  template <class N> void refresh(N &root, M &model) {
    if (fShown && fRevision == model.revision() && fEpoch == localEpoch())
      return;
    auto changes = model.takeChanges();
    if (!fShown || !std::get<0>(changes).empty()) {  // an edit of nothing tracked
      bind::refresh(root, model);
    } else {
      targeted(root, model, changes,
               std::make_index_sequence<std::tuple_size_v<decltype(changes)>>{});
      if (fEpoch != localEpoch()) {
        detail::LocalPass<M> op{model};
        detail::visitNode(op, root, model::Place<typename M::RootType, model::Path<>>{});
      }
    }
    fShown = true;
    fRevision = model.revision();
    fEpoch = localEpoch();
  }
  // And the events nothing takes, to the program's sink where it takes them.
  template <class N, class S = detail::NoSink> void drain(N &root, M &model, S *sink = nullptr) {
    if (pendingCount() == 0)
      return;
    bind::drain(root, model, sink);
  }
  // A tree changed by hand -- nodes made or replaced: walked whole next time.
  void invalidate() { fShown = false; }

private:
  template <class N, class Changes, std::size_t... I>
  void targeted(N &root, const M &model, const Changes &changes, std::index_sequence<I...>) {
    using Places = model::ChangedPlaces<typename M::RootType>;
    (targetedAt<typename Places::template at<I>>(root, model, std::get<I>(changes)), ...);
  }
  template <class C, class N, class Keys>
  void targetedAt(N &root, const M &model, const std::vector<Keys> &changed) {
    for (const Keys &keys : changed) {
      detail::Targeting<M, C> op{model, keys};
      detail::visitNode(op, root, model::Place<typename M::RootType, model::Path<>>{});
    }
  }

  model::Revision fRevision = 0;
  std::uint64_t fEpoch = 0;
  bool fShown = false;
};

} // namespace skiff::bind
