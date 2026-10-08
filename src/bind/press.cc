// skiff.bind:press -- a press delivered along the path to the node pressed:
// the walk goes down that one path -- the nodes the press went through --
// with
// every type known, gathering the
// frames around it as the drain does, and what the node's onPress() returns
// is sent up through them at once (the module is skiff.bind: see bind.cc).
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

// ---- the child on the way -------------------------------------------------
//
// A press's path (scene::hostWork().pressed) is the nodes the press went
// through, from the root, by their states: at each level, the child held
// there, however it is held -- in an optional, a variant, a list -- with its
// own type. Nothing has moved since: the press is delivered right after the
// dispatch. An erased node is not seen through: a press inside one is not
// delivered from here.
using PressPath = std::vector<const scene::State *>;

template <class F> bool pressFind(std::monostate &, const scene::State *, F &&) { return false; }
template <class... Ts, class F> bool pressFind(spl::variant<Ts...> &held, const scene::State *id, F &&f) {
  return spl::visit([&](auto &one) { return pressFind(one, id, f); }, held);
}
template <class V, class F>
  requires(scene::one_of_several<V> && !std::derived_from<V, scene::Node>)
bool pressFind(V &held, const scene::State *id, F &&f) {
  bool found = false;
  held.visit([&](auto &one) { found = pressFind(one, id, f); });
  return found;
}
template <class T, class F> bool pressFind(std::optional<T> &held, const scene::State *id, F &&f) {
  return held && pressFind(*held, id, f);
}
template <class T, class D, class F> bool pressFind(std::unique_ptr<T, D> &held, const scene::State *id, F &&f) {
  return held && pressFind(*held, id, f);
}
template <class T, class F> bool pressFind(std::shared_ptr<T> &held, const scene::State *id, F &&f) {
  return held && pressFind(*held, id, f);
}
template <class T, class F> bool pressFind(std::reference_wrapper<T> &held, const scene::State *id, F &&f) {
  return pressFind(held.get(), id, f);
}
template <class... Ts, class F> bool pressFind(std::tuple<Ts...> &held, const scene::State *id, F &&f) {
  return std::apply([&](auto &...each) { return (pressFind(each, id, f) || ...); }, held);
}
template <class F> bool pressFind(scene::AnyNode &, const scene::State *, F &&) { return false; }
template <class R, class F>
  requires(std::ranges::range<R> && !scene::kTreatAsNode<R> && !std::derived_from<R, scene::Node>)
bool pressFind(R &held, const scene::State *id, F &&f) {
  return std::ranges::any_of(held, [&](auto &each) { return pressFind(each, id, f); });
}
template <class N, class F>
  requires(std::derived_from<N, scene::Node> || scene::kTreatAsNode<N>)
bool pressFind(N &held, const scene::State *id, F &&f) {
  if (&held.fState != id)
    return false;
  f(held);
  return true;
}

// ---- the walk along the path ------------------------------------------------

template <class Op, class N, class Here, class... Frames>
void pressNode(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames);

// Below a node: an Each's row, a scope of its element; a pipe's end, what
// the first sends going to the second; else the node's own child.
template <class Op, class N, class Here, class... Frames>
  requires IsEach<N>
void pressRows(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &list = asEach(node);
  const auto row_at = std::ranges::find(list.fRows, path[at], [](const auto &row) { return &row.fState; });
  if (row_at == list.fRows.end())
    return;
  const auto i = static_cast<std::size_t>(row_at - list.fRows.begin());
  const auto place = listPlaceOf(node, here);
  using P = typename std::remove_cvref_t<decltype(place)>::PathType;
  using Key = decltype(keyOf(node));
  using RowPath = model::Join<P, model::Path<model::At<Key>>>;
  const model::Place<typename Here::RootType, RowPath, model::BorrowedKeysOf<RowPath>> row{
      std::tuple_cat(place.fKeys, std::tuple<const Key &>{list.fKeys[i]})};
  pressNode(op, list.fRows[i], path, at + 1, row, frames...);
}
template <class Op, class N, class Here, class... Frames>
  requires(!IsEach<N> && requires(N &n) { n.fPipe; })
