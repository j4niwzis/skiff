// skiff.bind:press -- a press delivered along the path to the node pressed:
// the walk goes down that one path with every type known, gathering the
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

// ---- the child at a place, as the scene counts them ------------------------
//
// The scene's paths (scene::Path) give, at each level, a child's place in
// its parent's count of its children: each node held, however it is held --
// in an optional, a variant, a list -- one; an erased node one, not seen
// through. The same count here, with the child's own type.
template <class F> bool pressPart(std::monostate &, std::uint32_t &, F &&) { return false; }
template <class... Ts, class F> bool pressPart(spl::variant<Ts...> &held, std::uint32_t &left, F &&f) {
  return spl::visit([&](auto &one) { return pressPart(one, left, f); }, held);
}
template <class V, class F>
  requires(scene::one_of_several<V> && !std::derived_from<V, scene::Node>)
bool pressPart(V &held, std::uint32_t &left, F &&f) {
  bool found = false;
  held.visit([&](auto &one) { found = pressPart(one, left, f); });
  return found;
}
template <class T, class F> bool pressPart(std::optional<T> &held, std::uint32_t &left, F &&f) {
  return held && pressPart(*held, left, f);
}
template <class T, class D, class F> bool pressPart(std::unique_ptr<T, D> &held, std::uint32_t &left, F &&f) {
  return held && pressPart(*held, left, f);
}
template <class T, class F> bool pressPart(std::shared_ptr<T> &held, std::uint32_t &left, F &&f) {
  return held && pressPart(*held, left, f);
}
template <class T, class F> bool pressPart(std::reference_wrapper<T> &held, std::uint32_t &left, F &&f) {
  return pressPart(held.get(), left, f);
}
template <class... Ts, class F> bool pressPart(std::tuple<Ts...> &held, std::uint32_t &left, F &&f) {
  return std::apply([&](auto &...each) { return (pressPart(each, left, f) || ...); }, held);
}
// Erased: counted, but what it is was given up -- a press inside it is not
// delivered from here.
template <class F> bool pressPart(scene::AnyNode &held, std::uint32_t &left, F &&) {
  if (!held)
    return false;
  if (left != 0) {
    --left;
    return false;
  }
  return true;
}
template <class R, class F>
  requires(std::ranges::range<R> && !scene::kTreatAsNode<R> && !std::derived_from<R, scene::Node>)
bool pressPart(R &held, std::uint32_t &left, F &&f) {
  return std::ranges::any_of(held, [&](auto &each) { return pressPart(each, left, f); });
}
template <class N, class F>
  requires(std::derived_from<N, scene::Node> || scene::kTreatAsNode<N>)
bool pressPart(N &held, std::uint32_t &left, F &&f) {
  if (left != 0) {
    --left;
    return false;
  }
  f(held);
  return true;
}

// ---- the walk along the path ------------------------------------------------

template <class Op, class N, class Here, class... Frames>
void pressNode(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames);

// Below a node: an Each's row, a scope of its element; a pipe's end, what
// the first sends going to the second; else the node's own child.
template <class Op, class N, class Here, class... Frames>
  requires IsEach<N>
void pressRows(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &list = asEach(node);
  const std::size_t i = path[at];
  if (i >= list.fRows.size())
    return;
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
void pressRows(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &[from, to] = node.fPipe;
  std::uint32_t left = path[at];
  if (pressPart(from, left, [&](auto &child) {
        pressNode(op, child, path, at + 1, here, PipeFrame<std::remove_cvref_t<decltype(to)>>{&to}, frames...);
      }))
    return;
  (void)pressPart(to, left, [&](auto &child) { pressNode(op, child, path, at + 1, here, frames...); });
}
template <class Op, class N, class Here, class... Frames>
void pressRows(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  std::uint32_t left = path[at];
  bool found = false;
  node.forEachChild([&](auto &part) {
    found = found || pressPart(part, left, [&](auto &child) { pressNode(op, child, path, at + 1, here, frames...); });
  });
}

// The node itself: pressed where the path ends, else its child on the path.
template <class Op, class N, class Here, class... Frames>
void pressBody(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
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
void pressLocal(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &local = asLocal(node);
  const LocalFrame<std::remove_reference_t<decltype(local)>> frame{&local};
  pressBody(op, node, path, at, here, frame, frames...);
}
template <class Op, class N, class Here, class... Frames>
void pressLocal(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  pressBody(op, node, path, at, here, frames...);
}
template <class Op, class N, class Here, class... Frames>
  requires(IsScoped<N> && (Op::template kOwns<N> || (kInLocal<N, Frames> || ...)))
void pressScoped(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  auto &scoped = asScoped(node);
  using Within = decltype(scopeOfOf(scoped));
  using Handlers = decltype(handlersOf(scoped));
  const auto inner = std::apply([&](const auto &...keys) { return model::borrowBelow<Within>(here, keys...); }, scoped.fKeys);
  pressLocal(op, node, path, at, inner, ScopeFrame<std::remove_cvref_t<decltype(inner)>, Handlers>{&inner, &scoped.fHandlers},
             frames...);
}
template <class Op, class N, class Here, class... Frames>
void pressScoped(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
  pressLocal(op, node, path, at, here, frames...);
}
template <class Op, class N, class Here, class... Frames>
void pressNode(Op &op, N &node, const scene::Path &path, std::size_t at, const Here &here, const Frames &...frames) {
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

// A press, delivered: down `path` from `root` -- the path the scene found
// to the node pressed (Scene::pathOf) -- to that node, whose onPress() says
// what it asks for; that sent at once up the frames around it, as an event
// a node emits: to the first scope, Local or pipe that takes it, else to the
// model's reactions, else to the program's sink. The edits it makes, one
// batch.
template <class M, class N, class S = detail::NoSink>
bool press(N &root, M &model, const scene::Path &path, S *sink = nullptr) {
  model.beginBatch();
  detail::Pressing<M, S> op{{model, sink}};
  detail::pressNode(op, root, path, 0, model::Place<typename M::RootType, model::Path<>>{});
  model.endBatch();
  return op.fReached;
}

}  // namespace skiff::bind
