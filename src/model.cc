// skiff.model: what a program shows, kept in one place.
//
// A program's state is one aggregate, its model, and nothing else holds a
// copy of it: a node shows a part of it and keeps only what is its own (a
// hover, a scroll, an animation). Parts are found by their types. A place in
// the model is the path the compiler finds to a type through the model's
// members -- nobody writes one -- and, where that path goes into a keyed
// list, the key of the element: keys are all of a place known at run time.
//
// The model changes only by edits: a function from a part's old value to its
// new one (or a change made in place), applied at a place. Each edit moves
// the revisions of the tracked parts it went through, so what shows a part
// tells in one comparison whether it changed; and on its way back up it
// tells the program's reactions what changed. A reaction is an overload,
// on(Changed<T>, at), and returns the effect it asks for as a value, which
// the program drains and carries out. An effect that says which one it is
// (key()) replaces the one of the same kind and key before it: within a
// batch the last -- the one from the state the batch ends in -- is what is
// carried out.
//
// What happens that is not a change of state -- saved, closed, removed -- is
// an event: sent up through the scopes it was sent in, and taken by the first
// whose handlers have an on() for it, which answers with edits.
//
// Parts that live elsewhere -- a store on disk, a window of a timeline -- are
// External: the model points at them and is told when they move, and they
// are read through it like any other; they are not edited through it.
//
// Everything here is constexpr, and nothing is erased: the paths, the
// reactions and the routes are all resolved while the program is compiled.
// An aggregate's members are reached through skiff.aggregate: the
// language's binding packs on C++26, Boost.PFR on C++23.
export module skiff.model;

export import :core;
export import :places;
export import :effects;
export import :model;
