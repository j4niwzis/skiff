// skiff.compose:wiring -- In and Out, a >> b, b << a, a + b, place (the
// module is skiff.compose: see compose.cc).
export module skiff.compose:wiring;

import std;
import splice;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import :boxes;

export namespace skiff::compose {
// ---- In and Out ------------------------------------------------------------

namespace detail {
template <class A, class B> struct UnionT;
template <class... A> struct UnionT<model::Types<A...>, model::Types<>> {
  using type = model::Types<A...>;
};
template <class... A, class B, class... Bs>
struct UnionT<model::Types<A...>, model::Types<B, Bs...>> {
  using type = typename UnionT<
      std::conditional_t<(std::same_as<A, B> || ...), model::Types<A...>,
                         model::Types<A..., B>>,
      model::Types<Bs...>>::type;
};
template <class A, class B> struct MinusT;
template <class B> struct MinusT<model::Types<>, B> {
  using type = model::Types<>;
};
template <class A, class... As, class... B>
struct MinusT<model::Types<A, As...>, model::Types<B...>> {
  using rest = typename MinusT<model::Types<As...>, model::Types<B...>>::type;
  using type = std::conditional_t<(std::same_as<A, B> || ...), rest,
                                  typename UnionT<model::Types<A>, rest>::type>;
};
} // namespace detail
template <class A, class B> using Union = typename detail::UnionT<A, B>::type;
template <class A, class B> using Minus = typename detail::MinusT<A, B>::type;

// What a component sends: declared; else its parts'; else, for a node that
// declares nothing, what it was seen to send.
template <class C, auto Tag> struct OutOfT;
template <class C, auto Tag = [] {}>
using OutOf = typename OutOfT<std::remove_cvref_t<C>, Tag>::type;
template <class C> struct InOfT;
template <class C> using InOf = typename InOfT<std::remove_cvref_t<C>>::type;

template <class... C> struct OutsOf {
  using type = model::Types<>;
};
template <class C, class... Cs> struct OutsOf<C, Cs...> {
  using type = Union<OutOf<C>, typename OutsOf<Cs...>::type>;
};

template <int N> struct Rank : Rank<N - 1> {};
template <> struct Rank<0> {};
template <class C, auto>
  requires requires { typename C::Out; }
typename C::Out outOf(Rank<3>);
template <class C, auto>
  requires requires { typename C::OutOfParts; }
typename C::OutOfParts outOf(Rank<2>);
template <class C, auto Tag>
  requires std::derived_from<C, bind::Emitter>
bind::loophole::Deduced<C, Tag> outOf(Rank<1>);
template <class C, auto> model::Types<> outOf(Rank<0>);
// Asked anew where it is asked (Tag): a deduced Out is what was seen by then.
template <class C, auto Tag> struct OutOfT {
  using type = decltype(outOf<C, Tag>(Rank<3>{}));
};

template <class C>
  requires requires { typename C::In; }
typename C::In inOf(Rank<1>);
template <class C> model::Types<> inOf(Rank<0>);
template <class C> struct InOfT {
  using type = decltype(inOf<C>(Rank<1>{}));
};

// ---- a >> b, a + b ----------------------------------------------------------

template <class A, class B> struct Ends {
  A from;
  B to;
};
// What a sends goes to b first; what b does not take goes on up.
template <class A, class B> struct Pipe : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<A, B>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<A> || bind::detail::kHasLocal<B>>;
  using OutOfParts = Union<OutOf<B>, Minus<OutOf<A>, InOf<B>>>;
  using In = InOf<A>;
  Ends<A, B> fPipe;
  Pipe(A from, B to) : fPipe{std::move(from), std::move(to)} {}
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fPipe.from);
    f(self.fPipe.to);
  }
};
template <class A, class B>
  requires(std::derived_from<A, scene::Node> && std::derived_from<B, scene::Node>)
Pipe<A, B> operator>>(A from, B to) {
  return Pipe<A, B>(std::move(from), std::move(to));
}

// Fudgets' own way round (>==<): the receiver first, what feeds it after --
// b << a is a >> b, the same pipe.
template <class B, class A>
  requires(std::derived_from<A, scene::Node> && std::derived_from<B, scene::Node>)
Pipe<A, B> operator<<(B to, A from) {
  return Pipe<A, B>(std::move(from), std::move(to));
}

// Side by side, where nothing yet says where.
template <class... Parts> struct Par : scene::Node {
  using Walked = std::bool_constant<kAnyWalks<Parts...>>;
  using HasLocal = std::bool_constant<(bind::detail::kHasLocal<Parts> || ...)>;
  using OutOfParts = typename OutsOf<Parts...>::type;
  std::tuple<Parts...> fParts;
  explicit Par(Parts... parts) : fParts(std::move(parts)...) {}
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, self.fParts);
  }
};
template <class A, class B>
  requires(std::derived_from<A, scene::Node> && std::derived_from<B, scene::Node>)
Par<A, B> operator+(A a, B b) {
  return Par<A, B>(std::move(a), std::move(b));
}
template <class... As, class B>
  requires std::derived_from<B, scene::Node>
Par<As..., B> operator+(Par<As...> a, B b) {
  return std::apply(
      [&](auto &...each) { return Par<As..., B>(std::move(each)..., std::move(b)); },
      a.fParts);
}

// Where things go, said apart from how messages flow: a Par's parts
// stacked; anything else, alone in a stack.
template <class... Parts> Box<Parts...> place(Look look, Par<Parts...> parts) {
  return std::apply(
      [&](auto &...each) { return Box<Parts...>(std::move(look), std::move(each)...); },
      parts.fParts);
}
template <class N> Box<N> place(Look look, N node) {
  return Box<N>(std::move(look), std::move(node));
}

} // namespace skiff::compose
