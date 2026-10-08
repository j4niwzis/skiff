// skiff.compose:boxes -- stacks, and the model's kinds made around a node
// (the module is skiff.compose: see compose.cc).
export module skiff.compose:boxes;

import std;
import splice;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;

export namespace skiff::compose {

// How a stack lays its parts out: which way, the gap between them, and the
// node's own spec.
struct Look {
  bool fHorizontal = false;
  float fGap = 0.0f;
  scene::Spec fSpec{};
};
inline Look vbox(float gap = 0.0f, scene::Spec spec = {}) {
  return {false, gap, std::move(spec)};
}
inline Look hbox(float gap = 0.0f, scene::Spec spec = {}) {
  return {true, gap, std::move(spec)};
}

template <class... Parts>
inline constexpr bool kAnyWalks = (bind::detail::kWalks<Parts> || ...);

// A stack of its parts, in order.
template <class... Parts> struct Box : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<Parts...>>;
  using HasLocal = std::bool_constant<(bind::detail::kHasLocal<Parts> || ...)>;
  std::tuple<Parts...> fParts;
  explicit Box(Look look, Parts... parts) : fParts(std::move(parts)...) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, self.fParts);
  }
};

// A stack of a list of parts, as many as there are: made from them, in
// their order.
template <class N> struct Many : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<N>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  std::vector<N> fParts;
  Many(Look look, std::vector<N> parts) : fParts(std::move(parts)) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) { std::ranges::for_each(self.fParts, f); }
};
template <class N> Many<N> many(Look look, std::vector<N> parts) { return Many<N>(std::move(look), std::move(parts)); }

// A node shown, or there but hidden.
template <class N>
  requires std::derived_from<N, scene::Node>
N visible(bool on, N node) {
  node.setVisible(on);
  return node;
}

// A node with its spec applied: a leaf said as it is put in its place.
template <class N>
  requires std::derived_from<N, scene::Node>
N styled(scene::Spec spec, N node) {
  node.apply(spec);
  return node;
}

template <class... Parts> Box<Parts...> column(Look look, Parts... parts) {
  look.fHorizontal = false;
  return Box<Parts...>(std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires(std::derived_from<Parts, scene::Node> && ...)
Box<Parts...> column(Parts... parts) {
  return Box<Parts...>(vbox(), std::move(parts)...);
}
template <class... Parts> Box<Parts...> row(Look look, Parts... parts) {
  look.fHorizontal = true;
  return Box<Parts...>(std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires(std::derived_from<Parts, scene::Node> && ...)
Box<Parts...> row(Parts... parts) {
  return Box<Parts...>(hbox(), std::move(parts)...);
}

// ---- the model's kinds, around a node ------------------------------------

template <class Want, class N> bind::Bound<Want, N> bound(N node) {
  return bind::Bound<Want, N>(std::move(node));
}
// A node shown what Compute makes of the model, as the parts it reads move.
template <class... Reads, class Compute, class N>
bind::Derived<Compute, N, Reads...> derived(Compute compute, N node) {
  return bind::Derived<Compute, N, Reads...>(std::move(node), std::move(compute));
}
template <class Within, class Handlers, class N, class... Keys>
bind::Scoped<Within, Handlers, N, Keys...> scoped(Handlers handlers, N node,
                                                  Keys... keys) {
  return bind::Scoped<Within, Handlers, N, Keys...>(
      std::move(handlers), std::move(node), std::move(keys)...);
}
template <class T, class N> bind::Local<T, bind::NoHandlers, N> local(N node, T initial = {}) {
  return bind::Local<T, bind::NoHandlers, N>({}, std::move(node), std::move(initial));
}
template <class T, class Handlers, class N>
bind::Local<T, Handlers, N> local(Handlers handlers, N node, T initial = {}) {
  return bind::Local<T, Handlers, N>(std::move(handlers), std::move(node),
                                     std::move(initial));
}
template <class T, class Handlers, class N>
bind::Local<T, Handlers, N, true> hidden(Handlers handlers, N node,
                                         T initial = {}) {
  return bind::Local<T, Handlers, N, true>(std::move(handlers),
                                           std::move(node), std::move(initial));
}

// A row made by a callable, from the element or from nothing.
template <class T, class F> struct MakeRow {
  F fMake;
  auto operator()(const T &value) const
    requires std::invocable<const F &, const T &>
  {
    return fMake(value);
  }
  auto operator()(const T &) const
    requires(!std::invocable<const F &, const T &>)
  {
    return fMake();
  }
};
template <class T, class F>
using RowOf = std::invoke_result_t<const MakeRow<T, F> &, const T &>;

template <class Key, class T, class F>
auto each(F make, Look look = vbox()) {
  using Row = RowOf<T, F>;
  using Container = Box<>;
  return bind::Each<Key, T, Row, Container, MakeRow<T, F>>(
      MakeRow<T, F>{std::move(make)}, Container(std::move(look)));
}

// Handlers written where they are used: handle<Removed>([](auto &here) { ... })
// -- given the place (or the event and the place) -- and several put
// together, handlers(...).
// Each a lambda's own type, kept as it is: no std::function.
template <class E, class F> struct Handle {
  F fAnswer;
  template <class P>
    requires std::invocable<const F &, const E &, const P &>
  auto on(const E &event, const P &here) const {
    return fAnswer(event, here);
  }
  template <class P>
    requires(std::invocable<const F &, const P &> &&
             !std::invocable<const F &, const E &, const P &>)
  auto on(const E &, const P &here) const {
    return fAnswer(here);
  }
};
template <class E, class F> Handle<E, F> handle(F answer) { return {std::move(answer)}; }
template <class... H> struct Handlers : H... {
  using H::on...;
};
template <class... H> Handlers<H...> handlers(H... each) { return {std::move(each)...}; }

// Answers a press with a copy of its event.
template <class E, class N> struct OnClick : N {
  using Walked = std::true_type;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  using Out = model::Types<E>;
  E fEvent;
  OnClick(E event, N node) : N(std::move(node)), fEvent(std::move(event)) {}
  bool onClick(float, float) {
    scene::pressLater(this->fState);
    return true;
  }
  E onPress() const { return fEvent; }
  bool acceptsInput() const { return true; }
};
template <class E, class N> OnClick<E, N> onClick(E event, N node) {
  return OnClick<E, N>(std::move(event), std::move(node));
}

} // namespace skiff::compose
