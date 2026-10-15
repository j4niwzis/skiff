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
template <class C, auto>
  requires requires(C &c) { c.onPress(); }
model::Types<std::remove_cvref_t<decltype(std::declval<C &>().onPress())>> outOf(Rank<1>);
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

// Pointer handling composed around an existing node. The handler sees the
// node so it can read the current text selection and laid-out children.
template <class E, class Make, class N> struct OnPointer : N {
  using Answer = E;
  using Out = Union<OutOf<N>, model::Types<E>>;
  Make fPointer;
  OnPointer(Make make, N node) : N(std::move(node)), fPointer(std::move(make)) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  using N::onPointer;
  std::optional<E> onPointer(scene::phase::bubble, const scene::pointer::down& press, scene::PointerReply& reply) {
    return std::invoke(fPointer, static_cast<N&>(*this), press, reply);
  }
};
template <class E, class Make, class N>
OnPointer<E, Make, N> onPointer(Make make, N node) {
  return OnPointer<E, Make, N>(std::move(make), std::move(node));
}

// A node dragged across: pressed, it takes the pointer, and each move until
// it is let go answers what Act makes of where across the pointer is --
// asked as the move is delivered (onPress), as a press's answer is: a
// divider dragged, the column beside it as wide as it says.
template <class Act, class N> struct OnDragAcross : N {
  using Out = Union<OutOf<N>, model::Types<std::remove_cvref_t<std::invoke_result_t<Act &, float>>>>;
  Act fAct;
  float fAt = 0.0f;
  bool fDragging = false;
  OnDragAcross(Act act, N node) : N(std::move(node)), fAct(std::move(act)) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool focusable() const { return false; }
  auto onPress() { return std::invoke(fAct, fAt); }
  using N::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down &, scene::PointerReply &reply) {
    fDragging = true;
    reply.capturePointer();
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::move &at, scene::PointerReply &reply) {
    if (!fDragging)
      return;
    fAt = at.x;
    scene::pressLater(this->fState);
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::up &, scene::PointerReply &reply) {
    fDragging = false;
    reply.releasePointer();
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::cancel &, scene::PointerReply &reply) {
    fDragging = false;
    reply.releasePointer();
  }
};
template <class Act, class N> OnDragAcross<Act, N> onDragAcross(Act act, N node) {
  return OnDragAcross<Act, N>(std::move(act), std::move(node));
}

// The other button pressed on a node -- on it, or on what is in it --
// answered with a copy of its event: what lists or opens a menu. A press on
// the node itself is its target's, which the way back up does not reach:
// taken there too; any other press there as the node takes it.
template <class E, class N> struct OnRightPress : N {
  using Answer = E;
  using Out = Union<OutOf<N>, model::Types<E>>;
  E fEvent;
  OnRightPress(E event, N node) : N(std::move(node)), fEvent(std::move(event)) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  using N::onPointer;
  std::optional<E> onPointer(scene::phase::bubble, const scene::pointer::down &press, scene::PointerReply &reply) {
    if (press.button != 3)
      return std::nullopt;
    reply.handle();
    return fEvent;
  }
  std::optional<E> onPointer(scene::phase::target, const scene::pointer::down &press, scene::PointerReply &reply) {
    if (press.button != 3) {
      scene::defaultPointer(*this, scene::phase::target{}, press, reply);
      return std::nullopt;
    }
    reply.handle();
    return fEvent;
  }
};
template <class E, class N> OnRightPress<E, N> onRightPress(E event, N node) {
  return OnRightPress<E, N>(std::move(event), std::move(node));
}

