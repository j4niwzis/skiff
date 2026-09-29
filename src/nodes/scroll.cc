export module skiff.nodes:scroll;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

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
    const skia::SkRect scrolled = skia::SkRect::MakeXYWH(
        box.fLeft, box.fTop - fScroll.offset(), box.width(), box.height());
    scene::eachChild(*this, [&](auto &child) { scene::layout(child, scrolled); });
    const skia::SkRect content = scene::childBounds(*this);
    fExtent = std::max(0.0f, content.height() - box.height());
    fScroll.setBounds(0.0f, fExtent);
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
    if (fExtent <= 0.0f || !(fState.fHovered || fScroll.moving() || fScroll.dragging())) {
      return;
    }
    const skia::SkRect &box = fState.fBounds;
    const float view = box.height();
    const float thumb = std::max(24.0f, view * view / (view + fExtent));
    const float at = box.fTop + (view - thumb) * std::clamp(fScroll.offset() / fExtent, 0.0f, 1.0f);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(skia::colorSetARGB(255, 255, 255, 255));
    paint.setAlphaf(alpha * 0.28f);
    canvas->drawRRect(skia::SkRRect::MakeRectXY(
                          skia::SkRect::MakeXYWH(box.fRight - 7.0f, at + 2.0f, 4.0f, thumb - 4.0f), 2.0f, 2.0f),
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
    fArmed = fExtent > 0.0f; // nothing to scroll, nothing to drag
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
    if (!fArmed) {
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
};

} // namespace skiff::nodes
