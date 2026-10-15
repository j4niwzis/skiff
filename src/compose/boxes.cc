// skiff.compose:boxes -- stacks, and the model's kinds made around a node
// (the module is skiff.compose: see compose.cc).
export module skiff.compose:boxes;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import skiff.paint;

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

// One of the nodes Shown may be (a variant of them), in a column as wide as
// where it is put and as tall as it is: a node a program's plugin or
// protocol makes of its own, held where it is put. The node is parts.shown;
// made from one of its alternatives, or in place as one of them. Another
// look said first: as tall and as wide as it is, for one.
template <class Shown> struct Holding : Stacked {
  struct parts_t {
    Shown shown;
  } parts;
  template <class Made>
    requires std::constructible_from<Shown, Made>
  explicit Holding(Made made) : Holding(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(made)) {}
  template <class Made>
    requires std::constructible_from<Shown, Made>
  Holding(Look look, Made made) : Stacked(std::move(look)), parts{.shown = Shown(std::move(made))} {}
  template <class Node, class... Args>
  explicit Holding(std::in_place_type_t<Node> which, Args &&...args)
      : Stacked(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY})), parts{.shown = Shown(which, std::forward<Args>(args)...)} {}
};

// A node of its own drawing or input, its parts placed in it by their own
// specs: its spec said where it is made.
struct Specced : scene::Node {
  explicit Specced(scene::Spec spec) { this->fState.apply(spec); }
};


// A part of a stack as the walks see a child: where it is, and how what it
// holds is handed on -- a function made for the part's type where the
// stack is made, each node it holds seen through its own table.
struct SeenPart {
  void *fAt;
  void (*fVisit)(void *part, void *context, scene::AnyChildVisit visit);
};
template <class Part> void handOn(void *part, void *context, scene::AnyChildVisit visit) {
  scene::visitChild(*static_cast<Part *>(part), [&](auto &child) { scene::visitAsAny(child, context, visit); });
}
// Outside a release build, every stack of parts (Box), whatever its parts,
// as one type the walks know: one table for all of them (see kSeenAsBase
// below). It goes over its parts as data -- the list a box makes of them as
// it is made -- so its table names none of their types: a table does not
// make the tables of what is under it, nor they theirs, down the tree. A
// box made makes its own parts' tables only, each its type's own, the same
// one wherever it is made (a box's: this one).
struct ErasedStack : nodes::Stack {
  std::vector<SeenPart> fSeen;
  template <class F> void forEachChild(F &&f) const {
    for (const SeenPart &part : fSeen)
      part.fVisit(part.fAt, std::addressof(f), +[](void *context, scene::AnyNodeRef &child) {
        (*static_cast<std::remove_reference_t<F> *>(context))(child);
      });
  }
};

// A stack of its parts, in order.
// Outside a release build, seen as an ErasedStack: its parts listed as it is
// made, moved or copied -- the list pointing at its own parts.
template <class... Parts> struct Box : std::conditional_t<scene::kErasedWalks, ErasedStack, nodes::Stack> {
  using Base = std::conditional_t<scene::kErasedWalks, ErasedStack, nodes::Stack>;
  using Walked = std::bool_constant<kAnyWalks<Parts...>>;
  using HasLocal = std::bool_constant<(bind::detail::kHasLocal<Parts> || ...)>;
  using SeenAs = ErasedStack;
  std::tuple<Parts...> fParts;
  explicit Box(Look look, Parts... parts) : fParts(std::move(parts)...) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
    this->see();
  }
  // (Outside a release build its list is made again, which allocates.)
  Box(Box &&other) noexcept(!scene::kErasedWalks && std::is_nothrow_move_constructible_v<std::tuple<Parts...>>)
      : Box::Base(std::move(other)), fParts(std::move(other.fParts)) {
    this->see();
  }
  Box(const Box &other)
    requires(std::copy_constructible<Parts> && ...)
      : Box::Base(other), fParts(other.fParts) {
    this->see();
  }
  Box &operator=(Box &&other) noexcept(!scene::kErasedWalks && std::is_nothrow_move_assignable_v<std::tuple<Parts...>>) {
    Box::Base::operator=(std::move(other));
    fParts = std::move(other.fParts);
    this->see();
    return *this;
  }
  Box &operator=(const Box &other)
    requires(std::copyable<Parts> && ...)
  {
    Box::Base::operator=(other);
    fParts = other.fParts;
    this->see();
    return *this;
  }
  // Its parts walked as they are in a release build; outside it, as listed
  // (for a node made from a box, which has a table of its own).
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    if constexpr (scene::kErasedWalks)
      self.ErasedStack::forEachChild(f);
    else
      std::apply([&](auto &...each) { (f(each), ...); }, self.fParts);
  }

