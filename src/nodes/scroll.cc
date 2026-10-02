export module skiff.nodes.scroll;

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
  // Put there at once -- once the contents are laid out: a list shown anew
  // is not as long yet as it will be, and an offset clamped to what it was
  // then was glided on from.
  void setCurrent(float offset) {
    fJumpTo = offset;
    this->seeAt(offset);
    this->invalidateLayout();
  }
  // To the end, however long the contents turn out to be: where they are
  // not laid out yet, once they are.
  // Gliding there, or at once -- a list shown anew starts at its end.
  void scrollToEnd(bool glide = true) {
    fToEnd = true;
    fToEndGlide = glide;
    if (!glide) {
      this->seeAt(fExtent);
    }
    // Laid out again, painting nothing by itself: already at the end -- a
    // list that follows what comes, asked at every change -- nothing moves,
    // and the whole view was painted each time.
    fState.relayoutQuietly();
  }
  // Eased: the view glides there rather than jumping.
  void scrollTo(float offset) {
    fScroll.glideTo(offset);
    this->invalidateLayout();
  }
  [[nodiscard]] bool moving() const noexcept { return fScroll.moving(); }
  // Whether a scroll step copies what is in view, where the host copies:
  // not over what does not scroll with it -- a gradient behind it, which a
  // copy moved along, a little further off at every step.
  void setCopiesOnScroll(bool copies) { fCopies = copies; }
  // Whether what is in view is held there as what is above it changes -- a
  // history paging in above, a row above growing -- the view following the
  // first row in view (the default); or the view kept at its offset. A list
  // whose rows change places -- chats by their newest -- keeps its offset:
  // held, it followed the row it showed first down as one from below went
  // to the top, even with the view at the top.
  void setHoldsInView(bool holds) { fHolds = holds; }
  // On its way to the end, asked for and not yet there: what would move
  // the end meanwhile -- more made above, dropped below -- waits.
  [[nodiscard]] bool glidingToEnd() const noexcept { return fToEnd || fGlidingToEnd; }

  // What of the contents is in view at an offset -- and a screen above and
  // below -- said to them as soon as the offset changes: a jump, a glide's
  // step, the wheel. The frame's tick comes before its layout; told only
  // there, the tick went by the view before the move, passed over what the
  // move brought into view -- a message flashed after a jump, a row made as
  // it came -- and the damage walk, by the view after, cleared its mark: it
  // stood still until something else there was repainted.
  void seeAt(float offset) {
    if (!fLaidOut) {
      return;
    }
    const skia::SkRect seen =
        fLastBox.makeOutset(0.0f, fLastBox.height()).makeOffset(0.0f, skiff::scene::snapToPixel(offset));
    skiff::scene::eachChild(*this, [&](auto &child) { skiff::scene::stateOf(child).fInView = seen; });
  }
  // Only its offset changed: laid out again at the next frame, where the move
  // is found -- copied where the host copies, else repainted.
  void scrolled() {
    this->seeAt(fScroll.offset());
    if (skiff::scene::blitScrolling() && fCopies) {
      fState.relayoutQuietly();
    } else {
      this->invalidateLayout();
    }
  }
  // The view put where it follows what it showed -- the end, or the row it
  // was on -- once the contents were laid out again, `before` the offset it
  // was drawn at: each row repainted where it was and where it is on the
  // screen, only where those differ. Something far above growing moves
  // every row down in the contents and the view down with them: nothing on
  // the screen changes, and nothing is repainted -- not the whole view,
  // every bubble in it drawn again. The recordings of rows moved in the
  // contents are let go (they are in its space), not repainted.
  void followed(float before) {
    namespace scene = skiff::scene;
    const skia::SkRect view = fState.fBounds;
    bool whole = false;
    scene::eachChild(*this, [&](auto &contents) {
      scene::State &list = scene::stateOf(contents);
      // A row gone -- or made again in its place: where it was, said in the
      // contents' space, put where it was drawn on the screen -- that, not
      // all of the view, repainted.
      if (!list.fMovedDamage.isEmpty()) {
        skia::SkRect gone = list.fMovedDamage.makeOffset(0.0f, -before);
        if (gone.intersect(view)) {
          fState.fMovedDamage = scene::joined(fState.fMovedDamage, gone);
        }
        list.fMovedDamage = skia::SkRect::MakeEmpty();
      }
      list.fLayoutMoved = skia::SkRect::MakeEmpty();
      scene::eachChild(contents, [&](auto &item) {
        scene::State &one = scene::stateOf(item);
        const bool laid = !one.fLayoutMoved.isEmpty();
        // New: its own damage says where it is.
        if (laid && one.fBoundsAtPass.isEmpty()) {
          return;
        }
        const skia::SkRect was = (laid ? one.fBoundsAtPass : one.fBounds).makeOffset(0.0f, -before);
        const skia::SkRect now = one.fBounds.makeOffset(0.0f, -fLastOffset);
        if (laid) {
          one.fDrawnBounds.offset(0.0f, one.fBounds.fTop - one.fBoundsAtPass.fTop);
          one.fLayoutMoved = skia::SkRect::MakeEmpty();
          forgetRecordings(item);
        }
        if (was == now || !one.fVisible) {
          return;
        }
        for (skia::SkRect area : {was, now}) {
          if (area.intersect(view)) {
            fState.fMovedDamage = scene::joined(fState.fMovedDamage, area);
          }
        }
      });
    });
    if (whole) {
      fState.markDamaged();
      return;
    }
    // The bar: its thumb is another length now.
    const skia::SkRect bar = skia::SkRect::MakeLTRB(view.fRight - kBarReach, view.fTop, view.fRight, view.fBottom);
    fState.fMovedDamage = scene::joined(fState.fMovedDamage, bar);
    scene::work::mark(fState.fId);
  }
  // A subtree's recordings let go: made in the space it was laid out in.
  template <class N> static void forgetRecordings(N &node) {
    skiff::scene::stateOf(node).fPicture = nullptr;
    skiff::scene::eachChild(node, [](auto &child) { forgetRecordings(child); });
  }
  // Its contents moved by dy as a whole: the view copied there by the host,
  // and repainted only the strip that came into view and the bar, which
  // moved over it. Too far to be worth it, or with nothing to copy onto, all
  // of it.
  void moved(float dy) {
    namespace scene = skiff::scene;
    const skia::SkRect view = fState.fBounds;
    if (!scene::blitScrolling() || !fCopies) {
      return;  // invalidateLayout damaged it all
    }
    if (std::abs(dy) >= view.height() * 0.75f || view.isEmpty()) {
      fState.markDamaged();
      return;
    }
    scene::scrollMoves().push_back({view, dy, fState.fId});
    const skia::SkRect strip = dy < 0.0f ? skia::SkRect::MakeLTRB(view.fLeft, view.fBottom + dy - 1.0f, view.fRight, view.fBottom)
                                         : skia::SkRect::MakeLTRB(view.fLeft, view.fTop, view.fRight, view.fTop + dy + 1.0f);
    const skia::SkRect bar = skia::SkRect::MakeLTRB(view.fRight - kBarReach, view.fTop, view.fRight, view.fBottom);
    fState.fMovedDamage = scene::joined(scene::joined(fState.fMovedDamage, strip), bar);
    scene::work::mark(fState.fId);
  }
  [[nodiscard]] float current() const noexcept { return fScroll.offset(); }
  [[nodiscard]] float extent() const noexcept { return fExtent; }

  void layoutChildren() {
    namespace scene = skiff::scene;
    const skia::SkRect box = fState.contentBox();
    // Placed on whole device pixels: the glide goes on smoothly, and what
    // is drawn follows it a whole pixel at a time -- every row, its text
    // and its fills together.
    const float offset = scene::snapToPixel(fScroll.offset());
    // Only scrolled: the contents are neither moved nor laid out again --
    // they stay laid out in the view's box and are drawn shifted up by the
    // offset (their fShiftY, set below), so a step of a scroll costs the
    // same with any number of rows. Every row's bounds were moved at each
    // step, which grew with every page of history.
    if (fLaidOut && box == fLastBox && offset != fLastOffset) {
      this->moved(fLastOffset - offset);
    }
    // What the reader is looking at stays where it is when what is above
    // it changes -- history coming in above, a row above growing: the first
    // item in view is remembered, and the view follows it. Not at the end,
    // where the view follows the newest instead. The rows' bounds are where
    // they are laid out: in view, less the offset.
    struct Anchor {
      scene::NodeId id = 0;
      float top = 0.0f;
    };
    // Several, in case the first goes with the change: the first of them
    // still there is kept in place.
    std::vector<Anchor> anchors;
    // At the end and at rest, the view stays at the end, however much comes
    // above and however its own box changes (a bar opened under it): not
    // left where it was to be glided down again. Not while it is moving: a
    // wheel moving it off the end is not pulled back.
    const bool following = fLaidOut && this->atEnd() && !fScroll.dragging() && !fScroll.moving();
    // Not where the view is put somewhere on purpose -- an offset set, the
    // end asked for: held to what was in view, a jump landed back near where
    // it left, beside the message it went to.
    if (fHolds && fLaidOut && box == fLastBox && !this->atEnd() && !fJumpTo && !fToEnd && !fGlidingToEnd) {
      this->eachItem([&](const scene::State &item) {
        if (anchors.size() < 4 && item.fVisible && item.fBounds.fBottom - offset > box.fTop) {
          anchors.push_back(Anchor{item.fId, item.fBounds.fTop});
        }
      });
    }
    fLaidOut = true;
    fLastBox = box;
    fLastOffset = offset;
    scene::eachChild(*this, [&](auto &child) { scene::layout(child, box); });
    const skia::SkRect content = scene::childBounds(*this);
    fExtent = std::max(0.0f, content.height() - box.height());
    fScroll.setBounds(0.0f, fExtent);
    if (fJumpTo) {
      const float to = std::clamp(*fJumpTo, 0.0f, fExtent);
      fJumpTo.reset();
      fScroll.jumpTo(to);
      fState.markDamaged();
      fLastOffset = scene::snapToPixel(to);
    } else if (following && !fToEnd && fScroll.offset() != fExtent) {
      fScroll.jumpTo(fExtent);
      fLastOffset = scene::snapToPixel(fExtent);
      this->followed(offset);
    }
    // A glide to the end under way: the end it set out for is not where the
    // end is once the rows it passes are laid out -- measured taller than
    // guessed, more made as it nears the newest -- and it stopped short of
    // the bottom. It goes on to the end as it moves, until it is there or
    // the reader takes the view (a wheel, a drag, a jump elsewhere).
    if (fGlidingToEnd) {
      if (fScroll.dragging() || fScroll.target() != std::min(fEndTarget, fExtent)) {
        fGlidingToEnd = false;
      } else if (fScroll.target() != fExtent) {
        fScroll.glideTo(fExtent);
        fEndTarget = fScroll.target();
      } else if (!fScroll.moving()) {
        fGlidingToEnd = false;
      }
    }
    if (fToEnd) {
      fToEnd = false;
      if (fToEndGlide) {
        fScroll.glideTo(fExtent);
        fGlidingToEnd = true;
        fEndTarget = fScroll.target();
        // Begun here, in the layout -- after this frame's tick, which went by
        // it at rest: ticked from the next frame on, and that frame asked
        // for. Nothing else marked it, and the glide waited for some other
        // change to walk it -- the arrow to the newest pressed twice.
        scene::work::moving().push_back(fState.fId);
      } else {
        fScroll.jumpTo(fExtent);
        if (fLastOffset != scene::snapToPixel(fExtent)) {
          fState.markDamaged();
        }
        fLastOffset = scene::snapToPixel(fExtent);
      }
    }
    if (!anchors.empty()) {
      float moved = 0.0f;
      std::size_t best = anchors.size();
      this->eachItem([&](const scene::State &item) {
        for (std::size_t i = 0; i < best; ++i) {
          if (item.fId == anchors[i].id) {
            moved = item.fBounds.fTop - anchors[i].top;
            best = i;
            break;
          }
        }
      });
      // Shifted, not jumped: a drag or a glide under way -- a finger past
      // the top pulling history in -- goes on from where it is instead of
      // snapping back and forth.
      if (moved != 0.0f) {
        fScroll.shift(moved);
        fLastOffset = offset + scene::snapToPixel(moved);
        this->followed(offset);
      }
    }
    // The contents drawn where the view is -- and what of them is in view,
    // with a screen above and below, so what is scrolled to next is ready:
    // in the space they are laid out in, the view moved down by the offset.
    const skia::SkRect seen = box.makeOutset(0.0f, box.height()).makeOffset(0.0f, fLastOffset);
    scene::eachChild(*this, [&](auto &child) {
      scene::State &contents = scene::stateOf(child);
      contents.setShift(0.0f, -fLastOffset);
      contents.fInView = seen;
    });
  }

  // Where a rect of the contents -- a row's bounds -- is in the view, and
  // back: the contents are laid out as if unscrolled.
  [[nodiscard]] skia::SkRect toView(const skia::SkRect &inContents) const {
    return inContents.makeOffset(0.0f, -fLastOffset);
  }
  [[nodiscard]] float contentsShift() const noexcept { return -fLastOffset; }

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
      this->scrolled();
    }
    // A drag held past an edge: the view goes on that way, frame by frame.
    if (fEdgeSpeed != 0.0f) {
      const float was = fScroll.offset();
      fScroll.jumpTo(was + fEdgeSpeed * static_cast<float>(dt));
      if (fScroll.offset() != was) {
        this->scrolled();
      }
    }
  }
  [[nodiscard]] bool settling() const { return fScroll.moving(); }
  // Ticked while it moves, or a finger holds it: at rest, nothing to step.
  [[nodiscard]] bool wantsTick() const { return fScroll.moving() || fScroll.dragging() || fEdgeSpeed != 0.0f; }

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
  // Hovered or not, only its bar shows or goes -- and only where there is
  // one: a list that does not scroll has none, and the pointer going in and
  // out of it changed nothing on the screen yet repainted a strip of it.
  [[nodiscard]] bool hoverChangesAppearance() const { return fExtent > 0.0f; }
  // What the hover changes: the thumb, where it is drawn -- not the bar's
  // whole height.
  [[nodiscard]] skia::SkRect hoverDamage() const {
    const skia::SkRect &box = fState.fBounds;
    const float top = this->thumbTop();
    return skia::SkRect::MakeLTRB(box.fRight - 6.0f, top, box.fRight, top + this->thumbLength() + 1.0f);
  }
  // Whether the view is at the end of the contents, as a chat's newest.
  [[nodiscard]] bool atEnd(float slack = 4.0f) const noexcept {
    return fScroll.offset() >= fExtent - slack;
  }

  [[nodiscard]] bool onScroll(float ticks) {
    fScroll.wheel(ticks, 60.0f);
    this->scrolled();
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
    this->scrolled();
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
    // Something in it holds the pointer -- a text being selected -- and is
    // dragged past its top or bottom: it scrolls that way, faster the
    // further out, until the drag comes back or ends.
    if (reply.fCaptured && reply.fTarget != fState.fId) {
      const skia::SkRect &box = fState.fBounds;
      const float out = move.y < box.fTop ? move.y - box.fTop : (move.y > box.fBottom ? move.y - box.fBottom : 0.0f);
      const float speed = std::clamp(out, -150.0f, 150.0f) * kEdgeSpeed;
      if (speed != fEdgeSpeed) {
        fEdgeSpeed = speed;
        skiff::scene::work::mark(fState.fId);  // ticked from now, or no longer
      }
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
    this->scrolled();
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
    fEdgeSpeed = 0.0f;
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
  // How fast a drag past an edge scrolls: pixels a millisecond, for each
  // pixel it is past; and how fast it does now.
  static constexpr float kEdgeSpeed = 0.01f;
  float fEdgeSpeed = 0.0f;
  bool fToEnd = false;
  std::optional<float> fJumpTo;
  bool fToEndGlide = true;
  bool fCopies = true;
  bool fHolds = true;
  // Gliding to the end, and the end it was last aimed at: aimed again as the
  // end moves on.
  bool fGlidingToEnd = false;
  float fEndTarget = 0.0f;
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
