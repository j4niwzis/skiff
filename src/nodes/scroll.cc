export module skiff.nodes:scroll;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// The colours of scroll bars, as the program's theme gives them: the bar,
// and the bar under the pointer or dragged. Light on dark by default.
struct ScrollBarColours {
  skia::SkColor bar = skia::colorSetARGB(0x53, 255, 255, 255);
  skia::SkColor over = skia::colorSetARGB(0x7a, 255, 255, 255);
};
inline ScrollBarColours &scrollBarColours() {
  static ScrollBarColours colours;
  return colours;
}

// A container that scrolls its children and clips them to itself.
template <class... Children> class ScrollContainer : public skiff::scene::Node {
public:
  explicit ScrollContainer(Children... children)
      : fChildren(std::move(children)...) {
    fState.fMasking = true;
  }

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }

  void scrollToStart() {
    if (fScroll.offset() == 0.0f && fScroll.target() == 0.0f) {
      return;
    }
    fScroll.jumpTo(0.0f);
    this->invalidateLayout();
  }
  // Carried across a rebuild: a list that grew should stay where the reader
  // left it.
  void setCurrent(float offset) {
    if (fScroll.offset() == offset && fScroll.target() == offset) {
      return;
    }
    fScroll.jumpTo(offset);
    this->invalidateLayout();
  }
  // To the end, however long the contents turn out to be: where they are
  // not laid out yet, once they are.
  // Gliding there, or at once -- a list shown anew starts at its end.
  void scrollToEnd(bool glide = true) {
    fToEnd = true;
    fToEndGlide = glide;
    this->invalidateLayout();
  }
  // Eased: the view glides there rather than jumping.
  void scrollTo(float offset) {
    fScroll.glideTo(offset);
    this->invalidateLayout();
  }
  [[nodiscard]] bool moving() const noexcept { return fScroll.moving(); }
  [[nodiscard]] float current() const noexcept { return fScroll.offset(); }
  [[nodiscard]] float extent() const noexcept { return fExtent; }

  void layoutChildren() {
    namespace scene = skiff::scene;
    const skia::SkRect box = fState.contentBox();
    const float offset = fScroll.offset();
    const skia::SkRect scrolled = skia::SkRect::MakeXYWH(
        box.fLeft, box.fTop - offset, box.width(), box.height());
    // Only scrolled: the contents move as they are, not laid out again --
    // with many rows, measuring them all at every step is what made a
    // scroll stutter. What changed in them is laid out as ever.
    if (fLaidOut && box == fLastBox && offset != fLastOffset) {
      const float dy = fLastOffset - offset;
      scene::eachChild(*this, [&](auto &child) { scene::shiftSubtree(child, dy); });
    }
    // What the reader is looking at stays where it is when what is above
    // it changes -- history coming in above, a row above growing: the first
    // item in view is remembered, and the view follows it. Not at the end,
    // where the view follows the newest instead.
    struct Anchor {
      scene::NodeId id = 0;
      float top = 0.0f;
    };
    std::optional<Anchor> anchor;
    if (fLaidOut && box == fLastBox && !this->atEnd()) {
      this->eachItem([&](const scene::State &item) {
        if (!anchor && item.fVisible && item.fBounds.fBottom > box.fTop) {
          anchor = Anchor{item.fId, item.fBounds.fTop};
        }
      });
    }
    fLaidOut = true;
    fLastBox = box;
    fLastOffset = offset;
    scene::eachChild(*this, [&](auto &child) { scene::layout(child, scrolled); });
    const skia::SkRect content = scene::childBounds(*this);
    fExtent = std::max(0.0f, content.height() - box.height());
    fScroll.setBounds(0.0f, fExtent);
    if (fToEnd) {
      fToEnd = false;
      if (fToEndGlide) {
        fScroll.glideTo(fExtent);
      } else {
        fScroll.jumpTo(fExtent);
        const float dy = fLastOffset - fExtent;
        scene::eachChild(*this, [&](auto &child) { scene::shiftSubtree(child, dy); });
        fLastOffset = fExtent;
      }
    }
    // What of the contents is in view -- and a screen above and below, so
    // what is scrolled to next is ready: the frame's walks go no further.
    const skia::SkRect seen = box.makeOutset(0.0f, box.height());
    scene::eachChild(*this, [&](auto &child) { scene::stateOf(child).fInView = seen; });
    if (anchor) {
      float moved = 0.0f;
      this->eachItem([&](const scene::State &item) {
        if (item.fId == anchor->id) {
          moved = item.fBounds.fTop - anchor->top;
        }
      });
      // Shifted, not jumped: a drag or a glide under way -- a finger past
      // the top pulling history in -- goes on from where it is instead of
      // snapping back and forth.
      if (moved != 0.0f) {
        fScroll.shift(moved);
        scene::eachChild(*this, [&](auto &child) { scene::shiftSubtree(child, -moved); });
        fLastOffset = offset + moved;
      }
    }
  }

  // The items of the list: the children of what this scrolls.
  template <class F> void eachItem(F &&f) {
    skiff::scene::eachChild(*this, [&](auto &child) {
      skiff::scene::eachChild(child, [&](auto &item) { f(skiff::scene::stateOf(item)); });
    });
  }

  void update(double nowMs) {
    const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
    fLastMs = nowMs;
    fNowMs = nowMs;
    if (fScroll.advance(dt)) {
      this->invalidateLayout();
    }
  }
  [[nodiscard]] bool settling() const { return fScroll.moving(); }

  // The contents, and over them a thin bar on the right saying how much
  // there is and where the view is in it: only where there is more than
  // shows, and only while the pointer is over it or it moves.
  void draw(skia::SkCanvas *canvas, float alpha) {
    skiff::scene::drawDefault(*this, canvas, alpha);
    if (fExtent <= 0.0f || !(fState.fHovered || fScroll.moving() || fScroll.dragging() || fBarDragging)) {
      return;
    }
    const skia::SkRect &box = fState.fBounds;
    const float thumb = this->thumbLength();
    const float at = this->thumbTop();
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    const bool over = fBarDragging || this->overBar(fState.fHoverX, fState.fHoverY);
    paint.setColor(over ? scrollBarColours().over : scrollBarColours().bar);
    paint.setAlphaf(paint.getAlphaf() * alpha);
    canvas->drawRRect(skia::SkRRect::MakeRectXY(
                          skia::SkRect::MakeXYWH(box.fRight - 5.0f, at + 2.0f, 4.0f, thumb - 4.0f), 2.0f, 2.0f),
                      paint);
  }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  // Whether the view is at the end of the contents, as a chat's newest.
  [[nodiscard]] bool atEnd(float slack = 4.0f) const noexcept {
    return fScroll.offset() >= fExtent - slack;
  }

  [[nodiscard]] bool onScroll(float ticks) {
    fScroll.wheel(ticks, 60.0f);
    this->invalidateLayout();
    return true;
  }
  // A wheel over something inside that did not use it -- a message's text,
  // a row that takes presses -- scrolls this on the way back up: the
  // innermost container scrolls, as in any toolkit.
  void onPointer(const skiff::scene::phase::bubble &, const skiff::scene::pointer::scroll &wheel,
                 skiff::scene::PointerReply &reply) {
    if (fExtent > 0.0f && this->onScroll(wheel.dy)) {
      reply.handle();
    }
  }

  // The bar: where its thumb is, and whether a point is over the bar.
  [[nodiscard]] float thumbLength() const {
    const float view = fState.fBounds.height();
    return std::max(24.0f, view * view / (view + fExtent));
  }
  [[nodiscard]] float thumbTop() const {
    const float view = fState.fBounds.height();
    return fState.fBounds.fTop +
           (view - this->thumbLength()) * std::clamp(fScroll.offset() / std::max(fExtent, 1.0f), 0.0f, 1.0f);
  }
  [[nodiscard]] bool overBar(float x, float y) const {
    return fExtent > 0.0f && x >= fState.fBounds.fRight - kBarReach && x <= fState.fBounds.fRight &&
           y >= fState.fBounds.fTop && y <= fState.fBounds.fBottom;
  }
  // The offset that puts the thumb's grabbed point under y.
  void dragBarTo(float y) {
    const float room = std::max(1.0f, fState.fBounds.height() - this->thumbLength());
    const float at = std::clamp((y - fBarGrab - fState.fBounds.fTop) / room, 0.0f, 1.0f);
    fScroll.jumpTo(at * fExtent);
    this->invalidateLayout();
  }
  static constexpr float kBarReach = 12.0f;

  // Dragging the contents, which is how a finger scrolls. A press is watched
  // in the capture phase and only remembered -- a press that does not travel
  // belongs to what is under it. Past the slop this takes the pointer, and
  // the rest of the gesture comes to it as the target.
  using Node::onPointer;
  template <class Phase>
  void onPointer(const Phase &, const skiff::scene::pointer::down &press,
                 skiff::scene::PointerReply &reply)
    requires(std::same_as<Phase, skiff::scene::phase::capture> ||
             std::same_as<Phase, skiff::scene::phase::target>)
  {
    // Only the main button drags or scrolls: a right press is for what is
    // under it -- a message's menu -- and was caught as the end of a glide
    // a wheel had started, and never reached it.
    if (press.button > 1) {
      return;
    }
    // On the bar: its thumb is taken where it was pressed, or, pressed
    // beside the thumb, brought under the pointer by its middle.
    if (this->overBar(press.x, press.y)) {
      const float top = this->thumbTop();
      const float length = this->thumbLength();
      fBarGrab = press.y >= top && press.y <= top + length ? press.y - top : length * 0.5f;
      fBarDragging = true;
      this->dragBarTo(press.y);
      reply.capturePointer();
      reply.suppressHover();
      reply.handle();
      return;
    }
    fArmed = fExtent > 0.0f; // nothing to scroll, nothing to drag
    fPressedAt = std::chrono::steady_clock::now();
    fPressX = press.x;
    fPressY = press.y;
    if (fArmed) {
      reply.suppressHover();
      this->deferIn(Phase{}, reply);
    }
    if (fScroll.press(press.y)) {
      reply.handle(); // the press was spent catching a flick
    }
  }
  template <class Phase>
  void onPointer(const Phase &, const skiff::scene::pointer::move &move,
                 skiff::scene::PointerReply &reply)
    requires(std::same_as<Phase, skiff::scene::phase::capture> ||
             std::same_as<Phase, skiff::scene::phase::target>)
  {
    if (fBarDragging) {
      this->dragBarTo(move.y);
      reply.handle();
      return;
    }
    if (!fArmed) {
      return;
    }
    // Held still a while before moving: the press was for what is under it
    // -- a selection being begun in a text -- not for scrolling.
    if (!fScroll.dragging() && std::chrono::steady_clock::now() - fPressedAt > kHoldBeforeSelecting) {
      fArmed = false;
      return;
    }
    if (!fScroll.dragging()) {
      const float dx = move.x - fPressX;
      const float dy = move.y - fPressY;
      if (std::abs(dx) >= skiff::scene::ScrollGesture::kSlop &&
          std::abs(dx) > std::abs(dy)) {
        // Direction is decided once: a horizontal gesture never turns into
        // list scrolling later in the same contact.
        fArmed = false;
        return;
      }
    }
    reply.suppressHover();
    const bool wasDragging = fScroll.dragging();
    if (!fScroll.drag(move.y, fNowMs)) {
      return;
    }
    if (!wasDragging) {
      if (reply.fCaptured) {
        fArmed = false; // something else is already being dragged
        return;
      }
      reply.capturePointer();
    }
    this->invalidateLayout();
    reply.handle();
  }
  template <class Phase>
  void onPointer(const Phase &, const skiff::scene::pointer::up &,
                 skiff::scene::PointerReply &reply)
    requires(std::same_as<Phase, skiff::scene::phase::capture> ||
             std::same_as<Phase, skiff::scene::phase::target>)
  {
    this->finish(reply);
  }
  template <class Phase>
  void onPointer(const Phase &, const skiff::scene::pointer::cancel &,
                 skiff::scene::PointerReply &reply)
    requires(std::same_as<Phase, skiff::scene::phase::capture> ||
             std::same_as<Phase, skiff::scene::phase::target>)
  {
    this->finish(reply);
  }

  // A target in its own right, so the empty part of a short list can still
  // be dragged. Draws nothing for a pointer.
  [[nodiscard]] bool acceptsInput() const { return true; }

  // What the gesture does at its end, released or cancelled.
  void finish(skiff::scene::PointerReply &reply) {
    if (fBarDragging) {
      fBarDragging = false;
      reply.releasePointer();
      reply.handle();
      return;
    }
    if (fArmed || fScroll.dragging()) {
      reply.suppressHover();
    }
    if (fScroll.dragging()) {
      fScroll.release();
      this->invalidateLayout();
      reply.releasePointer();
      reply.handle();
    }
    fArmed = false;
  }
  // Watching from above, the press is deferred: the target must not click
  // before this has had its chance to make the press a drag.
  static void deferIn(skiff::scene::phase::capture,
                      skiff::scene::PointerReply &reply) {
    reply.deferClick();
  }
  static void deferIn(skiff::scene::phase::target, skiff::scene::PointerReply &) {}

  std::tuple<Children...> fChildren;

private:
  skiff::scene::ScrollGesture fScroll;
  float fExtent = 0.0f;
  double fLastMs = 0.0;
  double fNowMs = 0.0;
  float fPressX = 0.0f;
  float fPressY = 0.0f;
  bool fArmed = false;
  bool fToEnd = false;
  bool fToEndGlide = true;
  // When the press was, and how long it may rest before a move is no
  // longer a scroll.
  std::chrono::steady_clock::time_point fPressedAt{};
  static constexpr std::chrono::milliseconds kHoldBeforeSelecting{250};
  // The bar's thumb being dragged, and where on it it was taken.
  bool fBarDragging = false;
  float fBarGrab = 0.0f;
  // Where the contents were last laid out: a scroll alone moves them.
  bool fLaidOut = false;
  skia::SkRect fLastBox = skia::SkRect::MakeEmpty();
  float fLastOffset = 0.0f;
};

} // namespace skiff::nodes
