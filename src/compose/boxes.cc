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
  // Where along its way its parts sit, together.
  nodes::Justify fJustify = nodes::justify::start{};
};
inline Look vbox(float gap = 0.0f, scene::Spec spec = {}) {
  return {false, gap, std::move(spec)};
}
inline Look hbox(float gap = 0.0f, scene::Spec spec = {}) {
  return {true, gap, std::move(spec)};
}
// A look with its parts put along its way as said: in the middle, at the end.
inline Look justified(Look look, nodes::Justify justify) {
  look.fJustify = std::move(justify);
  return look;
}

template <class... Parts>
inline constexpr bool kAnyWalks = (bind::detail::kWalks<Parts> || ...);

// A stack laid out as its look says: the base of a node whose parts are
// named (a parts aggregate) rather than listed -- its parts each said with
// their own spec where they are made (styled), nothing set after.
struct Stacked : nodes::Stack {
  explicit Stacked(Look look, scene::Spec spec = {}) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fState.apply(spec);
    this->fStack.justify = look.fJustify;
  }
};

// A node of its own drawing or input, its parts placed in it by their own
// specs: its spec said where it is made.
struct Specced : scene::Node {
  explicit Specced(scene::Spec spec) { this->fState.apply(spec); }
};

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
    this->fStack.justify = look.fJustify;
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
    this->fStack.justify = look.fJustify;
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

// Keep a subtree polling while an external deadline is pending. The
// node's own polling, if any, remains active independently.
template <class N> struct Ticking : N {
  bool fTick;
  Ticking(bool tick, N node) : N(std::move(node)), fTick(tick) {}
  bool wantsTick() const {
    if constexpr (requires(const N& node) { node.wantsTick(); })
      return fTick || N::wantsTick();
    else
      return fTick;
  }
};
template <class N> Ticking<N> keep_ticking(bool on, N node) {
  return Ticking<N>(on, std::move(node));
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

// A subtree whose presence and constructor arguments are model data. Made
// in place: controls may refer to themselves, so a rendered temporary must
// never be moved into the tree. Make returns the arguments, not a node.
template <class Content, class Facts, class Make> struct Mounted : Specced {
  Make fMake;
  std::optional<Content> fContent;
  std::optional<Facts> fLast;
  bool fRead = false;
  explicit Mounted(Make make, scene::Spec spec = {}, bool floats = false)
      : Specced(std::move(spec)), fMake(std::move(make)) {
    this->fState.setFloats(floats);
    this->setVisible(false);
  }
  void read(const std::optional<Facts> &now) {
    if constexpr (requires { fLast == now; })
      if (fRead && fLast == now)
        return;
    fLast = now;
    fRead = true;
    if (now)
      std::apply(
          [&](auto &&...arguments) {
            fContent.emplace(std::forward<decltype(arguments)>(arguments)...);
          },
          fMake(*now));
    else
      fContent.reset();
    this->setVisible(now.has_value());
    this->invalidateLayout();
    this->markDamaged();
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fContent);
  }
  Content *shown() { return fContent ? &*fContent : nullptr; }
  const Content *shown() const { return fContent ? &*fContent : nullptr; }
};
template <class Content, class Facts, class Make>
Mounted<Content, Facts, Make> mount(Make make, scene::Spec spec = {},
                                    bool floats = false) {
  return Mounted<Content, Facts, Make>(std::move(make), std::move(spec),
                                       floats);
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
  std::string fLabel;
  OnClick(E event, N node, std::string label = {})
      : N(std::move(node)), fEvent(std::move(event)), fLabel(std::move(label)) {}
  bool onClick(float, float) {
    scene::pressLater(this->fState);
    return true;
  }
  E onPress() const { return fEvent; }
  bool acceptsInput() const { return true; }
  scene::Semantics semantics() const {
    scene::Semantics result;
    if constexpr (requires(const N& node) { node.semantics(); })
      result = N::semantics();
    if (!fLabel.empty()) {
      result.fRole = scene::semantic_role::button{};
      result.fLabel = fLabel;
      result.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    }
    return result;
  }
};
template <class E, class N> OnClick<E, N> onClick(E event, N node, std::string label = {}) {
  return OnClick<E, N>(std::move(event), std::move(node), std::move(label));
}

// Answers a key with a copy of its event: Esc, as a bar is let go.
template <class E, class N, class K> struct OnKey : N {
  using Walked = std::bool_constant<bind::detail::kWalks<N>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  using Answer = E;
  K fKey;
  E fEvent;
  OnKey(K key, E event, N node) : N(std::move(node)), fKey(key), fEvent(std::move(event)) {}
  using N::onKey;
  std::optional<E> onKey(scene::phase::bubble, const scene::key::down &press, scene::Reply &reply) {
    if (press.key != fKey)
      return std::nullopt;
    reply.handle();
    return fEvent;
  }
};
template <class K, class E, class N> OnKey<E, N, K> onKey(K key, E event, N node) {
  return OnKey<E, N, K>(key, std::move(event), std::move(node));
}

// What a node shows, read from the model: a text made from the parts it
// reads (Reads), and a node shown or hidden as they say -- never set by
// hand. Compute takes the parts' values.
struct TextOf : nodes::Text {
  explicit TextOf(nodes::Text text) : nodes::Text(std::move(text)) {}
  void read(const std::string &now) { this->setText(now); }
};
template <class... Reads, class Compute> bind::Derived<Compute, TextOf, Reads...> text_of(Compute compute, nodes::Text text) {
  return bind::Derived<Compute, TextOf, Reads...>(TextOf(std::move(text)), std::move(compute));
}
template <class N> struct ShownBy : N {
  explicit ShownBy(N node) : N(std::move(node)) {}
  void read(bool on) { this->setVisible(on); }
};
template <class... Reads, class Compute, class N> bind::Derived<Compute, ShownBy<N>, Reads...> shown_if(Compute compute, N node) {
  return bind::Derived<Compute, ShownBy<N>, Reads...>(ShownBy<N>(std::move(node)), std::move(compute));
}

// Project a bound part into the value a node reads. Unlike derived, this
// works below scopes and local state as well as at the root.
template <class Compute, class N> struct Projected : N {
  Compute fCompute;
  Projected(Compute compute, N node)
      : N(std::move(node)), fCompute(std::move(compute)) {}
  template <class T> void read(const T &value) { N::read(fCompute(value)); }
};
template <class Want, class Compute, class N>
auto projected(Compute compute, N node) {
  return bound<Want>(
      Projected<Compute, N>(std::move(compute), std::move(node)));
}
template <class N> struct SpecOf : N {
  explicit SpecOf(N node) : N(std::move(node)) {}
  void read(const scene::Spec &spec) { this->apply(spec); }
};
template <class Want, class Compute, class N>
auto spec_for(Compute compute, N node) {
  return projected<Want>(std::move(compute), SpecOf<N>(std::move(node)));
}
template <class Want, class Compute>
auto text_for(Compute compute, nodes::Text text) {
  return projected<Want>(std::move(compute), TextOf(std::move(text)));
}

} // namespace skiff::compose