private:
  // Keep lookup dependent: the typed base has no list of erased parts.
  template <class Self> void see(this Self &self) {
    if constexpr (scene::kErasedWalks)
      self.fSeen = std::apply(
          [](auto &...each) { return std::vector<SeenPart>{SeenPart{std::addressof(each), &handOn<std::remove_cvref_t<decltype(each)>>}...}; },
          self.fParts);
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

// A stack of a list's parts made from what it reads -- a value projected
// from the model or a Local's state, a search's results -- each element made
// into a part by `make`, all of them made again at each read.
template <class Make, class N> struct Listed : Many<N> {
  Make fMake;
  Listed(Look look, Make make) : Many<N>(std::move(look), {}), fMake(std::move(make)) {}
  template <std::ranges::input_range R> void read(const R &items) {
    this->fParts = items | std::views::transform(std::ref(fMake)) | std::ranges::to<std::vector>();
    this->invalidateLayout();
    this->markDamaged();
  }
};
template <class Element, class Make> auto listed(Look look, Make make) {
  return Listed<Make, std::invoke_result_t<Make &, const Element &>>(std::move(look), std::move(make));
}

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

// A subtree whose drawing can be replayed until it is damaged.
template <class N>
  requires std::derived_from<N, scene::Node>
N recorded(N node, bool on = true) {
  node.fState.setRecorded(on);
  return node;
}

// A node showing the pointer as `shape` over it.
template <class N>
  requires std::derived_from<N, scene::Node>
N with_cursor(scene::Cursor shape, N node) {
  node.fState.setCursor(shape);
  return node;
}

// Leaf options declared where the node is composed. Keep the concrete
// node type, including a text's picture provider and a widget's actions.
template <class N> requires requires(N& node) { node.setWrapped(true); }
N wrapped(N node, bool on = true) {
  node.setWrapped(on);
  return node;
}
template <class N> requires requires(N& node) { node.setElided(true); }
N elided(N node, bool on = true) {
  node.setElided(on);
  return node;
}
template <class N> requires requires(N& node) { node.setPrimary(true); }
N primary(N node, bool on = true) {
  node.setPrimary(on);
  return node;
}
template <class N> requires requires(N& node) { node.setOnNow(true); }
N on_now(N node, bool on) {
  node.setOnNow(on);
  return node;
}

// A node with its spec applied: a leaf said as it is put in its place.
template <class N>
  requires std::derived_from<N, scene::Node>
N styled(scene::Spec spec, N node) {
  node.apply(spec);
  return node;
}

} // namespace skiff::compose

// Every box, outside a release build, walked as one type. Its table is made
// in the program, not here: its drawing takes the program's paint hooks
// (scene::detail::ProgramPaint, which the program specializes), and a table
// made here had them before the program said them. It is small -- it names
// no part's type -- so made in each unit that walks one.
namespace skiff::scene {
template <class... Parts> inline constexpr bool kSeenAsBase<compose::Box<Parts...>> = kErasedWalks;
} // namespace skiff::scene

export namespace skiff::compose {
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
  using BindingChildren = std::tuple<Content>;
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

template <class Want, class N>
  requires std::move_constructible<N>
bind::Bound<Want, N> bound(N node) {
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
  requires std::derived_from<N, scene::Node>
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
      : N(std::move(node)), fEvent(std::move(event)), fLabel(std::move(label)) {
    if (!fLabel.empty()) this->fState.setCursor(scene::cursor::hand{});
  }
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
      result.fRole = spl::visit(spl::overloaded{
          [](scene::semantic_role::toggle role) -> scene::SemanticRole { return role; },
          [](scene::semantic_role::tab role) -> scene::SemanticRole { return role; },
          [](auto) -> scene::SemanticRole { return scene::semantic_role::button{}; }}, result.fRole);
      result.fLabel = fLabel;
      result.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    }
    return result;
  }
};
template <class E, class N> OnClick<E, N> onClick(E event, N node, std::string label = {}) {
  return OnClick<E, N>(std::move(event), std::move(node), std::move(label));
}

