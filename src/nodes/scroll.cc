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

  [[nodiscard]] bool onScroll(float ticks) {
    fScroll.wheel(ticks, 60.0f);
    this->invalidateLayout();
    return true;
  }

  // Dragging the contents, which is how a finger scrolls. A press is watched
  // in the capture phase and only remembered -- a press that does not travel
  // belongs to what is under it. Past the slop this takes the pointer.
  void onPointerEvent(skiff::scene::PointerEvent &event) {
    namespace scene = skiff::scene;
    const bool watching = event.fPhase == scene::EventPhase::kCapture;
    const bool mine = event.fPhase == scene::EventPhase::kTarget &&
                      (event.fAction == scene::PointerAction::kDown ||
                       fScroll.dragging() || fArmed);
    if (!watching && !mine) {
      return;
    }
    switch (event.fAction) {
    case scene::PointerAction::kDown:
      fArmed = fExtent > 0.0f; // nothing to scroll, nothing to drag
      fPressX = event.fX;
      fPressY = event.fY;
      if (fArmed) {
        event.suppressHover();
        if (watching) {
          event.deferClick();
        }
      }
      if (fScroll.press(event.fY)) {
        event.handle(); // the press was spent catching a flick
      }
      break;
    case scene::PointerAction::kMove: {
      if (!fArmed) {
        break;
      }
      if (!fScroll.dragging()) {
        const float dx = event.fX - fPressX;
        const float dy = event.fY - fPressY;
        if (std::abs(dx) >= scene::ScrollGesture::kSlop &&
            std::abs(dx) > std::abs(dy)) {
          // Direction is decided once: a horizontal gesture never turns into
          // list scrolling later in the same contact.
          fArmed = false;
          break;
        }
      }
      event.suppressHover();
      const bool wasDragging = fScroll.dragging();
      if (!fScroll.drag(event.fY, fNowMs)) {
        break;
      }
      if (!wasDragging) {
        if (event.fCaptured) {
          fArmed = false; // something else is already being dragged
          break;
        }
        event.capturePointer();
      }
      this->invalidateLayout();
      event.handle();
      break;
    }
    case scene::PointerAction::kUp:
    case scene::PointerAction::kCancel:
      if (fArmed || fScroll.dragging()) {
        event.suppressHover();
      }
      if (fScroll.dragging()) {
        fScroll.release();
        this->invalidateLayout();
        event.releasePointer();
        event.handle();
      }
      fArmed = false;
      break;
    default:
      break;
    }
  }

  // A target in its own right, so the empty part of a short list can still
  // be dragged. Draws nothing for a pointer.
  [[nodiscard]] bool acceptsInput() const { return true; }

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
