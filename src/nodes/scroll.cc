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
    // Kept for the pointer handler, which measures speed against the clock
    // and is not given one.
    fNowMs = nowMs;
    const double dt = fLastMs > 0.0 ? std::min(nowMs - fLastMs, 64.0) : 16.0;
    fLastMs = nowMs;
    const float previous = fCurrent;

    if (fDragging) {
      // The finger owns the offset; nothing else moves it.
      fLastMs = nowMs;
    } else if (fFlinging) {
      // Friction per millisecond rather than per frame, so the same flick
      // travels the same distance at thirty frames and at two hundred.
      fVelocity *= std::pow(kFriction, static_cast<float>(dt));
      fCurrent += fVelocity * static_cast<float>(dt);
      if (fCurrent < 0.0f || fCurrent > fExtent) {
        // Past the end the flick is over; what is left is the spring.
        fFlinging = false;
        fVelocity = 0.0f;
        fTarget = std::clamp(fCurrent, 0.0f, fExtent);
      } else if (std::abs(fVelocity) < kMinVelocity) {
        fFlinging = false;
        fVelocity = 0.0f;
        fTarget = fCurrent;
      } else {
        fTarget = fCurrent;
      }
    } else {
      // Everything else -- a wheel, a jump to a section, the spring back
      // from an overscrolled edge -- eases towards the target.
      fCurrent = skiff::paint::approach(fCurrent, fTarget, kSettleTau, dt);
      if (std::abs(fCurrent - fTarget) < 0.05f) {
        fCurrent = fTarget; // settle, so a still list stops re-laying out
      }
    }

    if (fCurrent != previous) {
      this->invalidateLayout();
    }
  }

  // Still moving on its own: a flick that has not run out, or an edge
  // springing back. The frame loop asks this to know whether to keep drawing.
  [[nodiscard]] bool settling() const override {
    return fFlinging || !skiff::paint::settled(fCurrent, fTarget);
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
      fPressOffset = fCurrent;
      fLastDragY = event.fY;
      fLastDragMs = 0.0;
      fVelocity = 0.0f;
      fArmed = fExtent > 0.0f; // nothing to scroll, nothing to drag
      fDragging = false;
      if (fFlinging) {
        // Touching a list that is still flying stops it where it is, the way
        // every touch surface does. The press is spent on the catch.
        fFlinging = false;
        fTarget = fCurrent;
        event.handle();
      }
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
      // Velocity in units per millisecond, smoothed a little: a finger
      // stutters, and the last frame alone is a poor answer for how fast it
      // was going. Measured against the clock rather than the frame, or a
      // slow frame reads as a fast flick.
      if (fLastDragMs > 0.0 && fNowMs > fLastDragMs) {
        const float sample = static_cast<float>((fLastDragY - event.fY) /
                                                (fNowMs - fLastDragMs));
        fVelocity = fVelocity * (1.0f - kVelocityMix) + sample * kVelocityMix;
      }
      fLastDragY = event.fY;
      fLastDragMs = fNowMs;

      // Follows the finger exactly rather than easing towards it: an eased
      // drag feels like the list is on a spring. Past either end it follows
      // at a fraction, which is the resistance every touch surface has.
      const float wanted = fPressOffset - travelled;
      if (wanted < 0.0f) {
        fCurrent = wanted * kOverscroll;
      } else if (wanted > fExtent) {
        fCurrent = fExtent + (wanted - fExtent) * kOverscroll;
      } else {
        fCurrent = wanted;
      }
      fTarget = fCurrent;
      this->invalidateLayout();
      event.handle();
      break;
    }
    case scene::PointerAction::kUp:
    case scene::PointerAction::kCancel:
      if (fDragging) {
        if (fCurrent < 0.0f || fCurrent > fExtent) {
          // Let go past the end: the spring takes it back, no flick.
          fVelocity = 0.0f;
          fTarget = std::clamp(fCurrent, 0.0f, fExtent);
        } else if (std::abs(fVelocity) > kMinVelocity) {
          fFlinging = true;
        }
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
  // What is left of the speed after a millisecond of flying. 0.994 puts a
  // fast flick at roughly a second of travel, which is what a phone does.
  static constexpr float kFriction = 0.994f;
  // Below this the flick is over: a twentieth of a unit per millisecond is
  // fifty a second, slower than anything reads as movement.
  static constexpr float kMinVelocity = 0.05f;
  // How much of each new speed sample to believe, against the running one.
  static constexpr float kVelocityMix = 0.35f;
  // How far the contents follow past either end while held.
  static constexpr float kOverscroll = 0.4f;
  // How quickly everything that is not a flick settles.
  static constexpr float kSettleTau = 30.0f;

  float fCurrent = 0.0f;
  float fTarget = 0.0f;
  float fExtent = 0.0f;
  double fLastMs = 0.0;
  float fPressY = 0.0f;
  float fPressOffset = 0.0f;
  float fLastDragY = 0.0f;
  double fLastDragMs = 0.0;
  double fNowMs = 0.0;
  float fVelocity = 0.0f; // units per millisecond
  bool fArmed = false;
  bool fDragging = false;
  bool fFlinging = false;
};

} // namespace skiff::nodes