// Compute a typed answer at press time, for actions whose data can change
// while the node remains mounted.
template <class Make, class N> struct OnPress : OnClick<model::Nothing, N> {
  using Out = model::Types<std::invoke_result_t<Make&>>;
  Make fMake;
  OnPress(Make make, N node, std::string label)
      : OnClick<model::Nothing, N>({}, std::move(node), std::move(label)), fMake(std::move(make)) {}
  auto onPress() { return std::invoke(fMake); }
};
template <class Make, class N>
  requires std::invocable<Make&> && (!std::is_void_v<std::invoke_result_t<Make&>>)
OnPress<Make, N> onPress(Make make, N node, std::string label = {}) {
  return OnPress<Make, N>(std::move(make), std::move(node), std::move(label));
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
// Visibility projected from a bound part, including a scope or local model.
template <class Want, class Compute, class N>
auto shown_for(Compute compute, N node) {
  return projected<Want>(std::move(compute), ShownBy<N>(std::move(node)));
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
// A scope at the part what it reads says -- the chat chosen, as what is
// shown has it: Compute makes the scope's keys of the value read, and the
// scope is moved to them (rekey), shown that part by the next refresh of
// its model that walks it.
template <class Compute, class N> struct Rekeyed : N {
  Compute fCompute;
  Rekeyed(Compute compute, N node) : N(std::move(node)), fCompute(std::move(compute)) {}
  template <class T> void read(const T &value) {
    std::apply([&](auto &&...keys) { this->rekey(std::forward<decltype(keys)>(keys)...); }, fCompute(value));
  }
};
template <class Want, class Compute, class N>
auto rekeyed_by(Compute compute, N node) {
  return bound<Want>(Rekeyed<Compute, N>(std::move(compute), std::move(node)));
}

// What sections show: their parts, one under another, the tabs of every
// one there is -- the same, unless what is shown is a search's results,
// when the tabs stay every group's and none is lit -- and whether the tab of
// the part at the top is lit.
template <class Element> struct SectionsShown {
  std::vector<Element> parts;
  std::vector<Element> tabs;
  bool lit = true;
};

// The strips sections put their tabs in, made for the tabs' type by the
// maker sections are given. A strip takes the tabs, each with its element,
// lights one, and is the node they are shown in.
//
// All of them in rows that wrap, in a view of its own height that scrolls to
// the one lit; shown where there is more than one.
template <class Element, class Button> struct WrappedStrip : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<Button>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<Button>>;
  nodes::ScrollContainer<Many<Button>> fView;
  WrappedStrip(Look view, Look row) : fView(Many<Button>(std::move(row), {})) {
    this->fState.apply(view.fSpec);
    std::get<0>(fView.fChildren).fStack.wrap = true;
    fView.apply({.fill = true});
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) { f(self.fView); }
  [[nodiscard]] std::vector<Button> &buttons() { return std::get<0>(fView.fChildren).fParts; }
  void put(const std::vector<Element> &, std::vector<Button> made) {
    buttons() = std::move(made);
    this->setVisible(buttons().size() > 1);
    fView.invalidateLayout();
    fView.scrollToStart();
  }
  // Lit, or none (an index past them all).
  void light(std::size_t at) {
    for (auto &&[index, tab] : std::views::enumerate(buttons())) {
      const bool on = static_cast<std::size_t>(index) == at;
      if (on == tab.fState.selected())
        continue;
      tab.fState.apply({.selected = on});
      if (on && !tab.bounds().isEmpty()) {
        const auto shown = fView.toView(tab.bounds());
        if (shown.fTop < fView.bounds().fTop || shown.fBottom > fView.bounds().fBottom)
          fView.scrollTo(tab.bounds().fTop - fView.bounds().fTop);
      }
    }
  }
};
template <class Element> struct Wrapped {
  Look fView, fRow;
  template <class Button> WrappedStrip<Element, Button> operator()(std::type_identity<Button>) const { return {fView, fRow}; }
};
template <class Element> Wrapped<Element> wrapped(Look view, Look row) { return {std::move(view), std::move(row)}; }

// All of them in one row, as it is laid out -- each as wide as it says,
// growing to fill it where it says so: a panel's category row.
template <class Element, class Button> struct RowStrip : Many<Button> {
  explicit RowStrip(Look look) : Many<Button>(std::move(look), {}) {}
  void put(const std::vector<Element> &, std::vector<Button> made) {
    this->fParts = std::move(made);
    this->invalidateLayout();
  }
  void light(std::size_t at) {
    for (auto &&[index, tab] : std::views::enumerate(this->fParts))
      tab.fState.apply({.selected = static_cast<std::size_t>(index) == at});
  }
};
template <class Element> struct InRow {
  Look fLook;
  template <class Button> RowStrip<Element, Button> operator()(std::type_identity<Button>) const { return RowStrip<Element, Button>(fLook); }
};
template <class Element> InRow<Element> in_row(Look look) { return {std::move(look)}; }

// As many as its width holds, a page of them at a time, between the arrows
// that page back and on -- each arrow off where there is no page that way --
// paged to the one lit; shown where there is any.
template <class Element, class Button, class MakeArrow> struct PagedStrip : nodes::Stack {
  using Arrow = std::invoke_result_t<MakeArrow &, bool>;
  // An arrow's press: the page before, or the next.
  struct Page {
    PagedStrip *of = nullptr;
    bool next = false;
    scene::Taken operator()() const {
      of->page(next);
      return {};
    }
  };
  using ArrowButton = OnPress<Page, Arrow>;
  using Walked = std::bool_constant<kAnyWalks<Button, Arrow>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<Button> || bind::detail::kHasLocal<Arrow>>;
  MakeArrow fMakeArrow;
  float fTabWidth = 30.0f, fArrowsWidth = 60.0f;
  std::string fBackLabel, fOnLabel;
  std::optional<ArrowButton> fBack, fOn;
  Many<Button> fRow;
  std::size_t fFirst = 0, fHolds = 1;
  PagedStrip(Look look, Look row, float tabWidth, float arrowsWidth, MakeArrow makeArrow, std::string backLabel, std::string onLabel)
      : fMakeArrow(std::move(makeArrow)), fTabWidth(tabWidth), fArrowsWidth(arrowsWidth), fBackLabel(std::move(backLabel)),
        fOnLabel(std::move(onLabel)), fRow(std::move(row), {}) {
    this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    if (self.fBack)
      f(*self.fBack);
    f(self.fRow);
    if (self.fOn)
      f(*self.fOn);
  }
  [[nodiscard]] std::vector<Button> &buttons() { return fRow.fParts; }
  void put(const std::vector<Element> &, std::vector<Button> made) {
    // The arrows made where it is kept: they point at it.
    if (!fBack) {
      fBack.emplace(Page{this, false}, fMakeArrow(false), fBackLabel);
      fOn.emplace(Page{this, true}, fMakeArrow(true), fOnLabel);
    }
    buttons() = std::move(made);
    fFirst = 0;
    this->setVisible(!buttons().empty());
    this->invalidateLayout();
  }
  void page(bool next) {
    if (next && fFirst + fHolds < buttons().size())
      fFirst += fHolds;
    else if (!next)
      fFirst = fFirst > fHolds ? fFirst - fHolds : 0;
    this->fState.relayoutQuietly();
  }
  void light(std::size_t at) {
    for (auto &&[index, tab] : std::views::enumerate(buttons())) {
      const bool on = static_cast<std::size_t>(index) == at;
      if (on && !tab.fState.selected() && !tab.visible()) {
        fFirst = static_cast<std::size_t>(index) / fHolds * fHolds;
        this->fState.relayoutQuietly();
      }
      tab.fState.apply({.selected = on});
    }
  }
  void layoutChildren() {
    fHolds = static_cast<std::size_t>(std::max(1.0f, std::floor((this->fState.contentBox().width() - fArrowsWidth) / fTabWidth)));
    fFirst = buttons().empty() ? 0 : std::min(fFirst / fHolds, (buttons().size() - 1) / fHolds) * fHolds;
    for (auto &&[index, tab] : std::views::enumerate(buttons()))
      tab.setVisible(static_cast<std::size_t>(index) >= fFirst && static_cast<std::size_t>(index) < fFirst + fHolds);
    if (fBack) {
      fBack->apply({.disabled = fFirst == 0});
      fOn->apply({.disabled = fFirst + fHolds >= buttons().size()});
    }
    this->nodes::Stack::layoutChildren();
  }
};
template <class Element, class MakeArrow> struct Paged {
  Look fLook, fRow;
  float fTabWidth, fArrowsWidth;
  MakeArrow fMakeArrow;
  std::string fBackLabel, fOnLabel;
  template <class Button> PagedStrip<Element, Button, MakeArrow> operator()(std::type_identity<Button>) const {
    return {fLook, fRow, fTabWidth, fArrowsWidth, fMakeArrow, fBackLabel, fOnLabel};
  }
};
template <class Element, class MakeArrow>
Paged<Element, MakeArrow> paged(Look look, Look row, float tabWidth, float arrowsWidth, MakeArrow makeArrow,
                                std::string backLabel = "Previous page", std::string onLabel = "Next page") {
  return {std::move(look), std::move(row), tabWidth, arrowsWidth, std::move(makeArrow), std::move(backLabel), std::move(onLabel)};
}

// Two strips one over the other, each tab in the first or the second as its
// element says (`second(element)`): an emoji panel's groups over its packs.
template <class Element, class Button, class Second, class First, class Then> struct SplitStrip : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<First, Then>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<First> || bind::detail::kHasLocal<Then>>;
  Second fSecond;
  First fFirst;
  Then fThen;
  // Where each tab went: in the second, and its place there.
  std::vector<std::pair<bool, std::size_t>> fWhere;
  SplitStrip(Look look, Second second, First first, Then then)
      : fSecond(std::move(second)), fFirst(std::move(first)), fThen(std::move(then)) {
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fFirst);
    f(self.fThen);
  }
  void put(const std::vector<Element> &elements, std::vector<Button> made) {
    std::vector<Element> firstElements, thenElements;
    std::vector<Button> firstMade, thenMade;
    fWhere.clear();
    for (auto &&[element, button] : std::views::zip(elements, made)) {
      const bool second = fSecond(element);
      fWhere.emplace_back(second, second ? thenMade.size() : firstMade.size());
      (second ? thenElements : firstElements).push_back(element);
      (second ? thenMade : firstMade).push_back(std::move(button));
    }
    fFirst.put(firstElements, std::move(firstMade));
    fThen.put(thenElements, std::move(thenMade));
  }
  void light(std::size_t at) {
    const auto none = std::numeric_limits<std::size_t>::max();
    const bool known = at < fWhere.size();
    fFirst.light(known && !fWhere[at].first ? fWhere[at].second : none);
    fThen.light(known && fWhere[at].first ? fWhere[at].second : none);
  }
};
template <class Element, class Second, class MakeFirst, class MakeThen> struct Split {
  Look fLook;
  Second fSecond;
  MakeFirst fMakeFirst;
  MakeThen fMakeThen;
  template <class Button> auto operator()(std::type_identity<Button> button) const {
    using First = decltype(fMakeFirst(button));
    using Then = decltype(fMakeThen(button));
    return SplitStrip<Element, Button, Second, First, Then>(fLook, fSecond, fMakeFirst(button), fMakeThen(button));
  }
};
template <class Element, class Second, class MakeFirst, class MakeThen>
Split<Element, Second, MakeFirst, MakeThen> split(Look look, Second second, MakeFirst first, MakeThen then) {
  return {std::move(look), std::move(second), std::move(first), std::move(then)};
}

