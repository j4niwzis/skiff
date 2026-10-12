// skiff.bind:takes -- what an overload set takes, found while compiling: the
// event types a scope's or a Local's handlers have an on() for, the
// requests a program's part has an apply() for (the module is skiff.bind:
// see bind.cc).
//
// A debug build routes what a press sends at run time, through tables made
// from these: an event to the first frame around it with an on() for it,
// a request to the part that applies it. Nothing names them by hand. Found
// by a loophole: a probe that converts to any one type is offered to the
// overload set, and each conversion it is asked for puts that type in a
// slot of its own (a friend defined where it is asked for); the slots are
// then read in order. Asked in a dependent context, an overload set it fits
// more than one of is ambiguous there, not an error -- the slots are filled
// all the same.
export module skiff.bind:takes;

import std;
import skiff.model;

export namespace skiff::bind::takes {
namespace loophole {
template <class Tag, std::size_t N> struct Slot {
  friend constexpr auto taken(Slot);
};
template <class Tag, std::size_t N, class T> struct Fill {
  friend constexpr auto taken(Slot<Tag, N>) { return std::type_identity<T>{}; }
};
// How many slots of a tag are filled: the first with nothing in it. Each
// call made anew (its lambda), so nothing of an earlier count is kept.
template <class Tag, std::size_t N = 0, auto = [] {}> consteval std::size_t filled() {
  if constexpr (requires { taken(Slot<Tag, N>{}); })
    return filled<Tag, N + 1, [] {}>();
  else
    return N;
}
// A type put in the next slot.
template <class Tag, class T, auto = [] {}> consteval std::size_t put() {
  constexpr std::size_t n = filled<Tag, 0, [] {}>();
  (void)sizeof(Fill<Tag, n, T>);
  return n;
}
// What converts to the one argument asked for, putting it in a slot.
template <class Tag> struct Probe {
  template <class T, std::size_t = put<Tag, std::remove_cvref_t<T>>()> operator T &() const;
  template <class T, std::size_t = put<Tag, std::remove_cvref_t<T>>()> operator T &&() const;
};
// What converts to anything, putting nothing: the place a handler takes
// after its event.
struct Any {
  template <class T> operator T &() const;
  template <class T> operator T &&() const;
};
template <class Tag, class Indices> struct Collected;
template <class Tag, std::size_t... I> struct Collected<Tag, std::index_sequence<I...>> {
  using type = model::Types<typename decltype(taken(Slot<Tag, I>{}))::type...>;
};
// The same type once, in the order first found.
template <class Seen, class... Rest> struct Unique {
  using type = Seen;
};
template <class... Seen, class T, class... Rest> struct Unique<model::Types<Seen...>, T, Rest...> {
  // Seen already where it is one of the bases of those seen.
  struct All : std::type_identity<Seen>... {};
  using type = typename std::conditional_t<std::derived_from<All, std::type_identity<T>>, Unique<model::Types<Seen...>, Rest...>,
                                           Unique<model::Types<Seen..., T>, Rest...>>::type;
};
template <class L> struct UniqueOf;
template <class... T> struct UniqueOf<model::Types<T...>> {
  using type = typename Unique<model::Types<>, T...>::type;
};

template <class H> struct OnOne {};
template <class H, class Place> struct OnTwo {};
template <class H> struct ApplyOne {};
template <class H> constexpr bool probeOnOne() {
  return requires(const H &h) { h.on(Probe<OnOne<H>>{}); };
}
// With what a handler is told besides: its place, or its Local's state --
// the type it will be given, so that a handler generic over it is made
// with that, not with something it cannot use.
template <class H, class Place> constexpr bool probeOnTwo() {
  return requires(const H &h, const Place &where) { h.on(Probe<OnTwo<H, Place>>{}, where); };
}
template <class H> constexpr bool probeApplyOne() {
  return requires(H &h) { h.apply(Probe<ApplyOne<H>>{}); };
}
// Probed first, then read: one constant expression, in that order.
template <class Tag, bool Probed> struct Read {
  using type = typename UniqueOf<typename Collected<Tag, std::make_index_sequence<filled<Tag>()>>::type>::type;
};
} // namespace loophole

// The event types handlers take: those of an on(event), and of an
// on(event, where), together, each once.
template <class H, class Place = loophole::Any>
using OnEvents = typename loophole::UniqueOf<typename model::detail::Concat<
    typename loophole::Read<loophole::OnOne<H>, loophole::probeOnOne<H>()>::type,
    typename loophole::Read<loophole::OnTwo<H, Place>, loophole::probeOnTwo<H, Place>()>::type>::type>::type;
// The event types an on(event) takes, alone: what a model's reactions take
// as events (their on(Changed<T>, at) take two).
template <class H>
using OnOneEvents = typename loophole::Read<loophole::OnOne<H>, loophole::probeOnOne<H>()>::type;
// Whether a list has a type.
template <class T, class L> inline constexpr bool kIn = false;
template <class T, class... L>
inline constexpr bool kIn<T, model::Types<L...>> = [] {
  struct All : std::type_identity<L>... {};
  return std::derived_from<All, std::type_identity<T>>;
}();
// The request types a part takes: those of an apply(request).
template <class H>
using Applied = typename loophole::Read<loophole::ApplyOne<H>, loophole::probeApplyOne<H>()>::type;
} // namespace skiff::bind::takes
