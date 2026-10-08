// skiff.bind:press -- a press delivered to the node pressed: the walk goes
// down the tree with every type known, gathering the frames around each node
// as the drain does, and at the node pressed -- found by its address, as the
// scene recorded it -- what its onPress() returns is sent up through them at
// once (the module is skiff.bind: see bind.cc).
export module skiff.bind:press;

import std;
import splice;
import skiff.model;
import skiff.scene;
import :kinds;
import :frames;
import :walk;
import :ops;

export namespace skiff::bind {
namespace detail {

// What a node's onPress() may return: nothing, one event, one perhaps, a
// few at once.
template <class N>
concept PressesNothing = requires(N &n) {
  { n.onPress() } -> std::same_as<void>;
};
template <class N>
concept Presses = requires(N &n) { n.onPress(); } && !PressesNothing<N>;

// A drain of one press: nothing else a drain does -- no change applied, no
// event kept elsewhere sent -- only the node pressed, where it is met, with
// the frames around it; the walk done once it is.
template <class M, class Sink> struct Pressing : Draining<M, Sink> {
  const scene::State *fPressed = nullptr;
  bool fDone = false;

  template <class N, class Here, class... Frames> void bound(N &, const Here &, const Frames &...) {}
  template <class N> void itself(N &) {}
  template <class N, class Here, class... Frames>
  void emitted(N &node, const Here &, const Frames &...frames) {
    if (fDone || &node.fState != fPressed)
      return;
    fDone = true;
    this->pressed(node, frames...);
  }
  template <class S, class Place> bool enter(S &, const Place &) { return !fDone; }
  template <class N, class Here, class... Frames> bool local(N &, const Here &, const Frames &...) { return !fDone; }
  template <class N, class Place, class VisitRow>
  void eachRows(N &node, const Place &, const VisitRow &visitRow) {
    for (std::size_t i = 0; !fDone && i < asEach(node).fRows.size(); ++i)
      visitRow(*this, i);
  }

  template <class N, class... Frames>
    requires PressesNothing<N>
  void pressed(N &node, const Frames &...) {
    node.onPress();
  }
  template <class N, class... Frames>
    requires Presses<N>
  void pressed(N &node, const Frames &...frames) {
    this->sendAll(node.onPress(), frames...);
  }
  // Pressed, but saying nothing of what a press does: the scene's own.
  template <class N, class... Frames> void pressed(N &, const Frames &...) {}

  template <class... Frames> void sendAll(model::Nothing, const Frames &...) {}
  template <class E, class... Frames> void sendAll(const std::optional<E> &maybe, const Frames &...frames) {
    if (maybe)
      this->sendAll(*maybe, frames...);
  }
  template <class... E, class... Frames> void sendAll(const std::tuple<E...> &all, const Frames &...frames) {
    std::apply([&](const auto &...each) { (this->sendAll(each, frames...), ...); }, all);
  }
  template <class E, class... Frames> void sendAll(const E &event, const Frames &...frames) { this->send(event, frames...); }
};

}  // namespace detail

// A press, delivered: the node below `root` whose state is `pressed` -- as
// the scene recorded it (scene::hostWork().pressed) -- found with every
// type known, and what its onPress() says it asks for sent at once up the
// frames around it, as an event a node emits: to the first scope, Local or
// pipe that takes it, else to the model's reactions, else to the program's
// sink. The edits it makes, one batch.
template <class M, class N, class S = detail::NoSink>
bool press(N &root, M &model, const scene::State *pressed, S *sink = nullptr) {
  model.beginBatch();
  detail::Pressing<M, S> op{{model, sink}, pressed};
  detail::visitNode(op, root, model::Place<typename M::RootType, model::Path<>>{});
  model.endBatch();
  return op.fDone;
}

}  // namespace skiff::bind