void pressRows(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &[from, to] = node.fPipe;
  if (pressFind(from, path[at], [&](auto &child) {
        pressNode(op, child, path, at + 1, here, PipeFrame<std::remove_cvref_t<decltype(to)>>{&to}, frames...);
      }))
    return;
  (void)pressFind(to, path[at], [&](auto &child) { pressNode(op, child, path, at + 1, here, frames...); });
}
template <class Op, class N, class Here, class... Frames>
void pressRows(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  bool found = false;
  node.forEachChild([&](auto &part) {
    found = found || pressFind(part, path[at], [&](auto &child) { pressNode(op, child, path, at + 1, here, frames...); });
  });
}

// The node itself: pressed where the path ends, else its child on the path.
template <class Op, class N, class Here, class... Frames>
void pressBody(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  if (at == path.size()) {
    op.fReached = true;
    op.pressed(node, frames...);
    return;
  }
  pressRows(op, node, path, at, here, frames...);
}

// The frames a node is: a Local's, a scope's -- as the drain has them.
template <class Op, class N, class Here, class... Frames>
  requires IsLocal<N>
void pressLocal(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &local = asLocal(node);
  const LocalFrame<std::remove_reference_t<decltype(local)>> frame{&local};
  pressBody(op, node, path, at, here, frame, frames...);
}
template <class Op, class N, class Here, class... Frames>
void pressLocal(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  pressBody(op, node, path, at, here, frames...);
}
template <class Op, class N, class Here, class... Frames>
  requires(IsScoped<N> && (Op::template kOwns<N> || (kInLocal<N, Frames> || ...)))
void pressScoped(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &scoped = asScoped(node);
  using Within = decltype(scopeOfOf(scoped));
  using Handlers = decltype(handlersOf(scoped));
  const auto inner = std::apply([&](const auto &...keys) { return model::borrowBelow<Within>(here, keys...); }, scoped.fKeys);
  pressLocal(op, node, path, at, inner, ScopeFrame<std::remove_cvref_t<decltype(inner)>, Handlers>{&inner, &scoped.fHandlers},
             frames...);
}
template <class Op, class N, class Here, class... Frames>
void pressScoped(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  pressLocal(op, node, path, at, here, frames...);
}
template <class Op, class N, class Here, class... Frames>
void pressNode(Op &op, N &node, const PressPath &path, std::size_t at, const Here &here, const Frames &...frames) {
  pressScoped(op, node, path, at, here, frames...);
}

// ---- what a press asks for, sent ------------------------------------------

// What a node's onPress() may return: nothing, one event, one perhaps, a
// few at once.
template <class N>
concept PressesNothing = requires(N &n) {
  { n.onPress() } -> std::same_as<void>;
};
template <class N>
concept Presses = requires(N &n) { n.onPress(); } && !PressesNothing<N>;

template <class M, class Sink> struct Pressing : Draining<M, Sink> {
  bool fReached = false;
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

// A press, delivered: down `path` from `root` -- the nodes the press went
// through, from the root, as the scene kept them (hostWork().pressed) -- to
// the node pressed, whose onPress() says
// what it asks for; that sent at once up the frames around it, as an event
// a node emits: to the first scope, Local or pipe that takes it, else to the
// model's reactions, else to the program's sink. The edits it makes, one
// batch.
template <class M, class N, class S = detail::NoSink>
bool press(N &root, M &model, const detail::PressPath &path, S *sink = nullptr) {
  if (path.empty() || path.front() != &root.fState)
    return false;
  model.beginBatch();
  detail::Pressing<M, S> op{{model, sink}};
  detail::pressNode(op, root, path, 1, model::Place<typename M::RootType, model::Path<>>{});
  model.endBatch();
  return op.fReached;
}

}  // namespace skiff::bind
