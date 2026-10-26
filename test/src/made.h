// What the tests reach in what a combinator made, in either build: as the
// type it made -- itself, where combinators are concrete; found by the
// walks under the Held it is erased into, where they are not (a program's
// Debug build, SKIFF_TEST_ERASED). Included after skiff's modules and gtest.
#pragma once

// Itself, or the part of it that it is made over (a projection over a
// node derives from that node).
template <class T, class N>
  requires std::derived_from<N, T>
T &made_as(N &node) {
  return node;
}
template <class T, class N>
  requires(!std::derived_from<N, T>)
T &made_as(N &node) {
  // Every node, shown or not (findNode passes over what is hidden).
  T *found = nullptr;
  skiff::scene::forEachNode<T>(node, [&](T &one) {
    if (found == nullptr)
      found = &one;
  });
  // Not found: the test fails there, and the rest of the suite goes on.
  if (found == nullptr)
    throw std::logic_error(std::format("no {} made", skiff::scene::nameOf<T>()));
  return *found;
}
// The path a press takes to a node under a root, as the walks find it --
// through each Held where combinators are erased, which a path written by
// hand for the concrete tree went past.
template <class N> skiff::scene::Path pressPathTo(N &root, const skiff::scene::State &target) {
  skiff::scene::Path way;
  if constexpr (skiff::scene::kErasedWalks) {
    auto seen = skiff::scene::AnyNodeRef::of(root);
    (void)skiff::scene::walk::findPath(seen, target.fId, way);
  } else {
    (void)skiff::scene::walk::findPath(root, target.fId, way);
  }
  return way;
}
// What presses answered, given to the tree as a host gives them after its
// presses: through a Held a press's answer is queued (hostWork), not handed
// up the types at once.
template <class N, class M> void deliverAnswers(N &root, M &model) {
  for (const auto &one : std::exchange(skiff::scene::hostWork().answers, {}))
    (void)skiff::bind::answer(root, model, one);
}
// Every node of a type under a root, in the walks' order.
template <class T, class N> std::vector<T *> all_made_as(N &root) {
  std::vector<T *> found;
  skiff::scene::forEachNode<T>(root, [&](T &one) { found.push_back(&one); });
  return found;
}
// A projection's node as projected() makes it: bound to what it wants, its
// function as the build keeps it (itself, or erased).
template <class Want, class Compute, class N>
using projected_t = skiff::bind::Bound<
    Want, skiff::compose::Projected<
              decltype(skiff::compose::erased_fn<std::invoke_result_t<Compute &, const typename skiff::bind::BoundValueT<Want>::type &>(
                           const typename skiff::bind::BoundValueT<Want>::type &)>(std::declval<Compute>())),
              skiff::compose::part_t<N>>>;
// The box place() makes of side-by-side parts.
template <class> struct BoxOfPar;
template <class... Parts> struct BoxOfPar<skiff::compose::Par<Parts...>> {
  using type = skiff::compose::Box<Parts...>;
};
