export module skiff.nodes:scroll;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::nodes

export namespace skiff::nodes {

// A container that scrolls its children and clips them to itself.
class ScrollContainer : public skiff::scene::TypedDrawable<ScrollContainer> {
public:
  ScrollContainer() { fMasking = true; }

  void scrollToStart() {
    if (fScroll.offset() == 0.0f && fScroll.target() == 0.0f) {
      return;
    }
    fScroll.jumpTo(0.0f);
    this->invalidateLayout();
  }

  // Carried across a rebuild: a list that grew a page should stay where the
  // reader left it, not jump back to the top.
  void setCurrent(float offset) {
    if (fScroll.offset() == offset && fScroll.target() == offset) {
      return;
    }
    fScroll.jumpTo(offset);
    this->invalidateLayout();
  }
  // Eased: the view glides to the offset rather than jumping to it, which is
  // what a jump to a section in a settings list should look like.
  void scrollTo(float offset) {
    fScroll.glideTo(offset);
    this->invalidateLayout();
  }

  // Still gliding towards where it was asked to go.
  [[nodiscard]] bool moving() const noexcept { return fScroll.moving(); }

  [[nodiscard]] float current() const noexcept { return fScroll.offset(); }
  [[nodiscard]] float extent() const noexcept { return fExtent; }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    const skia::SkRect scrolled =
        skia::SkRect::MakeXYWH(box.fLeft, box.fTop - fScroll.offset(),
                               box.width(), box.height());
    for (auto &child : fChildren) {
      child->layout(scrolled);
    }
    const skia::SkRect content = this->childBounds();
    fExtent = std::max(0.0f, content.height() - box.height());
    fScroll.setBounds(0.0f, fExtent);
  }

  void update(double nowMs) override {
    const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
    fLastMs = nowMs;
    fNowMs = nowMs;
    if (fScroll.advance(dt)) {
      this->invalidateLayout();
    }
  }

  // A flick that has not run out, or an end springing back. Damage says what
  // changed; this says the next frame will differ too.
  bool settling() const override { return fScroll.moving(); }

  bool onScroll(float ticks) override {
    fScroll.wheel(ticks, 60.0f);
    this->invalidateLayout();
    return true;
  }

  // Dragging the contents, which is the only way to scroll with a finger.
  //
  // A press is watched in the capture phase, before whatever is under it sees
  // it, but only remembered -- a press that does not travel belongs to what
  // is under it. Once past the slop this takes the pointer, and from then on
  // it is the target, so the rest of the gesture arrives in the target phase.
  void onPointerEvent(skiff::scene::PointerEvent &event) override {
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
      if (fScroll.press(event.fY)) {
        event.handle(); // the press was spent catching a flick
      }
      break;
    case scene::PointerAction::kMove: {
      if (!fArmed) {
        break;
      }
      const bool wasDragging = fScroll.dragging();
      if (!fScroll.drag(event.fY, fNowMs)) {
        break;
      }
      if (!wasDragging) {
        if (this->capturedNode() != nullptr) {
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

  // A target in its own right, so the empty part of a short list can still be
  // dragged. It never draws anything for a pointer.
  bool acceptsInput() const override { return true; }
  bool hoverChangesAppearance() const override { return false; }

private:
  skiff::scene::ScrollGesture fScroll;
  float fExtent = 0.0f;
  double fLastMs = 0.0;
  double fNowMs = 0.0;
  bool fArmed = false;
};

} // namespace skiff::nodes