// A list of sections, one under another in a view that scrolls, and a strip
// of their tabs: a tab pressed asks its owner what that does first --
// leaving a search, say -- and brings its section to the top of the view,
// once it is laid out; the tab of the section at the top is lit, where what
// is shown says so. Made from what it reads (SectionsShown), each element's tab
// made by `makeTab`, its section by `makePart`, the strip by `makeStrip`:
// a picker's packs, an emoji panel's groups.
template <class Element, class MakeTab, class MakePart, class MakeStrip, class OnTab> struct Sections : nodes::Stack {
  using Tab = std::invoke_result_t<MakeTab &, const Element &>;
  using Part = std::invoke_result_t<MakePart &, const Element &>;
  // A tab's press: what its owner says, and its section brought.
  struct Bring {
    Sections *of = nullptr;
    std::size_t at = 0;
    std::invoke_result_t<const OnTab &, std::size_t> operator()() const {
      of->fBringing = at;
      return std::invoke(of->fOnTab, at);
    }
  };
  using TabButton = OnPress<Bring, Tab>;
  using Strip = std::invoke_result_t<const MakeStrip &, std::type_identity<TabButton>>;
  using List = nodes::ScrollContainer<Many<Part>>;
  using Walked = std::bool_constant<kAnyWalks<Part, Tab>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<Part> || bind::detail::kHasLocal<Tab>>;
  MakeTab fMakeTab;
  MakePart fMakePart;
  OnTab fOnTab;
  List fList;
  Strip fStrip;
  bool fLit = true;
  std::optional<std::size_t> fBringing;

  Sections(Look look, Look parts, MakeTab makeTab, MakePart makePart, const MakeStrip &makeStrip, OnTab onTab)
      : fMakeTab(std::move(makeTab)), fMakePart(std::move(makePart)), fOnTab(std::move(onTab)),
        fList(Many<Part>(std::move(parts), {})), fStrip(makeStrip(std::type_identity<TabButton>{})) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
    fList.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(fList.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fList);
    f(self.fStrip);
  }
  [[nodiscard]] std::vector<Part> &parts() { return std::get<0>(fList.fChildren).fParts; }

  // The parts made again, the view at its start; the tabs too, where they
  // are others.
  void read(const SectionsShown<Element> &shown) {
    parts() = shown.parts | std::views::transform(std::ref(fMakePart)) | std::ranges::to<std::vector>();
    fStrip.put(shown.tabs, shown.tabs | std::views::enumerate | std::views::transform([&](const auto &one) {
                             const auto &[at, element] = one;
                             return TabButton(Bring{this, static_cast<std::size_t>(at)}, fMakeTab(element), {});
                           }) |
                               std::ranges::to<std::vector>());
    fLit = shown.lit;
    fList.invalidateLayout();
    fList.scrollTo(0.0f);
  }
  // A section brought to the top of the view.
  void bring(std::size_t at) {
    if (at >= parts().size() || parts()[at].bounds().isEmpty())
      return;
    fList.scrollTo(std::max(0.0f, fList.current() + (fList.toView(parts()[at].bounds()).fTop - fList.bounds().fTop)));
  }
  // The section a tab asked for, brought once it is laid out; the tab of
  // the section at the top lit.
  [[nodiscard]] bool settling() const { return fBringing.has_value(); }
  void update(double) {
    if (fBringing && fBringing < parts().size() && !parts()[*fBringing].bounds().isEmpty())
      bring(*std::exchange(fBringing, std::nullopt));
    const float top = fList.bounds().fTop + 1.0f;
    // The last of those whose top has reached the view's: they come in order.
    const std::size_t lit = std::ranges::fold_left(
        std::views::iota(std::size_t{0}, parts().size()) | std::views::filter([&](std::size_t at) {
          return !parts()[at].bounds().isEmpty() && fList.toView(parts()[at].bounds()).fTop <= top;
        }),
        std::size_t{0}, [](std::size_t, std::size_t at) { return at; });
    fStrip.light(fLit ? lit : std::numeric_limits<std::size_t>::max());
  }
};
// A tab's press, where it asks nothing of its owner first.
struct OnlyBrings {
  scene::Taken operator()(std::size_t) const { return {}; }
};
template <class Element, class MakeTab, class MakePart, class MakeStrip, class OnTab = OnlyBrings>
auto sections(Look look, Look parts, MakeTab makeTab, MakePart makePart, const MakeStrip &makeStrip, OnTab onTab = {}) {
  return Sections<Element, MakeTab, MakePart, MakeStrip, OnTab>(std::move(look), std::move(parts), std::move(makeTab),
                                                                std::move(makePart), makeStrip, std::move(onTab));
}

