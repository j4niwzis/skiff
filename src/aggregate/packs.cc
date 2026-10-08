// skiff.aggregate, on C++26: an aggregate's members walked as a structured
// binding pack -- nothing but the language. (On C++23 the same module is
// pfr.cc, with Boost.PFR; the build compiles one or the other.)
export module skiff.aggregate;

import std;

export namespace skiff::aggregate {

// Each member of an aggregate, in the order it is declared.
template <class Aggregate, class F> void each(Aggregate &value, F &&f) {
  auto &[... member] = value;
  (f(member), ...);
}

// Each member of one aggregate with the member of another at the same place.
template <class A, class B, class F> void eachPair(A &one, B &other, F &&f) {
  auto &[... mine] = one;
  auto &[... theirs] = other;
  (f(mine, theirs), ...);
}

// An aggregate's member types, in order, as a std::tuple of them (named,
// never made).
template <class Aggregate>
using Members = typename decltype([](Aggregate &value) {
  auto &[... member] = value;
  return std::type_identity<
      std::tuple<std::remove_cvref_t<decltype(member)>...>>{};
}(std::declval<Aggregate &>()))::type;

// The member at a place, as the aggregate holds it (const where it is).
template <std::size_t I, class Aggregate>
constexpr auto &get(Aggregate &value) {
  auto &[... member] = value;
  return member...[I];
}

} // namespace skiff::aggregate