// A row longer than its room, moved sideways -- by a finger or a mouse
// dragged along it, flicked to glide on, or a sideways wheel -- and cut to
// its room. Up and down are left to the page round it: a gesture going more
// that way than along is not taken, nor is an upright wheel. A press is only
// noted, and what is under it clicked later: one that goes along is a drag
// of the row, not a choice.
template <class Line> struct SideScroll : Box<Line> {
  explicit SideScroll(Line line) : Box<Line>(hbox(0.0f, {.masking = true}), std::move(line)) {}
  [[nodiscard]] Line &line() { return std::get<0>(this->fParts); }
  [[nodiscard]] const Line &line() const { return std::get<0>(this->fParts); }
  // How far it goes along: what of the line is past its room.
  [[nodiscard]] float most() const { return std::max(0.0f, line().bounds().width() - this->fState.contentBox().width()); }
  [[nodiscard]] float offset() const noexcept { return fGesture.offset(); }
  [[nodiscard]] bool acceptsInput() const { return true; }
  using nodes::Stack::onPointer;
  template <class Phase>
    requires(std::same_as<Phase, scene::phase::bubble> || std::same_as<Phase, scene::phase::target>)
  void onPointer(const Phase &, const scene::pointer::scroll &wheel, scene::PointerReply &reply) {
    if (wheel.dx == 0.0f || most() <= 0.0f)
      return;
    fGesture.setBounds(0.0f, most());
    fGesture.glideTo(fGesture.target() - wheel.dx * 40.0f);
    scene::work::mark(this->fState.fId);
    reply.handle();
  }
  template <class Phase>
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  void onPointer(const Phase &, const scene::pointer::down &press, scene::PointerReply &reply) {
    if (press.button > 1 || most() <= 0.0f)
      return;
    fGesture.setBounds(0.0f, most());
    fArmed = true;
    fPressX = press.x;
    fPressY = press.y;
    if constexpr (std::same_as<Phase, scene::phase::capture>)
      reply.deferClick();
    if (fGesture.press(press.x))
      reply.handle();  // the press was spent catching a glide
  }
  template <class Phase>
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  void onPointer(const Phase &, const scene::pointer::move &move, scene::PointerReply &reply) {
    if (!fArmed)
      return;
    // Going more up or down than along: the page's, from here on.
    if (!fGesture.dragging() && std::abs(move.y - fPressY) >= scene::ScrollGesture::kSlop &&
        std::abs(move.y - fPressY) > std::abs(move.x - fPressX)) {
      fArmed = false;
      return;
    }
    const bool was = fGesture.dragging();
    if (!fGesture.drag(move.x, nowMs()))
      return;
    if (!was) {
      if (reply.fCaptured) {
        fArmed = false;  // something else is dragged already
        return;
      }
      reply.capturePointer();
    }
    reply.suppressHover();
    place();
    reply.handle();
  }
  template <class Phase>
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  void onPointer(const Phase &, const scene::pointer::up &, scene::PointerReply &reply) {
    finish(reply);
  }
  template <class Phase>
    requires(std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  void onPointer(const Phase &, const scene::pointer::cancel &, scene::PointerReply &reply) {
    finish(reply);
  }
  // Gliding: a frame at a time, until it rests.
  void update(double now) {
    const double dt = fLastMs > 0.0 ? now - fLastMs : 16.0;
    fLastMs = now;
    fGesture.setBounds(0.0f, most());
    if (fGesture.advance(dt))
      place();
  }
  [[nodiscard]] bool wantsTick() const { return fGesture.moving() || fGesture.dragging(); }

private:
  // Where the line is put: at the gesture's offset, its ends held to.
  void place() {
    const float at = scene::snapToPixel(fGesture.offset());
    if (at == fShown)
      return;
    fShown = at;
    line().apply({.shiftX = -at});
    this->markDamaged();
  }
  void finish(scene::PointerReply &reply) {
    fArmed = false;
    if (!fGesture.dragging())
      return;
    fGesture.release();
    scene::work::mark(this->fState.fId);  // to glide on, or spring back
    reply.releasePointer();
    reply.suppressHover();
    reply.handle();
  }
  [[nodiscard]] static double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  scene::ScrollGesture fGesture;
  float fShown = 0.0f;
  bool fArmed = false;
  float fPressX = 0.0f, fPressY = 0.0f;
  double fLastMs = 0.0;
};
template <class Line> SideScroll<Line> side_scroll(Line line) { return SideScroll<Line>(std::move(line)); }

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