// Placed at a point of the window a part of the model says -- in window
// coordinates, as what asked for it saw them -- out of its parent's flow,
// kept inside its parent's box; shown where there is one: a popover over
// the cell it is for.
template <class N> struct PlacedAt : N {
  std::optional<skia::SkPoint> fAt;
  explicit PlacedAt(N node) : N(std::move(node)) {
    this->fState.apply({.place = scene::anchor::kTopLeft});
    this->setVisible(false);
  }
  void read(const std::optional<skia::SkPoint> &at) {
    fAt = at;
    this->setVisible(at.has_value());
    this->invalidateLayout();
  }
  // Where it is laid out, set from where it is drawn: its parent's box, as
  // its layout last gave it, and the shift of all above it.
  [[nodiscard]] bool settling() const { return fAt.has_value() && !placed(); }
  void update(double) {
    if (!fAt || placed())
      return;
    const skia::SkRect box = this->fState.fLastConstraint;
    const auto shift = scene::work::drawnOffset(this->fState.fId);
    const skia::SkRect &self = this->fState.fBounds;
    const float x = std::clamp(fAt->fX - shift.x, box.fLeft, std::max(box.fLeft, box.fRight - self.width()));
    const float y = std::clamp(fAt->fY - shift.y, box.fTop, std::max(box.fTop, box.fBottom - self.height()));
    this->setPosition(x - box.fLeft, y - box.fTop);
  }

private:
  [[nodiscard]] bool placed() const {
    const skia::SkRect box = this->fState.fLastConstraint;
    if (box.isEmpty() || this->fState.fBounds.isEmpty())
      return false;
    const auto shift = scene::work::drawnOffset(this->fState.fId);
    const float x = std::clamp(fAt->fX - shift.x, box.fLeft, std::max(box.fLeft, box.fRight - this->fState.fBounds.width()));
    const float y = std::clamp(fAt->fY - shift.y, box.fTop, std::max(box.fTop, box.fBottom - this->fState.fBounds.height()));
    return std::abs(this->fState.fBounds.fLeft - x) < 0.5f && std::abs(this->fState.fBounds.fTop - y) < 0.5f;
  }
};
template <class Want, class Compute, class N> auto placed_at(Compute compute, N node) {
  return projected<Want>(std::move(compute), PlacedAt<N>(std::move(node)));
}

