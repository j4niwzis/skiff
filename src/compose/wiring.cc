// skiff.compose:wiring -- In and Out, a >> b, b << a, a + b, place (the
// module is skiff.compose: see compose.cc).
export module skiff.compose:wiring;

import std;
import splice;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import skiff.paint;
import skia;
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

// The pointer resting on a node: once it has rested there `ms`, what
// `act(true)` answers; as it leaves after that, what `act(false)` answers --
// a preview shown while it rests, as Telegram's pickers do, said to the
// model like any press. The node is held, not derived from: its own press
// (a cell's click) stays its own. Frames are asked for only while it counts.
template <class Act, class N> struct Resting : Box<N> {
  using Out = Union<OutOf<N>, model::Types<std::remove_cvref_t<std::invoke_result_t<Act &, bool>>>>;
  double fMs = 0.0;
  Act fAct;
  std::optional<double> fSince;
  bool fSaid = false;
  bool fOn = false;
  Resting(double ms, Act act, N node)
      : Box<N>(vbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(node)), fMs(ms), fAct(std::move(act)) {}
  [[nodiscard]] bool settling() const { return this->hovered() && !fSaid; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  void update(double now) {
    if (!this->hovered()) {
      fSince.reset();
      if (std::exchange(fSaid, false))
        say(false);
      return;
    }
    if (!fSince)
      fSince = now;
    if (!fSaid && now - *fSince >= fMs) {
      fSaid = true;
      say(true);
    }
  }
  auto onPress() { return std::invoke(fAct, fOn); }

private:
  void say(bool on) {
    fOn = on;
    scene::pressLater(this->fState);
  }
};
template <class Act, class N> Resting<Act, N> resting(double ms, Act act, N node) {
  return Resting<Act, N>(ms, std::move(act), std::move(node));
}

// A node whose presses are held off while what it reads says so -- a list's
// row while rows are being selected: what is in it neither sees the press
// nor is clicked; the list takes it (spanned). As wide as its room, as tall
// as the node. Read through projected (held_off_for).
template <class N> struct HeldOff : Box<N> {
  bool fOn = false, fHeld = false;
  explicit HeldOff(N node) : Box<N>(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(node)) {}
  void read(bool on) { fOn = on; }
  [[nodiscard]] bool acceptsInput() const { return true; }
  using nodes::Stack::onPointer;
  void onPointer(scene::phase::capture, const scene::pointer::down &press, scene::PointerReply &reply) {
    fHeld = fOn && press.button <= 1;
    if (fHeld)
      reply.handle();
  }
  void onPointer(scene::phase::capture, const scene::pointer::up &, scene::PointerReply &reply) {
    if (std::exchange(fHeld, false))
      reply.handle();
  }
  void onPointer(scene::phase::capture, const scene::pointer::cancel &, scene::PointerReply &) { fHeld = false; }
};
template <class Want, class Take, class N> auto held_off_for(Take take, N node) {
  return projected<Want>(std::move(take), HeldOff<N>(std::move(node)));
}

// A window list whose rows are picked by a press -- or a span of them by a
// drag across them -- while what it reads says so (rows being selected):
// as the drag goes, what it spans (ListSpan, not done), and as it is let go,
// the span done; a press that does not move, the row it was on. The rows'
// own presses are theirs to hold off (held_off_for).
template <class Key, class Tag = void> struct ListSpan {
  Key first, last;
  bool done = false;
  friend bool operator==(const ListSpan &, const ListSpan &) = default;
};
template <class List> struct Spanned : Box<List> {
  using Key = std::remove_cvref_t<decltype(std::declval<List &>().rows().fRows.front().fKey)>;
  using Span = ListSpan<Key, typename List::ListTag>;
  using Out = Union<OutOf<List>, model::Types<Span>>;
  bool fOn = false;
  std::optional<Key> fFirst, fLast;
  bool fMoved = false;
  float fX = 0.0f, fY = 0.0f;
  std::optional<Span> fDue;
  explicit Spanned(List list) : Box<List>(vbox(0.0f, {.fill = true}), std::move(list)) {}
  void read(bool on) { fOn = on; }
  [[nodiscard]] List &list() { return std::get<0>(this->fParts); }
  [[nodiscard]] bool acceptsInput() const { return true; }
  using nodes::Stack::onPointer;
  void onPointer(scene::phase::capture, const scene::pointer::down &press, scene::PointerReply &) {
    fFirst.reset();
    if (!fOn || press.button > 1)
      return;
    fFirst = this->keyAt(press.x, press.y);
    fLast = fFirst;
    fMoved = false;
    fX = press.x;
    fY = press.y;
  }
  void onPointer(scene::phase::capture, const scene::pointer::move &at, scene::PointerReply &reply) {
    if (!fFirst)
      return;
    if (!fMoved && std::hypot(at.x - fX, at.y - fY) <= 6.0f)
      return;
    if (!fMoved) {
      fMoved = true;
      reply.capturePointer();
    }
    if (const auto here = this->keyAt(at.x, at.y); here && here != fLast) {
      fLast = here;
      this->say(Span{*fFirst, *fLast, false});
    }
    reply.handle();
  }
  void onPointer(scene::phase::capture, const scene::pointer::up &, scene::PointerReply &reply) {
    if (!fFirst)
      return;
    if (fMoved)
      reply.releasePointer();
    this->say(Span{*fFirst, fLast.value_or(*fFirst), true});
    fFirst.reset();
    reply.handle();
  }
  void onPointer(scene::phase::capture, const scene::pointer::cancel &, scene::PointerReply &reply) {
    if (std::exchange(fFirst, std::nullopt) && fMoved)
      reply.releasePointer();
  }
  std::optional<Span> onPress() { return std::exchange(fDue, std::nullopt); }
  // The list's own, where it has them: whether the reader is away from its
  // end, and how many came meanwhile (with_way_down).
  [[nodiscard]] bool away() const
    requires requires(const List &list) { list.away(); }
  {
    return std::get<0>(this->fParts).away();
  }
  [[nodiscard]] std::size_t unseen() const
    requires requires(const List &list) { list.unseen(); }
  {
    return std::get<0>(this->fParts).unseen();
  }

private:
  // The row under a point in the view, by its key.
  [[nodiscard]] std::optional<Key> keyAt(float x, float y) {
    auto &shown = this->list();
    for (const auto &one : shown.rows().fRows)
      if (!one.bounds().isEmpty() && shown.toView(one.bounds()).contains(x, y))
        return one.fKey;
    return std::nullopt;
  }
  void say(Span span) {
    fDue = std::move(span);
    scene::pressLater(this->fState);
  }
};
template <class Want, class On, class List> auto spanned_for(On on, List list) {
  return projected<Want>(std::move(on), Spanned<List>(std::move(list)));
}

// A right press on a node -- a long press, on a phone -- answered by what
// `ask(node, x, y)` makes of where it was: a message's menu, from what is
// selected in it and the link pressed. Nothing asked, not taken.
template <class Ask, class N> struct OnRightPressAt : N {
  using Answer = std::remove_cvref_t<decltype(*std::declval<std::invoke_result_t<Ask &, N &, float, float>>())>;
  using Out = Union<OutOf<N>, model::Types<Answer>>;
  Ask fAsk;
  OnRightPressAt(Ask ask, N node) : N(std::move(node)), fAsk(std::move(ask)) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  using N::onPointer;
  std::optional<Answer> onPointer(scene::phase::bubble, const scene::pointer::down &press, scene::PointerReply &reply) {
    return this->asked(press, reply);
  }
  std::optional<Answer> onPointer(scene::phase::target, const scene::pointer::down &press, scene::PointerReply &reply) {
    return this->asked(press, reply);
  }

private:
  std::optional<Answer> asked(const scene::pointer::down &press, scene::PointerReply &reply) {
    if (press.button != 3)
      return std::nullopt;
    auto answer = std::invoke(fAsk, static_cast<N &>(*this), press.x, press.y);
    if (answer)
      reply.handle();
    return answer;
  }
};
template <class Ask, class N> OnRightPressAt<Ask, N> onRightPressAt(Ask ask, N node) {
  return OnRightPressAt<Ask, N>(std::move(ask), std::move(node));
}

// A node swiped to the left to do something with it -- a message answered,
// as Telegram's: a press that moves left at once -- more across than up or
// down, within a quarter of a second, nothing holding the pointer yet --
// takes the pointer and moves the node with it, as far as `most`; let go
// past `reach`, what `act` answers; either way it goes back. `mark` beside
// it, out of the flow, told how far it is as a share of the reach
// (`show(mark, share)`): an arrow filling as it gets there. The node is
// held, not derived from: its own presses stay its own.
template <class Act, class Mark, class Show, class N> struct Swiped : Box<N, Mark> {
  using Out = Union<OutOf<N>, model::Types<std::remove_cvref_t<std::invoke_result_t<Act &>>>>;
  Act fAct;
  Show fShow;
  float fReach, fMost;
  paint::Tween fShift{0.0f, 180.0f, paint::movement::subtle{}};
  float fDrawn = 0.0f;
  bool fArmed = false, fDragging = false;
  float fX = 0.0f, fY = 0.0f;
  std::chrono::steady_clock::time_point fPressed{};
  Swiped(float reach, float most, Act act, Show show, Mark mark, N node)
      : Box<N, Mark>(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(node), std::move(mark)),
        fAct(std::move(act)), fShow(std::move(show)), fReach(reach), fMost(most) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  using nodes::Stack::onPointer;
  void onPointer(scene::phase::capture, const scene::pointer::down &press, scene::PointerReply &) {
    fArmed = press.button <= 1;
    fX = press.x;
    fY = press.y;
    fPressed = std::chrono::steady_clock::now();
  }
  void onPointer(scene::phase::capture, const scene::pointer::move &at, scene::PointerReply &reply) {
    if (fDragging) {
      fShift.jump(std::clamp(at.x - fX, -fMost, 0.0f));
      this->markDamaged();
      reply.handle();
      return;
    }
    if (!fArmed)
      return;
    // Something holds the pointer already -- a text being selected: this
    // drag is its, not a swipe.
    if (reply.fCaptured) {
      fArmed = false;
      return;
    }
    const float dx = at.x - fX, dy = at.y - fY;
    if (std::abs(dx) < 8.0f && std::abs(dy) < 8.0f)
      return;
    fArmed = false;
    if (dx >= 0.0f || std::abs(dx) < 2.0f * std::abs(dy) ||
        std::chrono::steady_clock::now() - fPressed > std::chrono::milliseconds(250))
      return;
    fDragging = true;
    fShift.jump(std::clamp(dx, -fMost, 0.0f));
    reply.capturePointer();
    reply.suppressHover();
    reply.handle();
  }
  void onPointer(scene::phase::capture, const scene::pointer::up &, scene::PointerReply &reply) {
    fArmed = false;
    if (!std::exchange(fDragging, false))
      return;
    if (fShift.value() <= -fReach)
      scene::pressLater(this->fState);
    fShift.setTarget(0.0f);
    scene::work::mark(this->fState.fId);  // ticked back: nothing else asks for its frames
    reply.releasePointer();
    reply.handle();
  }
  void onPointer(scene::phase::capture, const scene::pointer::cancel &, scene::PointerReply &reply) {
    fArmed = false;
    if (!std::exchange(fDragging, false))
      return;
    fShift.setTarget(0.0f);
    scene::work::mark(this->fState.fId);
    reply.releasePointer();
  }
  auto onPress() { return std::invoke(fAct); }
  [[nodiscard]] bool settling() const { return fShift.moving() || fShift.value() != fDrawn; }
  [[nodiscard]] bool wantsTick() const { return settling(); }
  void update(double nowMs) {
    if (!fShift.step(nowMs) && fShift.value() == fDrawn)
      return;
    fDrawn = fShift.value();
    auto &[node, mark] = this->fParts;
    node.apply({.shiftX = fDrawn});
    std::invoke(fShow, mark, std::clamp(-fDrawn / fReach, 0.0f, 1.0f));
  }
};
template <class Act, class Mark, class Show, class N>
Swiped<Act, Mark, Show, N> swiped(float reach, float most, Act act, Show show, Mark mark, N node) {
  return Swiped<Act, Mark, Show, N>(reach, most, std::move(act), std::move(show), std::move(mark), std::move(node));
}

// A wash over a node in a colour, as the model says: held while it is said
// to be (a message selected), and a flash that fades -- each time it is
// told a newer one (a message jumped to). What it reads: Wash.
struct Wash {
  bool held = false;
  std::uint64_t flash = 0;  // a newer one: flashed again
  friend bool operator==(const Wash &, const Wash &) = default;
};
template <class N> struct Washed : N {
  skia::SkColor fColour;
  float fHeld, fFlash;
  paint::Tween fFading{0.0f, 1200.0f};
  Wash fNow{};
  bool fRead = false;
  Washed(skia::SkColor colour, float held, float flash, N node) : N(std::move(node)), fColour(colour), fHeld(held), fFlash(flash) {}
  void read(const Wash &now) {
    const bool flashed = fRead && now.flash != fNow.flash;
    fRead = true;
    fNow = now;
    if (flashed) {
      fFading.jump(1.0f);
      fFading.setTarget(0.0f);
      scene::work::mark(this->fState.fId);
    }
    paint();
  }
  [[nodiscard]] bool settling() const { return fFading.moving(); }
  [[nodiscard]] bool wantsTick() const { return settling(); }
  void update(double nowMs) {
    if (fFading.step(nowMs))
      paint();
  }

private:
  void paint() {
    const float strength = std::max(fFlash * fFading.value(), fNow.held ? fHeld : 0.0f);
    this->apply({.background = strength > 0.0f ? (fColour & 0x00FFFFFFu) | (static_cast<skia::SkColor>(std::lround(strength)) << 24)
                                               : skia::SkColor{0}});
    this->markDamaged();
  }
};
template <class Want, class Compute, class N> auto washed_for(skia::SkColor colour, float held, float flash, Compute compute, N node) {
  return projected<Want>(std::move(compute), Washed<N>(colour, held, flash, std::move(node)));
}

// A node that comes in moving, where it is made so: from below, fading in,
// as a message that has just come in Telegram. Unseen until its first frame
// is laid out, so it is first seen where it goes.
template <class N> struct Appearing : N {
  paint::Tween fIn{1.0f, 220.0f};
  float fFrom;
  Appearing(bool now, float from, N node) : N(std::move(node)), fFrom(from) {
    if (!now)
      return;
    fIn.jump(0.0f);
    fIn.setTarget(1.0f);
    this->apply({.alpha = 0.0f, .shiftY = fFrom});
  }
  [[nodiscard]] bool settling() const { return fIn.moving(); }
  [[nodiscard]] bool wantsTick() const { return settling(); }
  void update(double nowMs) {
    if (fIn.step(nowMs)) {
      const float shown = fIn.value();
      this->apply({.alpha = shown, .shiftY = (1.0f - shown) * fFrom});
    }
  }
};
template <class N> Appearing<N> appearing(bool now, N node, float from = 12.0f) { return Appearing<N>(now, from, std::move(node)); }

// A node laid out, then arranged and settled by rules of its own -- a
// message's bubble: its time beside its last line where they fit, a
// sticker's name and quote beside it. The rules are Policy's: a type with
// static members and the state it keeps (Policy::State) -- prepare(N&,
// State&) before its parts are laid out, arrange(N&, State&) after,
// settle(N&, State&) at each frame it asks for (settling(const N&, const
// State&)) -- a template parameter, not a callback.
template <class Policy, class N> struct Arranged : N {
  typename Policy::State fRules;
  Arranged(typename Policy::State rules, N node) : N(std::move(node)), fRules(std::move(rules)) {}
  void layoutChildren() {
    Policy::prepare(static_cast<N &>(*this), fRules);
    this->nodes::Stack::layoutChildren();
    Policy::arrange(static_cast<N &>(*this), fRules);
  }
  [[nodiscard]] bool settling() const { return Policy::settling(static_cast<const N &>(*this), fRules); }
  [[nodiscard]] bool wantsTick() const { return settling(); }
  void update(double) { Policy::settle(static_cast<N &>(*this), fRules); }
};
template <class Policy, class N> Arranged<Policy, N> arranged(typename Policy::State rules, N node) {
  return Arranged<Policy, N>(std::move(rules), std::move(node));
}

// A press at a place in a node, asked of it there -- a quote in a text,
// pressed, the message it quotes: `ask(node, x, y)` answers, or says
// nothing and the press is not taken.
template <class Ask, class N> struct OnClickAt : N {
  using Answer = std::remove_cvref_t<decltype(*std::declval<std::invoke_result_t<Ask &, N &, float, float>>())>;
  using Out = Union<OutOf<N>, model::Types<Answer>>;
  Ask fAsk;
  std::optional<Answer> fAsked;
  OnClickAt(Ask ask, N node) : N(std::move(node)), fAsk(std::move(ask)) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  bool onClick(float x, float y) {
    fAsked = std::invoke(fAsk, static_cast<N &>(*this), x, y);
    if (!fAsked)
      return false;
    scene::pressLater(this->fState);
    return true;
  }
  std::optional<Answer> onPress() { return std::exchange(fAsked, std::nullopt); }
};
template <class Ask, class N> OnClickAt<Ask, N> onClickAt(Ask ask, N node) { return OnClickAt<Ask, N>(std::move(ask), std::move(node)); }

// A list in a view that scrolls, made of a window of a longer one -- a
// chat's messages, a thread's: its rows by their keys, each kept while the
// facts it was made from are the same (MemoRows), the window chosen by
// what reads the model (Shown: the window's items, and whether there is
// more of the list above and below it). Near an edge with more beyond it,
// it asks for its window to slide (ListEdge); told to aim at a key, it
// brings that row into view -- stepping the view a page at a time toward a
// row made but not laid out yet -- and answers that it has (ListAimed). It
// keeps no list of its own and calls nothing.
// What a list answers, tagged by which list it is (Tag): a program with two
// -- a chat's and a thread's -- tells them apart by type.
template <class Key, class Tag = void> struct ListAimed {
  Key key;
  friend bool operator==(const ListAimed &, const ListAimed &) = default;
};
template <class Tag = void> struct ListEdge {
  bool older = false;  // the top; else the bottom
  friend bool operator==(ListEdge, ListEdge) = default;
};
// What the reader sees of it, said as it changes: the rows at the view's
// top, middle and bottom (the last wholly above the view's bottom), and
// whether it is at its end -- what is read, and how far a way back is
// come down to. Not while it is aiming: what it passes on its way is not
// seen.
template <class Key, class Tag = void> struct ListSeen {
  std::optional<Key> top, middle, bottom;
  bool end = false;
  friend bool operator==(const ListSeen &, const ListSeen &) = default;
};
template <class Item, class Key> struct WindowShown {
  std::vector<Item> items;
  bool more_above = false, more_below = false;
  // What to aim at, and the aim's number: a newer one aimed at again. Where
  // in the view: its top this far under the view's top (a chat opened at
  // what was read), else in the middle -- its top, where it is taller than
  // the view; or, where the row says where a stretch of it is (`aim_line`,
  // by Locate), that line at the view's upper middle.
  std::optional<Key> aim;
  std::uint64_t aim_number = 0;
  std::optional<float> aim_top;
  std::optional<std::string> aim_line;
};
// Where in a row a stretch of it is, as the row knows it: none by default.
struct NoLocate {
  template <class Row> std::optional<float> operator()(const Row &, const std::string &) const { return std::nullopt; }
};
template <class Key, class View, class Row, class Make, class Locate = NoLocate, class Tag = void>
class WindowList : public nodes::ScrollContainer<nodes::MemoRows<Key, View, Row, Make>> {
public:
  using Rows = nodes::MemoRows<Key, View, Row, Make>;
  using BindingChildren = std::tuple<Rows>;
  using Aimed = ListAimed<Key, Tag>;
  using Edge = ListEdge<Tag>;
  using Seen = ListSeen<Key, Tag>;
  using ListTag = Tag;
  using Answer = std::variant<Aimed, Edge, Seen>;
  using Out = Union<OutOf<Rows>, model::Types<Answer>>;
  // Its rows a column as wide as the view and as tall as they are: the
  // scroll's extent theirs.
  explicit WindowList(Make make) : nodes::ScrollContainer<Rows>(Rows(std::move(make))) {
    rows().fState.apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  [[nodiscard]] Rows &rows() { return std::get<0>(this->fChildren); }
  [[nodiscard]] const Rows &rows() const { return std::get<0>(this->fChildren); }
  // A row by its key, where it is made.
  [[nodiscard]] typename Rows::Kept *row(const Key &key) {
    const auto found = std::ranges::find(rows().fRows, key, &Rows::Kept::fKey);
    return found == rows().fRows.end() ? nullptr : &*found;
  }
  template <class Item> void read(const WindowShown<Item, Key> &shown) {
    if (rows().read(shown.items))
      this->invalidateLayout();
    fMoreAbove = shown.more_above;
    fMoreBelow = shown.more_below;
    if (shown.aim && shown.aim_number != fAimNumber) {
      fAimNumber = shown.aim_number;
      fAim = shown.aim;
      fAimTop = shown.aim_top;
      fAimLine = shown.aim_line;
      fAimedAt.reset();
      fAimFrames = 0;
      scene::work::mark(this->fState.fId);
    }
    // Another window: the edges may be asked about again.
    if (!rows().fRows.empty())
      fWindow = std::pair{rows().fRows.front().fKey, rows().fRows.back().fKey};
  }
  // Another list shown in it: what it aimed at in the last let go.
  void forgetAim() {
    fAim.reset();
    fAimedAt.reset();
    fSaid.reset();
    fSeenAt.reset();
  }
  [[nodiscard]] bool settling() const { return fAim.has_value() || !fDue.empty() || nodes::ScrollContainer<Rows>::settling(); }
  void update(double nowMs) {
    nodes::ScrollContainer<Rows>::update(nowMs);
    // One answer a frame, the first said first.
    if (!fDue.empty())
      scene::pressLater(this->fState);
    if (fAim) {
      this->aimed();
      return;
    }
    this->seen();
    // Near an edge of what is made, with more beyond it: asked once for each
    // window.
    const float ahead = std::max(300.0f, this->bounds().height() * 1.5f);
    if (fMoreAbove && this->current() <= ahead && fAsked != fWindow) {
      fAsked = fWindow;
      this->say(Edge{true});
    } else if (fMoreBelow && this->current() >= this->extent() - ahead && fAsked != fWindow) {
      fAsked = fWindow;
      this->say(Edge{false});
    }
  }
  std::optional<Answer> onPress() {
    if (fDue.empty())
      return std::nullopt;
    Answer first = std::move(fDue.front());
    fDue.pop_front();
    return first;
  }

private:
  // The row aimed at: in view once it is laid out; a page at a time toward
  // it while it is made but not laid out; kept waiting while nothing is laid
  // out yet, or it is not made yet (its window coming).
  void aimed() {
    auto &all = rows().fRows;
    const auto found = std::ranges::find(all, *fAim, &Rows::Kept::fKey);
    if (found == all.end()) {
      if (fAimedAt)
        fAim.reset();  // gone, once aimed at
      return;
    }
    if (!found->bounds().isEmpty()) {
      // Aimed again at each frame while what is above it moves it; let go
      // once the view is still where it was aimed -- or after three seconds
      // of frames, where it never settles. Its arrival said at the first aim,
      // where a flash of it is seen.
      const skia::SkRect view = this->bounds();
      const skia::SkRect box = this->toView(found->bounds());
      const float above = fAimTop ? *fAimTop : box.height() < view.height() ? (view.height() - box.height()) * 0.5f : 0.0f;
      float to = std::max(0.0f, this->current() + (box.fTop - view.fTop) - above);
      if (fAimLine)
        if (const auto line = std::invoke(fLocate, found->fRow, *fAimLine))
          to = std::max(0.0f, this->current() + (box.fTop + *line - view.fTop) - view.height() * 0.4f);
      if (!fAimedAt) {
        // Far away -- a window just made around it -- put there at once, not
        // glided to through it; near, glided to.
        if (std::abs(to - this->current()) > view.height() * 2.0f)
          this->setCurrent(to);
        else
          this->scrollTo(to);
        fAimedAt = to;
        this->say(Aimed{*fAim});
      } else if (std::abs(to - *fAimedAt) > 1.0f) {
        this->setCurrent(to);
        fAimedAt = to;
      } else if (!this->moving() || ++fAimFrames > 180) {
        fAim.reset();
      }
      return;
    }
    if (this->moving())
      return;
    const auto laid = std::ranges::find_if(all, [](const auto &one) { return !one.bounds().isEmpty(); });
    if (laid == all.end())
      return;
    const float page = this->bounds().height();
    this->setCurrent(std::max(0.0f, this->current() + (found < laid ? -page : page)));
    this->markDamaged();
  }
  void say(Answer answer) {
    fDue.push_back(std::move(answer));
    scene::work::mark(this->fState.fId);
  }
  // What is in view, said where it changed -- looked at again only where
  // the view or its rows moved.
  void seen() {
    const auto &all = rows().fRows;
    const std::tuple now{this->current(), this->extent(), all.size(), all.empty() ? std::optional<Key>() : std::optional<Key>(all.back().fKey)};
    if (fSeenAt == now)
      return;
    fSeenAt = now;
    const skia::SkRect view = this->bounds();
    Seen said{.end = this->atEnd(1.0f)};
    for (const auto &one : all) {
      if (one.bounds().isEmpty() || !one.visible())
        continue;
      const skia::SkRect box = this->toView(one.bounds());
      if (box.fBottom <= view.fTop || box.fTop >= view.fBottom)
        continue;
      if (!said.top)
        said.top = one.fKey;
      if (box.fTop <= view.centerY())
        said.middle = one.fKey;
      if (box.fBottom <= view.fBottom + 1.0f)
        said.bottom = one.fKey;
    }
    if (said != fSaid) {
      fSaid = said;
      this->say(std::move(said));
    }
  }
  bool fMoreAbove = false, fMoreBelow = false;
  std::optional<Key> fAim;
  std::uint64_t fAimNumber = 0;
  std::optional<float> fAimTop, fAimedAt;
  std::optional<std::string> fAimLine;
  int fAimFrames = 0;
  [[no_unique_address]] Locate fLocate{};
  std::optional<std::pair<Key, Key>> fWindow, fAsked;
  std::deque<Answer> fDue;
  std::optional<std::tuple<float, float, std::size_t, std::optional<Key>>> fSeenAt;
  std::optional<Seen> fSaid;
};
template <class Key, class View, class Row, class Make, class Locate = NoLocate, class Tag = void>
WindowList<Key, View, Row, Make, Locate, Tag> window_list(Make make) {
  return WindowList<Key, View, Row, Make, Locate, Tag>(std::move(make));
}

// A window_list whose list is read where it is kept -- in the model it is
// bound to (M), through Source, a type with static members -- and told by
// another (the program's shown model) which list, how, and what to aim at:
//
//   using Told = ...;  what it is told: of (which list, optional), aim
//                      (WindowAim<Key>), and whatever else its items need
//                      (a chat's settings); compared to see it changed
//   revision(const M &, const Told &) -> model::Revision  moved when the list did
//   count(const M &, const Told &) -> std::size_t
//   key(const M &, const Told &, std::size_t) -> Key
//   index(const M &, const Told &, const Key &) -> std::optional<std::size_t>
//   items(const M &, const Told &, std::size_t from, std::size_t to,
//         const std::optional<Key> &newest) -> std::vector<Item>
//   counts(const M &, const Told &, std::size_t) -> bool   one come while away, counted
//     (newest: the newest it showed of this list -- those after it have just
//     come, where it was at the end)
//
// Only its window's items are made, all at once -- what one row needs of its
// neighbours (a run, who has read up to it) worked out once; its window --
// kept by the keys at its ends, so that what comes in above does not move it
// -- slides as the view nears an edge, and is made around what it is told to
// aim at. Past the list's ends -- history not here yet -- it asks
// (ListEdge), as a window_list does.
template <class Key> struct WindowAim {
  std::optional<Key> key;
  // Aimed at its end instead -- its newest made, the view glided down.
  bool end = false;
  std::uint64_t number = 0;
  std::optional<float> top;
  std::optional<std::string> line;
  friend bool operator==(const WindowAim &, const WindowAim &) = default;
};
struct WindowSizes {
  std::size_t first = 80, most = 240, step = 16;
};
template <class Source, class M, class Key, class View, class Row, class Make, class Locate = NoLocate, class Tag = void>
class WindowFrom : public WindowList<Key, View, Row, Make, Locate, Tag> {
  using List = WindowList<Key, View, Row, Make, Locate, Tag>;

public:
  using Told = typename Source::Told;
  WindowFrom(Make make, WindowSizes sizes = {}) : List(std::move(make)), fSizes(sizes) {}
  // How the runtime walks refresh it from its model (outside a release
  // build), made where it is made.
  bind::Refreshes<1> fRefreshes = bind::refreshes(bind::refreshFrom<WindowFrom, M>());
  void read(const Told &told) {
    const bool another = !fTold || fTold->of != told.of;
    const bool anew = !fTold || told.aim.number != fTold->aim.number;
    const bool aimed = anew && told.aim.key;
    const bool ended = anew && told.aim.end && fTold;
    if (fTold && *fTold == told)
      return;
    if (another) {
      // Where the list left was scrolled to, and its window: kept for it --
      // at its end, it comes back at its end.
      if (fTold && fTold->of)
        fLeft.insert_or_assign(*fTold->of, Left{this->atEnd(40.0f) ? std::nullopt : this->topRow(), fFrom, fTo, fToEnd});
      fFrom.reset();
      fTo.reset();
      fToEnd = true;
      fShownLast.reset();
      fShownEnd = false;
      fUnseen = 0;
      fRestore.reset();
      this->forgetAim();
      if (told.of)
        if (const auto kept = fLeft.find(*told.of); kept != fLeft.end()) {
          fFrom = kept->second.from;
          fTo = kept->second.to;
          fToEnd = kept->second.toEnd;
          fRestore = kept->second.at;
        }
      fOpened = true;
      fCountWas.reset();
    }
    fTold = told;
    fSeen.reset();
    fAimNew = aimed;
    if (ended && !another)
      this->toEnd();
    else
      this->show();
  }
  // The model it is bound to, moved: its window made again where its list
  // moved.
  void refresh(const M &model) {
    fModel = &model;
    this->show();
  }
  // Slid; the row at the top of the view where it was left, put back there
  // once laid out; back at the end, nothing more has come unseen.
  void update(double nowMs) {
    List::update(nowMs);
    if (std::exchange(fSlide, false)) {
      fSeen.reset();
      this->show();
    }
    if (fRestore)
      if (const auto *kept = this->row(fRestore->first); !kept)
        fRestore.reset();
      else if (!kept->bounds().isEmpty()) {
        const float top = this->toView(kept->bounds()).fTop - this->bounds().fTop;
        this->setCurrent(std::max(0.0f, this->current() + top - std::exchange(fRestore, std::nullopt)->second));
      }
    if (fUnseen && this->atEnd(40.0f))
      fUnseen = 0;
  }
  std::optional<typename List::Answer> onPress() {
    auto asked = List::onPress();
    // At an edge of what is made, with more of the list beyond it: slid
    // here, not asked of anyone; past the list's own end, asked on.
    if (asked && std::visit(spl::overloaded{[&](const typename List::Edge &edge) { return this->slides(edge.older); },
                                            [](const auto &) { return false; }},
                            *asked))
      return std::nullopt;
    return asked;
  }

private:
  [[nodiscard]] bool ready() const { return fModel && fTold && fTold->of; }
  bool slides(bool older) {
    if (!ready())
      return false;
    auto [from, to] = this->window();
    const std::size_t count = Source::count(*fModel, *fTold);
    if (older && from > 0) {
      from = from > fSizes.step ? from - fSizes.step : 0;
      to = std::min(to, from + fSizes.most);
    } else if (!older && to < count) {
      to = std::min(count, to + fSizes.step);
      from = to - from > fSizes.most ? to - fSizes.most : from;
    } else {
      return false;
    }
    this->keep(from, to);
    fSlide = true;
    scene::work::mark(this->fState.fId);
    return true;
  }
  [[nodiscard]] std::pair<std::size_t, std::size_t> window() const {
    const std::size_t count = Source::count(*fModel, *fTold);
    std::size_t to = count;
    if (!fToEnd && fTo)
      if (const auto at = Source::index(*fModel, *fTold, *fTo))
        to = *at + 1;
    std::size_t from = to > fSizes.first ? to - fSizes.first : 0;
    if (fFrom)
      if (const auto at = Source::index(*fModel, *fTold, *fFrom); at && *at <= to)
        from = *at;
    if (to - from > fSizes.most)
      from = to - fSizes.most;
    return {from, to};
  }
  void keep(std::size_t from, std::size_t to) {
    const std::size_t count = Source::count(*fModel, *fTold);
    to = std::min(to, count);
    from = std::min(from, to);
    fFrom = from < count ? std::optional<Key>(Source::key(*fModel, *fTold, from)) : std::nullopt;
    fTo = to > 0 ? std::optional<Key>(Source::key(*fModel, *fTold, to - 1)) : std::nullopt;
    fToEnd = to == count;
  }
  void show() {
    if (!ready())
      return;
    const model::Revision now = Source::revision(*fModel, *fTold);
    if (fSeen == now)
      return;
    fSeen = now;
    // Aimed at anew: its window made around it, where it is held and not made.
    if (std::exchange(fAimNew, false))
      if (const auto at = Source::index(*fModel, *fTold, *fTold->aim.key)) {
        const auto [from, to] = this->window();
        if (*at < from || *at >= to)
          this->keep(*at > fSizes.first / 2 ? *at - fSizes.first / 2 : 0, *at + fSizes.first / 2);
      }
    const auto [from, to] = this->window();
    this->keep(from, to);
    using Item = typename decltype(Source::items(*fModel, *fTold, 0, 0, std::optional<Key>()))::value_type;
    const std::optional<Key> newest = fToEnd && fShownEnd ? fShownLast : std::nullopt;
    fShownLast = to > 0 ? std::optional<Key>(Source::key(*fModel, *fTold, to - 1)) : std::nullopt;
    fShownEnd = fToEnd;
    // What came while the reader was away from the end: counted, where the
    // source counts it (Source::counts) -- what the way down says.
    const bool atEnd = this->atEnd(40.0f);
    const std::size_t count = Source::count(*fModel, *fTold);
    if (fCountWas && !atEnd && count > *fCountWas)
      fUnseen += static_cast<std::size_t>(std::ranges::count_if(std::views::iota(*fCountWas, count), [&](std::size_t i) { return Source::counts(*fModel, *fTold, i); }));
    fCountWas = count;
    WindowShown<Item, Key> shown{.items = Source::items(*fModel, *fTold, from, to, newest),
                                 // At the top: the window slides, or the history is asked for.
                                 .more_above = true,
                                 .more_below = !fToEnd,
                                 .aim = fTold->aim.key,
                                 .aim_number = fTold->aim.number,
                                 .aim_top = fTold->aim.top,
                                 .aim_line = fTold->aim.line};
    const bool opened = fOpened;
    List::read(shown);
    // Opened: where it was left, else at its end -- unless it aims elsewhere.
    // At its end, it follows what comes: glided only to a newer item.
    if (opened) {
      fOpened = false;
      if (!fRestore && !fTold->aim.key)
        this->scrollToEnd(false);
    } else if (atEnd && fToEnd && !fTold->aim.key && newest && fShownLast != newest) {
      // Followed at its end by the scroll itself; a newer item glided to.
      this->scrollToEnd();
    }
    if (atEnd)
      fUnseen = 0;
  }
public:
  // The reader away from the end, and how many have come meanwhile.
  [[nodiscard]] bool away() const { return !this->atEnd(40.0f) || !fToEnd; }
  [[nodiscard]] std::size_t unseen() const { return fUnseen; }
  // Back to the end: its newest made, the view glided down.
  void toEnd() {
    fFrom.reset();
    fTo.reset();
    fToEnd = true;
    fUnseen = 0;
    fSeen.reset();
    this->show();
    this->scrollToEnd();
  }

private:
  // The first row seen at the view's top, and how far under the top it is.
  [[nodiscard]] std::optional<std::pair<Key, float>> topRow() {
    const skia::SkRect view = this->bounds();
    for (auto &one : this->rows().fRows)
      if (!one.bounds().isEmpty())
        if (const skia::SkRect box = this->toView(one.bounds()); box.fBottom > view.fTop)
          return std::pair{one.fKey, box.fTop - view.fTop};
    return std::nullopt;
  }
  struct Left {
    std::optional<std::pair<Key, float>> at;
    std::optional<Key> from, to;
    bool toEnd = true;
  };
  std::map<typename decltype(Told::of)::value_type, Left> fLeft;
  std::optional<std::pair<Key, float>> fRestore;
  std::optional<std::size_t> fCountWas;
  bool fOpened = false;
  std::size_t fUnseen = 0;
  WindowSizes fSizes;
  const M *fModel = nullptr;
  std::optional<Told> fTold;
  std::optional<Key> fFrom, fTo;
  bool fToEnd = true, fSlide = false, fAimNew = false, fShownEnd = false;
  std::optional<Key> fShownLast;
  std::optional<model::Revision> fSeen;
};

// A list with its way down over it -- Telegram's "\u2193", in a corner the
// program fills: told whether it is wanted -- the reader away from the
// list's end, or what it reads says so (a way back to go) -- and how many
// have come meanwhile (`show(corner, wanted, unseen)`). What is pressed in
// the corner answers as it says.
template <class List, class Corner, class Show> struct WithWayDown : Box<List, Corner> {
  Show fShow;
  bool fAlso = false;
  std::optional<std::pair<bool, std::size_t>> fShown;
  WithWayDown(Look look, List list, Corner corner, Show show)
      : Box<List, Corner>(std::move(look), std::move(list), std::move(corner)), fShow(std::move(show)) {}
  [[nodiscard]] List &list() { return std::get<0>(this->fParts); }
  [[nodiscard]] Corner &corner() { return std::get<1>(this->fParts); }
  void read(bool also) {
    fAlso = also;
    this->told();
  }
  [[nodiscard]] bool wantsTick() const { return true; }
  void update(double) { this->told(); }

private:
  void told() {
    auto &[list, corner] = this->fParts;
    const std::pair now{list.away() || fAlso, list.unseen()};
    if (fShown == now)
      return;
    fShown = now;
    std::invoke(fShow, corner, now.first, now.second);
    this->invalidateLayout();
  }
};
template <class List, class Corner, class Show> WithWayDown<List, Corner, Show> with_way_down(Look look, List list, Corner corner, Show show) {
  return WithWayDown<List, Corner, Show>(std::move(look), std::move(list), std::move(corner), std::move(show));
}

// A row longer than its room, moved sideways -- by a finger or a mouse
// dragged along it, flicked to glide on, or a sideways wheel -- and cut to
// its room. Up and down are left to the page round it: a gesture going more
// that way than along is not taken, nor is an upright wheel. A press is only
// noted, and what is under it clicked later: one that goes along is a drag
// of the row, not a choice.
template <class Line> struct SideScroll : Box<Line> {
  explicit SideScroll(Line line, bool verticalWheel = false)
      : Box<Line>(hbox(0.0f, {.masking = true}), std::move(line)), fVerticalWheel(verticalWheel) {}
  [[nodiscard]] Line &line() { return std::get<0>(this->fParts); }
  [[nodiscard]] const Line &line() const { return std::get<0>(this->fParts); }
  // How far it goes along: what of the line is past its room.
  [[nodiscard]] float most() const { return std::max(0.0f, line().bounds().width() - this->fState.contentBox().width()); }
  [[nodiscard]] float offset() const noexcept { return fGesture.offset(); }
  // A parent gesture moves the view while it holds the pointer itself.
  void scroll_by(float delta) {
    fGesture.setBounds(0.0f, most());
    fGesture.jumpTo(fGesture.offset() + delta);
    place();
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  using nodes::Stack::onPointer;
  template <class Phase>
    requires(std::same_as<Phase, scene::phase::bubble> || std::same_as<Phase, scene::phase::target>)
  void onPointer(const Phase &, const scene::pointer::scroll &wheel, scene::PointerReply &reply) {
    const float delta = wheel.dx != 0.0f ? wheel.dx : fVerticalWheel ? wheel.dy : 0.0f;
    if (delta == 0.0f || most() <= 0.0f)
      return;
    fGesture.setBounds(0.0f, most());
    fGesture.glideTo(fGesture.target() - delta * 40.0f);
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
  bool fVerticalWheel = false;
};
template <class Line> SideScroll<Line> side_scroll(Line line, bool verticalWheel = false) {
  return SideScroll<Line>(std::move(line), verticalWheel);
}

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
