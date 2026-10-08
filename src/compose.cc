// skiff.compose: screens written as expressions of combinators, and
// components wired by the types of what they send -- Fudgets' way, with
// its wiring done by the compiler.
//
//   column(look, parts...), row(look, parts...)  a stack of its parts:
//       the tree's type is the expression's, as static as a parts_t.
//   bound<T, Changes...>(node), scoped<W>(handlers, node, keys...),
//   each<K, T>(make), local<T>(node), hidden<T>(handlers, node)
//       skiff.bind's kinds, made around a node.
//   onClick(event, node)  sends a copy of the event when pressed.
//   a >> b  what a sends goes to b first (b's on(), or a processor's step);
//       what b does not take goes on up, as from the pipe itself.
//   b << a  the same, written Fudgets' way round: the receiver first.
//       (+ binds tighter than >> and <<: a >> b + c is a >> (b + c).)
//   a + b  side by side, with nothing said of where: place(look, a + b)
//       says that. What each sends is its own; who takes it is by type.
//   process<P>()  a component as a pure processor: P::State, hidden, and
//       its steps, P::step(State, E) giving a new State, or a pair of it and
//       an event sent on, or -- the imperative way -- changing it in place,
//       step(State &, E); P::view() its picture, built once, its parts bound
//       to the state.
//   every<E>(ms), debounce<M>(ms)  components with no picture: time, as
//       events.
//
// What a component takes and sends -- In and Out -- is what it declares
// (using In = model::Types<...>, using Out = ...); a composite's is made of
// its parts'; a node that declares nothing sends what it is seen to send
// (skiff.bind's emitFrom, deduced).
export module skiff.compose;

export import :boxes;
export import :wiring;
export import :processes;
