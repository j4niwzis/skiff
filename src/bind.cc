// skiff.bind: a scene's nodes, showing a skiff.model and changing it.
//
// What a node is to the model is said by what it derives from; everything
// else is walked through as it is:
//
//   Bound<Want, Node, Changes...>  shows the one Want where it is found --
//       in the state of a Local around it, else below the scope it is in:
//       read(const Want &) when first seen and whenever its revision moves,
//       gone() where it is not there (a key no longer in its list). What it
//       asks to change -- change(flip) -- is applied where it is found.
//   Scoped<Within, Handlers, Node, Keys...>  narrows what is below it to the
//       one Within below the scope around it, through the keys it holds; and
//       takes, by Handlers' on() overloads, the events sent from within.
//   Each<Key, T, Row, Container, Make>  a row per element of the one list of
//       T's, in the list's order, each row a scope of its element.
//   Local<T, Handlers, Node, Hidden>  state of its own, of type T, beside the
//       model's (or, hidden, instead of it): its parts are found there first;
//       its handlers take events with it at hand. Nothing in it reaches the
//       model's reactions, nor is kept anywhere.
//   Emits<E...>  a node's events: emit(e) sends one up through the frames
//       around it -- pipes, local handlers, scopes -- to the first that takes
//       it. One that nothing takes does not compile.
//
// Nothing here is called from a node, and no node knows where it is: two
// walks carry the place and the frames down the tree. refresh() shows what
// changed; drain() does what the nodes asked for. A Binding does both only
// where something can have happened: refresh nothing where neither the
// model nor any local state moved, drain nothing where nothing was asked,
// and a scope's subtree not at all where its part did not move.
//
// The walks are static even where the scene's are erased: what a part is
// decides what is done with it, and that is known only of its type. A type
// with nothing bound anywhere below it is not walked at all.
//
// And the imperative way stays open: the nodes are skiff's nodes, set
// directly where that is what is wanted; a node with
// refresh(const Model &) reads the model itself; changes can be made in
// place (a change F taking T &).
export module skiff.bind;

export import :kinds;
export import :frames;
export import :walk;
export import :ops;
export import :target;