// The pointer resting on a node: `act(true)` once it has rested there `ms`,
// `act(false)` as it leaves after that -- a preview shown while it rests,
// as Telegram's pickers do. Frames are asked for only while it counts.
template <class Act, class N> struct Resting : N {
  using Walked = std::bool_constant<bind::detail::kWalks<N>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  double fMs = 0.0;
  Act fAct;
  std::optional<double> fSince;
  bool fSaid = false;
  Resting(double ms, Act act, N node) : N(std::move(node)), fMs(ms), fAct(std::move(act)) {}
  [[nodiscard]] bool settling() const { return this->hovered() && !fSaid; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  void update(double now) {
    if (!this->hovered()) {
      fSince.reset();
      if (std::exchange(fSaid, false))
        fAct(false);
      return;
    }
    if (!fSince)
      fSince = now;
    if (!fSaid && now - *fSince >= fMs) {
      fSaid = true;
      fAct(true);
    }
  }
};
template <class Act, class N> Resting<Act, N> resting(double ms, Act act, N node) {
  return Resting<Act, N>(ms, std::move(act), std::move(node));
}

// Pages in a view that scrolls, one up at a time, chosen by what it reads:
// a page made by `show` from the facts -- which says whether it is another
// page than the one up -- comes in sliding from the side `side` says and
// fading in; the same page made again is made where it was scrolled to, at
// the next frame, without a slide. The header `pin` finds in a page stays at
// the top while the rest scrolls, drawn over it. Esc on a page goes where
// `back` says, if anywhere: a settings box's pages, whose home Esc closes.
//
//   page_view<Facts, Page>(show, same, side, pin, back, first_page, spec)
template <class Facts, class Page, class Show, class Same, class Side, class Pin, class Back> struct PageView : Specced {
  Show fShow;
  Same fSame;
  Side fSide;
  Pin fPin;
  Back fBack;
  nodes::ScrollContainer<Page> fScroll;
  paint::Tween fSwap{1.0f, 200.0f, paint::movement::subtle{}};
  float fFrom = 1.0f;
  std::optional<float> fOffsetDue;
  std::optional<Facts> fShown, fDue;
  PageView(Show show, Same same, Side side, Pin pin, Back back, Page first, scene::Spec spec)
      : Specced(spec), fShow(std::move(show)), fSame(std::move(same)), fSide(std::move(side)), fPin(std::move(pin)), fBack(std::move(back)),
        fScroll(std::move(first)) {
    fScroll.apply({.fill = true});
    fScroll.setCopiesOnScroll(false);
    fit();
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) { f(self.fScroll); }
  [[nodiscard]] Page &page() { return std::get<0>(fScroll.fChildren); }
  // The facts it shows now: another page slid in, the same one made again
  // in place at the next frame.
  void read(const Facts &now) {
    if (fShown && fSame(*fShown, now)) {
      fDue = now;
      scene::work::mark(this->fState.fId);
      return;
    }
    fShow(page(), now);
    fShown = now;
    fDue.reset();
    fFrom = fSide(now);
    fit();
    keepOffset(0.0f);
    fSwap.jump(0.0f);
    fSwap.setTarget(1.0f);
    this->invalidateLayout();
  }
  // Where it is scrolled to, and kept there as it is laid out again.
  [[nodiscard]] float offset() const { return fScroll.current(); }
  void keepOffset(float at) {
    fOffsetDue = at;
    this->invalidateLayout();
  }
  // Esc: the page's way back, where it has one; false where it has none.
  bool stepBack() { return fBack(page()); }
  [[nodiscard]] bool settling() const { return fSwap.moving() || fDue.has_value(); }
  [[nodiscard]] bool wantsTick() const { return settling(); }
  void update(double nowMs) {
    if (auto due = std::exchange(fDue, std::nullopt)) {
      const float at = fScroll.current();
      fShow(page(), *due);
      fShown = std::move(due);
      fit();
      keepOffset(at);
    }
    if (fSwap.step(nowMs))
      this->invalidateLayout();
  }
  void draw(scene::Painting &painting, skia::SkCanvas *canvas, float alpha) {
    if (scene::Node *header = fPin(page()))
      header->fState.setShift(0.0f, -fScroll.contentsShift());
    scene::drawDefault(*this, painting, canvas, alpha);
  }
  void layoutChildren() {
    const skia::SkRect box = this->fState.contentBox();
    // The offset kept through measuring and the box's resizing: a view
    // otherwise follows its end when a short page becomes one that scrolls.
    if (auto at = std::exchange(fOffsetDue, std::nullopt);
        at || fScroll.bounds().width() != box.width() || fScroll.bounds().height() != box.height())
      fScroll.setCurrent(at.value_or(fScroll.current()));
    fScroll.fState.arrange(0.0f, 0.0f);
    const float value = fSwap.value();
    fScroll.fState.setAlpha(value);
    scene::layout(fScroll, skia::SkRect::MakeXYWH(box.fLeft + (1.0f - value) * 32.0f * fFrom, box.fTop, box.width(), box.height()));
  }

 private:
  // A page fills the view across, as tall as what it holds.
  void fit() {
    spl::visit([](auto &one) { one.fState.apply({.relativeSize = scene::axes::kX, .autoSize = scene::axes::kY}); }, page());
  }
};
template <class Facts, class Page, class Show, class Same, class Side, class Pin, class Back>
PageView<Facts, Page, Show, Same, Side, Pin, Back> page_view(Show show, Same same, Side side, Pin pin, Back back, Page first,
                                                            scene::Spec spec = {}) {
  return {std::move(show), std::move(same), std::move(side), std::move(pin), std::move(back), std::move(first), std::move(spec)};
}

} // namespace skiff::compose
