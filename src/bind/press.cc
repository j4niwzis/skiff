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

// A change of the part a bound node shows, as what its press answers: made
// where the part is found -- in a Local around it, else below its scope --
// as the press is answered. What a model widget answers with.
template <class C> struct Own {
  C fChange;
};
template <class C> Own<C> own(C change) { return {std::move(change)}; }

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
    op.pressed(node, here, frames...);
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
  // An own change whose part is not in this model.
  bool fMissed = false;
  template <class N, class Here, class... Frames>
    requires PressesNothing<N>
  void pressed(N &node, const Here &, const Frames &...) {
    node.onPress();
  }
  template <class N, class Here, class... Frames>
    requires Presses<N>
  void pressed(N &node, const Here &here, const Frames &...frames) {
    this->answerAt(node, here, node.onPress(), frames...);
  }
  // Pressed, but saying nothing of what a press does: the scene's own.
  template <class N, class Here, class... Frames> void pressed(N &, const Here &, const Frames &...) {}

  // An answer at its node: a change of the node's own part made where the
  // part is found; else sent up the frames.
  template <class N, class Here, class C, class... Frames>
    requires(IsBound<N> && kOfModel<typename M::RootType, N>)
  void answerAt(N &node, const Here &here, const Own<C> &own, const Frames &...frames) {
    using Want = decltype(boundToOf(asBound(node)));
    this->change(where<Want>(here, frames...), own.fChange);
  }
  // A part of a bound node that binds nothing itself -- a segment of a
  // field: the part it sets is the one it is in, of the type it sets.
  template <class N, class Here, class T, class... Frames>
    requires(!IsBound<N> && std::same_as<typename Here::Target, T>)
  void answerAt(N &, const Here &here, const Own<model::SetTo<T>> &own, const Frames &...frames) {
    this->change(where<T>(here, frames...), own.fChange);
  }
  // Its part is in another model: pressed there.
  template <class N, class Here, class C, class... Frames>
  void answerAt(N &, const Here &, const Own<C> &, const Frames &...) {
    fMissed = true;
  }
  template <class N, class Here, class A, class... Frames>
  void answerAt(N &node, const Here &here, const std::optional<A> &answer, const Frames &...frames) {
    if (answer)
      this->answerAt(node, here, *answer, frames...);
  }
  template <class N, class Here, class A, class... Frames>
  void answerAt(N &, const Here &, const A &answer, const Frames &...frames) {
    this->sendAll(answer, frames...);
  }

  template <class... Frames> void sendAll(model::Nothing, const Frames &...) {}
  template <class... Frames> void sendAll(scene::Taken, const Frames &...) {}
  template <class E, class... Frames> void sendAll(const std::optional<E> &maybe, const Frames &...frames) {
    if (maybe)
      this->sendAll(*maybe, frames...);
  }
  template <class... E, class... Frames> void sendAll(const std::tuple<E...> &all, const Frames &...frames) {
    std::apply([&](const auto &...each) { (this->sendAll(each, frames...), ...); }, all);
  }
  template <class... E, class... Frames> void sendAll(const std::variant<E...> &one, const Frames &...frames) {
    std::visit([&](const auto &event) { this->sendAll(event, frames...); }, one);
  }
  template <class... E, class... Frames> void sendAll(const spl::variant<E...> &one, const Frames &...frames) {
    spl::visit([&](const auto &event) { this->sendAll(event, frames...); }, one);
  }
  template <class E, class... Frames> void sendAll(const E &event, const Frames &...frames) { this->send(event, frames...); }
};

// What a handler kept, delivered along its path: at its node, the answer --
// of the node's own Answer, as it was kept -- sent up the frames there.
template <class M, class Sink> struct Answering : Pressing<M, Sink> {
  const scene::HostWork::KeptAnswer *fKept = nullptr;
  template <class N, class Here, class... Frames>
    requires requires { typename N::Answer; }
  void pressed(N &node, const Here &here, const Frames &...frames) {
    if (fKept->type == &scene::kTypeKey<typename N::Answer>)
      this->answerAt(node, here, *static_cast<const typename N::Answer *>(fKept->answer.get()), frames...);
  }
  template <class N, class Here, class... Frames> void pressed(N &, const Here &, const Frames &...) {}
};
}  // namespace detail

// What a handler returned, kept outside a release build (the routing erased
// carries nothing down): delivered along its path, sent up the frames there.
template <class M, class N, class S = detail::NoSink>
bool answer(N &root, M &model, const scene::HostWork::KeptAnswer &kept, S *sink = nullptr) {
  model.beginBatch();
  detail::Answering<M, S> op{{{model, sink}}, &kept};
  detail::pressNode(op, root, kept.path, 0, model::Place<typename M::RootType, model::Path<>>{});
  model.endBatch();
  return op.fReached && !op.fMissed;
}

// A press, delivered: down `path` from `root` -- the path the scene routed
// the press along to the node that answered it (hostWork().pressed) -- to
// that node, whose onPress() says
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
  return op.fReached && !op.fMissed;
}


