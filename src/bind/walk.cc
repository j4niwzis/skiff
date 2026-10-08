// skiff.bind:walk -- the walk over a tree, carrying places and frames (the
// module is skiff.bind: see bind.cc).
export module skiff.bind:walk;

import std;
import splice;
import skiff.aggregate;
import skiff.model;
import skiff.scene;
import :frames;

export namespace skiff::bind {
namespace detail {
// ---- the walk ----------------------------------------------------------

template <class Op, class N, class Here, class... Frames>
void visitNode(Op &op, N &node, const Here &here, const Frames &...frames);

template <class Op, class Here, class... Frames>
void visitHeld(Op &, std::monostate &, const Here &, const Frames &...) {}
template <class Op, class... Ts, class Here, class... Frames>
void visitHeld(Op &op, spl::variant<Ts...> &held, const Here &here,
               const Frames &...frames) {
  spl::visit([&](auto &one) { visitHeld(op, one, here, frames...); }, held);
}
template <class Op, class T, class Here, class... Frames>
void visitHeld(Op &op, std::optional<T> &held, const Here &here,
               const Frames &...frames) {
  if (held)
    visitHeld(op, *held, here, frames...);
}
template <class Op, class T, class D, class Here, class... Frames>
void visitHeld(Op &op, std::unique_ptr<T, D> &held, const Here &here,
               const Frames &...frames) {
  if (held)
    visitHeld(op, *held, here, frames...);
}
template <class Op, class T, class Here, class... Frames>
void visitHeld(Op &op, std::shared_ptr<T> &held, const Here &here,
               const Frames &...frames) {
  if (held)
    visitHeld(op, *held, here, frames...);
}
template <class Op, class T, class Here, class... Frames>
void visitHeld(Op &op, std::reference_wrapper<T> &held, const Here &here,
               const Frames &...frames) {
  visitHeld(op, held.get(), here, frames...);
}
template <class Op, class... Ts, class Here, class... Frames>
void visitHeld(Op &op, std::tuple<Ts...> &held, const Here &here,
               const Frames &...frames) {
  std::apply([&](auto &...each) { (visitHeld(op, each, here, frames...), ...); },
             held);
}
// A node erased into an AnyNode is not seen through: what it is was given
// up when it was erased. A bound node is kept as itself.
template <class Op, class Here, class... Frames>
void visitHeld(Op &, scene::AnyNode &, const Here &, const Frames &...) {}
template <class Op, class R, class Here, class... Frames>
  requires(std::ranges::range<R> && !std::derived_from<R, scene::Node>)
void visitHeld(Op &op, R &held, const Here &here, const Frames &...frames) {
  for (auto &each : held)
    visitHeld(op, each, here, frames...);
}
template <class Op, class N, class Here, class... Frames>
  requires std::derived_from<N, scene::Node>
void visitHeld(Op &op, N &node, const Here &here, const Frames &...frames) {
  if constexpr (kWalks<N> && Op::template kWants<N>)
    visitNode(op, node, here, frames...);
}

template <class Op, class N, class Here, class... Frames>
void visitChildren(Op &op, N &node, const Here &here,
                   const Frames &...frames) {
  node.forEachChild(
      [&](auto &part) { visitHeld(op, part, here, frames...); });
}

// The list an Each's rows are, in the model below a scope.
template <class N, class Here> auto listPlaceOf(N &node, const Here &here) {
  using Key = decltype(keyOf(node));
  using T = decltype(elementOf(node));
  return model::borrowBelow<model::Keyed<Key, T>>(here);
}

// Children: an Each's rows, each a scope of its element; a pipe's ends,
// what the first sends going to the second; else the node's own.
template <class Op, class N, class Here, class... Frames>
  requires IsEach<N>
void visitRows(Op &op, N &node, const Here &here, const Frames &...frames) {
  auto &list = asEach(node);
  const auto place = listPlaceOf(node, here);
  using P = typename std::remove_cvref_t<decltype(place)>::PathType;
  using Key = decltype(keyOf(node));
  using RowPath = model::Join<P, model::Path<model::At<Key>>>;
  // A row walked, by whichever walk asks: as a scope of its element.
  const auto visitRow = [&](auto &walk, std::size_t i) {
    const model::Place<typename Here::RootType, RowPath,
                       model::BorrowedKeysOf<RowPath>>
        row{std::tuple_cat(place.fKeys, std::tuple<const Key &>{list.fKeys[i]})};
    visitNode(walk, list.fRows[i], row, frames...);
  };
  op.eachRows(node, place, visitRow);
}
template <class Op, class N, class Here, class... Frames>
  requires(!IsEach<N> && requires(N &n) { n.fPipe; })
void visitRows(Op &op, N &node, const Here &here, const Frames &...frames) {
  auto &[from, to] = node.fPipe;
  visitHeld(op, from, here, PipeFrame<std::remove_cvref_t<decltype(to)>>{&to},
            frames...);
  visitHeld(op, to, here, frames...);
}
template <class Op, class N, class Here, class... Frames>
void visitRows(Op &op, N &node, const Here &here, const Frames &...frames) {
  visitChildren(op, node, here, frames...);
}

template <class Op, class N, class Here, class... Frames>
void visitBody(Op &op, N &node, const Here &here, const Frames &...frames) {
  op.bound(node, here, frames...);
  op.itself(node);
  visitRows(op, node, here, frames...);
}

template <class Op, class N, class Here, class... Frames>
  requires IsLocal<N>
void visitLocal(Op &op, N &node, const Here &here, const Frames &...frames) {
  auto &local = asLocal(node);
  const LocalFrame<std::remove_reference_t<decltype(local)>> frame{&local};
  if (!op.local(node, here, frame, frames...))
    return;
  visitBody(op, node, here, frame, frames...);
}
template <class Op, class N, class Here, class... Frames>
void visitLocal(Op &op, N &node, const Here &here, const Frames &...frames) {
  visitBody(op, node, here, frames...);
}

template <class Op, class N, class Here, class... Frames>
  requires(IsScoped<N> && (Op::template kOwns<N> || (kInLocal<N, Frames> || ...)))
void visitScoped(Op &op, N &node, const Here &here, const Frames &...frames) {
  auto &scoped = asScoped(node);
  using Within = decltype(scopeOfOf(scoped));
  using Handlers = decltype(handlersOf(scoped));
  const auto inner = std::apply(
      [&](const auto &...keys) {
        return model::borrowBelow<Within>(here, keys...);
      },
      scoped.fKeys);
  if (!op.enter(scoped, inner))
    return;
  visitLocal(op, node, inner,
             ScopeFrame<std::remove_cvref_t<decltype(inner)>, Handlers>{
                 &inner, &scoped.fHandlers},
             frames...);
}
template <class Op, class N, class Here, class... Frames>
void visitScoped(Op &op, N &node, const Here &here, const Frames &...frames) {
  visitLocal(op, node, here, frames...);
}

template <class Op, class N, class Here, class... Frames>
void visitNode(Op &op, N &node, const Here &here, const Frames &...frames) {
  visitScoped(op, node, here, frames...);
}

// What the read and gone hooks are, where a node has them.
template <class N, class T>
  requires requires(N &n, const T &t) { n.read(t); }
void read(N &node, const T &value) {
  node.read(value);
}
template <class N, class T> void read(N &, const T &) {}
template <class N>
  requires requires(N &n) { n.gone(); }
void gone(N &node) {
  node.gone();
}
template <class N> void gone(N &) {}

// A part as it is now, where it is found.
template <class M, class Place>
auto lookAt(const M &model, const InModel<Place> &at) {
  return model.look(at.fPlace);
}
template <class M, class L, class Place>
auto lookAt(const M &, const InLocal<L, Place> &at) {
  return at.fLocal->fState.look(at.fPlace);
}

} // namespace detail
} // namespace skiff::bind
