// skiff.bind:ops -- refreshing and draining (the module is skiff.bind: see
// bind.cc).
export module skiff.bind:ops;

import std;
import splice;
import skiff.aggregate;
import skiff.model;
import skiff.scene;
import :walk;

export namespace skiff::bind {
namespace detail {
// ---- refreshing: what changed, shown --------------------------------------

template <class M> struct Refreshing {
  const M &fModel;

  template <class N, class Here, class... Frames>
    requires(IsBound<N> && (kOfModel<typename M::RootType, N> || (kInLocal<N, Frames> || ...)))
  void bound(N &node, const Here &here, const Frames &...frames) {
    auto &shown = asBound(node);
    using Want = decltype(boundToOf(shown));
    const auto seen = lookAt(fModel, where<Want>(here, frames...));
    if (!seen) {
      if (!std::exchange(shown.fGone, true))
        gone(node);
      return;
    }
    if (shown.fShown && !shown.fGone && shown.fSeen == seen.fRevision)
      return;
    shown.fSeen = seen.fRevision;
    shown.fShown = true;
    shown.fGone = false;
    if (!seen.fTracked)
      ++untrackedReads();
    read(node, *seen);
  }
  template <class N, class Here, class... Frames>
  void bound(N &, const Here &, const Frames &...) {}

  template <class N>
    requires ReadsItself<N, M>
  void itself(N &node) {
    node.refresh(fModel);
  }
  template <class N> void itself(N &) {}
  template <class N> static constexpr bool kWants = true;
  // Its model's nodes, not another model's in the same tree.
  template <class N> static constexpr bool kOwns = kOfModel<typename M::RootType, N>;
  // A Local walked whole: what its state logged is shown by this walk.
  template <class N, class Here, class... Frames>
  bool local(N &node, const Here &, const Frames &...) {
    (void)asLocal(node).fModel.takeChanges();
    return true;
  }

  // A scope's subtree walked only where its part moved, or a Local's state
  // did, since it was last.
  template <class S, class Place> bool enter(S &scoped, const Place &place) {
    const auto seen = fModel.look(place);
    const model::Revision now = seen ? seen.fRevision : fModel.revision();
    if (scoped.fShown && scoped.fSeen == now && scoped.fEpoch == localEpoch())
      return false;
    scoped.fShown = true;
    scoped.fSeen = now;
    scoped.fEpoch = localEpoch();
    return true;
  }

  // The rows made to be the list's elements, in its order: a row kept for
  // a key still there, made for a key new to it, gone with a key gone.
  template <class N, class Place> void rows(N &node, const Place &place) {
    auto &list = asEach(node);
    const auto seen = fModel.look(place);
    if (!seen) {
      if (!list.fRows.empty()) {
        list.fRows.clear();
        list.fKeys.clear();
        list.invalidateLayout();
      }
      return;
    }
    if (list.fShown && list.fSeen == seen.fRevision)
      return;
    list.fSeen = seen.fRevision;
    list.fShown = true;
    using Row = typename decltype(list.fRows)::value_type;
    using Key = typename decltype(list.fKeys)::value_type;
    bool same = seen->size() == list.fKeys.size();
    for (std::size_t i = 0; same && i < seen->size(); ++i)
      same = list.fKeys[i] == seen->keyAt(i);
    if (same)
      return;  // the elements changed, not which: the rows read them
    std::vector<Key> keys;
    std::vector<Row> rows;
    keys.reserve(seen->size());
    rows.reserve(seen->size());
    for (std::size_t i = 0; i < seen->size(); ++i) {
      const auto &key = seen->keyAt(i);
      keys.push_back(key);
      const auto was = std::ranges::find(list.fKeys, key);
      if (was != list.fKeys.end())
        rows.push_back(std::move(
            list.fRows[static_cast<std::size_t>(was - list.fKeys.begin())]));
      else
        rows.push_back(list.fMake(seen->valueAt(i)));
    }
    list.fKeys = std::move(keys);
    list.fRows = std::move(rows);
    list.indexRows();
    list.invalidateLayout();
  }
  template <class N, class Place, class VisitRow>
  void eachRows(N &node, const Place &place, const VisitRow &visitRow) {
    rows(node, place);
    for (std::size_t i = 0; i < asEach(node).fRows.size(); ++i)
      visitRow(*this, i);
  }
};

// ---- draining: what the nodes asked for, done ------------------------------

// Where the events nothing in the tree nor the model takes go: nowhere, by
// default -- one sent there does not compile.
struct NoSink {};

template <class M, class Sink = NoSink> struct Draining {
  M &fModel;
  Sink *fSink = nullptr;

  template <class N, class Here, class... Frames>
  void bound(N &, const Here &, const Frames &...) {}

  template <class N> void itself(N &) {}

  template <class S, class Place> bool enter(S &, const Place &) {
    return true;
  }
  template <class N> static constexpr bool kWants = true;
  // Its model's nodes, not another model's in the same tree.
  template <class N> static constexpr bool kOwns = kOfModel<typename M::RootType, N>;
  template <class N, class Here, class... Frames>
  bool local(N &, const Here &, const Frames &...) {
    return true;
  }
  template <class N, class Place> void rows(N &, const Place &) {}
  template <class N, class Place, class VisitRow>
  void eachRows(N &node, const Place &, const VisitRow &visitRow) {
    for (std::size_t i = 0; i < asEach(node).fRows.size(); ++i)
      visitRow(*this, i);
  }

  // An event sent from within frames: taken by the first that takes it.
  template <class... E, class... Frames>
  void send(const spl::variant<E...> &one, const Frames &...frames) {
    spl::visit([&](const auto &event) { send(event, frames...); }, one);
  }
  template <class E, class... Frames>
  void send(const E &event, const Frames &...frames) {
    route(event, frames...);
  }

  // One nothing it was sent within takes, given to the model, whose
  // reactions take it where they say so: an effect for the program. Else,
  // to the program's sink -- the type it drains with -- where it takes it.
  template <class E>
    requires requires(const typename M::ReactionsType &r, const E &e) { r.on(e); }
  void route(const E &event) {
    fModel.send(event);
  }
  template <class E>
    requires(!requires(const typename M::ReactionsType &r, const E &e) { r.on(e); } &&
             requires(Sink &s, const E &e) { s.take(e); })
  void route(const E &event) {
    fSink->take(event);
  }
  template <class E> void route(const E &) {
    static_assert(false, "skiff::bind: nothing this event was sent within -- "
                         "no pipe, no local handler, no scope -- takes it");
  }
  template <class E, class F, class... Rest>
    requires kTakes<F, E>
  void route(const E &event, const F &frame, const Rest &...rest) {
    take(event, frame, rest...);
  }
  template <class E, class F, class... Rest>
  void route(const E &event, const F &, const Rest &...rest) {
    route(event, rest...);
  }

protected:
  // Changes made where the part is found.
  template <class At> void change(const At &, const model::Nothing &) {}
  template <class At, class... C>
  void change(const At &at, const spl::variant<C...> &one) {
    spl::visit([&](const auto &each) { change(at, each); }, one);
  }
  template <class At, class C> void change(const At &at, const C &one) {
    changeAt(at, one);
  }
  template <class Root, class P, class K, class C>
  void changeAt(const InModel<model::Place<Root, P, K>> &at, const C &change) {
    (void)fModel.apply(model::Edit<Root, P, C, K>{at.fPlace.borrowed(), change});
  }
  template <class L, class Root, class P, class K, class C>
  void changeAt(const InLocal<L, model::Place<Root, P, K>> &at, const C &change) {
    (void)at.fLocal->fModel.apply(model::Edit<Root, P, C, K>{at.fPlace.borrowed(), change});
    ++localEpoch();
  }

  // Taken: by a scope's handlers, a Local's (with its state), a pipe's end.
  template <class E, class P, class H, class... Rest>
  void take(const E &event, const ScopeFrame<P, H> &frame,
            const Rest &...rest) {
    if constexpr (requires { frame.fHandlers->on(event, *frame.fPlace); })
      answer(frame.fHandlers->on(event, *frame.fPlace), frame, rest...);
    else
      answer(frame.fHandlers->on(event), frame, rest...);
  }
  template <class E, class L, class... Rest>
  void take(const E &event, const LocalFrame<L> &frame, const Rest &...rest) {
    auto &local = *frame.fLocal;
    if constexpr (requires { local.fHandlers.on(event, *local.fModel.template look<typename L::LocalOf>()); })
      answer(local.fHandlers.on(
                 event, *local.fModel.template look<typename L::LocalOf>()),
             frame, rest...);
    else
      answer(local.fHandlers.on(event), frame, rest...);
  }
  template <class E, class To, class... Rest>
  void take(const E &event, const PipeFrame<To> &frame, const Rest &...rest) {
    deliver(*frame.fTo, event, rest...);
  }

public:
  // What a component is given from a pipe or from send(): its on(), whose
  // answer -- edits of its own state, events passed on -- is done with the
  // frames around it.
  template <class To, class E, class... Rest>
    requires IsLocal<To>
  void deliver(To &to, const E &event, const Rest &...rest) {
    auto &local = asLocal(to);
    using L = std::remove_reference_t<decltype(local)>;
    take(event, LocalFrame<L>{&local}, rest...);
  }
  template <class To, class E, class... Rest>
  void deliver(To &to, const E &event, const Rest &...) {
    to.on(event);
  }

private:
  // What a handler answered, done: each edit applied where its part is
  // found -- the frame that took the event, else one around it, else the
  // whole model; an event passed up sent on from the frame above.
  template <class F, class... Rest>
  void answer(model::Nothing, const F &, const Rest &...) {}
  template <class... A, class F, class... Rest>
  void answer(const std::tuple<A...> &all, const F &frame,
              const Rest &...rest) {
    std::apply([&](const auto &...each) { (answer(each, frame, rest...), ...); },
               all);
  }
  template <class E, class F, class... Rest>
  void answer(const model::Up<E> &up, const F &, const Rest &...rest) {
    send(up.fEvent, rest...);
  }
  template <class E, class F, class... Rest>
  void answer(const model::Up<std::optional<E>> &up, const F &,
              const Rest &...rest) {
    if (up.fEvent)
      send(*up.fEvent, rest...);
  }
  template <class Want, class C, class... Keys, class F, class... Rest>
  void answer(const model::Over<Want, C, Keys...> &over, const F &frame,
              const Rest &...rest) {
    edit(over, frame, rest...);
  }

  template <class Want, class C, class... Keys>
  void edit(const model::Over<Want, C, Keys...> &over) {
    (void)fModel.apply(over);
  }
  template <class Want, class C, class... Keys, class P, class H,
            class... Rest>
    requires(model::kFound<Want, typename P::Target> == 1)
  void edit(const model::Over<Want, C, Keys...> &over,
            const ScopeFrame<P, H> &frame, const Rest &...) {
    (void)fModel.apply(M::edit(*frame.fPlace, over));
  }
  template <class Want, class C, class... Keys, class L, class... Rest>
    requires(model::kFound<Want, typename L::LocalOf> == 1)
  void edit(const model::Over<Want, C, Keys...> &over,
            const LocalFrame<L> &frame, const Rest &...) {
    (void)frame.fLocal->fModel.apply(over);
    ++localEpoch();
  }
  template <class Want, class C, class... Keys, class F, class... Rest>
  void edit(const model::Over<Want, C, Keys...> &over, const F &,
            const Rest &...rest) {
    static_assert(!kHiddenFrame<F>,
                  "skiff::bind: a hidden state's handler edits only that "
                  "state, and this part is not in it");
    edit(over, rest...);
  }
};
} // namespace detail
} // namespace skiff::bind
