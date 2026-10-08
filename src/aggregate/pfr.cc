// skiff.aggregate, on C++23: an aggregate's members walked with Boost.PFR.
// (On C++26 the same module is packs.cc, with the language's binding packs
// and no Boost; the build compiles one or the other.)
export module skiff.aggregate;

import std;
import boost.pfr;

export namespace skiff::aggregate {

// Each member of an aggregate, in the order it is declared.
template <class Aggregate, class F> void each(Aggregate &value, F &&f) {
  boost::pfr::for_each_field(value, [&](auto &member) { f(member); });
}

// Each member of one aggregate with the member of another at the same place.
template <class A, class B, class F> void eachPair(A &one, B &other, F &&f) {
  boost::pfr::for_each_field(one, [&](auto &mine, auto index) { f(mine, boost::pfr::get<decltype(index)::value>(other)); });
}

// An aggregate's member types, in order, as a std::tuple of them (named,
// never made).
template <class Aggregate, class Indices> struct MembersOf;
template <class Aggregate, std::size_t... I>
struct MembersOf<Aggregate, std::index_sequence<I...>> {
  using type = std::tuple<
      std::remove_cvref_t<boost::pfr::tuple_element_t<I, Aggregate>>...>;
};
template <class Aggregate>
using Members = typename MembersOf<
    Aggregate,
    std::make_index_sequence<boost::pfr::tuple_size_v<Aggregate>>>::type;

// The member at a place, as the aggregate holds it (const where it is).
template <std::size_t I, class Aggregate>
constexpr auto &get(Aggregate &value) {
  return boost::pfr::get<I>(value);
}

} // namespace skiff::aggregate