// ---- answered where it is made: a release build's routing ------------------
//
// A release build routes an event statically, every node with its type: what
// binds the tree is carried down along it -- the model and the program's sink
// at the root, then at each node that binds something its place below the
// model and its frames -- and a press is answered at its node there and then,
// through the frames it is in. No path kept, no walk again. (skiff.scene calls
// carryInto and answerPress as it routes; they are found here by the carried
// type.) Each step is made where the routing goes into a node and lives while
// the routing is below it: it points to the step above, and owns its own
// place.
template <class M, class S> struct CarryRoot {
  using Model = M;
  using Sink = S;
  M *fModel = nullptr;
  S *fSink = nullptr;
  model::Place<typename M::RootType, model::Path<>> fPlace{};
};
// What a routing starts with, for a program to give from its root's
// startCarry: the model its scopes are bound to, and its sink.
template <class M, class S = detail::NoSink> CarryRoot<M, S> carryFrom(M *model, S *sink = nullptr) {
  return {model, sink, {}};
}

namespace detail {
struct NoPart {};
// An Each's row: its element's place.
template <class Place> struct RowPart {
  Place fPlace;
};
// A scope of the model's: the place below it, and its handlers.
template <class Place, class H> struct ScopePart {
  Place fPlace;
  const H *fHandlers;
};
// A Local: its state and handlers.
template <class L> struct LocalPart {
  L *fLocal;
};

template <class Outer, class Row, class Scope, class Local> struct CarryStep;
template <class C> struct IsCarryT : std::false_type {};
template <class M, class S> struct IsCarryT<CarryRoot<M, S>> : std::true_type {};
template <class O, class R, class Sc, class L> struct IsCarryT<CarryStep<O, R, Sc, L>> : std::true_type {};
template <class C>
concept IsCarry = IsCarryT<C>::value;

// The place a step leaves the routing at: its scope's, else its row's, else
// the one above it.
template <class M, class S> const auto &hereOf(const CarryRoot<M, S> &root) { return root.fPlace; }
template <class O, class R, class Sc, class L> const auto &hereOf(const CarryStep<O, R, Sc, L> &step);
template <class P, class H, class R, class O> const auto &placeOf(const ScopePart<P, H> &scope, const R &, const O &) {
  return scope.fPlace;
}
template <class P, class O> const auto &placeOf(NoPart, const RowPart<P> &row, const O &) { return row.fPlace; }
template <class O> const auto &placeOf(NoPart, NoPart, const O &outer) { return hereOf(outer); }
template <class O, class R, class Sc, class L> const auto &hereOf(const CarryStep<O, R, Sc, L> &step) {
  return placeOf(step.fScope, step.fRow, *step.fOuter);
}
template <class R, class O> const auto &rowOrAbove(const RowPart<R> &row, const O &) { return row.fPlace; }
template <class O> const auto &rowOrAbove(NoPart, const O &outer) { return hereOf(outer); }

template <class M, class S> const auto &rootOf(const CarryRoot<M, S> &root) { return root; }
template <class O, class R, class Sc, class L> const auto &rootOf(const CarryStep<O, R, Sc, L> &step) {
  return rootOf(*step.fOuter);
}

// What a node binds, made where the routing goes into it.
template <class P, class N, class C>
  requires IsEach<P>
auto rowPartOf(P &parent, N &row, const C &outer) {
  auto &list = asEach(parent);
  const auto i = static_cast<std::size_t>(&row - list.fRows.data());
  const auto place = listPlaceOf(parent, hereOf(outer));
  using Pl = typename std::remove_cvref_t<decltype(place)>::PathType;
  using Key = decltype(keyOf(parent));
  using RowPath = model::Join<Pl, model::Path<model::At<Key>>>;
  using RowPlace = model::Place<typename std::remove_cvref_t<decltype(hereOf(outer))>::RootType, RowPath,
                                model::BorrowedKeysOf<RowPath>>;
  return RowPart<RowPlace>{RowPlace{std::tuple_cat(place.fKeys, std::tuple<const Key &>{list.fKeys[i]})}};
}
template <class P, class N, class C> NoPart rowPartOf(P &, N &, const C &) { return {}; }

template <class N, class Here, class Root>
  requires(IsScoped<N> && kOfModel<Root, N>)
auto scopePartOf(N &node, const Here &here, std::type_identity<Root>) {
  auto &scoped = asScoped(node);
  using Within = decltype(scopeOfOf(scoped));
  using Handlers = decltype(handlersOf(scoped));
  using Inner = decltype(std::apply([&](const auto &...keys) { return model::borrowBelow<Within>(here, keys...); }, scoped.fKeys));
  return ScopePart<Inner, Handlers>{
      std::apply([&](const auto &...keys) { return model::borrowBelow<Within>(here, keys...); }, scoped.fKeys), &scoped.fHandlers};
}
template <class N, class Here, class Root> NoPart scopePartOf(N &, const Here &, std::type_identity<Root>) { return {}; }

template <class N>
  requires IsLocal<N>
auto localPartOf(N &node) {
  auto &local = asLocal(node);
  return LocalPart<std::remove_reference_t<decltype(local)>>{&local};
}
template <class N> NoPart localPartOf(N &) { return {}; }

// A step: what one node binds, its parts made in order -- the row's place,
// then the scope's below it -- each where it is kept.
template <class Outer, class Row, class Scope, class Local> struct CarryStep {
  using Model = typename Outer::Model;
  using Sink = typename Outer::Sink;
  template <class P, class N>
  CarryStep(const Outer &outer, P &parent, N &child)
      : fOuter(&outer), fRow(rowPartOf(parent, child, outer)),
        fScope(scopePartOf(child, rowOrAbove(fRow, outer), std::type_identity<typename Model::RootType>{})),
        fLocal(localPartOf(child)) {}
  CarryStep(const CarryStep &) = delete;
  CarryStep &operator=(const CarryStep &) = delete;
  const Outer *fOuter;
  Row fRow;
  Scope fScope;
  Local fLocal;
};

template <class P, class N, class C>
using StepOf = CarryStep<C, decltype(rowPartOf(std::declval<P &>(), std::declval<N &>(), std::declval<const C &>())),
                         decltype(scopePartOf(std::declval<N &>(),
                                              rowOrAbove(std::declval<decltype(rowPartOf(std::declval<P &>(), std::declval<N &>(),
                                                                                         std::declval<const C &>())) &>(),
                                                         std::declval<const C &>()),
                                              std::type_identity<typename C::Model::RootType>{})),
                         decltype(localPartOf(std::declval<N &>()))>;
template <class S>
inline constexpr bool kBindsNothing = false;
template <class C, class... Parts>
inline constexpr bool kBindsNothing<CarryStep<C, Parts...>> = (std::same_as<Parts, NoPart> && ...);

// The frames a step is, the innermost first, as the drain makes them: its
// Local's, then its scope's.
template <class G> void framesHere(NoPart, NoPart, G &&g) { g(); }
template <class L, class G> void framesHere(const LocalPart<L> &local, NoPart, G &&g) { g(LocalFrame<L>{local.fLocal}); }
template <class P, class H, class G> void framesHere(NoPart, const ScopePart<P, H> &scope, G &&g) {
  g(ScopeFrame<P, H>{&scope.fPlace, scope.fHandlers});
}
template <class L, class P, class H, class G> void framesHere(const LocalPart<L> &local, const ScopePart<P, H> &scope, G &&g) {
  g(LocalFrame<L>{local.fLocal}, ScopeFrame<P, H>{&scope.fPlace, scope.fHandlers});
}
template <class M, class S, class F> void framesOf(const CarryRoot<M, S> &, F &&f) { f(); }
template <class O, class R, class Sc, class L, class F> void framesOf(const CarryStep<O, R, Sc, L> &step, F &&f) {
  framesOf(*step.fOuter, [&](const auto &...outer) {
    framesHere(step.fLocal, step.fScope, [&](const auto &...mine) { f(mine..., outer...); });
  });
}
}  // namespace detail

