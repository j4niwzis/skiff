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

} // namespace skiff::aggregate
