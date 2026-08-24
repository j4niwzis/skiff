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
    if (fTarget == 0.0f && fCurrent == 0.0f) {
      return;
    }
    fTarget = 0.0f;
    fCurrent = 0.0f;
    this->invalidateLayout();
  }

  // Carried across a rebuild: a list that grew a page should stay where the
  // reader left it, not jump back to the top.
  void setCurrent(float offset) {
    if (fCurrent == offset && fTarget == offset) {
      return;
    }
    fCurrent = offset;
    fTarget = offset;
    this->invalidateLayout();
  }
  // Eased: the view glides to the offset rather than jumping to it, which is
  // what a jump to a section in a settings list should look like.
  void scrollTo(float offset) {
    fTarget = std::clamp(offset, 0.0f, fExtent);
    this->invalidateLayout();
  }

  // Still gliding towards where it was asked to go.
  [[nodiscard]] bool moving() const noexcept {
    return !skiff::paint::settled(fCurrent, fTarget);
  }

  [[nodiscard]] float current() const noexcept { return fCurrent; }
  [[nodiscard]] float extent() const noexcept { return fExtent; }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    const skia::SkRect scrolled = skia::SkRect::MakeXYWH(
        box.fLeft, box.fTop - fCurrent, box.width(), box.height());
    for (auto &child : fChildren) {
      child->layout(scrolled);
    }
    const skia::SkRect content = this->childBounds();
    fExtent = std::max(0.0f, content.height() - box.height());
    fTarget = std::clamp(fTarget, 0.0f, fExtent);
  }

  void update(double nowMs) override {
    const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
    fLastMs = nowMs;
    const float previous = fCurrent;
    fCurrent = skiff::paint::approach(fCurrent, fTarget, 30.0f, dt);
    if (std::abs(fCurrent - fTarget) < 0.05f) {
      fCurrent = fTarget; // settle, so a still list stops re-laying out
    }
    if (fCurrent != previous) {
      this->invalidateLayout();
    }
  }

  bool onScroll(float ticks) override {
    fTarget = std::clamp(fTarget - ticks * 60.0f, 0.0f, fExtent);
    this->invalidateLayout();
    return true;
  }

  // Dragging the contents, which is the only way to scroll with a finger.
  //
  // Handled in the capture phase, so the container sees a press before the
  // card or the slider under it does -- but the press is only remembered,
  // never taken. A press that does not move belongs to whatever is under it;
  // only once the finger has travelled past the slop does this take the
  // pointer, and from then on the child sees nothing. That is what makes a
  // list draggable by its contents without breaking a tap on them.
  void onPointerEvent(skiff::scene::PointerEvent &event) override {
    namespace scene = skiff::scene;
    if (event.fPhase != scene::EventPhase::kCapture) {
      return;
    }
    switch (event.fAction) {
    case scene::PointerAction::kDown:
      fPressY = event.fY;
      fPressOffset = fTarget;
      fLastDragY = event.fY;
      fVelocity = 0.0f;
      fArmed = fExtent > 0.0f; // nothing to scroll, nothing to drag
      fDragging = false;
      break;
    case scene::PointerAction::kMove: {
      if (!fArmed) {
        break;
      }
      const float travelled = event.fY - fPressY;
      if (!fDragging) {
        if (std::abs(travelled) < kDragSlop) {
          break;
        }
        if (this->capturedNode() != nullptr) {
          fArmed = false; // something else is already being dragged
          break;
        }
        fDragging = true;
        event.capturePointer();
      }
      fVelocity = event.fY - fLastDragY;
      fLastDragY = event.fY;
      // Follows the finger exactly rather than easing towards it: an eased
      // drag feels like the list is on a spring.
      fTarget = std::clamp(fPressOffset - travelled, 0.0f, fExtent);
      fCurrent = fTarget;
      this->invalidateLayout();
      event.handle();
      break;
    }
    case scene::PointerAction::kUp:
    case scene::PointerAction::kCancel:
      if (fDragging) {
        // A flick keeps going: the easing in update() carries it the rest of
        // the way and stops it at the end of the list.
        fTarget = std::clamp(fTarget - fVelocity * kFlick, 0.0f, fExtent);
        this->invalidateLayout();
        event.releasePointer();
        event.handle();
      }
      fArmed = false;
      fDragging = false;
      break;
    default:
      break;
    }
  }

  // The container is a target in its own right so that the empty part of a
  // short list can still be dragged. It never draws anything for a pointer.
  bool acceptsInput() const override { return true; }
  bool hoverChangesAppearance() const override { return false; }

private:
  // How far a finger travels before this decides it is a drag and not a tap.
  static constexpr float kDragSlop = 6.0f;
  // How much of the last frame's travel a flick carries on for.
  static constexpr float kFlick = 8.0f;

  float fCurrent = 0.0f;
  float fTarget = 0.0f;
  float fExtent = 0.0f;
  double fLastMs = 0.0;
  float fPressY = 0.0f;
  float fPressOffset = 0.0f;
  float fLastDragY = 0.0f;
  float fVelocity = 0.0f;
  bool fArmed = false;
  bool fDragging = false;
};

} // namespace skiff::nodes