// Down the routing, into a child: what it binds -- a row of an Each, a scope
// of the model's, a Local -- a step of its own; nothing, the same carried on.
template <class P, class N, class C>
  requires(detail::IsCarry<C> && !detail::kBindsNothing<detail::StepOf<P, N, C>>)
detail::StepOf<P, N, C> carryInto(P &parent, N &child, const C &carried) {
  return detail::StepOf<P, N, C>(carried, parent, child);
}
template <class P, class N, class C>
  requires(detail::IsCarry<C> && detail::kBindsNothing<detail::StepOf<P, N, C>>)
const C &carryInto(P &, N &, const C &carried) {
  return carried;
}

// A press answered where it is made: what the node's onPress() returns,
// sent up the frames carried down to it, as bind::press does.
template <class N, class C>
  requires(detail::IsCarry<C> && (detail::Presses<N> || detail::PressesNothing<N>))
bool answerPress(N &node, const C &carried) {
  const auto &root = detail::rootOf(carried);
  if (root.fModel == nullptr)
    return false;
  using M = typename C::Model;
  using S = typename C::Sink;
  M &model = *root.fModel;
  model.beginBatch();
  detail::Pressing<M, S> op{{model, root.fSink}};
  detail::framesOf(carried, [&](const auto &...frames) { op.pressed(node, detail::hereOf(carried), frames...); });
  model.endBatch();
  return true;
}

// What a handler returned, sent at once up the frames carried down to its
// node, in a release build.
template <class N, class A, class C>
  requires detail::IsCarry<C>
void answerWith(N &node, const A &answer, const C &carried) {
  const auto &root = detail::rootOf(carried);
  if (root.fModel == nullptr)
    return;
  using M = typename C::Model;
  using S = typename C::Sink;
  M &model = *root.fModel;
  model.beginBatch();
  detail::Pressing<M, S> op{{model, root.fSink}};
  detail::framesOf(carried, [&](const auto &...frames) { op.answerAt(node, detail::hereOf(carried), answer, frames...); });
  model.endBatch();
}

}  // namespace skiff::bind
