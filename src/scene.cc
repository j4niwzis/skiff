export module skiff.scene;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.aggregate;
export import skiff.core;

// A retained scene whose type is its tree.
//
// A node derives from Node, which holds its State and the default of every
// hook; a node overrides a hook by declaring it. Its children are its own
// members: nodes, or a std::variant, std::optional, pointer, tuple or range of
// them. AnyNode holds a node of any type by value where the type cannot be
// written down.
//
// Nothing points upwards. A change marks its node's State; each frame the
// walks from the root find what changed, lay it out, and gather what has to
// be repainted. Focus, pointer capture and the pressed node are ids held by
// the Scene, and an event goes to its node along a path, capture on the way
// down and bubble on the way back.
//
// Choices are types: a pointer event is a PointerEvent, one of pointer::down,
// pointer::move and the rest, each with what it carries, and a node handles
// the ones it cares about as overloads of onPointer. Sets of flags are Flags.
export namespace skiff::scene {
// What a widget does when it was not given anything to do.

// A scroll view's contents moved by `dy` as a whole: what was drawn in
// `rect` last frame is where it goes now, `dy` lower. A host that keeps its
// last frame copies it there, and repaints only the damage -- the strip that
// came into view, the bar, and what is drawn over it. `node` is the view.
struct ScrollMove {
  skia::SkRect rect = skia::SkRect::MakeEmpty();
  float dy = 0.0f;
  std::uint64_t node = 0;  // its NodeId
};
// Whether this program's host copies moved scroll views (ScrollMove) rather
// than repainting them: off, a scroll view repaints whole as it moves.
inline bool &blitScrolling() {
  static bool on = false;
  return on;
}
// The moves of the frame under way.
inline std::vector<ScrollMove> &scrollMoves() {
  static std::vector<ScrollMove> kept;
  return kept;
}
// Each node's own damage in this frame, as the damage walk finds it: the
// frame is repainted as a few rects, not their union -- a strip coming into
// view at the bottom and something moving at the top made the whole view.
inline std::vector<skia::SkRect> &damageFound() {
  static std::vector<skia::SkRect> kept;
  return kept;
}
// Rects merged down to a few: those that overlap, or nearly, joined; then,
// while too many, the two whose join adds the least. Past many, their union.
[[nodiscard]] inline std::vector<skia::SkRect> fewRects(std::vector<skia::SkRect> rects, std::size_t most = 6) {
  std::erase_if(rects, [](const skia::SkRect &one) { return one.isEmpty(); });
  if (rects.size() > 64) {
    // A long list can report hundreds of child rectangles. Keep the work
    // bounded without joining unrelated panels across the window. Parent
    // damage already covers its children's damage; discard those first.
    std::vector<skia::SkRect> bounded;
    bounded.reserve(64);
    for (const auto& rect : rects) {
      if (std::ranges::any_of(bounded, [&](const auto& kept) { return kept.contains(rect); }))
        continue;
      std::erase_if(bounded, [&](const auto& kept) { return rect.contains(kept); });
      if (bounded.size() == 64)
        bounded = fewRects(std::move(bounded), 32);
      bounded.push_back(rect);
    }
    return fewRects(std::move(bounded), most);
  }
  const auto area = [](const skia::SkRect &one) { return one.width() * one.height(); };
  // Joined where the join is hardly bigger than the two: one inside the
  // other, or side by side. Touching was not enough -- a list's thin bar
  // strip and a row it touches joined into the whole list.
  for (bool merged = true; merged;) {
    merged = false;
    for (std::size_t i = 0; i < rects.size() && !merged; ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        skia::SkRect both = rects[i];
        both.join(rects[j]);
        if (area(both) <= 1.25f * (area(rects[i]) + area(rects[j])) + 64.0f) {
          rects[i] = both;
          rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(j));
          merged = true;
          break;
        }
      }
    }
  }
  while (rects.size() > most) {
    std::size_t bestI = 0, bestJ = 1;
    float bestCost = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < rects.size(); ++i) {
      for (std::size_t j = i + 1; j < rects.size(); ++j) {
        skia::SkRect both = rects[i];
        both.join(rects[j]);
        const float cost = area(both) - area(rects[i]) - area(rects[j]);
        if (cost < bestCost) {
          bestCost = cost;
          bestI = i;
          bestJ = j;
        }
      }
    }
    rects[bestI].join(rects[bestJ]);
    rects.erase(rects.begin() + static_cast<std::ptrdiff_t>(bestJ));
  }
  return rects;
}

struct FrameResult {
  // Copied first, in order: the scroll views moved by this frame.
  std::vector<ScrollMove> fMoves;
  skia::SkRect fDamage = skia::SkRect::MakeEmpty();
  bool fWantsAnotherFrame = false;
  // When a node next wants a frame on its own, where none animates -- a
  // caret's blink -- in the time update() is given: infinity for never. A
  // host sleeps until then, not a frame at a time.
  double fWakeAtMs = std::numeric_limits<double>::infinity();
  // fDamage as a few rects, each repainted on its own: their union can be
  // much more than they are.
  std::vector<skia::SkRect> fDamageRects;
};

// The box a thing of that size occupies when its `origin` point is put on
// the `at` point of `parent`, offset by dx and dy.
[[nodiscard]] inline skia::SkRect
anchoredBox(const skia::SkRect &parent, float width, float height, Anchor at,
            Anchor origin, float dx = 0.0f, float dy = 0.0f) {
  const float ax = parent.fLeft + parent.width() * at.x;
  const float ay = parent.fTop + parent.height() * at.y;
  return skia::SkRect::MakeXYWH(ax - width * origin.x + dx,
                                ay - height * origin.y + dy, width, height);
}
[[nodiscard]] inline skia::SkRect anchoredBox(const skia::SkRect &parent,
                                              float width, float height,
                                              Anchor at, float dx = 0.0f,
                                              float dy = 0.0f) {
  return anchoredBox(parent, width, height, at, at, dx, dy);
}

[[nodiscard]] inline skia::SkRect inset(const skia::SkRect &rect,
                                        const Margin &by) {
  return skia::SkRect::MakeLTRB(rect.fLeft + by.fLeft, rect.fTop + by.fTop,
                                rect.fRight - by.fRight,
                                rect.fBottom - by.fBottom);
}
[[nodiscard]] inline skia::SkRect inset(const skia::SkRect &rect,
                                        float horizontal, float vertical) {
  return inset(rect, Margin{vertical, horizontal, vertical, horizontal});
}

[[nodiscard]] inline skia::SkRect joined(skia::SkRect a,
                                         const skia::SkRect &b) {
  if (a.isEmpty()) {
    return b;
  }
  if (!b.isEmpty()) {
    a.join(b);
  }
  return a;
}

// How many nodes a frame walked and how many it drew.
// How many nodes each walk touched, since the host last read them: what a
// frame costs, walk by walk -- read and reset by a host that traces frames.
struct WalkCounts {
  std::uint64_t tick = 0;       // update() at a frame's tick
  std::uint64_t restyle = 0;    // the walk between frames (a hover's restyle)
  std::uint64_t dirty = 0;      // markDirty
  std::uint64_t layout = 0;     // layoutNode, all calls
  std::uint64_t laidOut = 0;    // of those, laid out, not kept as it was
  std::uint64_t damage = 0;     // collectDamage
  std::uint64_t hover = 0;      // hover
  std::uint64_t animating = 0;  // animating and wakeAt
  std::uint64_t made = 0;       // nodes made
};
inline WalkCounts &walkCounts() {
  static WalkCounts kept;
  return kept;
}
inline std::uint64_t &visitedCount() {
  static std::uint64_t count = 0;
  return count;
}
inline std::uint64_t &drawnCount() {
  static std::uint64_t count = 0;
  return count;
}

// ---- transforms -----------------------------------------------------------


// How far along an ease is at time t of 1.
[[nodiscard]] inline float ease(const Easing &how, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return spl::visit(
      spl::overloaded{
          [t](easing::none) { return t; },
          [t](easing::out) { return 1.0f - (1.0f - t) * (1.0f - t); },
          [t](easing::out_quint) { return skiff::paint::outQuint(t); },
          [t](easing::out_elastic_half) {
            return skiff::paint::outElasticHalf(t);
          },
      },
      how);
}

// What a transform animates: a closed set.
namespace property {
struct alpha {};
struct x {};
struct y {};
struct width {};
struct height {};
struct scale {};
} // namespace property
using Property = spl::variant<property::alpha, property::x, property::y,
                                property::width, property::height,
                                property::scale>;

struct Transform {
  Property fProperty;
  float fFrom = 0.0f;
  float fTo = 0.0f;
  double fStartMs = 0.0;
  double fEndMs = 0.0;
  Easing fEasing;
};

// ---- style identity ---------------------------------------------------------


// ---- input and accessibility --------------------------------------------

// One axis of scrolling, as a gesture: the press, the slop before it counts
// as a drag, speed measured against the clock, friction, the spring past an
// end, and a wheel. Deliberately not a node -- a virtualised list positions
// its own children and cannot be a ScrollContainer, but it wants exactly this
// behaviour, and there is no version of it that is nearly right.
class ScrollGesture {
public:
  void setBounds(float lo, float hi) {
    fLo = lo;
    fHi = std::max(lo, hi);
    // The target kept as it was asked for, clamped where it is used: a
    // layout in a provisional box -- a list in a panel whose card sizes
    // itself, laid out first as wide as the window, so much shorter -- gave
    // bounds for one pass only, and clamping to them took the reader back
    // up the list once the real ones came.
  }
  [[nodiscard]] float offset() const noexcept { return fOffset; }
  [[nodiscard]] float target() const noexcept { return this->aim(); }
  [[nodiscard]] bool dragging() const noexcept { return fDragging; }
  // Still moving on its own: a flick running out, or an end springing back.
  [[nodiscard]] bool moving() const noexcept {
    return fFlinging || std::abs(fOffset - this->aim()) > 0.05f;
  }

  void jumpTo(float value) {
    fOffset = fTarget = std::clamp(value, fLo, fHi);
    fFlinging = false;
    fWheeling = false;
    fVelocity = 0.0f;
  }
  // Everything moved by `delta` -- the contents grew above what is looked
  // at: the view, where it is going, and where a drag began, together, so
  // a drag or a glide under way goes on as it was.
  void shift(float delta) {
    fOffset += delta;
    fTarget += delta;
    fPressOffset += delta;
  }
  void glideTo(float value) {
    fFromRest = !this->moving();
    fTarget = std::clamp(value, fLo, fHi);
    fFlinging = false;
    fWheeling = false;
    fVelocity = 0.0f;
  }
  // A wheel's notch: the target moved by a step, and the view glides there
  // slowly enough that the notches of a turn -- a tenth of a second or so
  // apart -- make one motion, easing out after the last, rather than a jump
  // and a stop each.
  void wheel(float ticks, float step) {
    fFromRest = !this->moving();
    fFlinging = false;
    fWheeling = true;
    fVelocity = 0.0f;
    fTarget = std::clamp(this->aim() - ticks * step, fLo, fHi);
  }

  // Touching a list that is still flying stops it where it is, which is the
  // gesture everyone already knows. Returns true when it caught something.
  bool press(float position) {
    fPressAt = position;
    fPressOffset = fOffset;
    fLastAt = position;
    fLastMs = 0.0;
    fVelocity = 0.0f;
    fDragging = false;
    const bool caught = fFlinging;
    fWheeling = false;
    if (caught) {
      fFlinging = false;
      fTarget = fOffset;
    }
    return caught;
  }

  // True once the finger has travelled far enough to be a drag rather than a
  // tap; from then on the caller should keep the gesture and stop whatever
  // else the press might have meant.
  bool drag(float position, double nowMs) {
    const float travelled = position - fPressAt;
    if (!fDragging) {
      if (std::abs(travelled) < kSlop) {
        return false;
      }
      fDragging = true;
    }
    // Units per millisecond, smoothed: a finger stutters, and one frame is a
    // poor witness. Against the clock, or a slow frame reads as a fast flick.
    if (fLastMs > 0.0 && nowMs > fLastMs) {
      const float sample =
          static_cast<float>((fLastAt - position) / (nowMs - fLastMs));
      fVelocity = fVelocity * (1.0f - kVelocityMix) + sample * kVelocityMix;
    }
    fLastAt = position;
    fLastMs = nowMs;

    // Past either end it follows at a fraction, which is the resistance a
    // touch surface has.
    const float wanted = fPressOffset - travelled;
    if (wanted < fLo) {
      fOffset = fLo + (wanted - fLo) * kOverscroll;
    } else if (wanted > fHi) {
      fOffset = fHi + (wanted - fHi) * kOverscroll;
    } else {
      fOffset = wanted;
    }
    fTarget = fOffset;
    return true;
  }

  void release() {
    if (!fDragging) {
      return;
    }
    fDragging = false;
    if (fOffset < fLo || fOffset > fHi) {
      fVelocity = 0.0f; // let go past the end: the spring, not a flick
      fTarget = std::clamp(fOffset, fLo, fHi);
    } else if (std::abs(fVelocity) > kMinVelocity) {
      fFlinging = true;
    }
  }
  void cancel() {
    fDragging = false;
    fFlinging = false;
    fVelocity = 0.0f;
    fTarget = std::clamp(fOffset, fLo, fHi);
  }

  // Per frame. Returns true when the offset moved and the caller has to be
  // laid out again.
  bool advance(double dtMs, float tauMs = 30.0f) {
    const float previous = fOffset;
    // A glide begun from rest: its first step one frame's, not the time
    // since the last frame drawn -- which, at rest, was long ago, and made
    // the first step most of the way at once.
    const double dt = std::max(0.0, fFromRest ? std::min(dtMs, 1000.0 / 60.0) : dtMs);
    fFromRest = false;
    // Between frames the bounds are the real ones: the target within them
    // from here, so that what shrank for good -- a search's few results --
    // does not take the view back down when it grows again.
    if (!fDragging) {
      fTarget = this->aim();
    }
    if (fDragging) {
      return false; // the finger owns it
    }
    if (fFlinging) {
      // Friction per millisecond, so thirty frames and two hundred agree on
      // how far a flick travels.
      const float decay = -std::log(kFriction);
      const float untilRest = std::log(std::abs(fVelocity) / kMinVelocity) / decay;
      const float elapsed = std::clamp(static_cast<float>(dt), 0.0f, std::max(0.0f, untilRest));
      const float before = fVelocity;
      fVelocity *= std::exp(-decay * elapsed);
      // Integrate the decay, rather than multiplying the final velocity by
      // the frame time: the latter made the distance depend on the FPS.
      fOffset += (before - fVelocity) / decay;
      if (fOffset < fLo || fOffset > fHi ||
          dt >= untilRest) {
        fFlinging = false;
        fVelocity = 0.0f;
        fTarget = std::clamp(fOffset, fLo, fHi);
      } else {
        fTarget = fOffset;
      }
    } else {
      const float aim = this->aim();
      fOffset = paint::approach(fOffset, aim, fWheeling ? kWheelTauMs : tauMs, dt);
      if (std::abs(fOffset - aim) < 0.05f) {
        fOffset = aim;
        fWheeling = false;
      }
    }
    return fOffset != previous;
  }

  // How far a finger travels before this is a drag and not a tap.
  static constexpr float kSlop = 6.0f;

private:
  // Where it is going, within its bounds as they are now.
  [[nodiscard]] float aim() const noexcept { return std::clamp(fTarget, fLo, fHi); }
  static constexpr float kFriction = 0.994f;
  static constexpr float kMinVelocity = 0.05f;
  static constexpr float kVelocityMix = 0.35f;
  static constexpr float kOverscroll = 0.4f;
  // A wheel's glide: its time constant -- short enough to feel quick, long
  // enough to join a turn's notches.
  static constexpr float kWheelTauMs = 55.0f;

  float fOffset = 0.0f;
  float fTarget = 0.0f;
  float fLo = 0.0f;
  float fHi = 0.0f;
  float fPressAt = 0.0f;
  float fPressOffset = 0.0f;
  float fLastAt = 0.0f;
  double fLastMs = 0.0;
  float fVelocity = 0.0f; // units per millisecond
  bool fDragging = false;
  bool fFlinging = false;
  bool fWheeling = false;
  bool fFromRest = false;
};



// What a pill's picture is -- a mention's avatar, in a text -- given what
// the pill links to: the program says, as data, and the text draws it as an
// avatar is drawn: the picture where there is one, over a gradient with
// initials in white. No drawing in the program.
struct PillPicture {
  const skia::Sp<skia::SkImage> *picture = nullptr;
  skia::SkColor top = 0, bottom = 0;  // the gradient, where there is no picture
  std::string initials;
};

// A node laid out past its parent's content box, in a flow that does not
// grow to hold it and does not clip it: nothing the layout meant. The node
// and its parent, by their types, and by how much on each axis -- told once
// a node, until it fits again. A program logs it, a test fails on it.
// ---- what a node is, without RTTI --------------------------------------------
//
// A type's name, as the compiler spells it in a function template's own
// signature (__PRETTY_FUNCTION__: "... [T = mux::ui::accounts_side]"; MSVC's
// __FUNCSIG__), cut at compile time to its last part with no template
// arguments -- "Bound", "Text", "accounts_side". Only the cut name is kept in
// the program; the signature is read in a constant expression and nothing
// of it is emitted. With it, and an address that is one per type, nothing
// in skiff needs typeid, and a program can be built without RTTI.

// The views being laid out, innermost last: each named view (SKIFF_NAMED)
// says itself while its children are laid out, so what is told of them can
// say where it is.
inline std::vector<std::string_view> &viewsLaidOut() {
  static std::vector<std::string_view> kept;
  return kept;
}
struct ViewLaidOut {
  // Nothing noted for a node that is no view (no name).
  explicit ViewLaidOut(const char *name) : fName(name) {
    if (fName != nullptr)
      viewsLaidOut().push_back(fName);
  }
  ~ViewLaidOut() {
    if (fName != nullptr)
      viewsLaidOut().pop_back();
  }
  ViewLaidOut(const ViewLaidOut &) = delete;
  ViewLaidOut &operator=(const ViewLaidOut &) = delete;
  const char *fName;
};
struct Overflow {
  std::string node;
  std::string parent;
  float x = 0.0f, y = 0.0f;
  // The innermost named view it was laid out in, where one was.
  std::string view;
};
// What the layout has found sticking out since the program last read it:
// the program drains it -- logs it, a test checks it. Held to a number, so
// a program that never reads it does not grow it for ever.
inline std::vector<Overflow> &overflows() {
  static std::vector<Overflow> kept;
  return kept;
}
// Which nodes asked for another frame, by type, and whether for a transform
// still running or for settling() -- where a program's frames never stop,
// what keeps them going. Kept only while a program asks (traceSettling);
// it drains them, and they are held to a number.
struct Settling {
  NodeKind kind;
  bool transform = false;
};
inline bool &traceSettling() {
  static bool on = false;
  return on;
}
inline std::vector<Settling> &settlers() {
  static std::vector<Settling> kept;
  return kept;
}
// Which nodes marked damage this frame, by type, and where: what a frame
// repaints, and why. Kept only while a program asks (traceSettling), as
// the settlers are.
struct Damager {
  NodeKind kind;
  skia::SkRect rect = skia::SkRect::MakeEmpty();
  bool relaid = false;  // its layout was made again, not only its look
  bool moved = false;   // only moved: laid out elsewhere, or a child gone
};
inline std::vector<Damager> &damagers() {
  static std::vector<Damager> kept;
  return kept;
}
// What made a frame lay out: the nodes found not laid out, and why -- their
// own layout undone, or their set of children changed. Kept while traced.
struct Dirtier {
  NodeKind kind;
  bool children = false;  // its children changed; else its own layout was undone
  skia::SkRect bounds = skia::SkRect::MakeEmpty();
};
inline std::vector<Dirtier> &dirtiers() {
  static std::vector<Dirtier> kept;
  return kept;
}
inline void tellOverflow(Overflow one) {
  if (overflows().size() < 256) {
    overflows().push_back(std::move(one));
  }
}


// How many device pixels a unit is: the window's display scale, said by the
// host each frame. What moves -- a scrolled list -- is placed on whole
// device pixels with it, so that its text and its fills move together
// rather than the glyphs creeping by fractions first.
inline float &pixelScale() {
  static float kept = 1.0f;
  return kept;
}
// A position on the device pixel grid.
[[nodiscard]] inline float snapToPixel(float v) {
  const float scale = pixelScale() > 0.0f ? pixelScale() : 1.0f;
  return std::round(v * scale) / scale;
}

// ---- the state every node has --------------------------------------------

// What every node is, beyond what its own type adds: its layout inputs and
// the box they produced, what it is animating, how it is styled, its flags,
// and what it has to repaint. A node holds one as `fState`.
//
// Changing a node goes through the setters here: they mark this State and
// nothing else, and the next frame's walks find the mark.
// What a frame has to look at, found without walking the whole tree: a node
// that changes -- damaged, laid out again, restyled -- marks itself and every
// node above it, by id, through the parents the walks saw; the frame's
// layout, damage and restyle walks go down only what is marked. What the
// table cannot answer -- a node no walk has seen, a node made since --
// makes the next frame walk everything, as every frame did before; and
// SKIFF_FULL_WALKS=1 makes every frame do that, to compare.
namespace work {
// Every node's entry in the work: a node's id is its slot here and the
// slot's generation, (generation << 32) | slot -- so what the walks ask of a
// node by its id is one index and one compare, not a hash and a probe. A
// slot let go is used again under the next generation: a dead node's id,
// kept somewhere, finds nothing. Slot 0 is never a node's.
struct Entry {
  NodeId id = 0;  // the node whose it is; 0 while free
  NodeId parent = 0;
  NodeId place = 0;
  NodeId hintHead = 0;
  float shiftX = 0.0f, shiftY = 0.0f;  // the node's fShiftX and fShiftY
  std::uint32_t generation = 0;
  bool pending = false, ticking = false, root = false, hinted = false, reshaped = false;
  bool unseen = false;  // made, and no tick has come to it yet
  bool hasParent = false, hasPlace = false, hasHintHead = false;
};
inline std::vector<Entry> &entries() {
  static std::vector<Entry> kept(1);
  return kept;
}
inline std::vector<std::uint32_t> &freeSlots() {
  static std::vector<std::uint32_t> kept;
  return kept;
}
[[nodiscard]] inline Entry *entry(NodeId id) {
  auto &all = entries();
  const auto slot = static_cast<std::size_t>(id & 0xffffffffu);
  if (slot == 0 || slot >= all.size()) {
    return nullptr;
  }
  Entry &found = all[slot];
  return found.id == id ? &found : nullptr;
}
// The nodes the last tick found still moving -- a flash, a fade, a glide:
// marked again once the frame is done, so that the next tick comes to them
// by their marks. The damage walk clears a mark as it passes; what moves
// was otherwise found again only through the ticking set.
inline std::vector<NodeId> &moving() {
  static std::vector<NodeId> kept;
  return kept;
}
// The node that asked for the keys as it read what it shows (a field the
// model says is to be typed into): given them by its scene at the next
// layout, where it is focusable then. 0: none asked.
inline NodeId &focusWanted() {
  static NodeId kept = 0;
  return kept;
}
// A subtree made in place by what holds it -- a dialog opened, a panel slid
// in, a page shown -- outside a binding's walk: for the program to walk its
// bindings whole once (it drains this), so what was made reads the model.
inline bool &subtreeMade() {
  static bool kept = false;
  return kept;
}
// The nodes made since the last tick: where one is not come to by a tick
// that goes only where things changed, it looks everywhere.
inline std::vector<NodeId> &births() {
  static std::vector<NodeId> kept;
  return kept;
}
// A new node's id: a free slot, or a new one.
[[nodiscard]] inline NodeId allocate() {
  auto &all = entries();
  auto &free = freeSlots();
  std::uint32_t slot = 0;
  if (!free.empty()) {
    slot = free.back();
    free.pop_back();
  } else {
    slot = static_cast<std::uint32_t>(all.size());
    all.emplace_back();
  }
  Entry &made = all[slot];
  const std::uint32_t generation = made.generation + 1;
  made = Entry{};
  made.generation = generation;
  made.id = (static_cast<NodeId>(generation) << 32) | slot;
  made.unseen = true;
  births().push_back(made.id);
  ++walkCounts().made;
  return made.id;
}
// A set of nodes: a flag in their entries, and how many have it -- and,
// where it is emptied or gone through at once, which were given it.
class FlagSet {
public:
  FlagSet(bool Entry::*flag, bool listed) : fFlag(flag), fListed(listed) {}
  bool insert(NodeId id, NodeId = 0) {
    Entry *at = entry(id);
    if (at == nullptr || at->*fFlag) {
      return false;
    }
    at->*fFlag = true;
    ++fCount;
    if (fListed) {
      fMembers.push_back(id);
    }
    return true;
  }
  [[nodiscard]] bool contains(NodeId id) const {
    const Entry *at = entry(id);
    return at != nullptr && at->*fFlag;
  }
  void erase(NodeId id) {
    Entry *at = entry(id);
    if (at != nullptr && at->*fFlag) {
      at->*fFlag = false;
      --fCount;
    }
  }
  [[nodiscard]] bool empty() const noexcept { return fCount == 0; }
  void clear() {
    for (const NodeId id : std::exchange(fMembers, {})) {
      this->erase(id);
    }
  }
  template <class F> void each(F &&f) const {
    for (const NodeId id : fMembers) {
      if (this->contains(id)) {
        f(id, NodeId{0});
      }
    }
  }

private:
  bool Entry::*fFlag;
  bool fListed;
  std::size_t fCount = 0;
  std::vector<NodeId> fMembers;
};
// A value for some nodes: a field of their entries, and whether they have it.
class FieldMap {
public:
  FieldMap(NodeId Entry::*value, bool Entry::*present, bool listed)
      : fValue(value), fPresent(present), fListed(listed) {}
  bool insert(NodeId id, NodeId value) {
    Entry *at = entry(id);
    if (at == nullptr) {
      return false;
    }
    const bool fresh = !(at->*fPresent);
    at->*fValue = value;
    at->*fPresent = true;
    if (fresh && fListed) {
      fMembers.push_back(id);
    }
    return fresh;
  }
  [[nodiscard]] const NodeId *find(NodeId id) const {
    const Entry *at = entry(id);
    return at != nullptr && at->*fPresent ? &(at->*fValue) : nullptr;
  }
  [[nodiscard]] bool contains(NodeId id) const { return this->find(id) != nullptr; }
  void erase(NodeId id) {
    if (Entry *at = entry(id)) {
      at->*fPresent = false;
    }
  }
  void clear() {
    for (const NodeId id : std::exchange(fMembers, {})) {
      this->erase(id);
    }
  }

private:
  NodeId Entry::*fValue;
  bool Entry::*fPresent;
  bool fListed;
  std::vector<NodeId> fMembers;
};

inline FlagSet &pending() {
  static FlagSet kept(&Entry::pending, false);
  return kept;
}
inline FieldMap &parents() {
  static FieldMap kept(&Entry::parent, &Entry::hasParent, false);
  return kept;
}
inline FlagSet &roots() {
  static FlagSet kept(&Entry::root, false);
  return kept;
}
// The nodes a frame's tick goes to, kept from frame to frame: those that do
// something each frame -- poll in update(), animate, settle -- and every
// node above them.
inline FlagSet &ticking() {
  static FlagSet kept(&Entry::ticking, false);
  return kept;
}
// Bumped as a node is made: where one was, the next tick goes everywhere
// once -- where it was put, no one said.
inline std::uint64_t &bornGeneration() {
  static std::uint64_t at = 1;
  return at;
}
// Frames finished: what a node did "the frame before" is told by it.
// Nodes raised over the rest -- a dropdown's plate hung below its button,
// over the rows under it -- and, built from them, those and all they are
// in: a child on the way to a raised one is drawn after its siblings, at
// every level, so the raised one is drawn over the whole window and -- the
// hit walk going by draw order -- pressed through what it covers.
inline std::vector<NodeId> &raised() {
  static std::vector<NodeId> ids;
  return ids;
}
inline bool &raisedStale() {
  static bool stale = false;
  return stale;
}
inline std::unordered_set<NodeId> &raisedPath() {
  static std::unordered_set<NodeId> path;
  return path;
}
inline std::uint64_t &frameNumber() {
  static std::uint64_t at = 1;
  return at;
}
// The frame's layout pass: a node's move is measured over one.
inline std::uint64_t &layoutPass() {
  static std::uint64_t at = 1;
  return at;
}
// How deep the layout now under way is in provisional boxes: a parent sized
// by what it holds lays its children out first in the room it might have,
// then in what it has. What lays out under one only measures: a view that
// scrolls keeps its offset for the real box.
inline int &provisionalDepth() {
  static int depth = 0;
  return depth;
}
// Whether the tick now under way goes everywhere.
inline bool &tickingFull() {
  static bool on = true;
  return on;
}
// Bumped where a full walk is needed; a scene walks everything while its own
// count is behind.
inline std::uint64_t &fullGeneration() {
  static std::uint64_t at = 1;
  return at;
}
// Whether the walks now under way go everywhere.
inline bool &walkingFull() {
  static bool on = true;
  return on;
}
inline bool disabled() {
  static const bool off = std::getenv("SKIFF_FULL_WALKS") != nullptr;
  return off;
}
// Where a node sits under its parent: its place in eachChild's order, as
// the last walk through all of the parent's children saw it.
inline FieldMap &places() {
  static FieldMap kept(&Entry::place, &Entry::hasPlace, false);
  return kept;
}
// Nodes whose children may not be those they were -- one went, was moved, or
// changed places: walked child by child, their set of children checked.
inline FlagSet &reshaped() {
  static FlagSet kept(&Entry::reshaped, true);
  return kept;
}
// The way down to what is marked: for each parent, the children marked
// under it and their places -- lists threaded through one vector, headed
// per parent. The walks go straight to them rather than past every child.
struct Hint {
  NodeId fParent;
  NodeId fChild;
  std::uint32_t fPlace;
  std::uint32_t fNext;  // the next under the same parent, 1-based; 0 ends
};
inline std::vector<Hint> &hints() {
  static std::vector<Hint> kept;
  return kept;
}
inline FieldMap &hintHeads() {
  static FieldMap kept(&Entry::hintHead, &Entry::hasHintHead, true);
  return kept;
}
inline FlagSet &hinted() {
  static FlagSet kept(&Entry::hinted, true);
  return kept;
}
// More marked children than this under one node: it is walked child by
// child -- going to each by its place costs more than passing them all.
inline constexpr std::size_t kFewHints = 8;
inline void hint(NodeId parent, NodeId child) {
  if (!hinted().insert(child)) {
    return;
  }
  const NodeId *place = places().find(child);
  if (place == nullptr) {
    reshaped().insert(parent);  // its place unknown: the parent, walked whole
    return;
  }
  auto &all = hints();
  const NodeId *head = hintHeads().find(parent);
  all.push_back({parent, child, static_cast<std::uint32_t>(*place),
                 head != nullptr ? static_cast<std::uint32_t>(*head) : 0u});
  hintHeads().insert(parent, all.size());
}
// A node changed: it and all above it, marked, and the way down to it.
inline void mark(NodeId id) {
  auto &marked = pending();
  const auto &up = parents();
  for (;;) {
    marked.insert(id);
    const NodeId *above = up.find(id);
    if (above == nullptr) {
      // Not seen under anything yet: looked for everywhere -- unless it is
      // new, which the next tick finds (where it was put, or everywhere).
      // A node marks itself as it is made, filling itself in: a list's new
      // rows had every walk of the frame go over the whole tree.
      const Entry *here = entry(id);
      if (!roots().contains(id) && (here == nullptr || !here->unseen)) {
        ++fullGeneration();
      }
      return;
    }
    const NodeId parent = *above;
    hint(parent, id);
    id = parent;
  }
}
// Laid out or moved by its parent's layout: for the damage walk to reach.
// One whose parent no walk has seen yet: this frame's damage is looked for
// everywhere.
inline void reach(NodeId id) {
  pending().insert(id);
  if (const NodeId *above = parents().find(id)) {
    hint(*above, id);
  } else if (!roots().contains(id)) {
    walkingFull() = true;
  }
}
// The child of `from` on the way down to `to`, as the walks last saw the
// tree: 0 where that is not known. A search for a node goes down this way,
// not through every node of the tree.
[[nodiscard]] inline NodeId childToward(NodeId from, NodeId to) {
  NodeId at = to;
  for (int depth = 0; depth < 512; ++depth) {
    const NodeId *up = parents().find(at);
    if (up == nullptr) {
      return 0;
    }
    if (*up == from) {
      return at;
    }
    at = *up;
  }
  return 0;
}
// The raised nodes and all they are in, as the walks last saw the tree:
// rebuilt when one is raised or put back, and once a frame, as one may
// have been moved.
[[nodiscard]] inline const std::unordered_set<NodeId> &raisedAbove() {
  static std::uint64_t builtAt = ~std::uint64_t{0};
  std::unordered_set<NodeId> &path = raisedPath();
  if (raisedStale() || builtAt != frameNumber()) {
    raisedStale() = false;
    builtAt = frameNumber();
    path.clear();
    for (const NodeId id : raised()) {
      NodeId at = id;
      for (int depth = 0; depth < 512 && at != 0; ++depth) {
        path.insert(at);
        const NodeId *up = parents().find(at);
        at = up == nullptr ? 0 : *up;
      }
    }
  }
  return path;
}
// How far a node is drawn from where it is laid out: its shift and all its
// ancestors', as the walks last saw them -- a scrolled list's rows are laid
// out where they were and drawn moved by the list's.
struct Offset {
  float x = 0.0f, y = 0.0f;
};
[[nodiscard]] inline Offset drawnOffset(NodeId id) {
  Offset sum;
  NodeId at = id;
  for (int depth = 0; depth < 512 && at != 0; ++depth) {
    const Entry *here = entry(at);
    if (here == nullptr) {
      break;
    }
    sum.x += here->shiftX;
    sum.y += here->shiftY;
    at = here->hasParent ? here->parent : 0;
  }
  return sum;
}
// The damage walk's way down: the shifts of the nodes above where it is, to
// say what it finds where it is on the screen.
inline Offset &damageOffset() {
  static Offset at;
  return at;
}
// The masks above the node, in screen coordinates. Individual damage
// rectangles must obey the same clips as the subtree's damage union.
inline std::optional<skia::SkRect> &damageClip() {
  static std::optional<skia::SkRect> clip;
  return clip;
}
// Whether a walk goes into a child.
[[nodiscard]] inline bool visit(NodeId id) { return walkingFull() || pending().contains(id); }
// Written only where it changed: most walks pass the same children as the
// frame before, and a write costs more than the look.
inline void record(NodeId child, NodeId parent) {
  if (const NodeId *was = parents().find(child); was == nullptr || *was != parent) {
    parents().insert(child, parent);
  }
}
inline void record(NodeId child, NodeId parent, std::uint32_t place) {
  record(child, parent);
  if (const NodeId *was = places().find(child); was == nullptr || *was != place) {
    places().insert(child, place);
  }
}
// Its parent's children changed: that parent, walked whole and marked.
inline void reshape(NodeId id) {
  if (const NodeId *above = parents().find(id)) {
    const NodeId parent = *above;
    // Marked once: a list whose rows all move said it once for each.
    if (reshaped().insert(parent)) {
      mark(parent);
    }
  }
}
// A node gone: its parent reshaped, its entries with it.
inline void gone(NodeId id) {
  reshape(id);
  pending().erase(id);
  ticking().erase(id);
  roots().erase(id);
  hinted().erase(id);
  reshaped().erase(id);
  if (std::erase(raised(), id) != 0) {
    raisedStale() = true;
  }
  if (Entry *at = entry(id)) {
    at->id = 0;
    freeSlots().push_back(static_cast<std::uint32_t>(id & 0xffffffffu));
  }
}
// A frame done: the hints to what is still marked -- out of view, or in
// another scene not walked yet -- kept, the rest dropped; so with what was
// reshaped.
inline void settle() {
  std::vector<Hint> kept = std::exchange(hints(), {});
  hintHeads().clear();
  hinted().clear();
  for (const Hint &each : kept) {
    if (pending().contains(each.fChild)) {
      hint(each.fParent, each.fChild);
    }
  }
  auto &was = reshaped();
  if (!was.empty()) {
    std::vector<NodeId> still;
    was.each([&](NodeId id, NodeId) {
      if (pending().contains(id)) {
        still.push_back(id);
      }
    });
    was.clear();
    for (const NodeId id : still) {
      was.insert(id);
    }
  }
}

// Whether a State still is its node: a moved-from one handed its identity on,
// and its end removes nothing. A move reshapes the parent -- a node moved in
// its list changed places; one moved onto another ends that one.
struct Alive {
  bool on = true;
  NodeId id = 0;
  Alive() = default;
  Alive(Alive &&other) noexcept : on(std::exchange(other.on, false)), id(other.id) {
    if (on) {
      reshape(id);
    }
  }
  Alive &operator=(Alive &&other) noexcept {
    if (this != &other) {
      if (on && id != other.id) {
        gone(id);
      }
      on = std::exchange(other.on, false);
      id = other.id;
      if (on) {
        reshape(id);
      }
    }
    return *this;
  }
};
}  // namespace work

// Nodes waiting on something from outside the scene -- a picture to be
// fetched: woken together when it comes, rather than each asking at every
// frame. A node waits by its id; one gone meanwhile is passed over.
class Waiters {
public:
  void wait(NodeId id) { fIds.insert(id); }
  void wake() {
    for (const NodeId id : std::exchange(fIds, {})) {
      if (work::parents().contains(id)) {
        work::mark(id);
      }
    }
  }
  [[nodiscard]] bool empty() const noexcept { return fIds.empty(); }

private:
  std::unordered_set<NodeId> fIds;
};

class State {
public:
  // Made: new until the frame's walk first sees it, which marks it there;
  // the next tick goes everywhere to find it.
  State() : fId(work::allocate()) {
    fAlive.id = fId;
    ++work::bornGeneration();
  }
  // Gone: its entries in the work tables with it -- not a moved-from one's --
  // and the node it was under marked, for where it was to be repainted.
  ~State() {
    if (fAlive.on) {
      work::gone(fId);
    }
  }
  // A node is one thing on the screen: copying it would make two with one
  // identity. Moving keeps the identity, so a node can be built and then
  // put where it lives.
  State(const State &) = delete;
  State &operator=(const State &) = delete;
  State(State &&) noexcept = default;
  State &operator=(State &&) noexcept = default;

  // -- layout inputs
  float fWidth = 0.0f, fHeight = 0.0f;  // absolute, or a fraction if relative
  Axes fRelativeSizeAxes; // size is a fraction of the parent
  Axes fAutoSizeAxes;     // size follows the children
  // Inside a flow, takes an equal share of what the other children leave
  // along the flow's axis.
  Axes fGrowAxes;
  // The axes it gives way along, and how narrow its flow asked it to be,
  // where it asked (0: not asked): laid out no wider, and no narrower than
  // its minWidth.
  Axes fShrinkAxes;
  float fShrunkTo = 0.0f;
  // Bounds on the computed size. Zero means no limit, on the maximums.
  float fMinWidth = 0.0f, fMaxWidth = 0.0f;
  float fMinHeight = 0.0f, fMaxHeight = 0.0f;
  // Overrides the container's cross-axis alignment for this child alone.
  std::optional<Align> fAlignSelf{};
  // Drawn and hit-tested in this order within the parent, low first.
  float fDepth = 0.0f;
  Anchor fAnchor = anchor::kTopLeft; // point in the parent to attach to
  Anchor fOrigin = anchor::kTopLeft; // point in this node that lands there
  Margin fMargin;                    // outside the node
  Margin fPadding;                   // inside, applied to children
  float fX = 0.0f, fY = 0.0f;        // offset from the anchor
  float fScale = 1.0f;
  float fAlpha = 1.0f;
  // Placed against this node's box rather than the parent's, when set. The
  // followed node is laid out first (earlier in the parent, or elsewhere
  // earlier in the tree) and does not move in memory while followed.
  const State *fFollow = nullptr;
  bool fMasking = false; // clip children to these bounds
  bool fRaised = false;  // drawn over the rest of the window: see work::raised
  // The view it is the top of, where it was named where it was written
  // (SKIFF_NAMED): noted while it is laid out, for what is told of its parts.
  const char *fViewName = nullptr;
  // Told already that it sticks out of its parent: told once, until it fits.
  bool fOverflowTold = false;
  // Two ways of sizing an axis said at once, and which one was kept: told
  // where the node is next laid out -- what it is, and the view it is in --
  // not where the spec is applied, which knows neither.
  std::string_view fSizingConflict;
  Cursor fCursor = cursor::arrow{};
  float fCornerRadius = 0.0f;
  std::optional<Corners> fCorners;
  std::optional<Tail> fTail;
  // Painted in the box, under the rest: see Spec.
  std::optional<skia::SkColor> fBackground, fHoverBackground, fSelectedBackground, fFocusBackground;
  std::optional<Gradient> fGradient;
  std::optional<Shadow> fShadow;
  // Placed by its anchor where its parent is a flow, not in the flow: a
  // badge over a corner, a mark beside a row.
  bool fOutOfFlow = false;
  // Drawn moved by this much: see Spec.
  float fShiftX = 0.0f, fShiftY = 0.0f;
  std::optional<Border> fBorder;
  bool fVisible = true;
  bool fRevealOnHover = false;  // shown while what holds it is hovered

  // -- the result of layout
  skia::SkRect fBounds = skia::SkRect::MakeEmpty();

  [[nodiscard]] NodeId id() const noexcept { return fId; }
  [[nodiscard]] float width() const noexcept { return fWidth; }
  [[nodiscard]] float height() const noexcept { return fHeight; }
  [[nodiscard]] float x() const noexcept { return fX; }
  [[nodiscard]] float y() const noexcept { return fY; }
  [[nodiscard]] float scale() const noexcept { return fScale; }
  [[nodiscard]] float alpha() const noexcept { return fAlpha; }
  [[nodiscard]] bool visible() const noexcept { return fVisible; }
  [[nodiscard]] Axes growAxes() const noexcept { return fGrowAxes; }
  [[nodiscard]] std::optional<Align> alignSelf() const noexcept {
    return fAlignSelf;
  }
  [[nodiscard]] const Margin &margin() const noexcept { return fMargin; }
  [[nodiscard]] const Margin &padding() const noexcept { return fPadding; }
  [[nodiscard]] const skia::SkRect &bounds() const noexcept { return fBounds; }
  // Where it is shown and pressed, in the space its parent lays it out in:
  // its bounds moved by its own shift. A part moved apart from its layout --
  // beside a sticker -- is pressed there, not where it was laid out.
  [[nodiscard]] skia::SkRect shownBounds() const noexcept { return fBounds.makeOffset(fShiftX, fShiftY); }
  // The box children are laid out in: this node, less its padding.
  [[nodiscard]] skia::SkRect contentBox() const {
    return inset(fBounds, fPadding);
  }

  [[nodiscard]] bool hovered() const noexcept { return fHovered; }
  [[nodiscard]] float hoverX() const noexcept { return fHoverX; }
  [[nodiscard]] float hoverY() const noexcept { return fHoverY; }
  [[nodiscard]] bool focused() const noexcept { return fFocused; }
  [[nodiscard]] bool selected() const noexcept { return fSelected; }
  [[nodiscard]] bool disabled() const noexcept { return fDisabled; }

  // -- runtime changes
  // Moved, nothing else: its subtree goes where it is put at the next
  // layout, as it is -- not measured and laid out again, as arrange() has a
  // flow's moves.
  void setPosition(float x, float y) {
    if (x == fX && y == fY) {
      return;
    }
    fX = x;
    fY = y;
    fPlacementDirty = true;
    fDamaged = true;
    work::mark(fId);
  }
  void setSize(float width, float height) {
    if (width == fWidth && height == fHeight) {
      return;
    }
    fWidth = width;
    fHeight = height;
    this->invalidateLayout();
  }
  void setPadding(Margin padding) {
    if (padding == fPadding) {
      return;
    }
    fPadding = padding;
    this->invalidateLayout();
  }
  void setMargin(Margin margin) {
    if (margin == fMargin) {
      return;
    }
    fMargin = margin;
    this->invalidateLayout();
  }
  void setScale(float scale) {
    scale = std::max(0.0f, scale);
    if (scale == fScale) {
      return;
    }
    fScale = scale;
    this->invalidateLayout();
  }
  void setAlpha(float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha == fAlpha) {
      return;
    }
    fAlpha = alpha;
    this->markMovedOnly();
  }
  void setVisible(bool visible) {
    if (visible == fVisible) {
      return;
    }
    fVisible = visible;
    this->invalidateLayout();
  }
  void setFollow(const State *follow) {
    if (follow == fFollow) {
      return;
    }
    fFollow = follow;
    this->invalidateLayout();
  }
  // The pointer's shape over this node, and over those below it that say
  // nothing of their own.
  void setCursor(Cursor shape) { fCursor = shape; }
  [[nodiscard]] const Cursor &cursorShape() const noexcept { return fCursor; }
  void setMasking(bool masking) {
    if (masking == fMasking) {
      return;
    }
    fMasking = masking;
    this->markDamaged();
  }
  void setCornerRadius(float radius) {
    radius = std::max(0.0f, radius);
    if (radius == fCornerRadius) {
      return;
    }
    fCornerRadius = radius;
    this->markDamaged();
  }

  void addStyleRole(StyleRole role) {
    if (this->hasStyleRole(role)) {
      return;
    }
    fStyleRoles.push_back(role);
    this->restyle(true);
  }
  template <class Role> void addStyleRole() {
    this->addStyleRole(StyleRole::of<Role>());
  }
  void removeStyleRole(StyleRole role) {
    const auto old = fStyleRoles.size();
    std::erase(fStyleRoles, role);
    if (fStyleRoles.size() != old) {
      this->restyle(true);
    }
  }
  template <class Role> void removeStyleRole() {
    this->removeStyleRole(StyleRole::of<Role>());
  }
  [[nodiscard]] bool hasStyleRole(StyleRole role) const noexcept {
    return std::ranges::find(fStyleRoles, role) != fStyleRoles.end();
  }
  [[nodiscard]] std::span<const StyleRole> styleRoles() const noexcept {
    return fStyleRoles;
  }

  // State the node's own picture may depend on, so it repaints even where no
  // rule mentions it.
  void setSelected(bool selected) {
    if (selected == fSelected) {
      return;
    }
    fSelected = selected;
    this->restyle(true);
    this->markDamaged();
  }
  // A disabled node takes no input; the scene lets go of it at the next
  // event.
  void setDisabled(bool disabled) {
    if (disabled == fDisabled) {
      return;
    }
    fDisabled = disabled;
    this->restyle(true);
    this->markDamaged();
  }

  // A theme owns a `static constexpr auto styles = makeStyleSheet()...`, and
  // this subtree is styled by it; a scene's own sheet is set on its root.
  template <class Theme> void setStyleSheet() {
    fStyleResolver = StyleResolver::of<Theme>();
    fStyleSheetChanged = true;
    work::mark(fId);
  }
  void clearStyleSheet() {
    if (!fStyleResolver) {
      return;
    }
    fStyleResolver = {};
    fStyleSheetChanged = true;
    work::mark(fId);
  }

  // Writes a spec. What it does not mention is left as it was.
  void apply(const Spec &spec) {
    const auto before = this->commonValues();
    // Out of its parent's flow by a place: not one of the style fields
    // compared below -- placed at the anchor it had already (the top left,
    // as every node starts), it stayed in the flow until something else laid
    // the parent out again.
    bool flowChanged = false;
    if (spec.place) {
      fAnchor = *spec.place;
      fOrigin = *spec.place;
      flowChanged = !fOutOfFlow;
      fOutOfFlow = true;
    }
    if (spec.shiftX && *spec.shiftX != fShiftX) {
      this->setShift(*spec.shiftX, fShiftY);
      this->markMovedOnly();
    }
    if (spec.shiftY && *spec.shiftY != fShiftY) {
      this->setShift(fShiftX, *spec.shiftY);
      this->markMovedOnly();
    }
    if (spec.anchor) {
      fAnchor = *spec.anchor;
    }
    if (spec.origin) {
      fOrigin = *spec.origin;
    }
    if (spec.x) {
      fX = *spec.x;
    }
    if (spec.y) {
      fY = *spec.y;
    }
    if (spec.width) {
      fWidth = *spec.width;
    }
    if (spec.height) {
      fHeight = *spec.height;
    }
    // Start from the constructor's mode when the spec only adds a fill axis,
    // but honour an explicit kNone: fixed-size callers must be able to clear
    // a widget's full-width default.
    Axes relative = spec.relativeSize.value_or(fRelativeSizeAxes);
    if (spec.fill || spec.fillX) {
      relative = relative.with<axis::x>();
      fWidth = 1.0f;
    }
    if (spec.fill || spec.fillY) {
      relative = relative.with<axis::y>();
      fHeight = 1.0f;
    }
    if (spec.relativeSize || spec.fill || spec.fillX || spec.fillY) {
      fRelativeSizeAxes = relative;
    }
    if (spec.autoSize) {
      fAutoSizeAxes = *spec.autoSize;
    }
    if (spec.shrink) {
      fShrinkAxes = *spec.shrink;
    }
    if (spec.grow) {
      fGrowAxes = *spec.grow;
      // A grow said takes the place of a size from the content on that axis
      // (a box as tall as what it holds, made so by a combinator, told to
      // take what its column leaves). A fill stays: a grow across the way
      // of what holds the node means nothing, and a fill there is its size.
      if (!spec.autoSize)
        fAutoSizeAxes = fAutoSizeAxes.except(*spec.grow);
      // And of a fill the node said before -- its own look's, told later to
      // take what its column leaves beside its siblings (mux's emoji sections
      // under the search, the chat under its bar): grown there, not filling
      // its parent's whole room and standing out of it past the siblings.
      // A fill said with the grow, in the same spec, stays.
      Axes saidFill = spec.relativeSize.value_or(Axes{});
      if (spec.fill || spec.fillX)
        saidFill = saidFill.with<axis::x>();
      if (spec.fill || spec.fillY)
        saidFill = saidFill.with<axis::y>();
      fRelativeSizeAxes = fRelativeSizeAxes.except(spec.grow->except(saidFill));
    }
    if (spec.minWidth) {
      fMinWidth = *spec.minWidth;
    }
    if (spec.maxWidth) {
      fMaxWidth = *spec.maxWidth;
    }
    if (spec.minHeight) {
      fMinHeight = *spec.minHeight;
    }
    if (spec.maxHeight) {
      fMaxHeight = *spec.maxHeight;
    }
    if (spec.alignSelf) {
      fAlignSelf = *spec.alignSelf;
    }
    if (spec.depth) {
      fDepth = *spec.depth;
    }
    if (spec.margin.given) {
      fMargin = *spec.margin.given;
    }
    if (spec.padding.given) {
      fPadding = *spec.padding.given;
    }
    if (spec.cornerRadius) {
      fCornerRadius = *spec.cornerRadius;
    }
    // What only paints -- not in the style fields, compared below --
    // repaints where it changes: a background set each frame (a row's flash
    // fading) changed nothing on the screen, and a recorded node played its
    // first colour back for good.
    const auto paint = [&](auto &live, const auto &given) {
      if (given && given != live) {
        live = given;
        this->markDamaged();
      }
    };
    if (spec.corners && spec.corners != fCorners) {
      fCorners = spec.corners;
      this->markDamaged();
    }
    if (spec.tail) {
      const std::optional<Tail> now = spec.tail->shown ? spec.tail : std::nullopt;
      if (now != fTail) {
        fTail = now;
        this->markDamaged();
      }
    }
    paint(fBackground, spec.background);
    paint(fHoverBackground, spec.hoverBackground);
    paint(fSelectedBackground, spec.selectedBackground);
    paint(fFocusBackground, spec.focusBackground);
    paint(fGradient, spec.gradient);
    paint(fShadow, spec.shadow);
    paint(fBorder, spec.border);
    if (spec.masking) {
      fMasking = *spec.masking;
    }
    if (spec.scale) {
      fScale = *spec.scale;
    }
    if (spec.alpha) {
      fAlpha = *spec.alpha;
    }
    if (spec.visible) {
      fVisible = *spec.visible;
    }
    if (spec.revealOnHover) {
      fRevealOnHover = *spec.revealOnHover;
    }
    bool identityChanged = false;
    for (StyleRole role : spec.roles) {
      if (!this->hasStyleRole(role)) {
        fStyleRoles.push_back(role);
        identityChanged = true;
      }
    }
    if (spec.selected && *spec.selected != fSelected) {
      fSelected = *spec.selected;
      identityChanged = true;
    }
    if (spec.disabled && *spec.disabled != fDisabled) {
      fDisabled = *spec.disabled;
      identityChanged = true;
    }
    if (identityChanged) {
      this->restyle(true);
    }
    // Sizing an axis both from the parent and from the children asks for two
    // numbers at once: say which, and keep the relative one.
    if (fRelativeSizeAxes.any(fAutoSizeAxes)) {
      fSizingConflict = "relative and automatic sizing on the same axis";
      fAutoSizeAxes = axes::kNone;
    }
    // A flow writes a grown child's size; anything else claiming the axis
    // would be overwritten every frame.
    if (fGrowAxes.any(fRelativeSizeAxes | fAutoSizeAxes)) {
      fSizingConflict = "growing and sized another way on the same axis";
      fGrowAxes = axes::kNone;
    }
    const auto after = this->commonValues();
    if (!sameLayout(before, after) || flowChanged) {
      this->invalidateLayout();
    } else if (before != after) {
      this->markDamaged();
    }
  }

  // -- transforms
  void fadeTo(float target, double durationMs, Easing e = easing::out_quint{}) {
    this->transformTo(property::alpha{}, fAlpha, target, durationMs, e);
  }
  void moveToX(float target, double durationMs, Easing e = easing::out_quint{}) {
    this->transformTo(property::x{}, fX, target, durationMs, e);
  }
  void moveToY(float target, double durationMs, Easing e = easing::out_quint{}) {
    this->transformTo(property::y{}, fY, target, durationMs, e);
  }
  void resizeWidthTo(float target, double durationMs,
                     Easing e = easing::out_quint{}) {
    this->transformTo(property::width{}, fWidth, target, durationMs, e);
  }
  void resizeHeightTo(float target, double durationMs,
                      Easing e = easing::out_quint{}) {
    this->transformTo(property::height{}, fHeight, target, durationMs, e);
  }
  void scaleTo(float target, double durationMs, Easing e = easing::out_quint{}) {
    this->transformTo(property::scale{}, fScale, target, durationMs, e);
  }
  // Everything queued after this starts that much later.
  void delay(double ms) { fDelayMs = ms; }
  [[nodiscard]] bool transforming() const noexcept {
    return !fTransforms.empty();
  }

  // Lays this node out again at the next frame, and repaints where it is and
  // where it was.
  void invalidateLayout() {
    fLayoutValid = false;
    fDamaged = true;
    fRedrawn = true;
    fRelaid = true;
    work::mark(fId);
  }
  // Laid out again at the next frame, repainting nothing by itself: what
  // the layout moves repaints, as it finds it (a scroll view, copied).
  // Its drawing, and all under it, recorded once and played back until
  // something in it is damaged: a message's bubble, drawn at every repaint
  // of a strip it is in, walked again through all its parts each time.
  void setRecorded(bool on) {
    fRecorded = on;
    fPicture = nullptr;
  }
  bool fRecorded = false;
  skia::Sp<skia::SkPicture> fPicture;
  // Drawn over other things -- a popup's plate, a sheet sliding in: what a
  // program's paint may treat apart (blurring what is under it, live).
  bool fFloats = false;
  // Recorded again frame after frame -- something in it moves at every
  // frame, a loader turning -- a recording is only a cost: drawn straight
  // until it rests.
  std::uint64_t fRecordedAt = 0;
  int fRecordedInARow = 0;
  // Drawn moved by this much, it and all under it, where it is laid out: a
  // scroll view's contents by its offset. Repaints nothing by itself.
  // Moved by a shift, apart from its layout -- as a CSS transform -- and
  // repainted where it was drawn and where it is now: what moves a node is
  // tracked here, not left to whoever moved it to say.
  void setShift(float x, float y) {
    if (x == fShiftX && y == fShiftY) {
      return;
    }
    this->setShiftQuietly(x, y);
    this->markMovedOnly();
  }
  // The same, repainting nothing: a scroll's contents, shifted at every step
  // and copied by the host, the strip that came into view repainted alone.
  void setShiftQuietly(float x, float y) {
    fShiftX = x;
    fShiftY = y;
    if (work::Entry *here = work::entry(fId)) {
      here->shiftX = x;
      here->shiftY = y;
    }
  }
  // setShift's other name, kept for what says it so.
  void shiftTo(float x, float y) { this->setShift(x, y); }
  // Out of its parent's flow, placed by its anchor, or back in it: the
  // parent laid out again either way -- written to fOutOfFlow alone, the
  // parent kept its layout.
  void setOutOfFlow(bool out) {
    if (out == fOutOfFlow) {
      return;
    }
    fOutOfFlow = out;
    this->invalidateLayout();
  }
  void relayoutQuietly() {
    fLayoutValid = false;
    work::mark(fId);
  }
  // Repaints where this node is and where it was drawn last.
  void markDamaged() {
    fDamaged = true;
    fRedrawn = true;
    work::mark(fId);
  }
  // Raised over the rest of the window, or put back: a dropdown's plate,
  // hung below its button over the rows under it (work::raised).
  void setRaised(bool raised) {
    if (raised == fRaised) {
      return;
    }
    fRaised = raised;
    std::vector<NodeId> &ids = work::raised();
    if (raised) {
      ids.push_back(fId);
    } else {
      std::erase(ids, fId);
    }
    work::raisedStale() = true;
    this->markDamaged();
  }
  [[nodiscard]] bool raised() const noexcept { return fRaised; }
  void setFloats(bool floats) { fFloats = floats; }
  // Repainted where it was and is, what it draws the same: moved by its
  // shift, or faded.
  void markMovedOnly() {
    fDamaged = true;
    work::mark(fId);
  }

  // Placement by a container, during its layout: writes the position a flow
  // decided without counting as a change somebody made.
  void arrange(float x, float y, Anchor at = anchor::kTopLeft,
               Anchor origin = anchor::kTopLeft) {
    if (fX == x && fY == y && fAnchor == at && fOrigin == origin) {
      return;
    }
    fX = x;
    fY = y;
    fAnchor = at;
    fOrigin = origin;
    // Only where it goes changed: its subtree is moved as it is at the next
    // layout, not laid out again. A list whose rows all move down when
    // something comes above them measured every row again.
    fPlacementDirty = true;
    work::mark(fId);
  }
  void arrangeAxisSize(bool horizontal, float size) {
    float &axis = horizontal ? fWidth : fHeight;
    if (axis == size) {
      return;
    }
    axis = size;
    fLayoutValid = false;
  }

  // What the walks keep. Public so that the walks, which are free functions,
  // can reach it; nothing else has a reason to.
  struct Common {
    Anchor fAnchor = anchor::kTopLeft;
    Anchor fOrigin = anchor::kTopLeft;
    float fX = 0.0f, fY = 0.0f;
    float fWidth = 0.0f, fHeight = 0.0f;
    Axes fRelativeSize;
    Axes fAutoSize;
    Axes fGrow;
    float fMinWidth = 0.0f, fMaxWidth = 0.0f;
    float fMinHeight = 0.0f, fMaxHeight = 0.0f;
    std::optional<Align> fAlignSelf{};
    float fDepth = 0.0f;
    Margin fMargin{}, fPadding{};
    float fCornerRadius = 0.0f;
    bool fMasking = false;
    float fScale = 1.0f, fAlpha = 1.0f;
    bool fVisible = true;
    bool operator==(const Common &) const = default;
  };

  // One property a style sheet can declare: where it is in a Style, in the
  // snapshot of a node's common values, and live on the node; whether it is
  // part of the layout; and how a sheet sets it -- at once, or animated as
  // a property.
  struct at_once {};
  template <class Animated> struct animated {
    Animated property;
  };
  template <auto InStyle, auto InCommon, auto Live, bool Layout, class How>
  struct style_field {
    static constexpr auto style = InStyle;
    static constexpr auto common = InCommon;
    static constexpr auto live = Live;
    static constexpr bool layout = Layout;
    using how = How;
  };
  // The table of them: the one place they are named.
  static constexpr auto styleFields() {
    return std::tuple{
        style_field<&Style::anchor, &Common::fAnchor, &State::fAnchor, true, at_once>{},
        style_field<&Style::origin, &Common::fOrigin, &State::fOrigin, true, at_once>{},
        style_field<&Style::x, &Common::fX, &State::fX, true, animated<property::x>>{},
        style_field<&Style::y, &Common::fY, &State::fY, true, animated<property::y>>{},
        style_field<&Style::width, &Common::fWidth, &State::fWidth, true, animated<property::width>>{},
        style_field<&Style::height, &Common::fHeight, &State::fHeight, true, animated<property::height>>{},
        style_field<&Style::relativeSize, &Common::fRelativeSize, &State::fRelativeSizeAxes, true, at_once>{},
        style_field<&Style::autoSize, &Common::fAutoSize, &State::fAutoSizeAxes, true, at_once>{},
        style_field<&Style::grow, &Common::fGrow, &State::fGrowAxes, true, at_once>{},
        style_field<&Style::minWidth, &Common::fMinWidth, &State::fMinWidth, true, at_once>{},
        style_field<&Style::maxWidth, &Common::fMaxWidth, &State::fMaxWidth, true, at_once>{},
        style_field<&Style::minHeight, &Common::fMinHeight, &State::fMinHeight, true, at_once>{},
        style_field<&Style::maxHeight, &Common::fMaxHeight, &State::fMaxHeight, true, at_once>{},
        style_field<&Style::alignSelf, &Common::fAlignSelf, &State::fAlignSelf, true, at_once>{},
        style_field<&Style::depth, &Common::fDepth, &State::fDepth, false, at_once>{},
        style_field<&Style::margin, &Common::fMargin, &State::fMargin, true, at_once>{},
        style_field<&Style::padding, &Common::fPadding, &State::fPadding, true, at_once>{},
        style_field<&Style::cornerRadius, &Common::fCornerRadius, &State::fCornerRadius, false, at_once>{},
        style_field<&Style::masking, &Common::fMasking, &State::fMasking, false, at_once>{},
        style_field<&Style::scale, &Common::fScale, &State::fScale, true, animated<property::scale>>{},
        style_field<&Style::alpha, &Common::fAlpha, &State::fAlpha, false, animated<property::alpha>>{},
        style_field<&Style::visible, &Common::fVisible, &State::fVisible, true, at_once>{},
    };
  }
  template <class F> static void eachStyleField(F &&f) {
    std::apply([&](auto... field) { (f(field), ...); }, styleFields());
  }

  [[nodiscard]] Common commonValues() const {
    Common out;
    eachStyleField([&]<class Field>(Field) { out.*Field::common = this->*Field::live; });
    return out;
  }
  [[nodiscard]] static bool sameLayout(const Common &a, const Common &b) noexcept {
    bool same = true;
    eachStyleField([&]<class Field>(Field) {
      if (Field::layout)
        same = same && a.*Field::common == b.*Field::common;
    });
    return same;
  }

  void restyle(bool animate) {
    fStyleDirty = true;
    fStyleAnimate = fStyleAnimate || animate;
    work::mark(fId);
  }

  // A declaration set on the node: at once, or animated from where it was.
  template <class Field>
  void setStyled(at_once, const Style &, const Common &target, const Common &, double, const Easing &, bool) {
    this->*Field::live = target.*Field::common;
  }
  template <class Field, class Animated>
  void setStyled(animated<Animated> how, const Style &, const Common &target, const Common &current, double duration,
                 const Easing &easing, bool animate) {
    const float previous = fResolvedStyle.*Field::style ? fStyledTarget.*Field::common : current.*Field::common;
    this->setStyledProperty(how.property, target.*Field::common, previous, duration, easing, animate);
  }

  // Applies the declarations a sheet resolved to this node's common
  // properties. A sheet owns only what it declares; a declaration that stops
  // matching restores the value from before it applied.
  void applyCommonStyle(const Style &style, bool active, bool animate) {
    if (!active && !fStyleApplied) {
      return;
    }
    if (!fStyleApplied) {
      fStyleBase = this->commonValues();
      fStyledTarget = fStyleBase;
    }
    const Common current = this->commonValues();
    // What is not declared any more follows the node as it is now.
    eachStyleField([&]<class Field>(Field) {
      if (!(fResolvedStyle.*Field::style))
        fStyleBase.*Field::common = current.*Field::common;
    });
    // What is declared goes where it says; what stopped being, back.
    Common target = current;
    eachStyleField([&]<class Field>(Field) {
      if (style.*Field::style)
        target.*Field::common = *(style.*Field::style);
      else if (fResolvedStyle.*Field::style)
        target.*Field::common = fStyleBase.*Field::common;
    });
    const bool changed = target != current;
    const bool layoutChanged = !sameLayout(target, current);
    const double duration = style.transitionMs.value_or(0.0);
    const Easing how = style.transitionEasing.value_or(easing::out_quint{});
    eachStyleField([&]<class Field>(Field) {
      if (style.*Field::style || fResolvedStyle.*Field::style)
        this->template setStyled<Field>(typename Field::how{}, style, target, current, duration, how, animate);
    });
    fStyledTarget = target;
    if (layoutChanged) {
      this->invalidateLayout();
    } else if (changed) {
      this->markDamaged();
    }
  }

  void transformTo(Property property, float from, float to,
                   double durationMs, Easing e) {
    // A new transform on a property replaces whatever was animating it.
    std::erase_if(fTransforms, [&property](const Transform &t) {
      return t.fProperty.index() == property.index();
    });
    if (durationMs <= 0.0) {
      this->applyProperty(property, to);
      return;
    }
    fTransforms.push_back({property, from, to, fPendingStartMs + fDelayMs,
                           fPendingStartMs + fDelayMs + durationMs, e});
    // Marked: begun between frames, it was in neither the ticking set nor
    // what was marked, the frame said nothing moved, and the host slept --
    // the animation began only when something else woke the window.
    work::mark(fId);
  }

  void updateTransforms(double nowMs) {
    fPendingStartMs = nowMs;
    if (fTransforms.empty()) {
      return;
    }
    for (auto &t : fTransforms) {
      // Queued before the first frame: start now rather than treating it as
      // long finished.
      if (t.fStartMs <= 0.0) {
        const double duration = t.fEndMs - t.fStartMs;
        t.fStartMs = nowMs;
        t.fEndMs = nowMs + duration;
      }
      const double span = t.fEndMs - t.fStartMs;
      const float progress =
          span > 0.0 ? static_cast<float>((nowMs - t.fStartMs) / span) : 1.0f;
      this->applyProperty(t.fProperty, t.fFrom + (t.fTo - t.fFrom) *
                                                     ease(t.fEasing, progress));
    }
    std::erase_if(fTransforms,
                  [nowMs](const Transform &t) { return nowMs >= t.fEndMs; });
  }

  // -- kept by the walks
  NodeId fId;
  std::vector<Transform> fTransforms;
  double fPendingStartMs = 0.0;
  double fDelayMs = 0.0;
  bool fLayoutValid = false;
  // Somewhere below has to be laid out again: found by the walk from the
  // root each frame, since nothing below can tell its ancestors.
  bool fSubtreeDirty = true;
  // Placed somewhere else since its last layout, and nothing else changed.
  bool fPlacementDirty = false;
  // What of this node's children is in view, where something scrolls it:
  // the walks a frame makes visit only the children that reach into it.
  // Layout still sees them all.
  std::optional<skia::SkRect> fInView;
  // A scrolled list's: the width it was last laid out at, and whether some
  // of its rows -- out of view while that width moved, a column dragged
  // wider -- were only moved, not laid out at it: laid out once it holds.
  float fFlowWidth = -1.0f;
  bool fChildrenWaiting = false;
  // Decided once a pass -- a list sized by its rows is laid out twice in
  // one, its width the same at the second: whether its width moved.
  std::uint64_t fFlowPass = 0;
  bool fFlowMoved = false;
  // The children, as they were last time: a variant that switched, a row
  // added to a vector. Compared each frame; a different set is laid out and
  // repainted.
  std::size_t fChildSignature = 0;
  // Where each child was drawn at the last frame, by id: for when it is
  // gone, only its place is repainted.
  struct DrawnChild {
    NodeId fId;
    skia::SkRect fArea;
  };
  std::vector<DrawnChild> fDrawnChildren;
  skia::SkRect fLastConstraint = skia::SkRect::MakeEmpty();
  bool fDamaged = true;
  // Whether what it draws changed -- not only where it is drawn or how
  // faintly: a recording is kept through a fade or a slide.
  bool fRedrawn = true;
  // Not yet seen by a frame's walk: the walk marks it, and where it is.
  bool fNew = true;
  work::Alive fAlive;
  bool fRelaid = false;  // damaged by a layout made again: for the trace
  skia::SkRect fMovedDamage = skia::SkRect::MakeEmpty();
  // Where it was when the frame's layout began, and so what its layout moved
  // over: once for the pass, however many times a flow lays it out in it --
  // measured at the flow's top, then placed -- not each time.
  std::uint64_t fPassSeen = 0;
  skia::SkRect fBoundsAtPass = skia::SkRect::MakeEmpty();
  skia::SkRect fLayoutMoved = skia::SkRect::MakeEmpty();
  skia::SkRect fDrawnBounds = skia::SkRect::MakeEmpty();
  // How far it and what it holds are drawn, in the space it is laid out in:
  // its bounds and each child's area -- the child's bounds, where it was
  // drawn and its own reach, moved by its shift. A child drawn past its
  // parent's edge -- shifted, or placed by its anchor outside it -- is in it:
  // a node is skipped as it is drawn only where its reach is not repainted,
  // as a browser goes by a box's overflow. Its bounds alone skipped a bar of
  // spaces placed under the field beside its head whenever only that was
  // repainted. Worked out by the damage walk on its way back up.
  skia::SkRect fReach = skia::SkRect::MakeEmpty();
  bool fHovered = false;
  // Whether something under it is hovered: gone into again as the pointer
  // moves, even where it has left this -- a submenu out of its row.
  bool fHoverWithin = false;
  float fHoverX = 0.0f, fHoverY = 0.0f;
  bool fFocused = false;
  bool fDeferredClick = false;
  std::vector<StyleRole> fStyleRoles;
  bool fSelected = false;
  bool fDisabled = false;
  StyleResolver fStyleResolver{};
  bool fStyleSheetChanged = false;
  bool fStyleDirty = true;
  bool fStyleAnimate = false;
  bool fStyleApplied = false;
  Style fResolvedStyle;
  Common fStyleBase;
  Common fStyledTarget;

private:
  [[nodiscard]] static NodeId nextId() {
    static std::atomic<NodeId> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
  }

  void setStyledProperty(const Property &property, float target,
                         float previousTarget, double durationMs,
                         const Easing &how, bool animate) {
    if (target == previousTarget) {
      return;
    }
    if (animate && durationMs > 0.0) {
      this->transformTo(property, this->propertyRef(property), target,
                        durationMs, how);
    } else {
      this->transformTo(property, target, target, 0.0, how);
    }
  }

  [[nodiscard]] float &propertyRef(const Property &property) {
    return spl::visit(
        spl::overloaded{[this](property::alpha) -> float & { return fAlpha; },
                   [this](property::x) -> float & { return fX; },
                   [this](property::y) -> float & { return fY; },
                   [this](property::width) -> float & { return fWidth; },
                   [this](property::height) -> float & { return fHeight; },
                   [this](property::scale) -> float & { return fScale; }},
        property);
  }

  void applyProperty(const Property &property, float value) {
    float &current = this->propertyRef(property);
    if (current == value) {
      return;
    }
    current = value;
    // Alpha only repaints; the rest move or resize.
    spl::visit(spl::overloaded{[this](property::alpha) { this->markDamaged(); },
                          [this](auto) { this->invalidateLayout(); }},
               property);
  }
};


// ---- children --------------------------------------------------------------


// The State of a child: a node's own, or the one inside an AnyNode.
template <class N> [[nodiscard]] State &stateOf(N &child) {
  return child.fState;
}
// One the walks see through, or see as its base (a Held, a view named over
// one), held as itself in a part -- a variant of pieces, a Box's typed parts:
// the state of what it holds, as its layout is; its own was never set, and a
// flow placing it by that measured it as nothing.
template <class N>
  requires(kSeenThrough<N> || kSeenAsBase<N>)
[[nodiscard]] State &stateOf(N &child);
[[nodiscard]] State &stateOf(AnyNodeRef child);

// ---- default handling --------------------------------------------------------
//
// What a node does with an event it has no handler for. Overload sets: the
// general one does nothing, and the ones for particular phases and events
// act. A node's own handler calls these for what it does not handle itself.

template <class T, class Phase, class Input>
void defaultPointer(T &, const Phase &, const Input &, PointerReply &) {}
// A press is a click -- unless a gesture-owning ancestor deferred it, when
// the click waits for the release.
// Only the main button clicks: a right press is a menu's, never a click.
// A click the node does not take goes on to the nodes above it -- at once,
// or, deferred, on the release -- as a click at a point does: a list that
// scrolls deferred every click in it, and one its rows did not take never
// reached the list itself.
template <class T>
void defaultPointer(T &node, const phase::target &, const pointer::down &press,
                    PointerReply &reply) {
  if (press.button > 1) {
    return;
  }
  if (reply.fDeferClick) {
    node.fState.fDeferredClick = true;
    reply.handle();
  } else if (node.onClick(press.x, press.y)) {
    reply.handle();
  } else {
    reply.fClickAbove = skia::SkPoint::Make(press.x, press.y);
  }
}
template <class T>
void defaultPointer(T &node, const phase::target &, const pointer::up &release,
                    PointerReply &reply) {
  if (std::exchange(node.fState.fDeferredClick, false)) {
    if (node.fState.fBounds.contains(release.x, release.y) && !node.onClick(release.x, release.y)) {
      reply.fClickAbove = skia::SkPoint::Make(release.x, release.y);
    }
    // The release belongs to where the deferred gesture began, even when it
    // ended outside.
    reply.handle();
  }
}
// A node whose onClick returns what the click asks for -- perhaps: taken
// where it does -- answered with it.
template <class T>
concept ClickAnswers = !std::same_as<decltype(std::declval<T &>().onClick(0.0f, 0.0f)), bool>;
template <class T>
  requires ClickAnswers<T>
auto defaultPointer(T &node, const phase::target &, const pointer::down &press, PointerReply &reply)
    -> decltype(node.onClick(press.x, press.y)) {
  if (press.button > 1) {
    return {};
  }
  if (reply.fDeferClick) {
    node.fState.fDeferredClick = true;
    reply.handle();
    return {};
  }
  auto answer = node.onClick(press.x, press.y);
  if (answer) {
    reply.handle();
  } else {
    reply.fClickAbove = skia::SkPoint::Make(press.x, press.y);
  }
  return answer;
}
template <class T>
  requires ClickAnswers<T>
auto defaultPointer(T &node, const phase::target &, const pointer::up &release, PointerReply &reply)
    -> decltype(node.onClick(release.x, release.y)) {
  if (!std::exchange(node.fState.fDeferredClick, false)) {
    return {};
  }
  reply.handle();
  if (!node.fState.fBounds.contains(release.x, release.y)) {
    return {};
  }
  auto answer = node.onClick(release.x, release.y);
  if (!answer) {
    reply.fClickAbove = skia::SkPoint::Make(release.x, release.y);
  }
  return answer;
}
template <class T>
void defaultPointer(T &node, const phase::target &, const pointer::cancel &,
                    PointerReply &) {
  node.fState.fDeferredClick = false;
}
template <class T>
void defaultPointer(T &node, const phase::target &,
                    const pointer::scroll &wheel, PointerReply &reply) {
  if (node.onScroll(wheel.dy)) {
    reply.handle();
  }
}

template <class T, class Phase, class Input>
void defaultKey(T &, const Phase &, const Input &, Reply &) {}
// Enter and Space activate.
template <class T>
void defaultKey(T &node, const phase::target &, const key::down &press,
                Reply &reply) {
  const skia::SkRect &bounds = node.fState.fBounds;
  if ((press.key == keys::kEnter || press.key == keys::kSpace) &&
      node.onClick(bounds.centerX(), bounds.centerY())) {
    reply.handle();
  }
}

template <class T>
  requires ClickAnswers<T>
auto defaultKey(T &node, const phase::target &, const key::down &press, Reply &reply)
    -> decltype(node.onClick(0.0f, 0.0f)) {
  const skia::SkRect &bounds = node.fState.fBounds;
  if (press.key != keys::kEnter && press.key != keys::kSpace) {
    return {};
  }
  auto answer = node.onClick(bounds.centerX(), bounds.centerY());
  if (answer) {
    reply.handle();
  }
  return answer;
}
template <class T, class Phase, class Action>
void defaultSemantic(T &, const Phase &, const Action &, Reply &) {}
template <class T>
void defaultSemantic(T &node, const phase::target &,
                     const semantic_action::focus &, Reply &reply) {
  if (node.focusable()) {
    reply.requestFocus();
    reply.handle();
  }
}
template <class T>
  requires ClickAnswers<T>
auto defaultSemantic(T &node, const phase::target &, const semantic_action::activate &, Reply &reply)
    -> decltype(node.onClick(0.0f, 0.0f)) {
  const skia::SkRect &bounds = node.fState.fBounds;
  auto answer = node.onClick(bounds.centerX(), bounds.centerY());
  if (answer) {
    reply.handle();
  }
  return answer;
}
template <class T>
void defaultSemantic(T &node, const phase::target &,
                     const semantic_action::activate &, Reply &reply) {
  const skia::SkRect &bounds = node.fState.fBounds;
  if (node.onClick(bounds.centerX(), bounds.centerY())) {
    reply.handle();
  }
}

template <class T> void layoutChildrenInContentBox(T &node);
// What a program paints with, handed down the tree as it is drawn: the
// program's paint (ProgramPaint) derives from it, and the host hands its own
// to the scene's draw. Empty: drawing passes it on, and paintBox takes the
// program's back from it.
struct Painting {};
template <class T> void drawDefault(T &node, Painting &painting, skia::SkCanvas *canvas, float alpha);

// ---- the node ----------------------------------------------------------------

// What every node derives from: its State, and the default of every hook.
// Nothing here is virtual -- the hooks take `this` by deduction, so a call on
// a node's own type reaches its own hook when it has one, and this default
// when it does not. The walks are templates over the real type and always
// call on it.
//
// A node overrides a hook by declaring one, publicly. The event hooks are
// overload sets, one overload per phase and event a node handles; a node
// that declares some writes `using Node::onPointer;` (or onKey, onText,
// onSemantic) to keep the defaults for the rest.
struct Node : NodeBase {
  State fState;

  // -- structure: a leaf has no children; a node whose children are the
  // members of one aggregate, `parts`, has them in the order they are
  // declared there -- walked one by one (skiff.aggregate), not listed by hand. (A
  // node cannot be walked itself: its State is a member of this base.)
  void forEachChild(this auto &, auto &&) {}
  template <class Self, class F>
    requires requires(Self &self) { self.parts; }
  void forEachChild(this Self &self, F &&f) {
    aggregate::each(self.parts, f);
  }

  // -- layout
  // Sets fWidth/fHeight from content before layout uses them.
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  void measure(const skia::SkRect &) {}
#else
  void measure(this auto &, const skia::SkRect &) {}
#endif
  void layoutChildren(this auto &self) { layoutChildrenInContentBox(self); }

  // -- drawing
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  void drawSelf(skia::SkCanvas *, float) {}
#else
  void drawSelf(this auto &, skia::SkCanvas *, float) {}
#endif
  // The whole subtree, overridden by a node that draws it another way.
  void draw(this auto &self, Painting &painting, skia::SkCanvas *canvas, float alpha) {
    drawDefault(self, painting, canvas, alpha);
  }

  // -- time
  void update(this auto &, double) {}
  // Part-way to somewhere by hand, so the next frame differs.
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] bool settling() const { return false; }
#else
  [[nodiscard]] bool settling(this const auto &) { return false; }
#endif

  // -- input
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] bool acceptsInput() const { return false; }
#else
  [[nodiscard]] bool acceptsInput(this const auto &) { return false; }
#endif
  [[nodiscard]] bool focusable(this const auto &self) {
    return self.acceptsInput();
  }
  // Whether a press on it moves the keyboard's focus to it: a text field's
  // does, a button's, a row's or a selectable text's does not -- they are
  // reached by Tab, and a click on them leaves the focus where it is (Qt's
  // ClickFocus and NoFocus, GTK's focus-on-click). A program where the
  // input keeps the focus through clicks elsewhere needs no work for it.
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] bool takesFocusOnPress() const { return false; }
#else
  [[nodiscard]] bool takesFocusOnPress(this const auto &) { return false; }
#endif
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] bool hoverChangesAppearance() const { return false; }
#else
  [[nodiscard]] bool hoverChangesAppearance(this const auto &) {
    return false;
  }
#endif
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] bool focusChangesAppearance() const { return false; }
#else
  [[nodiscard]] bool focusChangesAppearance(this const auto &) {
    return false;
  }
#endif
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  void onFocusChanged(bool) {}
#else
  void onFocusChanged(this auto &, bool) {}
#endif
  // A node that says what a press does by onPress() takes the press: the
  // host delivers it once the dispatch is over (skiff::bind::press).
  [[nodiscard]] bool onClick(this auto &self, float, float)
    requires requires { self.onPress(); }
  {
    pressLater(self.fState);
    return true;
  }
  [[nodiscard]] bool onClick(this auto &, float, float) { return false; }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] bool onScroll(float) { return false; }
#else
  [[nodiscard]] bool onScroll(this auto &, float) { return false; }
#endif
  // The defaults: what a click asks for, where onClick returns it, returned
  // with them, as a handler's own answer is.
  auto onPointer(this auto &self, const auto &at, const auto &input,
                 PointerReply &reply) {
    return defaultPointer(self, at, input, reply);
  }
  auto onKey(this auto &self, const auto &at, const auto &input,
             Reply &reply) {
    return defaultKey(self, at, input, reply);
  }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  void onText(const auto &, const auto &, Reply &) {}
#else
  void onText(this auto &, const auto &, const auto &, Reply &) {}
#endif
  auto onSemantic(this auto &self, const auto &at, const auto &action,
                  Reply &reply) {
    return defaultSemantic(self, at, action, reply);
  }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  [[nodiscard]] Semantics semantics() const { return {}; }
#else
  [[nodiscard]] Semantics semantics(this const auto &) { return {}; }
#endif
  // A box -- or drawn, where the node paints itself through the painter (a
  // widget, an icon, a picture) -- unless it says it is something else (see
  // Look): in every build.
  template <class Self> [[nodiscard]] Look look(this const Self &) {
    if constexpr (requires(Self &node, skiff::paint::Painter &p) { node.drawSelf(p, 1.0f); })
      return look::drawn{};
    else
      return look::box{};
  }

  // -- styling: the node's own declarations, colour and fonts
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  void applyNodeStyle(const Style &, bool) {}
#else
  void applyNodeStyle(this auto &, const Style &, bool) {}
#endif

  // -- the common state, reached from the node
  [[nodiscard]] NodeId id() const noexcept { return fState.id(); }
  [[nodiscard]] const skia::SkRect &bounds() const noexcept {
    return fState.bounds();
  }
  [[nodiscard]] skia::SkRect shownBounds() const noexcept { return fState.shownBounds(); }
  [[nodiscard]] bool visible() const noexcept { return fState.visible(); }
  [[nodiscard]] bool hovered() const noexcept { return fState.hovered(); }
  [[nodiscard]] bool focused() const noexcept { return fState.focused(); }
  // Focused, and to be drawn so: focus the keyboard gave.
  [[nodiscard]] bool showsFocus() const noexcept {
    return fState.focused() && focusVisible();
  }
  [[nodiscard]] bool selected() const noexcept { return fState.selected(); }
  [[nodiscard]] bool disabled() const noexcept { return fState.disabled(); }
  void apply(const Spec &spec) { fState.apply(spec); }
  void setPosition(float x, float y) { fState.setPosition(x, y); }
  void setSize(float width, float height) { fState.setSize(width, height); }
  void setPadding(Margin padding) { fState.setPadding(padding); }
  void setMargin(Margin margin) { fState.setMargin(margin); }
  void setScale(float scale) { fState.setScale(scale); }
  void setAlpha(float alpha) { fState.setAlpha(alpha); }
  void setVisible(bool visible) { fState.setVisible(visible); }
  void setFollow(const Node *follow) {
    fState.setFollow(follow != nullptr ? &follow->fState : nullptr);
  }
  void setMasking(bool masking) { fState.setMasking(masking); }
  void setCornerRadius(float radius) { fState.setCornerRadius(radius); }
  void setSelected(bool selected) { fState.setSelected(selected); }
  void setDisabled(bool disabled) { fState.setDisabled(disabled); }
  template <class Role> void addStyleRole() {
    fState.template addStyleRole<Role>();
  }
  template <class Role> void removeStyleRole() {
    fState.template removeStyleRole<Role>();
  }
  template <class Theme> void setStyleSheet() {
    fState.template setStyleSheet<Theme>();
  }
  void fadeTo(float target, double ms, Easing e = easing::out_quint{}) {
    fState.fadeTo(target, ms, e);
  }
  void moveToX(float target, double ms, Easing e = easing::out_quint{}) {
    fState.moveToX(target, ms, e);
  }
  void moveToY(float target, double ms, Easing e = easing::out_quint{}) {
    fState.moveToY(target, ms, e);
  }
  void resizeWidthTo(float target, double ms,
                     Easing e = easing::out_quint{}) {
    fState.resizeWidthTo(target, ms, e);
  }
  void resizeHeightTo(float target, double ms,
                      Easing e = easing::out_quint{}) {
    fState.resizeHeightTo(target, ms, e);
  }
  void scaleTo(float target, double ms, Easing e = easing::out_quint{}) {
    fState.scaleTo(target, ms, e);
  }
  void delay(double ms) { fState.delay(ms); }
  void invalidateLayout() { fState.invalidateLayout(); }
  void markDamaged() { fState.markDamaged(); }
  void setRaised(bool raised) { fState.setRaised(raised); }
};

// A node with no picture of its own that lays its children out in its box.
template <class... Children> struct Group : Node {
  explicit Group(Children... children) : fChildren(std::move(children)...) {}
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }
  std::tuple<Children...> fChildren;
};

// ---- style subjects ----------------------------------------------------------

template <class T>
[[nodiscard]] StyleSubject styleSubject(T &node, float viewportWidth) {
  const State &state = node.fState;
  const StyleStates now = StyleStates{}
                              .with<state::hover>(state.fHovered)
                              .with<state::focus>(state.fFocused)
                              .with<state::selected>(state.fSelected)
                              .with<state::disabled>(state.fDisabled);
  return {&detail::styleNodeKey<T>, detail::TemplateKeyOf<T>::value,
          state.styleRoles(), now, viewportWidth};
}

// ---- the walks ---------------------------------------------------------------
//
// Each is a function template over the child's type, with an overload for
// AnyNode that goes through its table, recursing through eachChild.

struct UpdateContext {
  double fNowMs = 0.0;
  float fViewportWidth = 0.0f;
  bool fAnimating = false;
  // Whether this is a frame's step, at the time now: only then do nodes and
  // their transforms advance. A restyle between frames walks the tree with
  // the last frame's time, which after a while of nothing to draw is long
  // gone -- an animation begun there would start in the past and end at
  // once.
  bool fTick = true;
};


// What routing a pointer event found out, for the scene to act on.
struct Routed {
  NodeId fCaptureRequest = 0;
  NodeId fFocusRequest = 0;
  bool fReleaseRequest = false;
  bool fTargetDelivered = false;
  bool fTargetFocusable = false;
};

// What a scene needs to know about a node it holds by id.
struct NodeInfo {
  bool fFocusable = false;
  bool fDisabled = false;
  bool fVisible = true;
  bool fTakesText = false;
  Cursor fCursor = cursor::arrow{};
  // Where it was last drawn, in the window.
  skia::SkRect fDrawn = skia::SkRect::MakeEmpty();
};

// The children a frame's walks visit: all of them -- or, below a node told
// what of it is in view (a scrolled list's content), those that reach into
// it, and those not laid out yet. What a frame costs is then what is on the
// screen, not what the list holds.
template <class T, class F> void eachChildInView(T &node, F &&f) {
  const std::optional<skia::SkRect> &view = stateOf(node).fInView;
  if (!view) {
    eachChild(node, f);
    return;
  }
  eachChild(node, [&](auto &child) {
    const State &one = stateOf(child);
    // By where it and what it holds are drawn -- its reach, moved by its
    // shift -- not its bounds alone: a row whose part stands out of it was
    // passed over where only that part was in view.
    if (one.fBounds.isEmpty() ||
        skia::SkRect::Intersects(joined(one.fBounds, one.fReach).makeOffset(one.fShiftX, one.fShiftY), *view)) {
      f(child);
    }
  });
}

// The children in the order they are drawn: eachChild's order, unless one of
// them has a depth. `f` gets the child and its position in eachChild order.
template <class T, class F> void eachChildInDrawOrder(T &node, F &&f) {
  bool sorted = true;
  std::uint32_t count = 0;
  // A raised one, or one on the way to it, after all the others.
  const bool raising = !work::raised().empty();
  const auto keyOf = [raising](const State &state) {
    return raising && work::raisedAbove().contains(state.fId) ? std::numeric_limits<float>::infinity()
                                                              : state.fDepth;
  };
  eachChild(node, [&](auto &child) {
    sorted = sorted && keyOf(stateOf(child)) == 0.0f;
    ++count;
  });
  if (sorted) {
    std::uint32_t at = 0;
    eachChild(node, [&](auto &child) { f(child, at++); });
    return;
  }
  std::vector<std::pair<float, std::uint32_t>> order;
  order.reserve(count);
  std::uint32_t at = 0;
  eachChild(node, [&](auto &child) {
    order.emplace_back(keyOf(stateOf(child)), at++);
  });
  std::ranges::stable_sort(order, {}, &std::pair<float, std::uint32_t>::first);
  for (const auto &[depth, index] : order) {
    std::uint32_t seen = 0;
    eachChild(node, [&](auto &child) {
      if (seen++ == index) {
        f(child, index);
      }
    });
  }
}

// The union of the visible children's boxes.
template <class T> [[nodiscard]] skia::SkRect childBounds(T &node) {
  skia::SkRect content = skia::SkRect::MakeEmpty();
  eachChild(node, [&](auto &child) {
    const State &state = stateOf(child);
    if (state.fVisible) {
      content = joined(content, state.fBounds);
    }
  });
  return content;
}
// What a node sized by its content is sized by, along an axis: its children
// in its flow that have a size of their own along it. One placed by its
// anchor instead -- a badge at a corner, a time at the end of a line -- is
// placed in the box, not what makes it; one sized as a share of the node
// along the axis (fillX, say) takes its size from the node, and cannot give
// it one. Counted, either was laid out in the widest box first and made the
// node as wide: a quote filling its bubble made every bubble with one the
// widest there is.
template <class Axis, class T> [[nodiscard]] skia::SkRect flowBounds(T &node) {
  std::optional<skia::SkRect> content;
  eachChild(node, [&](auto &child) {
    const State &state = stateOf(child);
    // With its margin: the room a flow gives a child is its box and its
    // margin. Its box alone, a column that began with a margined title or
    // ended with a margined slider came out short by as much, and what
    // followed it was put over its end.
    if (state.fVisible && !state.fOutOfFlow && !state.fRelativeSizeAxes.template has<Axis>()) {
      const Margin &m = state.fMargin;
      const auto box = skia::SkRect::MakeLTRB(state.fBounds.fLeft - m.fLeft, state.fBounds.fTop - m.fTop,
                                              state.fBounds.fRight + m.fRight, state.fBounds.fBottom + m.fBottom);
      // An empty child counted where it is, as a point: it takes its place
      // in the flow, and a gap after it. Skipped as empty, a column whose
      // label had no text yet measured from the field below it, and came
      // out short by the gap between them -- the field past its end.
      content = content ? skia::SkRect::MakeLTRB(std::min(content->fLeft, box.fLeft), std::min(content->fTop, box.fTop),
                                                 std::max(content->fRight, box.fRight), std::max(content->fBottom, box.fBottom))
                        : box;
    }
  });
  return content.value_or(skia::SkRect::MakeEmpty());
}

template <class N> void layout(N &child, const skia::SkRect &parentBox);
void layout(AnyNodeRef &child, const skia::SkRect &parentBox);
template <class N> void draw(N &child, Painting &painting, skia::SkCanvas *canvas, float alpha);
void draw(AnyNodeRef &child, Painting &painting, skia::SkCanvas *canvas, float alpha);

// Lays every child out in the content box: what a node without its own
// layoutChildren does.
template <class T> void layoutChildrenInContentBox(T &node) {
  const skia::SkRect box = node.fState.contentBox();
  eachChild(node, [&](auto &child) { layout(child, box); });
}

namespace detail {
// Where a node was as its pass began, noted at the pass's first look at it;
// what the pass moved it over, from there to where it is now.
inline void notePass(State &state) {
  if (state.fPassSeen != work::layoutPass()) {
    state.fPassSeen = work::layoutPass();
    state.fBoundsAtPass = state.fBounds;
  }
}
inline void noteMoved(State &state) {
  state.fLayoutMoved = state.fBounds != state.fBoundsAtPass ? joined(state.fBoundsAtPass, state.fBounds)
                                                            : skia::SkRect::MakeEmpty();
}

template <class T> void layoutNode(T &node, const skia::SkRect &parentBox) {
  State &state = node.fState;
  const ViewLaidOut inView{state.fViewName};
  ++walkCounts().layout;
  notePass(state);
  // Placed against another node, when asked: a dropdown list belongs to the
  // control that opened it.
  const skia::SkRect parent =
      (state.fFollow != nullptr && !state.fFollow->fBounds.isEmpty())
          ? state.fFollow->fBounds.makeOffset(work::drawnOffset(state.fFollow->fId).x,
                                              work::drawnOffset(state.fFollow->fId).y)
          : parentBox;
  if (state.fLayoutValid && !state.fSubtreeDirty &&
      parent == state.fLastConstraint) {
    if (state.fPlacementDirty) {
      work::reach(state.fId);
      // Moved, not changed: the subtree goes where it is placed as it is.
      state.fPlacementDirty = false;
      const skia::SkRect moved =
          anchoredBox(inset(parent, state.fMargin), state.fBounds.width(), state.fBounds.height(), state.fAnchor,
                      state.fOrigin, state.fX, state.fY);
      const float dx = moved.fLeft - state.fBounds.fLeft;
      const float dy = moved.fTop - state.fBounds.fTop;
      if (dx != 0.0f || dy != 0.0f) {
        state.fBounds = moved;
        noteMoved(state);
        eachChild(node, [&](auto &child) { shiftSubtree(child, dx, dy); });
      }
    }
    return;
  }
  state.fPlacementDirty = false;
  state.fLastConstraint = parent;
  work::reach(state.fId);
  ++walkCounts().laidOut;
  if (!state.fSizingConflict.empty()) {
    std::println(std::cerr, "[scene] {}: {}{}{}", std::exchange(state.fSizingConflict, {}), typeOf(node).name,
                 viewsLaidOut().empty() ? "" : " in ", viewsLaidOut().empty() ? std::string_view() : std::string_view(viewsLaidOut().back()));
  }
  // Where it was: taken before an auto-sized node is given a provisional box
  // to lay its children out in, which is not where it was drawn. Compared
  // with that box, every relayout of one -- a list's flow of rows -- was a
  // move over the whole view, and repainted it all.
  const skia::SkRect previous = state.fBounds;
  // A node that knows its own size says so first, given its box.
  node.measure(parent);

  // A margin holds a node off whichever edge it is anchored to.
  const skia::SkRect room = inset(parent, state.fMargin);
  const float parentW = room.width();
  const float parentH = room.height();
  const bool relativeX = state.fRelativeSizeAxes.has<axis::x>();
  const bool relativeY = state.fRelativeSizeAxes.has<axis::y>();
  const bool autoX = state.fAutoSizeAxes.has<axis::x>();
  const bool autoY = state.fAutoSizeAxes.has<axis::y>();
  float width = relativeX ? parentW * state.fWidth : state.fWidth;
  float height = relativeY ? parentH * state.fHeight : state.fHeight;

  // Auto-sized axes need the children laid out first, in a provisional box.
  if (autoX || autoY) {
    // Within its largest size, where it has one: what wraps in it wraps
    // there, rather than at the parent's width it is then cut back from.
    float provisionalW = state.fMaxWidth > 0.0f ? std::min(parentW, state.fMaxWidth) : parentW;
    if (state.fShrunkTo > 0.0f) {
      provisionalW = std::min(provisionalW, state.fShrunkTo);
    }
    const float provisionalH = state.fMaxHeight > 0.0f ? std::min(parentH, state.fMaxHeight) : parentH;
    state.fBounds = skia::SkRect::MakeXYWH(room.fLeft, room.fTop,
                                           autoX ? provisionalW : width,
                                           autoY ? provisionalH : height);
    ++work::provisionalDepth();
    node.layoutChildren();
    --work::provisionalDepth();
    if (autoX) {
      width = flowBounds<axis::x>(node).width() + state.fPadding.totalX();
    }
    if (autoY) {
      height = flowBounds<axis::y>(node).height() + state.fPadding.totalY();
    }
  }

  if (state.fShrunkTo > 0.0f) {
    width = std::min(width, state.fShrunkTo);
  }
  // At least its least width -- but, sized by what it holds, never past the
  // room its parent has: a message's bubble widened to hold its time beside
  // its last line, in a chat narrower than that, stood out of its row by as
  // much, over the edge and past what was repainted of it.
  width = std::max(width, autoX && parentW > 0.0f ? std::min(state.fMinWidth, parentW) : state.fMinWidth);
  height = std::max(height, state.fMinHeight);
  if (state.fMaxWidth > 0.0f) {
    width = std::min(width, state.fMaxWidth);
  }
  if (state.fMaxHeight > 0.0f) {
    height = std::min(height, state.fMaxHeight);
  }
  width *= state.fScale;
  height *= state.fScale;

  state.fBounds = anchoredBox(room, width, height, state.fAnchor,
                              state.fOrigin, state.fX, state.fY);
  // Moved or resized, over the pass: repainted where it was and where it is.
  (void)previous;
  noteMoved(state);
  node.layoutChildren();
  // Auto-sized in height, and its children laid out again in the box it
  // ended in: one that wraps -- a block of code's text, filling a width the
  // provisional box did not give it -- may have come out taller there than
  // in the provisional box, and the node kept the provisional height: what
  // it holds was drawn past its bottom, over what follows. Sized again to
  // what they came to, and placed in that.
  if (autoY) {
    float settled = flowBounds<axis::y>(node).height() + state.fPadding.totalY();
    settled = std::max(settled, state.fMinHeight);
    if (state.fMaxHeight > 0.0f) {
      settled = std::min(settled, state.fMaxHeight);
    }
    settled *= state.fScale;
    if (std::abs(settled - state.fBounds.height()) > 0.5f) {
      state.fBounds = anchoredBox(room, state.fBounds.width(), settled, state.fAnchor, state.fOrigin, state.fX, state.fY);
      noteMoved(state);
      node.layoutChildren();
    }
  }
  state.fLayoutValid = true;
  state.fSubtreeDirty = false;
}

// A node's declared box: its background -- the selected one where it is
// selected, the focused one where it has the focus, the hovered one where it
// A node's box rounded as it says: each corner its own where it gives them,
// else all by its one radius -- drawn in by `inset`, as a border is.
[[nodiscard]] inline skia::SkRRect roundedBox(const State &state, const skia::SkRect &box, float inset = 0.0f) {
  const skia::SkRect in = box.makeInset(inset, inset);
  if (!state.fCorners) {
    const float radius = std::max(0.0f, state.fCornerRadius - inset);
    return skia::SkRRect::MakeRectXY(in, radius, radius);
  }
  const auto r = [&](float each) { return std::max(0.0f, each - inset); };
  const Corners &c = *state.fCorners;
  const skia::SkPoint radii[4] = {{r(c.topLeft), r(c.topLeft)},
                                   {r(c.topRight), r(c.topRight)},
                                   {r(c.bottomRight), r(c.bottomRight)},
                                   {r(c.bottomLeft), r(c.bottomLeft)}};
  skia::SkRRect out;
  out.setRectRadii(in, radii);
  return out;
}

// Where a box's tail is: beside its bottom corner on its side, outside it.
[[nodiscard]] inline skia::SkRect tailBox(const State &state) {
  if (!state.fTail)
    return skia::SkRect::MakeEmpty();
  const Tail &tail = *state.fTail;
  const skia::SkRect &box = state.fBounds;
  const float left = spl::visit(spl::overloaded{[&](tail_side::left) { return box.fLeft - tail.width; },
                                                      [&](tail_side::right) { return box.fRight; }},
                                   tail.side);
  return skia::SkRect::MakeLTRB(left, box.fBottom - tail.height, left + tail.width, box.fBottom);
}
// A box and its tail as one shape: the rounded box, and the tail from a
// pixel inside its edge -- one path, filled once where the two overlap.
[[nodiscard]] inline skia::SkPath boxWithTail(const State &state, const skia::SkRect &box) {
  skia::SkPathBuilder built;
  built.addRRect(roundedBox(state, box));
  if (state.fTail) {
    const Tail &tail = *state.fTail;
    const float out = spl::visit(spl::overloaded{[](tail_side::left) { return -1.0f; }, [](tail_side::right) { return 1.0f; }},
                                    tail.side);
    const float edge = out > 0.0f ? box.fRight : box.fLeft;
    const float bottom = box.fBottom;
    const float w = tail.width, h = tail.height;
    // Match the rounded box's clockwise winding on both sides. A mirrored
    // contour cancels the fill where a left tail overlaps the box.
    if (out < 0.0f) {
      built.moveTo(edge - out, bottom);
      built.lineTo(edge + out * w, bottom);
      built.cubicTo(edge + out * w * 0.4f, bottom - h / 12.0f, edge, bottom - h * 5.0f / 12.0f, edge, bottom - h);
      built.lineTo(edge - out, bottom - h);
    } else {
      built.moveTo(edge - out, bottom - h);
      built.lineTo(edge, bottom - h);
      built.cubicTo(edge, bottom - h * 5.0f / 12.0f, edge + out * w * 0.4f, bottom - h / 12.0f, edge + out * w, bottom);
      built.lineTo(edge - out, bottom);
    }
    built.close();
  }
  return built.detach();
}

// What a program paints of every box's fill, besides the fill itself: a
// type -- a callback by its type, never a pointer -- whose object the host
// hands to the scene's draw, and drawing hands down (Painting). skiff's own
// does nothing; a program says its own by specializing ProgramPaint<> once,
// where every unit that draws its tree sees it.
//   under(state, fill, painter, alpha): painted under the fill, the fill to
//     paint returned -- the same, another (at an opacity, say), or none;
//   over(state, painter, alpha): painted over the fill, under the border;
//   scope: made from a node's State and held while its subtree is drawn --
//     what the program knows of the boxes above (inside a panel, say).
// Hooks that take a Skia canvas where the painter goes -- a program's, from
// before the painter drew everything -- are given the painter's canvas, as
// they were (paintUnder, paintOver).
struct PlainPaint : Painting {
  template <class P>
  std::optional<skia::SkColor> under(const State &, std::optional<skia::SkColor> fill, const P &, float) {
    return fill;
  }
  template <class P> void over(const State &, const P &, float) {}
  struct scope {
    scope(PlainPaint &, const State &) {}
  };
};
// The program's paint around a fill, with what draws it: the painter, where
// its hooks take one; else its canvas.
template <class Hooks, class P>
std::optional<skia::SkColor> paintUnder(Hooks &hooks, const State &state, std::optional<skia::SkColor> fill, const P &p,
                                        float alpha) {
  if constexpr (requires { hooks.under(state, fill, p, alpha); })
    return hooks.under(state, fill, p, alpha);
  else
    return hooks.under(state, fill, p.canvas(), alpha);
}
template <class Hooks, class P> void paintOver(Hooks &hooks, const State &state, const P &p, float alpha) {
  if constexpr (requires { hooks.over(state, p, alpha); })
    hooks.over(state, p, alpha);
  else
    hooks.over(state, p.canvas(), alpha);
}
// The font Skia's painter is made with as the scene draws: the program's
// default, or one with no face where it set none -- measuring nothing.
inline skia::SkFont &fontToDrawWith() {
  static skia::SkFont none;
  skia::SkFont *font = skiff::paint::defaultFont();
  return font != nullptr ? *font : none;
}
template <class = void> struct ProgramPaint {
  using type = PlainPaint;
};
// The program's, where a node of type T is drawn: dependent on T, so that
// what is found is the program's specialization, not this primary.
template <class T> using PaintOf = typename ProgramPaint<std::conditional_t<sizeof(T) != 0, void, T>>::type;
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
template <class Hooks> struct SharedDrawNode;
template <class T> struct DrawPaint { using type = PaintOf<T>; };
template <class Hooks> struct DrawPaint<SharedDrawNode<Hooks>> { using type = Hooks; };
#endif

// What blurs what is drawn under it as it is drawn (a backdrop filter): each
// such node's rect on the device, as it was last drawn, by its id. Whatever
// of a frame is repainted under one, all of it is repainted -- else it
// would blur its own last pixels -- and while any is shown, a frame is not
// played back in bands: the host reads this. Those gone, let go of; and the
// frame each was last drawn in, for the host to let go of one no longer
// shown: all of its rect repainted, and it not drawn there -- hidden, not
// gone (a dialog's sheet, shut), it was repainted as a piece of its own
// over what was there now, and blurred what was under it in that piece
// alone: another dialog's frost cut to its rect, by another degree.
// And whether it was last drawn from an image it keeps -- moving, the window
// under it taken once -- reading nothing under it: a frame may be played back
// in bands over it.
// And how far what is under it reaches into what it shows, on the device:
// a change under it changes what it shows that far round it, no further --
// the host repaints that much of it, not all of it.
struct LiveBackdrop {
  skia::SkRect rect = skia::SkRect::MakeEmpty();
  std::uint64_t frame = 0;
  bool kept = false;
  float reach = 0.0f;
};
// Set where one was drawn into a recording -- played back in bands -- that
// had to read what is under it: drawn from what it keeps, or cut at the
// bands' edges. The host repaints all of the window at the next frame, not
// in bands, and clears it.
inline bool &liveBackdropsStale() {
  static bool stale = false;
  return stale;
}
inline std::map<NodeId, LiveBackdrop> &liveBackdrops() {
  static std::map<NodeId, LiveBackdrop> kept;
  return kept;
}

[[nodiscard]] inline skia::SkColor atOpacity(skia::SkColor colour, float opacity) {
  const auto alpha = static_cast<skia::SkColor>(std::lround(static_cast<float>((colour >> 24) & 0xFFu) * opacity));
  return (colour & 0x00FFFFFFu) | (std::min<skia::SkColor>(alpha, 255u) << 24);
}

// is hovered -- and its border, in its corner radius; what the program
// paints with a fill (Hooks: ProgramPaint's) under it and over it.
template <class Hooks, class P> void paintBox(Hooks &hooks, const State &state, const P &p, float alpha) {
  std::optional<skia::SkColor> fill = state.fSelected && state.fSelectedBackground ? state.fSelectedBackground
                                            : state.fFocused && state.fFocusBackground     ? state.fFocusBackground
                                            // A disabled node does not light up under the pointer.
                                            : state.fHovered && !state.fDisabled && state.fHoverBackground
                                                ? state.fHoverBackground
                                                                                            : state.fBackground;
  fill = paintUnder(hooks, state, fill, p, alpha);
  if (state.fShadow)
    p.fill(roundedBox(state, state.fBounds.makeOffset(0.0f, state.fShadow->offsetY)), state.fShadow->colour, alpha);
  if (state.fGradient && !fill) {
    p.save();
    p.clip(roundedBox(state, state.fBounds));
    p.verticalGradient(state.fBounds, state.fGradient->top, state.fGradient->bottom, alpha);
    p.restore();
  }
  if (fill) {
    if (state.fTail)
      p.fill(boxWithTail(state, state.fBounds), *fill, alpha);
    else
      p.fill(roundedBox(state, state.fBounds), *fill, alpha);
  }
  paintOver(hooks, state, p, alpha);
  if (state.fBorder && state.fBorder->width > 0.0f)
    p.stroke(roundedBox(state, state.fBounds, state.fBorder->width * 0.5f), state.fBorder->width, state.fBorder->colour, alpha);
}

// A node's own drawing: with the program's paint where it asks for it (a
// Box's fill), else as it is.
template <class T, class Hooks> void drawSelfOf(T &node, Hooks &, skia::SkCanvas *canvas, float alpha) {
  node.drawSelf(canvas, alpha);
}
// A node that draws through the painter -- drawSelf(Painter &, alpha), or
// with the program's paint, drawSelf(hooks, Painter &, alpha) -- is given
// one over the canvas: what it draws, a painter that is not Skia's can draw
// too. One taking the canvas itself is given it, as it was.
template <class T, class Hooks>
concept DrawsWithHooksThroughPainter = requires(T &node, Hooks &hooks, skiff::paint::Painter &p) { node.drawSelf(hooks, p, 1.0f); };
template <class T, class Hooks>
  requires(requires(T &node, Hooks &hooks, skia::SkCanvas *canvas) { node.drawSelf(hooks, canvas, 1.0f); } &&
           !DrawsWithHooksThroughPainter<T, Hooks>)
void drawSelfOf(T &node, Hooks &hooks, skia::SkCanvas *canvas, float alpha) {
  node.drawSelf(hooks, canvas, alpha);
}
template <class T, class Hooks>
  requires DrawsWithHooksThroughPainter<T, Hooks>
void drawSelfOf(T &node, Hooks &hooks, skia::SkCanvas *canvas, float alpha) {
  skiff::paint::Painter p(canvas, fontToDrawWith());
  node.drawSelf(hooks, p, alpha);
}
template <class T, class Hooks>
  requires requires(T &node, skiff::paint::Painter &p) { node.drawSelf(p, 1.0f); }
void drawSelfOf(T &node, Hooks &, skia::SkCanvas *canvas, float alpha) {
  skiff::paint::Painter p(canvas, fontToDrawWith());
  node.drawSelf(p, alpha);
}

template <class T>
void drawNode(T &node, Painting &painting, skia::SkCanvas *canvas, float inheritedAlpha) {
  State &state = node.fState;
  if (!state.fVisible || state.fAlpha <= 0.001f) {
    return;
  }
  // What lies outside what is being repainted is skipped with its subtree.
  // Where it is drawn: its bounds moved by its shift -- what is skipped,
  // clipped and repainted goes by that, not by where layout put it.
  ++visitedCount();
  if (!state.fBounds.isEmpty() &&
      canvas->quickReject(joined(state.fBounds, state.fReach).makeOffset(state.fShiftX, state.fShiftY))) {
    return;
  }
  ++drawnCount();
  // What the program keeps while its subtree is drawn.
  // The program's paint, as the host handed it to the scene.
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  using Hooks = typename DrawPaint<T>::type;
  auto &hooks = static_cast<Hooks &>(painting);
  [[maybe_unused]] const typename Hooks::scope programScope{hooks, state};
#else
  auto &hooks = static_cast<PaintOf<T> &>(painting);
  [[maybe_unused]] const typename PaintOf<T>::scope programScope{hooks, state};
#endif
  const float alpha = inheritedAlpha * state.fAlpha;
  // The canvas kept and given back only where this moves or cuts it: most
  // nodes do neither, and a save and a restore for each was most of what
  // walking a drawn list cost.
  const bool shifted = state.fShiftX != 0.0f || state.fShiftY != 0.0f;
  const int saved = shifted || state.fMasking ? canvas->save() : -1;
  // Moved first, then cut to its shape: the cut goes with it (a round
  // avatar swiped aside stays round, not cut where it stood).
  if (shifted)
    canvas->translate(state.fShiftX, state.fShiftY);
  if (state.fMasking) {
    if (state.fCornerRadius > 0.0f || state.fCorners) {
      canvas->clipRRect(roundedBox(state, state.fBounds), true);
    } else {
      canvas->clipRect(state.fBounds, true);
    }
  }
  // Recordings off, where SKIFF_NO_RECORDING is set: to tell a stale one.
  static const bool noRecording = std::getenv("SKIFF_NO_RECORDING") != nullptr;
  if (state.fRecorded && !noRecording) {
    // Recorded again where it was let go -- something in it damaged -- or
    // drawn at another alpha; else played back, not walked.
    // Recorded whole, and faded as a whole as it is played back: a fade
    // does not record it again -- nor does a slide, the shift being put on
    // the canvas before it. Its parts drawn at their own alpha within.
    const std::uint64_t now = work::frameNumber();
    if (!state.fPicture) {
      state.fRecordedInARow = state.fRecordedAt + 1 >= now ? state.fRecordedInARow + 1 : 0;
      state.fRecordedAt = now;
    }
    if (!state.fPicture && state.fRecordedInARow >= 3) {
      paintBox(hooks, state, skiff::paint::Painter(canvas, fontToDrawWith()), alpha);
      drawSelfOf(node, hooks, canvas, alpha);
      eachChildInDrawOrder(node, [&](auto &child, std::uint32_t) { draw(child, painting, canvas, alpha); });
    } else if (!state.fPicture) {
      skia::SkPictureRecorder recorder;
      // Its reach, not its bounds alone, as the picture's cull: a part drawn
      // past it -- placed or shifted out of it -- was culled with the picture
      // wherever only that part was repainted.
      skia::SkCanvas *into = recorder.beginRecording(joined(state.fBounds, state.fReach).makeOutset(64.0f, 64.0f));
      paintBox(hooks, state, skiff::paint::Painter(into, fontToDrawWith()), 1.0f);
      drawSelfOf(node, hooks, into, 1.0f);
      eachChildInDrawOrder(node, [&](auto &child, std::uint32_t) { draw(child, painting, into, 1.0f); });
      state.fPicture = recorder.finishRecordingAsPicture();
    }
    if (state.fPicture && state.fRecordedInARow < 3) {
      if (alpha < 0.999f) {
        skia::SkPaint faded;
        faded.setAlphaf(alpha);
        canvas->drawPicture(state.fPicture, nullptr, &faded);
      } else {
        canvas->drawPicture(state.fPicture);
      }
    }
  } else {
    paintBox(hooks, state, skiff::paint::Painter(canvas, fontToDrawWith()), alpha);
    drawSelfOf(node, hooks, canvas, alpha);
    eachChildInDrawOrder(node, [&](auto &child, std::uint32_t) {
      draw(child, painting, canvas, alpha);
    });
  }
  if (saved >= 0) {
    canvas->restoreToCount(saved);
  }
  state.fDrawnBounds = state.fBounds.makeOffset(state.fShiftX, state.fShiftY);
}
} // namespace detail

// A laid-out subtree moved by `dy` as it is, without laying it out again:
// its bounds and the box it was laid out in, so that laying it out in the
// moved box finds nothing changed. What a scrolled list does on a scroll.
// An erased node in it is laid out again instead.
inline void shiftSubtree(AnyNodeRef &node, float dx, float dy);
template <class N> void shiftSubtree(N &node, float dx, float dy) {
  State &state = stateOf(node);
  state.fBounds.offset(dx, dy);
  state.fLastConstraint.offset(dx, dy);
  // A recording made where it was: let go, drawn again where it is -- kept,
  // it played the subtree back where it had been (a list pushed down).
  state.fPicture = nullptr;
  eachChild(node, [&](auto &child) { shiftSubtree(child, dx, dy); });
}
template <class N> void shiftSubtree(N &node, float dy) { shiftSubtree(node, 0.0f, dy); }

// Lays a child out in a box: what a container's layoutChildren calls for each
// of its children, after placing it.
// Draws a node and its subtree the way the scene does; a node that draws its
// subtree another way calls this for what it does not do itself.
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
template <class T> void drawDefaultShared(T &node, Painting &painting, skia::SkCanvas *canvas, float alpha);
#endif
template <class T> void drawDefault(T &node, Painting &painting, skia::SkCanvas *canvas, float alpha) {
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  drawDefaultShared(node, painting, canvas, alpha);
#else
  detail::drawNode(node, painting, canvas, alpha);
#endif
}

// ---- the walks made once ----------------------------------------------------
//
// Outside a release build (kErasedWalks) the walks are not made for each type
// of node, but once: for ErasedNode -- any node seen through its table, its
// State, its children and its hooks. A type's table then holds only those,
// each a call on the node; every walk's code is the same for all types, and a
// program's thousands of node types no longer make thousands of copies of it.
class ErasedNode;
[[nodiscard]] inline StyleSubject styleSubject(ErasedNode &node, float viewportWidth);
template <class N> [[nodiscard]] NodeKind typeOf(N &);
template <class N>
  requires(kSeenThrough<N> || kSeenAsBase<N>)
[[nodiscard]] NodeKind typeOf(N &);
[[nodiscard]] inline NodeKind typeOf(ErasedNode &node);
template <class F> bool childAt(ErasedNode &node, std::uint32_t place, F &&f);

namespace walk {

[[nodiscard]] inline bool pollsEachFrame(const ErasedNode &node);
[[nodiscard]] inline double ownWakeAt(const ErasedNode &node);
inline void damageForHover(ErasedNode &node);
// A pointer event to a node, in a phase: its own handler for the event's
// kind -- or, for an ErasedNode, its table's.
template <class N, class When, class C = NoCarry>
void pointerTo(N &node, const When &when, const PointerEvent &input, PointerReply &reply, const C &carried = {}) {
  spl::visit([&](const auto &event) { handled(node, carried, [&] { return node.onPointer(when, event, reply); }); }, input);
}
template <class When>
void pointerTo(ErasedNode &node, const When &when, const PointerEvent &input, PointerReply &reply);
template <class When, class C>
void pointerTo(ErasedNode &node, const When &when, const PointerEvent &input, PointerReply &reply, const C &) {
  pointerTo(node, when, input, reply);
}

// Every walk, for an AnyNode: through its table.
void update(AnyNodeRef &, UpdateContext &, StyleResolver, const Style *, bool);
template <class N, class F> bool viaHints(N &node, F &&f);
[[nodiscard]] bool markDirty(AnyNodeRef &);
[[nodiscard]] skia::SkRect collectDamage(AnyNodeRef &, bool);
[[nodiscard]] bool hasDamage(AnyNodeRef &);
void hover(AnyNodeRef &, float, float, bool, StyleResolver, float, bool);
[[nodiscard]] bool hitPath(AnyNodeRef &, float, float, Path &);
[[nodiscard]] bool findPath(AnyNodeRef &, NodeId, Path &);
[[nodiscard]] NodeId idAt(AnyNodeRef &, const Path &, std::size_t);
void routePointer(AnyNodeRef &, const Path &, std::size_t, const PointerEvent &,
                  PointerReply &, Routed &, bool);
void routeKey(AnyNodeRef &, const Path &, std::size_t, const KeyEvent &, Reply &);
void routeText(AnyNodeRef &, const Path &, std::size_t, const TextEvent &, Reply &);
void routeSemantic(AnyNodeRef &, const Path &, std::size_t,
                   const SemanticAction &, Reply &);
[[nodiscard]] std::optional<NodeInfo> info(AnyNodeRef &, NodeId);
[[nodiscard]] bool focusChanged(AnyNodeRef &, NodeId, bool, StyleResolver, float);
void collectSemantics(AnyNodeRef &, std::vector<Semantics> &, int, NodeId);
void collectFocusable(AnyNodeRef &, std::vector<NodeId> &);
[[nodiscard]] bool animating(AnyNodeRef &);
[[nodiscard]] bool clickPath(AnyNodeRef &, const Path &, std::size_t, float,
                             float);
[[nodiscard]] double wakeAt(AnyNodeRef &);
void damageOver(AnyNodeRef &, NodeId, const skia::SkRect &, bool &, skia::SkRect &);
// The routes given what was carried down to an erased node: nothing is
// carried past one (a scene's root walked through its table, outside a
// release build), so the carried is let go there.
template <class C>
void routePointer(AnyNodeRef &node, const Path &path, std::size_t at, const PointerEvent &e, PointerReply &reply, Routed &routed,
                  bool targetOnly, const C &) {
  routePointer(node, path, at, e, reply, routed, targetOnly);
}
template <class C> void routeKey(AnyNodeRef &node, const Path &path, std::size_t at, const KeyEvent &e, Reply &reply, const C &) {
  routeKey(node, path, at, e, reply);
}
template <class C> void routeText(AnyNodeRef &node, const Path &path, std::size_t at, const TextEvent &e, Reply &reply, const C &) {
  routeText(node, path, at, e, reply);
}
template <class C>
void routeSemantic(AnyNodeRef &node, const Path &path, std::size_t at, const SemanticAction &e, Reply &reply, const C &) {
  routeSemantic(node, path, at, e, reply);
}
template <class C> [[nodiscard]] bool clickPath(AnyNodeRef &node, const Path &path, std::size_t at, float x, float y, const C &) {
  return clickPath(node, path, at, x, y);
}

// A node that does something of its own each frame says update() itself:
// Node's is a template taking itself, which no member pointer names.
template <class N>
concept polls = requires { static_cast<void (N::*)(double)>(&N::update); };
// A node that polls may say when it has something to do (wantsTick()): a
// scroll view gliding, a picture still coming, a field with the caret. Then
// it is ticked only while it says so -- what wakes it marks it, and the tick
// goes to what is marked -- not every frame for as long as it lives.
template <class N>
concept saysWhenItTicks = requires(const N &n) {
  { n.wantsTick() } -> std::convertible_to<bool>;
};
template <class N>
  requires saysWhenItTicks<N>
constexpr bool pollsEachFrame(const N &node) {
  return node.wantsTick();
}
template <class N>
  requires(polls<N> && !saysWhenItTicks<N>)
constexpr bool pollsEachFrame(const N &) {
  return true;
}
template <class N> constexpr bool pollsEachFrame(const N &) { return false; }

// Transforms, the node's own time, and styles.
template <class N>
void update(N &child, UpdateContext &context, StyleResolver resolver,
            const Style *inherited, bool restyleAll) {
  State &state = child.fState;
  ++(context.fTick ? walkCounts().tick : walkCounts().restyle);
  if (context.fTick) {
    state.updateTransforms(context.fNowMs);
    child.update(context.fNowMs);
    if (hostWork().pressedNow == &state)
      hostWork().due.push_back(std::exchange(hostWork().pressedNow, nullptr));
  }
  if (!state.fTransforms.empty() || child.settling()) {
    context.fAnimating = true;
    // Still moving: marked again for the next frame once this one is done
    // (work::moving), not only left to the ticking set to be found by.
    if (context.fTick) {
      work::moving().push_back(state.fId);
    }
  }
  const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                 : resolver;
  const bool restyle =
      restyleAll || state.fStyleDirty || state.fStyleSheetChanged;
  if (restyle) {
    const bool active = static_cast<bool>(own);
    Style resolved =
        active ? resolveShared(own, styleSubject(child, context.fViewportWidth))
               : Style{};
    if (inherited != nullptr) {
      if (!resolved.colour)
        resolved.colour = inherited->colour;
      if (!resolved.fontSize)
        resolved.fontSize = inherited->fontSize;
      if (!resolved.fontBold)
        resolved.fontBold = inherited->fontBold;
    }
    state.applyCommonStyle(resolved, active, state.fStyleAnimate);
    child.applyNodeStyle(resolved, active);
    state.fResolvedStyle = resolved;
    state.fStyleApplied = active;
    state.fStyleDirty = false;
    state.fStyleAnimate = false;
    state.fStyleSheetChanged = false;
  }
  const Style *passed = state.fStyleApplied ? &state.fResolvedStyle
                                            : inherited;
  // Out of view, a child's time and styles wait: all are walked when all
  // are restyled.
  // Whether anything below still asks for ticks, as the children visited say.
  bool ticksBelow = false;
  const auto visit = [&](auto &each) {
    State &one = stateOf(each);
    work::record(one.fId, state.fId);
    if (one.fNew) {
      one.fNew = false;
      if (work::Entry *here = work::entry(one.fId)) {
        here->unseen = false;
      }
      work::mark(one.fId);
    }
    walk::update(each, context, own, passed, restyle);
    ticksBelow = ticksBelow || work::ticking().contains(one.fId);
  };
  // A frame's tick sees every node in view: a child come or gone, its node
  // marked, for the layout and the damage walks to go to.
  // Only where its children can have changed: one was made (a full tick),
  // went, or moved.
  if (context.fTick && (work::tickingFull() || work::disabled() || work::reshaped().contains(state.fId))) {
    std::size_t signature = 0;
    std::uint32_t place = 0;
    eachChild(child, [&](auto &each) {
      const NodeId id = stateOf(each).fId;
      work::record(id, state.fId, place++);
      signature = signature * 1099511628211ull ^ static_cast<std::size_t>(id);
    });
    if (signature != state.fChildSignature) {
      work::reshaped().insert(state.fId);
      work::mark(state.fId);
    }
  }
  if (restyle) {
    eachChild(child, visit);
  } else if (context.fTick) {
    // Only where something ticks, changed, or is new -- everywhere, where the
    // tick has to find what was made. A new child wherever it is, in view or
    // not: once, to be found. Passed over out of view, it was never found,
    // and every frame went over the whole tree looking for it.
    const std::optional<skia::SkRect> &view = state.fInView;
    eachChild(child, [&](auto &each) {
      const State &one = stateOf(each);
      const bool seen = !view || one.fBounds.isEmpty() || skia::SkRect::Intersects(one.fBounds, *view);
      if (one.fNew || work::pending().contains(one.fId) ||
          (seen && (work::tickingFull() || work::ticking().contains(one.fId)))) {
        visit(each);
      }
    });
  } else {
    // Between frames -- a hover's restyle -- only what is marked: straight
    // to it where the way is known.
    const std::optional<skia::SkRect> &view = state.fInView;
    const bool hinted = walk::viaHints(child, [&](auto &each) {
      const skia::SkRect &at = stateOf(each).fBounds;
      if (!view || at.isEmpty() || skia::SkRect::Intersects(at, *view)) {
        visit(each);
      }
    });
    if (!hinted) {
      eachChildInView(child, [&](auto &each) {
        if (work::visit(stateOf(each).fId)) {
          visit(each);
        }
      });
    }
  }
  // Ticking on: what polls, animates or settles, and what is above such.
  if (context.fTick) {
    if (pollsEachFrame(child) || !state.fTransforms.empty() || child.settling() || ticksBelow) {
      work::ticking().insert(state.fId);
    } else {
      work::ticking().erase(state.fId);
    }
  }
}

// What has to be laid out again, found bottom-up: nothing below can tell its
// ancestors, so the frame asks. A node whose set of children changed is laid
// out again and repainted where they were.
// The marked children of a node, straight from the hints: each at its
// place, its id checked there. Whether that was done -- not where the walk
// goes everywhere, the node's children changed, or more than a few are
// marked; nor where one is not at its place any more, which reshapes the
// node. The walk then goes through all of them.
template <class N, class F> bool viaHints(N &node, F &&f) {
  const NodeId id = stateOf(node).fId;
  if (work::walkingFull() || work::disabled() || work::reshaped().contains(id)) {
    return false;
  }
  const NodeId *head = work::hintHeads().find(id);
  if (head == nullptr) {
    return true;  // nothing marked below it
  }
  const auto first = static_cast<std::uint32_t>(*head);
  std::size_t count = 0;
  for (std::uint32_t at = first; at != 0;) {
    const work::Hint hint = work::hints()[at - 1];
    at = hint.fNext;
    if (++count > work::kFewHints) {
      return false;
    }
    bool same = false;
    if (!childAt(node, hint.fPlace, [&](auto &each) { same = stateOf(each).fId == hint.fChild; }) || !same) {
      work::reshaped().insert(id);
      return false;
    }
  }
  for (std::uint32_t at = first; at != 0;) {
    // Copied: what `f` marks may grow the hints.
    const work::Hint hint = work::hints()[at - 1];
    at = hint.fNext;
    if (work::pending().contains(hint.fChild)) {
      childAt(node, hint.fPlace, f);
    }
  }
  return true;
}

template <class N> bool markDirty(N &child) {
  State &state = child.fState;
  ++walkCounts().dirty;
  bool below = false;
  const std::optional<skia::SkRect> &view = state.fInView;
  // Out of view, only whether it changed itself: what changed below it is
  // found when it comes into view. Not marked, the same: nothing below it
  // changed.
  const auto into = [&](auto &each) {
    const State &one = stateOf(each);
    // Out of view and not marked, only whether it changed itself. A marked
    // one is gone into wherever it is: the view a list says is what it
    // shows, not what changed.
    const bool marked = work::pending().contains(one.fId);
    if ((!marked && view && !one.fBounds.isEmpty() && !skia::SkRect::Intersects(one.fBounds, *view)) ||
        !work::visit(one.fId)) {
      below = below || !one.fLayoutValid || one.fPlacementDirty;
    } else {
      below = walk::markDirty(each) || below;
    }
  };
  if (walk::viaHints(child, into)) {
    state.fSubtreeDirty = below || !state.fLayoutValid;
    return state.fSubtreeDirty || state.fPlacementDirty;
  }
  std::size_t signature = 0;
  std::uint32_t place = 0;
  eachChild(child, [&](auto &each) {
    const State &one = stateOf(each);
    // Every child placed, in view or not: one out of view that marks itself
    // -- a loader turning at the far end of a list -- with no parent known
    // had every frame walk the whole tree. Only read where nothing changed.
    work::record(one.fId, state.fId, place++);
    into(each);
    signature = signature * 1099511628211ull ^ static_cast<std::size_t>(one.fId);
  });
  if (signature != state.fChildSignature) {
    state.fChildSignature = signature;
    if (traceSettling() && dirtiers().size() < 64) {
      dirtiers().push_back({typeOf(child), true, state.fBounds});
    }
    state.fLayoutValid = false;
    // Repainted where a child that went was drawn -- not the whole: one that
    // came is new and repaints itself, one that stayed and moved repaints
    // where it was and is, as the layout finds it.
    std::vector<NodeId> now;
    eachChild(child, [&](auto &each) { now.push_back(stateOf(each).fId); });
    std::ranges::sort(now);
    for (const auto &was : state.fDrawnChildren) {
      if (!std::ranges::binary_search(now, was.fId)) {
        state.fMovedDamage = joined(state.fMovedDamage, was.fArea);
      }
    }
  }
  if (!state.fLayoutValid && !below && traceSettling() && dirtiers().size() < 64) {
    dirtiers().push_back({typeOf(child), false, state.fBounds});
  }
  state.fSubtreeDirty = below || !state.fLayoutValid;
  // A pending move is the parent's to carry out: it lays out again, and this
  // is moved rather than laid out.
  return state.fSubtreeDirty || state.fPlacementDirty;
}

// What has to be repainted, and forgets it. A masking node clips what its
// subtree reports; a hidden one drops it, though its own change counts.
template <class N> skia::SkRect collectDamage(N &child, bool drawnAbove) {
  State &state = child.fState;
  ++walkCounts().damage;
  skia::SkRect damage = skia::SkRect::MakeEmpty();
  // Changed, moved or laid out itself, or children gone: where each child is
  // drawn, all found again.
  const bool whole = state.fDamaged || state.fRelaid || !state.fLayoutMoved.isEmpty() ||
                     !state.fMovedDamage.isEmpty();
  // What it draws changed -- not only moved or faded -- or it was laid out
  // elsewhere, or children went: its recording, if it keeps one, let go.
  const bool redrawn = state.fRedrawn || !state.fLayoutMoved.isEmpty() || !state.fMovedDamage.isEmpty();
  state.fRedrawn = false;
  // A scroll view moved in this frame: its view, where it is on the screen.
  const work::Offset above = work::damageOffset();
  const auto aboveClip = work::damageClip();
  for (ScrollMove &move : scrollMoves()) {
    if (move.node == state.fId) {
      move.rect.offset(above.x, above.y);
    }
  }
  if (drawnAbove) {
    // What is said in its own space -- its children's, a bar's -- is drawn
    // moved by its shift.
    damage = joined(state.fMovedDamage.makeOffset(state.fShiftX, state.fShiftY), state.fLayoutMoved);
    // Moved, laid out, or a child gone: said too, as from its node.
    if (!state.fDamaged && !damage.isEmpty() && traceSettling() && damagers().size() < 64) {
      damagers().push_back({typeOf(child), damage, true, true});
    }
    if (state.fDamaged) {
      // Where it was drawn and where it will be: a shift moves it past both
      // its laid-out bounds and its last drawn ones -- and all it reached as
      // it was last drawn, what of it stood out of its box (a badge over a
      // button's top): hidden, or drawn otherwise, that was left on the
      // screen, the button half gone.
      damage = joined(joined(joined(joined(damage, state.fBounds), state.fDrawnBounds),
                             state.fBounds.makeOffset(state.fShiftX, state.fShiftY)),
                      state.fReach.makeOffset(state.fShiftX, state.fShiftY));
      if (traceSettling() && damagers().size() < 64) {
        damagers().push_back({typeOf(child), damage, state.fRelaid});
      }
    }
  }
  if (!damage.isEmpty()) {
    auto piece = damage.makeOffset(above.x, above.y);
    if (!aboveClip || piece.intersect(*aboveClip)) {
      damageFound().push_back(piece);
    }
  }
  work::damageOffset() = {above.x + state.fShiftX, above.y + state.fShiftY};
  if (state.fMasking) {
    auto clip = state.fBounds.makeOffset(above.x + state.fShiftX, above.y + state.fShiftY);
    if (aboveClip && !clip.intersect(*aboveClip)) {
      clip.setEmpty();
    }
    work::damageClip() = clip;
  }
  state.fMovedDamage = skia::SkRect::MakeEmpty();
  state.fLayoutMoved = skia::SkRect::MakeEmpty();
  state.fDamaged = false;
  state.fRelaid = false;
  // A transform under way -- begun between frames, not ticked yet: carried
  // to the next frame as the tick carries what it finds moving. This walk
  // takes it out of what is marked, and the frame then said nothing moved.
  if (!state.fTransforms.empty()) {
    work::moving().push_back(state.fId);
  }
  const bool drawn = drawnAbove && state.fVisible && state.fAlpha > 0.001f;
  skia::SkRect below = skia::SkRect::MakeEmpty();
  const std::optional<skia::SkRect> &view = state.fInView;
  const auto inView = [&](const State &one) {
    return !view || one.fBounds.isEmpty() ||
           skia::SkRect::Intersects(joined(one.fBounds, one.fReach).makeOffset(one.fShiftX, one.fShiftY), *view) ||
           work::pending().contains(one.fId);
  };
  const auto into = [&](auto &each) {
    const State &one = stateOf(each);
    if (work::visit(one.fId)) {
      below = joined(below, walk::collectDamage(each, drawn));
      work::pending().erase(one.fId);
    }
  };
  // Straight to the marked children, where the rest are as they were: only
  // theirs of where each is drawn changes.
  const bool hinted = !whole && walk::viaHints(child, [&](auto &each) {
    const State &one = stateOf(each);
    if (!inView(one)) {
      return;
    }
    into(each);
    const skia::SkRect area =
        joined(joined(one.fBounds, one.fDrawnBounds), one.fReach.makeOffset(one.fShiftX, one.fShiftY));
    auto was = std::ranges::find(state.fDrawnChildren, one.fId, &State::DrawnChild::fId);
    if (was != state.fDrawnChildren.end()) {
      was->fArea = area;
    } else {
      state.fDrawnChildren.push_back({one.fId, area});
    }
  });
  if (!hinted) {
    state.fDrawnChildren.clear();
    std::uint32_t place = 0;
    eachChild(child, [&](auto &each) {
      const State &one = stateOf(each);
      work::record(one.fId, state.fId, place++);
      if (!inView(one)) {
        return;
      }
      into(each);
      // Where it is, laid out and drawn: what is repainted if it goes.
      state.fDrawnChildren.push_back(
          {one.fId, joined(joined(one.fBounds, one.fDrawnBounds), one.fReach.makeOffset(one.fShiftX, one.fShiftY))});
    });
  }
  work::damageOffset() = above;
  work::damageClip() = aboveClip;
  // Its reach: its bounds and every child's area, as they are now.
  // One that clips what it holds reaches no further than itself.
  state.fReach = state.fBounds;
  // A tail is drawn out of the box: repainted with it, not cut at its edge.
  if (state.fTail)
    state.fReach = joined(state.fReach, detail::tailBox(state));
  if (!state.fMasking) {
    for (const State::DrawnChild &one : state.fDrawnChildren) {
      state.fReach = joined(state.fReach, one.fArea);
    }
  }
  if (redrawn || !below.isEmpty()) {
    state.fPicture = nullptr;
  }
  // Its children's, in the space they are laid out in: moved by its shift.
  below.offset(state.fShiftX, state.fShiftY);
  if (!below.isEmpty() && state.fMasking &&
      !below.intersect(state.fBounds.makeOffset(state.fShiftX, state.fShiftY))) {
    below = skia::SkRect::MakeEmpty();
  }
  return joined(damage, below);
}

template <class N> bool hasDamage(N &child) {
  const State &state = child.fState;
  if (state.fDamaged || !state.fMovedDamage.isEmpty() || !state.fLayoutMoved.isEmpty()) {
    return true;
  }
  bool any = false;
  eachChildInView(child, [&](auto &each) { any = any || walk::hasDamage(each); });
  return any;
}

// What a hover change repaints: a node may say a part of itself -- a
// scroll view, only its bar -- and otherwise it is all of it.
template <class N>
  requires requires(const N &n) {
    { n.hoverDamage() } -> std::convertible_to<skia::SkRect>;
  }
void damageForHover(N &child) {
  child.fState.fMovedDamage = joined(child.fState.fMovedDamage, child.hoverDamage());
  work::mark(child.fState.fId);
}
template <class N> void damageForHover(N &child) { child.fState.markDamaged(); }

// The front-most node under a point that takes input, defined below: hover
// asks it what covers what.
template <class N> bool hitPath(N &child, float x, float y, Path &path);

// Every node remembers where the pointer is: a control with parts has to
// know which of its parts is under it.
template <class N>
void hover(N &child, float x, float y, bool visibleAbove,
           StyleResolver resolver, float viewportWidth, bool hoveredAbove) {
  State &state = child.fState;
  ++walkCounts().hover;
  // Shown only while what holds it is hovered: shown or hidden here, as the
  // pointer comes and goes over its holder.
  if (state.fRevealOnHover && state.fVisible != (hoveredAbove && visibleAbove)) {
    state.setVisible(hoveredAbove && visibleAbove);
    state.markDamaged();
  }
  // The point where this is laid out: moved back by its shift, as all under
  // it are drawn moved by it.
  x -= state.fShiftX;
  y -= state.fShiftY;
  state.fHoverX = x;
  state.fHoverY = y;
  const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                 : resolver;
  // Disabled: neither it nor anything in it lights up under the pointer.
  const bool visible = visibleAbove && state.fVisible && !state.fDisabled;
  const bool hovered = visible && state.fBounds.contains(x, y);
  if (hovered != state.fHovered) {
    state.fHovered = hovered;
    if (own && own.fUsesState(styleSubject(child, viewportWidth),
                              states::kHover)) {
      state.restyle(true);
    }
    if (state.fHoverBackground && !state.fDisabled) {
      state.markDamaged();
    } else if (child.hoverChangesAppearance() && !state.fDisabled) {
      damageForHover(child);
    }
  }
  const bool childrenVisible =
      visible && (!state.fMasking || state.fBounds.contains(x, y));
  // Under something in front: not hovered. Where the point is in more than
  // one child -- a panel slid over the chats, a dialog over the window --
  // the front-most that takes input there covers those drawn before it, as
  // it does for a press. Hover was only where things are: the chats behind
  // an account's settings lit up under the pointer through the panel.
  std::vector<NodeId> covered;
  if (childrenVisible) {
    std::vector<std::pair<NodeId, std::uint32_t>> under;
    eachChildInDrawOrder(child, [&](auto &each, std::uint32_t index) {
      const State &one = stateOf(each);
      if (one.fVisible && !one.fBounds.isEmpty() &&
          one.fBounds.makeOffset(one.fShiftX, one.fShiftY).contains(x, y)) {
        under.emplace_back(one.fId, index);
      }
    });
    if (under.size() > 1) {
      std::optional<std::uint32_t> front;
      eachChildInDrawOrder(child, [&](auto &each, std::uint32_t index) {
        if (std::ranges::contains(under, index, &std::pair<NodeId, std::uint32_t>::second)) {
          Path ignored;
          if (walk::hitPath(each, x, y, ignored)) {
            front = index;
          }
        }
      });
      if (front) {
        for (const auto &[id, index] : under) {
          if (index < *front) {
            covered.push_back(id);
          }
        }
      }
    }
  }
  // Only where hover can change: a child the point is in, one that was
  // hovered, and one placed by its anchor, which can stick out of this --
  // not every node on the screen at every move of the mouse.
  bool within = false;
  eachChildInView(child, [&](auto &each) {
    const State &one = stateOf(each);
    const bool shown = childrenVisible && !std::ranges::contains(covered, one.fId);
    // Hidden, or under something hidden: only to let go of a hover it had.
    // A hidden list's rows, never laid out, have empty bounds, and were all
    // gone into at every move of the pointer.
    if (work::disabled() || one.fHovered || one.fHoverWithin || one.fRevealOnHover ||
        (shown && one.fVisible &&
         (one.fOutOfFlow || one.fBounds.isEmpty() ||
          joined(one.fBounds, one.fReach).makeOffset(one.fShiftX, one.fShiftY).contains(x, y)))) {
      walk::hover(each, x, y, shown, own, viewportWidth, hoveredAbove || state.fHovered);
    }
    within = within || one.fHovered || one.fHoverWithin;
  });
  state.fHoverWithin = within;
}

// The front-most node under a point that takes input: what is drawn last is
// hit first.
template <class N> bool hitPath(N &child, float x, float y, Path &path) {
  State &state = child.fState;
  if (!state.fVisible || state.fAlpha <= 0.001f || state.fDisabled) {
    return false;
  }
  // Where this and all under it are laid out: the point moved back by its
  // shift.
  x -= state.fShiftX;
  y -= state.fShiftY;
  if (state.fMasking && !state.fBounds.contains(x, y)) {
    return false;
  }
  bool found = false;
  Path best;
  // In a list told what of it is in view, a row is gone into only where the
  // point is in it: every message was searched at every move.
  const bool listed = state.fInView.has_value();
  eachChildInDrawOrder(child, [&](auto &each, std::uint32_t index) {
    if (listed) {
      const State &one = stateOf(each);
      // Its reach, as drawn: a part of a row standing out of it is pressed.
      if (!one.fOutOfFlow && !one.fBounds.isEmpty() &&
          !joined(one.fBounds, one.fReach).makeOffset(one.fShiftX, one.fShiftY).contains(x, y)) {
        return;
      }
    }
    Path below;
    if (walk::hitPath(each, x, y, below)) {
      found = true;
      best.clear();
      best.push_back(index);
      best.insert(best.end(), below.begin(), below.end());
    }
  });
  if (found) {
    path.insert(path.end(), best.begin(), best.end());
    return true;
  }
  return child.acceptsInput() && state.fBounds.contains(x, y);
}

template <class N> bool findPath(N &child, NodeId id, Path &path) {
  if (child.fState.fId == id) {
    return true;
  }
  // Straight down, where the walks have seen the way: the child toward it,
  // at its place.
  if (const NodeId next = work::childToward(child.fState.fId, id); next != 0) {
    if (const NodeId *place = work::places().find(next)) {
      const std::size_t mark = path.size();
      path.push_back(static_cast<std::uint32_t>(*place));
      bool reached = false;
      childAt(child, static_cast<std::uint32_t>(*place), [&](auto &each) {
        reached = stateOf(each).fId == next && walk::findPath(each, id, path);
      });
      if (reached) {
        return true;
      }
      path.resize(mark);
    }
  }
  bool found = false;
  std::uint32_t at = 0;
  eachChild(child, [&](auto &each) {
    if (found) {
      return;
    }
    const std::size_t mark = path.size();
    path.push_back(at);
    if (walk::findPath(each, id, path)) {
      found = true;
    } else {
      path.resize(mark);
    }
    ++at;
  });
  return found;
}

// The pointer's shape along a path: the deepest node on it with a shape of
// its own. One walk down the path, not a search of the tree for each node
// on it.
template <class N> std::optional<Cursor> cursorOnPath(N &child, const Path &path, std::size_t at) {
  std::optional<Cursor> below;
  if (at < path.size()) {
    childAt(child, path[at], [&](auto &each) { below = walk::cursorOnPath(each, path, at + 1); });
  }
  if (below) {
    return below;
  }
  const Cursor &own = stateOf(child).fCursor;
  if (!isArrow(own)) {
    return own;
  }
  return std::nullopt;
}

template <class N> NodeId idAt(N &child, const Path &path, std::size_t at) {
  if (at == path.size()) {
    return child.fState.fId;
  }
  NodeId found = 0;
  childAt(child, path[at],
          [&](auto &each) { found = walk::idAt(each, path, at + 1); });
  return found;
}

// Capture on the way down, the target at the end of the path, bubble on the
// way back up. `targetOnly` delivers to the end of the path alone.
// A press the node just answered by saying so (pressLater), answered here,
// with what was carried down to it, where that can: then not kept.
// An answer a handler on the way just kept: with the path to its node.
inline void keepAnswer(const Path &path, std::size_t at) {
  if constexpr (kErasedWalks) {
    auto &work = hostWork();
    if (work.answerNow) {
      work.answers.push_back({Path(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(at)), std::move(work.answerNow),
                              std::exchange(work.answerType, nullptr)});
      work.answerNow.reset();
    }
  }
}
template <class N, class C> void answerHere(N &child, const C &carried) {
  if (hostWork().pressedNow == &child.fState && answerPress(child, carried)) {
    hostWork().pressedNow = nullptr;
  }
}
template <class N, class C>
void routePointer(N &child, const Path &path, std::size_t at,
                  const PointerEvent &given, PointerReply &reply, Routed &routed,
                  bool targetOnly, const C &carried);
// Down to a child, with what is carried extended at it; an erased one is
// seen through its table, and nothing is carried past it.
template <class P, class N, class C>
void routeOn(P &parent, N &each, const Path &path, std::size_t at, const PointerEvent &input, PointerReply &reply,
             Routed &routed, bool targetOnly, const C &carried) {
  walk::routePointer(each, path, at, input, reply, routed, targetOnly, carryInto(parent, each, carried));
}
template <class P, class C>
void routeOn(P &, AnyNodeRef &each, const Path &path, std::size_t at, const PointerEvent &input, PointerReply &reply,
             Routed &routed, bool targetOnly, const C &) {
  walk::routePointer(each, path, at, input, reply, routed, targetOnly);
}
template <class N>
void routePointer(N &child, const Path &path, std::size_t at,
                  const PointerEvent &given, PointerReply &reply, Routed &routed,
                  bool targetOnly) {
  walk::routePointer(child, path, at, given, reply, routed, targetOnly, NoCarry{});
}
template <class N, class C>
void routePointer(N &child, const Path &path, std::size_t at,
                  const PointerEvent &given, PointerReply &reply, Routed &routed,
                  bool targetOnly, const C &carried) {
  State &state = child.fState;
  // In the space this is laid out in: moved back by its shift. Most nodes
  // have none, and are given the event as it is.
  const bool shifted = state.fShiftX != 0.0f || state.fShiftY != 0.0f;
  const PointerEvent moved = shifted ? shiftedBack(given, state.fShiftX, state.fShiftY) : PointerEvent{};
  const PointerEvent &input = shifted ? moved : given;
  const auto deliver = [&](const auto &when) {
    reply.fCurrent = state.fId;
    reply.fCapturePointer = false;
    reply.fReleasePointer = false;
    reply.fRequestFocus = false;
    pointerTo(child, when, input, reply, carried);
    keepAnswer(path, at);
    if (reply.fCapturePointer) {
      routed.fCaptureRequest = state.fId;
    }
    if (reply.fReleasePointer) {
      routed.fReleaseRequest = true;
    }
    if (reply.fRequestFocus) {
      routed.fFocusRequest = state.fId;
    }
  };
  if (at == path.size()) {
    if (!reply.fHandled) {
      routed.fTargetDelivered = true;
      routed.fTargetFocusable = child.focusable() && child.takesFocusOnPress();
      deliver(phase::target{});
      answerHere(child, carried);
    }
    return;
  }
  if (!targetOnly && !reply.fHandled) {
    deliver(phase::capture{});
  }
  childAt(child, path[at], [&](auto &each) {
    routeOn(child, each, path, at + 1, input, reply, routed, targetOnly, carried);
  });
  if (!targetOnly && !reply.fHandled) {
    deliver(phase::bubble{});
  }
}

// The same for keys, text and semantic actions, which have one kind of
// reply -- what binds the tree carried down as with the pointer's.
template <class N, class Input, class Deliver, class C>
void route(N &child, const Path &path, std::size_t at, const Input &input,
           Reply &reply, Deliver deliver, const C &carried) {
  const auto phaseOf = [&](const auto &when) {
    reply.fCurrent = child.fState.fId;
    deliver(child, when, input, reply, carried);
    keepAnswer(path, at);
  };
  if (at == path.size()) {
    if (!reply.fHandled) {
      phaseOf(phase::target{});
      answerHere(child, carried);
    }
    return;
  }
  if (!reply.fHandled) {
    phaseOf(phase::capture{});
  }
  childAt(child, path[at], [&](auto &each) {
    deliver.next(child, each, path, at + 1, input, reply, carried);
  });
  if (!reply.fHandled) {
    phaseOf(phase::bubble{});
  }
}

struct KeyDelivery {
  template <class When>
  void operator()(ErasedNode &node, const When &when, const KeyEvent &input, Reply &reply) const;
  template <class When, class C>
  void operator()(ErasedNode &node, const When &when, const KeyEvent &input, Reply &reply, const C &) const {
    (*this)(node, when, input, reply);
  }
  template <class N, class C = NoCarry>
  void operator()(N &node, const auto &when, const KeyEvent &input,
                  Reply &reply, const C &carried = {}) const {
    spl::visit([&](const auto &event) { handled(node, carried, [&] { return node.onKey(when, event, reply); }); },
               input);
  }
  // Down to a child, with what is carried extended at it; an erased one is
  // seen through its table, and nothing is carried past it.
  template <class P, class Child, class Carried>
  void next(P &parent, Child &child, const Path &path, std::size_t at, const KeyEvent &input, Reply &reply,
            const Carried &carried) const;
  template <class P, class Carried>
  void next(P &parent, AnyNodeRef &child, const Path &path, std::size_t at, const KeyEvent &input, Reply &reply,
            const Carried &carried) const;
};
struct TextDelivery {
  template <class When>
  void operator()(ErasedNode &node, const When &when, const TextEvent &input, Reply &reply) const;
  template <class When, class C>
  void operator()(ErasedNode &node, const When &when, const TextEvent &input, Reply &reply, const C &) const {
    (*this)(node, when, input, reply);
  }
  template <class N, class C = NoCarry>
  void operator()(N &node, const auto &when, const TextEvent &input,
                  Reply &reply, const C &carried = {}) const {
    spl::visit([&](const auto &event) { handled(node, carried, [&] { return node.onText(when, event, reply); }); },
               input);
  }
  // Down to a child, with what is carried extended at it; an erased one is
  // seen through its table, and nothing is carried past it.
  template <class P, class Child, class Carried>
  void next(P &parent, Child &child, const Path &path, std::size_t at, const TextEvent &input, Reply &reply,
            const Carried &carried) const;
  template <class P, class Carried>
  void next(P &parent, AnyNodeRef &child, const Path &path, std::size_t at, const TextEvent &input, Reply &reply,
            const Carried &carried) const;
};
struct SemanticDelivery {
  template <class When>
  void operator()(ErasedNode &node, const When &when, const SemanticAction &input, Reply &reply) const;
  template <class When, class C>
  void operator()(ErasedNode &node, const When &when, const SemanticAction &input, Reply &reply, const C &) const {
    (*this)(node, when, input, reply);
  }
  template <class N, class C = NoCarry>
  void operator()(N &node, const auto &when, const SemanticAction &input,
                  Reply &reply, const C &carried = {}) const {
    spl::visit([&](const auto &event) { handled(node, carried, [&] { return node.onSemantic(when, event, reply); }); },
               input);
  }
  // Down to a child, with what is carried extended at it; an erased one is
  // seen through its table, and nothing is carried past it.
  template <class P, class Child, class Carried>
  void next(P &parent, Child &child, const Path &path, std::size_t at, const SemanticAction &input, Reply &reply,
            const Carried &carried) const;
  template <class P, class Carried>
  void next(P &parent, AnyNodeRef &child, const Path &path, std::size_t at, const SemanticAction &input, Reply &reply,
            const Carried &carried) const;
};

template <class N, class C = NoCarry>
void routeKey(N &child, const Path &path, std::size_t at, const KeyEvent &input,
              Reply &reply, const C &carried = {}) {
  route(child, path, at, input, reply, KeyDelivery{}, carried);
}
template <class N, class C = NoCarry>
void routeText(N &child, const Path &path, std::size_t at,
               const TextEvent &input, Reply &reply, const C &carried = {}) {
  route(child, path, at, input, reply, TextDelivery{}, carried);
}
template <class N, class C = NoCarry>
void routeSemantic(N &child, const Path &path, std::size_t at,
                   const SemanticAction &input, Reply &reply, const C &carried = {}) {
  route(child, path, at, input, reply, SemanticDelivery{}, carried);
}
template <class P, class Child, class Carried>
void KeyDelivery::next(P &parent, Child &child, const Path &path, std::size_t at, const KeyEvent &input, Reply &reply,
               const Carried &carried) const {
  walk::routeKey(child, path, at, input, reply, carryInto(parent, child, carried));
}
template <class P, class Carried>
void KeyDelivery::next(P &, AnyNodeRef &child, const Path &path, std::size_t at, const KeyEvent &input, Reply &reply,
               const Carried &) const {
  walk::routeKey(child, path, at, input, reply);
}
template <class P, class Child, class Carried>
void TextDelivery::next(P &parent, Child &child, const Path &path, std::size_t at, const TextEvent &input, Reply &reply,
               const Carried &carried) const {
  walk::routeText(child, path, at, input, reply, carryInto(parent, child, carried));
}
template <class P, class Carried>
void TextDelivery::next(P &, AnyNodeRef &child, const Path &path, std::size_t at, const TextEvent &input, Reply &reply,
               const Carried &) const {
  walk::routeText(child, path, at, input, reply);
}
template <class P, class Child, class Carried>
void SemanticDelivery::next(P &parent, Child &child, const Path &path, std::size_t at, const SemanticAction &input, Reply &reply,
               const Carried &carried) const {
  walk::routeSemantic(child, path, at, input, reply, carryInto(parent, child, carried));
}
template <class P, class Carried>
void SemanticDelivery::next(P &, AnyNodeRef &child, const Path &path, std::size_t at, const SemanticAction &input, Reply &reply,
               const Carried &) const {
  walk::routeSemantic(child, path, at, input, reply);
}

template <class N> std::optional<NodeInfo> info(N &child, NodeId id) {
  const State &state = child.fState;
  if (state.fId == id) {
    return NodeInfo{child.focusable(), state.fDisabled, state.fVisible,
                    isTextBox(child.semantics().fRole), state.fCursor, state.fDrawnBounds};
  }
  std::optional<NodeInfo> found;
  if (const NodeId next = work::childToward(state.fId, id); next != 0) {
    if (const NodeId *place = work::places().find(next)) {
      childAt(child, static_cast<std::uint32_t>(*place), [&](auto &each) {
        if (stateOf(each).fId == next) {
          found = walk::info(each, id);
        }
      });
      if (found) {
        found->fVisible = found->fVisible && state.fVisible;
        found->fDisabled = found->fDisabled || state.fDisabled;
        return found;
      }
    }
  }
  eachChild(child, [&](auto &each) {
    if (found) {
      return;
    }
    found = walk::info(each, id);
    if (found) {
      found->fVisible = found->fVisible && state.fVisible;
      found->fDisabled = found->fDisabled || state.fDisabled;
    }
  });
  return found;
}

template <class N>
bool focusChanged(N &child, NodeId id, bool focused, StyleResolver resolver,
                  float viewportWidth) {
  State &state = child.fState;
  const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                 : resolver;
  if (state.fId == id) {
    state.fFocused = focused;
    work::mark(state.fId);  // ticked at once: a caret starts, or stops
    child.onFocusChanged(focused);
    if (own && own.fUsesState(styleSubject(child, viewportWidth),
                              states::kFocus)) {
      state.restyle(true);
    }
    if (child.focusChangesAppearance() || child.fState.fFocusBackground) {
      state.markDamaged();
    }
    return true;
  }
  bool found = false;
  eachChild(child, [&](auto &each) {
    found = found || walk::focusChanged(each, id, focused, own, viewportWidth);
  });
  return found;
}

template <class N>
void collectSemantics(N &child, std::vector<Semantics> &out, int parent,
                      NodeId focused) {
  const State &state = child.fState;
  if (!state.fVisible || state.fAlpha <= 0.001f) {
    return;
  }
  Semantics own = child.semantics();
  int childParent = parent;
  if (hasRole(own.fRole) || !own.fLabel.empty()) {
    own.fDisabled = own.fDisabled || state.fDisabled;
    own.fFocused = own.fFocused || state.fId == focused;
    own.fSelected = own.fSelected || state.fSelected;
    own.fBounds = state.fBounds;
    own.fParent = parent;
    own.fId = state.fId;
    childParent = static_cast<int>(out.size());
    out.push_back(std::move(own));
  }
  eachChild(child, [&](auto &each) {
    walk::collectSemantics(each, out, childParent, focused);
  });
}

template <class N> void collectFocusable(N &child, std::vector<NodeId> &out) {
  const State &state = child.fState;
  if (!state.fVisible || state.fDisabled || state.fAlpha <= 0.001f) {
    return;
  }
  if (child.focusable()) {
    out.push_back(state.fId);
  }
  eachChildInDrawOrder(child, [&](auto &each, std::uint32_t) {
    walk::collectFocusable(each, out);
  });
}

// What is drawn over a moved scroll view: after it, where it is. Copied with
// the view's pixels, it would move with them; repainted instead. A node that
// paints nothing of its own (no fill, no border, children) is only gone
// through -- a layer of the window over everything would repaint it all.
template <class N>
void damageOver(N &child, NodeId below, const skia::SkRect &rect, bool &passed, skia::SkRect &damage) {
  State &state = child.fState;
  if (state.fId == below) {
    passed = true;
    return;
  }
  if (!state.fVisible || state.fAlpha <= 0.001f) {
    return;
  }
  bool hasChildren = false;
  eachChild(child, [&](auto &) { hasChildren = true; });
  const bool paints = !hasChildren || state.fBackground || state.fBorder || state.fShadow ||
                     state.fHoverBackground || state.fSelectedBackground || state.fFocusBackground;
  if (passed && paints) {
    const skia::SkRect at = state.fBounds.makeOffset(state.fShiftX, state.fShiftY);
    if (!at.isEmpty() && skia::SkRect::Intersects(at, rect)) {
      damage = joined(damage, at);
    }
    if (!state.fDrawnBounds.isEmpty() && skia::SkRect::Intersects(state.fDrawnBounds, rect)) {
      damage = joined(damage, state.fDrawnBounds);
    }
  }
  // Under it, in the space they are laid out in; what they find, back in
  // this one's.
  const skia::SkRect local = rect.makeOffset(-state.fShiftX, -state.fShiftY);
  skia::SkRect inner = skia::SkRect::MakeEmpty();
  eachChildInDrawOrder(child, [&](auto &each, std::uint32_t) { walk::damageOver(each, below, local, passed, inner); });
  if (!inner.isEmpty()) {
    damage = joined(damage, inner.makeOffset(state.fShiftX, state.fShiftY));
  }
}

// When a node next wants a frame on its own: what it says with wakeAt(),
// infinity where it says nothing -- the earliest of it and all in view
// under it.
template <class N>
  requires requires(const N &n) {
    { n.wakeAt() } -> std::convertible_to<double>;
  }
double ownWakeAt(const N &child) {
  return child.wakeAt();
}
template <class N> double ownWakeAt(const N &) {
  return std::numeric_limits<double>::infinity();
}
template <class N> double wakeAt(N &child) {
  if (!child.fState.fVisible) {
    return std::numeric_limits<double>::infinity();
  }
  double at = ownWakeAt(child);
  // Only what ticks can want a frame: what the tick keeps, as it last found.
  eachChildInView(child, [&](auto &each) {
    if (work::disabled() || work::ticking().contains(stateOf(each).fId)) {
      at = std::min(at, walk::wakeAt(each));
    }
  });
  return at;
}

template <class N> bool animating(N &child) {
  ++walkCounts().animating;
  const bool transform = !child.fState.fTransforms.empty();
  if (transform || child.settling()) {
    if (traceSettling() && settlers().size() < 64) {
      settlers().push_back({typeOf(child), transform});
    }
    return true;
  }
  bool any = false;
  // Only what ticks can be moving: what the tick keeps, as it last found.
  eachChild(child, [&](auto &each) {
    const State &one = stateOf(each);
    const std::optional<skia::SkRect> &view = child.fState.fInView;
    const bool seen = !view || one.fBounds.isEmpty() || skia::SkRect::Intersects(one.fBounds, *view);
    if (work::disabled() || work::pending().contains(one.fId) || (seen && work::ticking().contains(one.fId))) {
      any = any || walk::animating(each);
    }
  });
  return any;
}

// A click told straight to the node at the end of a path and, when it does
// not take it, to each node above in turn.
// A click told to a node: whether it took it -- and, where its onClick
// returns what the click asks for, that answered.
template <class N, class C> bool clicked(N &child, float x, float y, const C &) { return child.onClick(x, y); }
template <class N, class C>
  requires ClickAnswers<N>
bool clicked(N &child, float x, float y, const C &carried) {
  auto answer = child.onClick(x, y);
  if (!answer) {
    return false;
  }
  answered(child, answer, carried);
  return true;
}
template <class N, class C>
bool clickPath(N &child, const Path &path, std::size_t at, float x, float y, const C &carried);
template <class P, class N, class C>
bool clickOn(P &parent, N &each, const Path &path, std::size_t at, float x, float y, const C &carried) {
  return walk::clickPath(each, path, at, x, y, carryInto(parent, each, carried));
}
template <class P, class C>
bool clickOn(P &, AnyNodeRef &each, const Path &path, std::size_t at, float x, float y, const C &) {
  return walk::clickPath(each, path, at, x, y);
}
template <class N>
bool clickPath(N &child, const Path &path, std::size_t at, float x, float y) {
  return walk::clickPath(child, path, at, x, y, NoCarry{});
}
template <class N, class C>
bool clickPath(N &child, const Path &path, std::size_t at, float x, float y, const C &carried) {
  x -= child.fState.fShiftX;
  y -= child.fState.fShiftY;
  bool taken = false;
  if (at < path.size()) {
    childAt(child, path[at], [&](auto &each) {
      taken = clickOn(child, each, path, at + 1, x, y, carried);
    });
  }
  if (taken) {
    return true;
  }
  if ((hostWork().laidOutElsewhere || child.fState.fBounds.contains(x, y)) && clicked(child, x, y, carried)) {
    answerHere(child, carried);
    keepAnswer(path, at);
    return true;
  }
  return false;
}

} // namespace walk

// ---- AnyNode ----------------------------------------------------------------

// A walk on a node of type T held as a void*: on the node itself in a release
// build; outside one, on it seen as an ErasedNode -- the walk made once.
template <class T, class F> decltype(auto) walkOn(void *node, F &&f);

// Any node, held by value, erased the way std::function erases a callable:
// one allocation, and a table of the walks for the node's own type.
// A family of events as a node seen through its table is given them: for
// each phase, one entry per alternative of the event -- a call of the node's
// own handler for that phase and that event, nothing else. The event is
// visited once, in deliver, where the code is the same for every node; a
// type's table is only its entries, each one call -- not a visit of the
// phase and then of the event for every type, which made most of a
// program's build outside a release build.
// A walk on any node through its table (defined with walkOn, below): the
// shared walks' way in.
template <class D, class Ops, class F> decltype(auto) walkErased(void *node, const Ops *ops, F &&f);
template <class Handler, class Event, class R> struct EventEntries;
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
// Node's default for a phase and an event, on any node seen through its
// table: one function for every type -- a node with no handler of its own
// of a family is given the same row as every other (EventEntries::of).
template <class Handler, class P, class E, class R>
void sharedDefault(void *node, const void *ops, const void *event, R &reply);
// Whether a node handles an event in a phase itself, or Node's default is
// all it has there: its handlers looked up beside a stand-in for the
// default, of the default's own form, which hides Node's -- the stand-in
// chosen, nothing of the node's own takes it.
struct NotOwnHandler {};
consteval bool notOwn(std::type_identity<NotOwnHandler>) { return true; }
template <class R> consteval bool notOwn(std::type_identity<R>) { return false; }
template <class N> struct PointerProbe : N {
  using N::onPointer;
  NotOwnHandler onPointer(this auto &, const auto &, const auto &, PointerReply &) { return {}; }
};
template <class N> struct KeyProbe : N {
  using N::onKey;
  NotOwnHandler onKey(this auto &, const auto &, const auto &, Reply &) { return {}; }
};
template <class N> struct TextProbe : N {
  using N::onText;
  NotOwnHandler onText(const auto &, const auto &, Reply &) { return {}; }
};
template <class N> struct SemanticProbe : N {
  using N::onSemantic;
  NotOwnHandler onSemantic(this auto &, const auto &, const auto &, Reply &) { return {}; }
};
// Where Node's defaults do anything -- as defaultPointer, defaultKey and
// defaultSemantic above say, each event its overload at the target: a
// press, a release, a cancel, a wheel; a key down; a focus, an activation.
// In every other phase, and for every other event, they do nothing.
template <class P, class E> consteval bool defaultActs(P, std::type_identity<E>) { return false; }
consteval bool defaultActs(phase::target, std::type_identity<pointer::down>) { return true; }
consteval bool defaultActs(phase::target, std::type_identity<pointer::up>) { return true; }
consteval bool defaultActs(phase::target, std::type_identity<pointer::cancel>) { return true; }
consteval bool defaultActs(phase::target, std::type_identity<pointer::scroll>) { return true; }
consteval bool defaultActs(phase::target, std::type_identity<key::down>) { return true; }
consteval bool defaultActs(phase::target, std::type_identity<semantic_action::focus>) { return true; }
consteval bool defaultActs(phase::target, std::type_identity<semantic_action::activate>) { return true; }
// Whether a node has handlers of its own of a family at all -- asked once,
// before any is asked of a phase and an event: its handlers offered
// stand-ins that convert to anything, beside the stand-in for Node's
// default. A handler of its own that names its phase or its event asks
// for a conversion to it, which marks a slot (a friend defined where it is
// asked for: unusable until then, its return type not yet deduced).
// Nothing marked: nothing of its own takes any event of the family, and
// nothing more is asked of it -- most nodes.
template <class Tag> struct Touched {
  friend constexpr auto touched(Touched);
};
template <class Tag> struct Touch {
  friend constexpr auto touched(Touched<Tag>) { return true; }
};
template <class Tag> struct ConvertsToAny;
// Marked only for a conversion to another type: binding the stand-in to a
// reference of its own type may ask for one to itself.
template <class Tag, class T> struct MarkFor {
  static constexpr std::size_t value = sizeof(Touch<Tag>);
};
template <class Tag> struct MarkFor<Tag, ConvertsToAny<Tag>> {
  static constexpr std::size_t value = 0;
};
template <class Tag> struct MarkFor<Tag, const ConvertsToAny<Tag>> {
  static constexpr std::size_t value = 0;
};
template <class Tag> struct ConvertsToAny {
  template <class T, std::size_t = MarkFor<Tag, T>::value> operator T &() const;
  template <class T, std::size_t = MarkFor<Tag, T>::value> operator T &&() const;
};
// Two types no handler names: what a handler of Node's own generic form is
// found for.
struct NoPhase {};
struct NoEvent {};
template <class Tag, bool Offered, auto = [] {}>
inline constexpr bool kTouched = requires { touched(Touched<Tag>{}); };
template <class Handler, class N> struct FamilyTag {};
template <class Handler, class N>
inline constexpr bool kOffered = requires(typename Handler::template Probe<N> &p, typename Handler::ReplyType &r) {
  Handler::ask(p, ConvertsToAny<FamilyTag<Handler, N>>{}, ConvertsToAny<FamilyTag<Handler, N>>{}, r);
};
template <class Handler, class N>
struct TouchedOwn : std::bool_constant<kTouched<FamilyTag<Handler, N>, kOffered<Handler, N>>> {};
// A handler of its own of Node's generic form -- which the stand-in hides
// as it hides Node's -- told apart by the function its name finds for two
// types no handler names: not Node's.
template <class Handler, class N> struct GenericOwn : std::bool_constant<Handler::template kGenericOwn<N>> {};
// A node's own onScroll -- which the default for a wheel calls -- told apart
// from Node's by the function its name finds (a final type: taken to have one).
template <class N>
inline constexpr bool kOwnScroll = std::is_final_v<N> || !requires { requires(&N::onScroll == &Node::onScroll); };
template <class Handler, class N>
  requires std::is_final_v<N>
struct GenericOwn<Handler, N> : std::true_type {};
// Its own for this phase and event, asked only of a node with any.
template <class Handler, class N, class P, class E> struct OwnFor : std::bool_constant<Handler::template kOwn<N, P, E>> {};
template <class Handler, class N, class P, class E>
inline constexpr bool kCalledFor =
    defaultActs(P{}, std::type_identity<E>{}) ||
    std::disjunction_v<GenericOwn<Handler, N>, std::conjunction<TouchedOwn<Handler, N>, OwnFor<Handler, N, P, E>>>;
// (A final type cannot be looked into so: taken to handle everything.)
template <class N, class P, class E> inline constexpr bool kOwnPointer = true;
template <class N, class P, class E>
  requires(!std::is_final_v<N>)
inline constexpr bool kOwnPointer<N, P, E> = !notOwn(std::type_identity<decltype(std::declval<PointerProbe<N> &>().onPointer(
    std::declval<const P &>(), std::declval<const E &>(), std::declval<PointerReply &>()))>{});
template <class N, class P, class E> inline constexpr bool kOwnKey = true;
template <class N, class P, class E>
  requires(!std::is_final_v<N>)
inline constexpr bool kOwnKey<N, P, E> = !notOwn(std::type_identity<decltype(std::declval<KeyProbe<N> &>().onKey(
    std::declval<const P &>(), std::declval<const E &>(), std::declval<Reply &>()))>{});
template <class N, class P, class E> inline constexpr bool kOwnText = true;
template <class N, class P, class E>
  requires(!std::is_final_v<N>)
inline constexpr bool kOwnText<N, P, E> = !notOwn(std::type_identity<decltype(std::declval<TextProbe<N> &>().onText(
    std::declval<const P &>(), std::declval<const E &>(), std::declval<Reply &>()))>{});
template <class N, class P, class E> inline constexpr bool kOwnSemantic = true;
template <class N, class P, class E>
  requires(!std::is_final_v<N>)
inline constexpr bool kOwnSemantic<N, P, E> = !notOwn(std::type_identity<decltype(std::declval<SemanticProbe<N> &>().onSemantic(
    std::declval<const P &>(), std::declval<const E &>(), std::declval<Reply &>()))>{});
#endif
template <class Handler, class... Es, class R> struct EventEntries<Handler, spl::variant<Es...>, R> {
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  // The node, its table, the event, the reply.
  using Entry = void (*)(void *, const void *, const void *, R &);
  using Row = std::array<Entry, sizeof...(Es)>;
#else
  using Row = std::tuple<void (*)(void *, const Es &, R &)...>;
#endif
  std::array<Row, 3> fRows;

  template <class T> [[nodiscard]] static constexpr EventEntries of() {
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
    // Most nodes handle nothing of a family themselves: asked once whether
    // this one does, and if not, the row every such node has -- Node's
    // defaults, made once -- and nothing asked of each phase and event.
    if constexpr (!Handler::template kAnyOwn<T>) {
      return defaults();
    } else
#endif
    return {{rowOf<T, phase::capture>(), rowOf<T, phase::target>(), rowOf<T, phase::bubble>()}};
  }
  template <class P>
  void deliver(void *node, [[maybe_unused]] const void *ops, P when, const spl::variant<Es...> &input, R &reply) const {
    const Row &row = fRows[indexOf(when)];
    spl::visit(
        [&](const auto &event) {
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
          // None: nothing of the node's own, and a default that does nothing.
          if (const Entry entry = row[eventIndex<std::remove_cvref_t<decltype(event)>>()])
            entry(node, ops, &event, reply);
#else
          using Entry = void (*)(void *, const std::remove_cvref_t<decltype(event)> &, R &);
          std::get<Entry>(row)(node, event, reply);
#endif
        },
        input);
  }

private:
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  // The event alternatives already have a stable order. A flat row avoids
  // generating tuple construction and lookup machinery in every node table.
  template <class E> static consteval std::size_t eventIndex() {
    std::size_t at = 0, found = 0;
    ((std::same_as<E, Es> ? found = at : 0, ++at), ...);
    return found;
  }
  // An entry only where the node's handler, or Node's default, does
  // something: most nodes handle nothing, and every phase of every event
  // was a function made for each of them -- most of a page's code.
  template <class T, class P> [[nodiscard]] static constexpr Row rowOf() {
    return Row{entryOf<T, P, Es>(std::bool_constant<Handler::template kCalls<T, P, Es>>{})...};
  }
  // The one row of every node with nothing of its own of the family.
  template <class P> [[nodiscard]] static constexpr Row defaultRow() {
    return Row{(defaultActs(P{}, std::type_identity<Es>{}) ? &sharedDefault<Handler, P, Es, R> : nullptr)...};
  }
  [[nodiscard]] static constexpr EventEntries defaults() {
    return {{defaultRow<phase::capture>(), defaultRow<phase::target>(), defaultRow<phase::bubble>()}};
  }
  template <class T, class P, class E> [[nodiscard]] static constexpr Entry entryOf(std::true_type) {
    return +[](void *node, const void *, const void *event, R &reply) {
      Handler{}(*static_cast<T *>(node), P{}, *static_cast<const E *>(event), reply);
    };
  }
  template <class T, class P, class E> [[nodiscard]] static constexpr Entry entryOf(std::false_type) { return nullptr; }
#else
  template <class T, class P> [[nodiscard]] static constexpr Row rowOf() {
    return Row{+[](void *node, const Es &event, R &reply) { Handler{}(*static_cast<T *>(node), P{}, event, reply); }...};
  }
#endif
  [[nodiscard]] static constexpr std::size_t indexOf(phase::capture) { return 0; }
  [[nodiscard]] static constexpr std::size_t indexOf(phase::target) { return 1; }
  [[nodiscard]] static constexpr std::size_t indexOf(phase::bubble) { return 2; }
};
// Each family's handler, called on a node of its own type.
struct PointerHandler {
  template <class N, class P, class E> void operator()(N &node, P when, const E &event, PointerReply &reply) const {
    handled(node, NoCarry{}, [&] { return node.onPointer(when, event, reply); });
  }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  template <class N> using Probe = PointerProbe<N>;
  template <class N>
  static constexpr bool kAnyOwn = GenericOwn<PointerHandler, N>::value || TouchedOwn<PointerHandler, N>::value || kOwnScroll<N>;
  template <class N, class P, class E> static void defaults(N &node, const P &when, const E &event, PointerReply &reply) {
    defaultPointer(node, when, event, reply);
  }
  using ReplyType = PointerReply;
  template <class N, class P, class E>
  static auto ask(N &node, const P &when, const E &event, PointerReply &reply) -> decltype(node.onPointer(when, event, reply));
  template <class N, class P, class E> static constexpr bool kOwn = kOwnPointer<N, P, E>;
  template <class N, class P, class E> static constexpr bool kCalls = kCalledFor<PointerHandler, N, P, E>;
  template <class N>
  static constexpr bool kGenericOwn =
      !requires { requires(&N::template onPointer<N, NoPhase, NoEvent> == &Node::template onPointer<N, NoPhase, NoEvent>); };
#endif
};
struct KeyHandler {
  template <class N, class P, class E> void operator()(N &node, P when, const E &event, Reply &reply) const {
    handled(node, NoCarry{}, [&] { return node.onKey(when, event, reply); });
  }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  template <class N> using Probe = KeyProbe<N>;
  template <class N>
  static constexpr bool kAnyOwn = GenericOwn<KeyHandler, N>::value || TouchedOwn<KeyHandler, N>::value;
  template <class N, class P, class E> static void defaults(N &node, const P &when, const E &event, Reply &reply) {
    defaultKey(node, when, event, reply);
  }
  using ReplyType = Reply;
  template <class N, class P, class E>
  static auto ask(N &node, const P &when, const E &event, Reply &reply) -> decltype(node.onKey(when, event, reply));
  template <class N, class P, class E> static constexpr bool kOwn = kOwnKey<N, P, E>;
  template <class N, class P, class E> static constexpr bool kCalls = kCalledFor<KeyHandler, N, P, E>;
  template <class N>
  static constexpr bool kGenericOwn =
      !requires { requires(&N::template onKey<N, NoPhase, NoEvent> == &Node::template onKey<N, NoPhase, NoEvent>); };
#endif
};
struct TextHandler {
  template <class N, class P, class E> void operator()(N &node, P when, const E &event, Reply &reply) const {
    handled(node, NoCarry{}, [&] { return node.onText(when, event, reply); });
  }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  template <class N> using Probe = TextProbe<N>;
  template <class N>
  static constexpr bool kAnyOwn = GenericOwn<TextHandler, N>::value || TouchedOwn<TextHandler, N>::value;
  template <class N, class P, class E> static void defaults(N &, const P &, const E &, Reply &) {}
  using ReplyType = Reply;
  template <class N, class P, class E>
  static auto ask(N &node, const P &when, const E &event, Reply &reply) -> decltype(node.onText(when, event, reply));
  template <class N, class P, class E> static constexpr bool kOwn = kOwnText<N, P, E>;
  template <class N, class P, class E> static constexpr bool kCalls = kCalledFor<TextHandler, N, P, E>;
  template <class N>
  static constexpr bool kGenericOwn =
      !requires { requires(&N::template onText<NoPhase, NoEvent> == &Node::template onText<NoPhase, NoEvent>); };
#endif
};
struct SemanticHandler {
  template <class N, class P, class E> void operator()(N &node, P when, const E &event, Reply &reply) const {
    handled(node, NoCarry{}, [&] { return node.onSemantic(when, event, reply); });
  }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  template <class N> using Probe = SemanticProbe<N>;
  template <class N>
  static constexpr bool kAnyOwn = GenericOwn<SemanticHandler, N>::value || TouchedOwn<SemanticHandler, N>::value;
  template <class N, class P, class E> static void defaults(N &node, const P &when, const E &event, Reply &reply) {
    defaultSemantic(node, when, event, reply);
  }
  using ReplyType = Reply;
  template <class N, class P, class E>
  static auto ask(N &node, const P &when, const E &event, Reply &reply) -> decltype(node.onSemantic(when, event, reply));
  template <class N, class P, class E> static constexpr bool kOwn = kOwnSemantic<N, P, E>;
  template <class N, class P, class E> static constexpr bool kCalls = kCalledFor<SemanticHandler, N, P, E>;
  template <class N>
  static constexpr bool kGenericOwn =
      !requires { requires(&N::template onSemantic<N, NoPhase, NoEvent> == &Node::template onSemantic<N, NoPhase, NoEvent>); };
#endif
};


struct NodeOps;
// The walks through a node: given its table, so that outside a release
// build one set of them, made once for ErasedNode, serves every type.
struct Walks {
  void (*fUpdate)(void *, const NodeOps *, UpdateContext &, StyleResolver, const Style *,
                  bool);
  bool (*fMarkDirty)(void *, const NodeOps *);
  skia::SkRect (*fCollectDamage)(void *, const NodeOps *, bool);
  bool (*fHasDamage)(void *, const NodeOps *);
  void (*fHover)(void *, const NodeOps *, float, float, bool, StyleResolver, float, bool);
  bool (*fHitPath)(void *, const NodeOps *, float, float, Path &);
  bool (*fFindPath)(void *, const NodeOps *, NodeId, Path &);
  NodeId (*fIdAt)(void *, const NodeOps *, const Path &, std::size_t);
  void (*fRoutePointer)(void *, const NodeOps *, const Path &, std::size_t,
                        const PointerEvent &, PointerReply &, Routed &, bool);
  void (*fRouteKey)(void *, const NodeOps *, const Path &, std::size_t, const KeyEvent &,
                    Reply &);
  void (*fRouteText)(void *, const NodeOps *, const Path &, std::size_t, const TextEvent &,
                     Reply &);
  void (*fRouteSemantic)(void *, const NodeOps *, const Path &, std::size_t,
                         const SemanticAction &, Reply &);
  std::optional<NodeInfo> (*fInfo)(void *, const NodeOps *, NodeId);
  bool (*fFocusChanged)(void *, const NodeOps *, NodeId, bool, StyleResolver, float);
  void (*fCollectSemantics)(void *, const NodeOps *, std::vector<Semantics> &, int, NodeId);
  void (*fCollectFocusable)(void *, const NodeOps *, std::vector<NodeId> &);
  bool (*fAnimating)(void *, const NodeOps *);
  bool (*fClickPath)(void *, const NodeOps *, const Path &, std::size_t, float, float);
  double (*fWakeAt)(void *, const NodeOps *);
  void (*fDamageOver)(void *, const NodeOps *, NodeId, const skia::SkRect &, bool &, skia::SkRect &);
};
struct NodeOps {
  // The core of it first: what skiff.bind reaches a node through, whatever draws it.
  AnyNodeOps core;
  State &(*fState)(void *);
  void (*fLayout)(void *, const skia::SkRect &);
  void (*fDraw)(void *, Painting &, skia::SkCanvas *, float);
  // The walks: the type's own in a release build, one table for every
  // node outside it (Walks).
  const Walks *fWalks;
  // The node's own hooks, which the walks made once (ErasedNode) call.
  void (*fTick)(void *, double);
  bool (*fPolls)(void *);
  void (*fApplyNodeStyle)(void *, const Style &, bool);
  StyleSubject (*fStyleSubject)(void *, float);
  void (*fHoverDamage)(void *);
  bool (*fHoverChangesAppearance)(void *);
  bool (*fAcceptsInput)(void *);
  bool (*fFocusable)(void *);
  bool (*fTakesFocusOnPress)(void *);
  bool (*fFocusChangesAppearance)(void *);
  Semantics (*fSemantics)(void *);
  void (*fOnFocusChanged)(void *, bool);
  bool (*fOnClick)(void *, float, float);
  double (*fOwnWakeAt)(void *);
  Look (*fLook)(void *);
  // Optional scrolling hooks for walks through held content. A page's
  // engine must both follow a scene jump and give user scrolling back.
  std::optional<float> (*fScrollOffset)(void *);
  void (*fSetScrollOffset)(void *, float);
  // Its own drawing through a painter of any type -- skiff.mirror's, which
  // writes it as elements: whether it draws so.
  bool (*fDrawThrough)(void *, skiff::paint::ErasedPainter &, float);
  EventEntries<PointerHandler, PointerEvent, PointerReply> fPointer;
  EventEntries<KeyHandler, KeyEvent, Reply> fKey;
  EventEntries<TextHandler, TextEvent, Reply> fText;
  EventEntries<SemanticHandler, SemanticAction, Reply> fSemantic;
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  // Layout and the default draw algorithm need only these typed hooks;
  // the surrounding algorithms do not depend on the node's subtree type.
  void (*fMeasure)(void *, const skia::SkRect &);
  void (*fLayoutChildren)(void *);
  void (*fDrawSelf)(void *, Painting &, skia::SkCanvas *, float);
#endif
  // What binds it (skiffBindTable), for the binding's walks at run time.
  // The mark it carries (MarkOf), and the way from it to its mark.
};

#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
inline void layoutShared(void *node, const NodeOps *ops, const skia::SkRect &box);
extern const Walks kDebugWalks;
#endif
template <class T> [[nodiscard]] T &nodeAs(void *node) {
  return *static_cast<T *>(node);
}

template <class T>
inline constexpr Walks kTypedWalks{
    +[](void *n, const NodeOps *, UpdateContext &c, StyleResolver r, const Style *s,
        bool all) { walkOn<T>(n, [&](auto &node) { return walk::update(node, c, r, s, all); }); },
    +[](void *n, const NodeOps *) { return walkOn<T>(n, [&](auto &node) { return walk::markDirty(node); }); },
    +[](void *n, const NodeOps *, bool drawn) {
      return walkOn<T>(n, [&](auto &node) { return walk::collectDamage(node, drawn); });
    },
    +[](void *n, const NodeOps *) { return walkOn<T>(n, [&](auto &node) { return walk::hasDamage(node); }); },
    +[](void *n, const NodeOps *, float x, float y, bool visible, StyleResolver r, float width,
        bool above) { walkOn<T>(n, [&](auto &node) { return walk::hover(node, x, y, visible, r, width, above); }); },
    +[](void *n, const NodeOps *, float x, float y, Path &path) {
      return walkOn<T>(n, [&](auto &node) { return walk::hitPath(node, x, y, path); });
    },
    +[](void *n, const NodeOps *, NodeId id, Path &path) {
      return walkOn<T>(n, [&](auto &node) { return walk::findPath(node, id, path); });
    },
    +[](void *n, const NodeOps *, const Path &path, std::size_t at) {
      return walkOn<T>(n, [&](auto &node) { return walk::idAt(node, path, at); });
    },
    +[](void *n, const NodeOps *, const Path &path, std::size_t at, const PointerEvent &e,
        PointerReply &reply, Routed &routed, bool targetOnly) {
      walkOn<T>(n, [&](auto &node) { return walk::routePointer(node, path, at, e, reply, routed, targetOnly); });
    },
    +[](void *n, const NodeOps *, const Path &path, std::size_t at, const KeyEvent &e,
        Reply &reply) { walkOn<T>(n, [&](auto &node) { return walk::routeKey(node, path, at, e, reply); }); },
    +[](void *n, const NodeOps *, const Path &path, std::size_t at, const TextEvent &e,
        Reply &reply) { walkOn<T>(n, [&](auto &node) { return walk::routeText(node, path, at, e, reply); }); },
    +[](void *n, const NodeOps *, const Path &path, std::size_t at,
        const SemanticAction &e, Reply &reply) {
      walkOn<T>(n, [&](auto &node) { return walk::routeSemantic(node, path, at, e, reply); });
    },
    +[](void *n, const NodeOps *, NodeId id) { return walkOn<T>(n, [&](auto &node) { return walk::info(node, id); }); },
    +[](void *n, const NodeOps *, NodeId id, bool focused, StyleResolver r, float width) {
      return walkOn<T>(n, [&](auto &node) { return walk::focusChanged(node, id, focused, r, width); });
    },
    +[](void *n, const NodeOps *, std::vector<Semantics> &out, int parent, NodeId focused) {
      walkOn<T>(n, [&](auto &node) { return walk::collectSemantics(node, out, parent, focused); });
    },
    +[](void *n, const NodeOps *, std::vector<NodeId> &out) {
      walkOn<T>(n, [&](auto &node) { return walk::collectFocusable(node, out); });
    },
    +[](void *n, const NodeOps *) { return walkOn<T>(n, [&](auto &node) { return walk::animating(node); }); },
    +[](void *n, const NodeOps *, const Path &path, std::size_t at, float x, float y) {
      return walkOn<T>(n, [&](auto &node) { return walk::clickPath(node, path, at, x, y); });
    },
    +[](void *n, const NodeOps *) { return walkOn<T>(n, [&](auto &node) { return walk::wakeAt(node); }); },
    +[](void *n, const NodeOps *, NodeId below, const skia::SkRect &rect, bool &passed, skia::SkRect &damage) {
      walkOn<T>(n, [&](auto &node) { return walk::damageOver(node, below, rect, passed, damage); });
    },
};
template <class D = void>
inline constexpr Walks kSharedWalks{
    +[](void *n, const NodeOps *ops, UpdateContext &c, StyleResolver r, const Style *s,
        bool all) { walkErased<D>(n, ops, [&](auto &node) { return walk::update(node, c, r, s, all); }); },
    +[](void *n, const NodeOps *ops) { return walkErased<D>(n, ops, [&](auto &node) { return walk::markDirty(node); }); },
    +[](void *n, const NodeOps *ops, bool drawn) {
      return walkErased<D>(n, ops, [&](auto &node) { return walk::collectDamage(node, drawn); });
    },
    +[](void *n, const NodeOps *ops) { return walkErased<D>(n, ops, [&](auto &node) { return walk::hasDamage(node); }); },
    +[](void *n, const NodeOps *ops, float x, float y, bool visible, StyleResolver r, float width,
        bool above) { walkErased<D>(n, ops, [&](auto &node) { return walk::hover(node, x, y, visible, r, width, above); }); },
    +[](void *n, const NodeOps *ops, float x, float y, Path &path) {
      return walkErased<D>(n, ops, [&](auto &node) { return walk::hitPath(node, x, y, path); });
    },
    +[](void *n, const NodeOps *ops, NodeId id, Path &path) {
      return walkErased<D>(n, ops, [&](auto &node) { return walk::findPath(node, id, path); });
    },
    +[](void *n, const NodeOps *ops, const Path &path, std::size_t at) {
      return walkErased<D>(n, ops, [&](auto &node) { return walk::idAt(node, path, at); });
    },
    +[](void *n, const NodeOps *ops, const Path &path, std::size_t at, const PointerEvent &e,
        PointerReply &reply, Routed &routed, bool targetOnly) {
      walkErased<D>(n, ops, [&](auto &node) { return walk::routePointer(node, path, at, e, reply, routed, targetOnly); });
    },
    +[](void *n, const NodeOps *ops, const Path &path, std::size_t at, const KeyEvent &e,
        Reply &reply) { walkErased<D>(n, ops, [&](auto &node) { return walk::routeKey(node, path, at, e, reply); }); },
    +[](void *n, const NodeOps *ops, const Path &path, std::size_t at, const TextEvent &e,
        Reply &reply) { walkErased<D>(n, ops, [&](auto &node) { return walk::routeText(node, path, at, e, reply); }); },
    +[](void *n, const NodeOps *ops, const Path &path, std::size_t at,
        const SemanticAction &e, Reply &reply) {
      walkErased<D>(n, ops, [&](auto &node) { return walk::routeSemantic(node, path, at, e, reply); });
    },
    +[](void *n, const NodeOps *ops, NodeId id) { return walkErased<D>(n, ops, [&](auto &node) { return walk::info(node, id); }); },
    +[](void *n, const NodeOps *ops, NodeId id, bool focused, StyleResolver r, float width) {
      return walkErased<D>(n, ops, [&](auto &node) { return walk::focusChanged(node, id, focused, r, width); });
    },
    +[](void *n, const NodeOps *ops, std::vector<Semantics> &out, int parent, NodeId focused) {
      walkErased<D>(n, ops, [&](auto &node) { return walk::collectSemantics(node, out, parent, focused); });
    },
    +[](void *n, const NodeOps *ops, std::vector<NodeId> &out) {
      walkErased<D>(n, ops, [&](auto &node) { return walk::collectFocusable(node, out); });
    },
    +[](void *n, const NodeOps *ops) { return walkErased<D>(n, ops, [&](auto &node) { return walk::animating(node); }); },
    +[](void *n, const NodeOps *ops, const Path &path, std::size_t at, float x, float y) {
      return walkErased<D>(n, ops, [&](auto &node) { return walk::clickPath(node, path, at, x, y); });
    },
    +[](void *n, const NodeOps *ops) { return walkErased<D>(n, ops, [&](auto &node) { return walk::wakeAt(node); }); },
    +[](void *n, const NodeOps *ops, NodeId below, const skia::SkRect &rect, bool &passed, skia::SkRect &damage) {
      walkErased<D>(n, ops, [&](auto &node) { return walk::damageOver(node, below, rect, passed, damage); });
    },
};
// A type's walks: its own in a release build, the shared ones outside it --
// chosen by naming a type, so that the other is never made.
template <class T> struct TypedWalksOf {
  static constexpr const Walks *value = &kTypedWalks<T>;
};
template <class D> struct SharedWalksOf {
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  static constexpr const Walks *value = &kDebugWalks;
#else
  static constexpr const Walks *value = &kSharedWalks<D>;
#endif
};
template <class T> [[nodiscard]] constexpr const Walks *walksOf() {
  return std::conditional_t<kErasedWalks, SharedWalksOf<void>, TypedWalksOf<T>>::value;
}
template <class T>
inline constexpr NodeOps kNodeOps{
    AnyNodeOps{
        +[](void *n) { delete static_cast<T *>(n); },
        kindOf<T>(),
        +[](void *n, void *context, AnyChildVisit visit) {
          eachChild(nodeAs<T>(n), [&](auto &child) { visitAsAny(child, context, visit); });
        },
        +[](void *n, std::uint32_t place, void *context, AnyChildVisit visit) -> bool {
          return childAt(nodeAs<T>(n), place, [&](auto &child) { visitAsAny(child, context, visit); });
        },
        +[](void *n) -> bool { return nodeAs<T>(n).settling(); },
        BindTableOf<T>::value,
        MarkOf<T>::key,
        &MarkOf<T>::as,
    },
    +[](void *n) -> State & {
      // A node whose state follows another's as it is seen (a wrapper over
      // held content: shown only while its content is) says so.
      if constexpr (requires(T &node) { { node.walkedState() } -> std::same_as<State &>; })
        return nodeAs<T>(n).walkedState();
      else
        return nodeAs<T>(n).fState;
    },
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
    +[](void *n, const skia::SkRect &box) { layoutShared(n, &kNodeOps<T>, box); },
#else
    +[](void *n, const skia::SkRect &box) { detail::layoutNode(nodeAs<T>(n), box); },
#endif
    +[](void *n, Painting &painting, skia::SkCanvas *canvas, float alpha) { nodeAs<T>(n).draw(painting, canvas, alpha); },
    walksOf<T>(),
    +[](void *n, double now) { nodeAs<T>(n).update(now); },
    +[](void *n) -> bool { return walk::pollsEachFrame(nodeAs<T>(n)); },
    +[](void *n, const Style &style, bool active) { nodeAs<T>(n).applyNodeStyle(style, active); },
    +[](void *n, float width) -> StyleSubject { return styleSubject(nodeAs<T>(n), width); },
    +[](void *n) { walk::damageForHover(nodeAs<T>(n)); },
    +[](void *n) -> bool { return nodeAs<T>(n).hoverChangesAppearance(); },
    +[](void *n) -> bool { return nodeAs<T>(n).acceptsInput(); },
    +[](void *n) -> bool { return nodeAs<T>(n).focusable(); },
    +[](void *n) -> bool { return nodeAs<T>(n).takesFocusOnPress(); },
    +[](void *n) -> bool { return nodeAs<T>(n).focusChangesAppearance(); },
    +[](void *n) -> Semantics { return nodeAs<T>(n).semantics(); },
    +[](void *n, bool focused) { nodeAs<T>(n).onFocusChanged(focused); },
    +[](void *n, float x, float y) -> bool { return walk::clicked(nodeAs<T>(n), x, y, NoCarry{}); },
    +[](void *n) -> double { return walk::ownWakeAt(nodeAs<T>(n)); },
    +[](void *n) -> Look { return nodeAs<T>(n).look(); },
    +[](void *n) -> std::optional<float> {
      if constexpr (requires(const T &node) { { node.current() } -> std::convertible_to<float>; node.extent(); })
        return nodeAs<T>(n).current();
      else
        return std::nullopt;
    },
    +[](void *n, float offset) {
      if constexpr (requires(T &node) { node.setCurrent(offset); node.extent(); })
        nodeAs<T>(n).setCurrent(offset);
    },
    +[](void *n, skiff::paint::ErasedPainter &p, float alpha) -> bool {
      if constexpr (requires(T &node) { node.drawSelf(p, alpha); }) {
        nodeAs<T>(n).drawSelf(p, alpha);
        return true;
      } else {
        return false;
      }
    },
    EventEntries<PointerHandler, PointerEvent, PointerReply>::of<T>(),
    EventEntries<KeyHandler, KeyEvent, Reply>::of<T>(),
    EventEntries<TextHandler, TextEvent, Reply>::of<T>(),
    EventEntries<SemanticHandler, SemanticAction, Reply>::of<T>(),
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
    +[](void *n, const skia::SkRect &box) { nodeAs<T>(n).measure(box); },
    +[](void *n) { nodeAs<T>(n).layoutChildren(); },
    +[](void *n, Painting &painting, skia::SkCanvas *canvas, float alpha) {
      auto &hooks = static_cast<detail::PaintOf<T> &>(painting);
      detail::drawSelfOf(nodeAs<T>(n), hooks, canvas, alpha);
    },
#endif
};


// The scene's table of a node type, reached from its core: skiff.core keeps a
// node by its AnyNodeOps, and the scene's is the struct that one begins.
[[nodiscard]] inline const NodeOps &sceneOps(const AnyNodeOps &core) noexcept {
  return *reinterpret_cast<const NodeOps *>(&core);
}
[[nodiscard]] inline const NodeOps &sceneOps(const AnyNodeRef &ref) noexcept { return sceneOps(ref.ops()); }
// The table skiff.core asks the scene for, by a node's base (skiff::scene::Node).
template <class T> [[nodiscard]] const AnyNodeOps &skiffOpsOf(T *) noexcept { return kNodeOps<T>.core; }


// The first node of type T shown under a node (itself included), as the
// walks see them -- through what holds them outside a release build, by
// their types in one; nothing under a hidden node: for a widget that works
// across marked parts of what it holds (a drag between lanes), never for a
// program to reach into its tree.
template <class T> T *findIn(AnyNodeRef &node) {
  if (!stateOf(node).fVisible)
    return nullptr;
  if (T *hit = node.get<T>())
    return hit;
  T *found = nullptr;
  node.forEachChild([&](AnyNodeRef &child) {
    if (found == nullptr)
      found = findIn<T>(child);
  });
  return found;
}
template <class T, class N>
  requires std::derived_from<N, T>
T *findTyped(N &node) {
  return node.fState.fVisible ? static_cast<T *>(&node) : nullptr;
}
template <class T, class N>
  requires(!std::derived_from<N, T>)
T *findTyped(N &node);
template <class T> T *findTyped(AnyNodeRef &node) { return findIn<T>(node); }
template <class T, class N>
  requires(!std::derived_from<N, T>)
T *findTyped(N &node) {
  if (!stateOf(node).fVisible)
    return nullptr;
  T *found = nullptr;
  eachChild(node, [&](auto &child) {
    if (found == nullptr)
      found = findTyped<T>(child);
  });
  return found;
}
// Every node of type T under a node (itself included), shown or not, in
// their order, each given to f -- as findNode finds one: for a widget's own
// rules over the marked parts of what it holds (an album's cells shrunk).
template <class T, class F> void eachIn(AnyNodeRef &node, F &f) {
  if (T *hit = node.get<T>()) {
    f(*hit);
    return;
  }
  node.forEachChild([&](AnyNodeRef &child) { eachIn<T>(child, f); });
}
template <class T, class N, class F>
  requires std::derived_from<N, T>
void eachTyped(N &node, F &f) {
  f(static_cast<T &>(node));
}
template <class T, class F> void eachTyped(AnyNodeRef &node, F &f) { eachIn<T>(node, f); }
template <class T, class N, class F>
  requires(!std::derived_from<N, T>)
void eachTyped(N &node, F &f) {
  eachChild(node, [&](auto &child) { eachTyped<T>(child, f); });
}
template <class T, class N, class F> void forEachNode(N &root, F &&f) {
  if constexpr (kErasedWalks) {
    AnyNodeRef seen = AnyNodeRef::of(root);
    eachIn<T>(seen, f);
  } else {
    eachTyped<T>(root, f);
  }
}
template <class T, class N> [[nodiscard]] T *findNode(N &root) {
  if constexpr (kErasedWalks) {
    AnyNodeRef seen = AnyNodeRef::of(root);
    return findIn<T>(seen);
  } else {
    return findTyped<T>(root);
  }
}

// Any node, as the walks made once see it: its State, its children, and its
// hooks through its table -- what a walk asks of a typed node, asked here.
class ErasedNode {
public:
  ErasedNode(void *node, const NodeOps *ops) : fState(ops->fState(node)), fNode(node), fOps(ops) {}
  explicit ErasedNode(AnyNodeRef &ref) : ErasedNode(ref.node(), &sceneOps(ref)) {}

  State &fState;

  template <class F> void forEachChild(F &&f) { AnyNodeRef(fNode, &fOps->core).forEachChild(f); }
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
  void measure(const skia::SkRect &box) { fOps->fMeasure(fNode, box); }
  void layoutChildren() { fOps->fLayoutChildren(fNode); }
#endif
  void update(double now) { fOps->fTick(fNode, now); }
  [[nodiscard]] bool settling() const { return fOps->core.fSettling(fNode); }
  void applyNodeStyle(const Style &style, bool active) { fOps->fApplyNodeStyle(fNode, style, active); }
  [[nodiscard]] bool hoverChangesAppearance() const { return fOps->fHoverChangesAppearance(fNode); }
  [[nodiscard]] bool acceptsInput() const { return fOps->fAcceptsInput(fNode); }
  [[nodiscard]] bool focusable() const { return fOps->fFocusable(fNode); }
  [[nodiscard]] bool takesFocusOnPress() const { return fOps->fTakesFocusOnPress(fNode); }
  [[nodiscard]] bool focusChangesAppearance() const { return fOps->fFocusChangesAppearance(fNode); }
  [[nodiscard]] Semantics semantics() const { return fOps->fSemantics(fNode); }
  [[nodiscard]] Look look() const { return fOps->fLook(fNode); }
  [[nodiscard]] std::optional<float> scrollOffset() const { return fOps->fScrollOffset(fNode); }
  void setScrollOffset(float offset) { fOps->fSetScrollOffset(fNode, offset); }
  // Its own drawing, through `p`: whether it draws so.
  bool drawThrough(skiff::paint::ErasedPainter &p, float alpha) { return fOps->fDrawThrough(fNode, p, alpha); }
  void onFocusChanged(bool focused) { fOps->fOnFocusChanged(fNode, focused); }
  [[nodiscard]] bool onClick(float x, float y) { return fOps->fOnClick(fNode, x, y); }
  // Node's: only a node without one of its own has its defaults run seen so.
  [[nodiscard]] bool onScroll(float) { return false; }

  void *fNode;
  const NodeOps *fOps;
};

#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
// Defined in the scene module, once. Previously kNodeOps<T> made the whole
// layout algorithm for every node in every walks_* subtree. Its geometry,
// autosizing, placement and invalidation all use State and child refs; only
// measure and layoutChildren require the node's original type.
inline void layoutShared(void *node, const NodeOps *ops, const skia::SkRect &box) {
  ErasedNode seen(node, ops);
  detail::layoutNode(seen, box);
}

namespace detail {
// Keep the paint type in the specialization: the application specializes
// ProgramPaint and its scope/under/over hooks must still be selected where
// the application's nodes are instantiated, rather than in skiff itself.
template <class Hooks> struct SharedDrawNode : ErasedNode {
  explicit SharedDrawNode(AnyNodeRef &node) : ErasedNode(node) {}
  void drawSelf(Hooks &hooks, skia::SkCanvas *canvas, float alpha) {
    fOps->fDrawSelf(fNode, hooks, canvas, alpha);
  }
};
}
template <class T> void drawDefaultShared(T &node, Painting &painting, skia::SkCanvas *canvas, float alpha) {
  auto ref = AnyNodeRef::of(node);
  detail::SharedDrawNode<detail::PaintOf<T>> seen(ref);
  detail::drawNode(seen, painting, canvas, alpha);
}
#endif

inline StyleSubject styleSubject(ErasedNode &node, float viewportWidth) {
  return node.fOps->fStyleSubject(node.fNode, viewportWidth);
}
inline NodeKind typeOf(ErasedNode &node) { return node.fOps->core.fKind; }
template <class F> bool childAt(ErasedNode &node, std::uint32_t place, F &&f) {
  return node.fOps->core.fChildAt(node.fNode, place, const_cast<void *>(static_cast<const void *>(std::addressof(f))), +[](void *context, AnyNodeRef &child) {
    (*static_cast<std::remove_reference_t<F> *>(context))(child);
  });
}
namespace walk {
inline bool pollsEachFrame(const ErasedNode &node) { return node.fOps->fPolls(node.fNode); }
inline double ownWakeAt(const ErasedNode &node) { return node.fOps->fOwnWakeAt(node.fNode); }
inline void damageForHover(ErasedNode &node) { node.fOps->fHoverDamage(node.fNode); }
template <class When>
void pointerTo(ErasedNode &node, const When &when, const PointerEvent &input, PointerReply &reply) {
  node.fOps->fPointer.deliver(node.fNode, node.fOps, when, input, reply);
}
template <class When>
void KeyDelivery::operator()(ErasedNode &node, const When &when, const KeyEvent &input, Reply &reply) const {
  node.fOps->fKey.deliver(node.fNode, node.fOps, when, input, reply);
}
template <class When>
void TextDelivery::operator()(ErasedNode &node, const When &when, const TextEvent &input, Reply &reply) const {
  node.fOps->fText.deliver(node.fNode, node.fOps, when, input, reply);
}
template <class When>
void SemanticDelivery::operator()(ErasedNode &node, const When &when, const SemanticAction &input,
                                  Reply &reply) const {
  node.fOps->fSemantic.deliver(node.fNode, node.fOps, when, input, reply);
}
} // namespace walk

#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
template <class Handler, class P, class E, class R>
void sharedDefault(void *node, const void *ops, const void *event, R &reply) {
  ErasedNode seen(node, static_cast<const NodeOps *>(ops));
  Handler::defaults(seen, P{}, *static_cast<const E *>(event), reply);
}
#endif
template <class D, class Ops, class F> decltype(auto) walkErased(void *node, const Ops *ops, F &&f) {
  ErasedNode erased(node, ops);
  return f(erased);
}
#ifdef SKIFF_SHARED_DEBUG_ALGORITHMS
// The erased walks do not depend on the importing application. Emit their
// callbacks once in this module, rather than again in every walks_* unit.
const Walks kDebugWalks = kSharedWalks<void>;
#endif
template <class T, class F> decltype(auto) walkOn(void *node, F &&f) {
  if constexpr (kErasedWalks) {
    ErasedNode erased(node, &kNodeOps<T>);
    return f(erased);
  } else {
    return f(*static_cast<T *>(node));
  }
}

// A child seen through its table, moved as it is -- its bounds, the box it
// was laid out in, and all under it -- as a typed one is. A scrolled list
// moves its rows this way without laying them out; a row only marked for a
// new layout instead stayed where it was for a frame while the list went on
// as if it had moved, and every list outside a release build jumped.
inline void shiftSubtree(AnyNodeRef &node, float dx, float dy) {
  State &state = stateOf(node);
  state.fBounds.offset(dx, dy);
  state.fLastConstraint.offset(dx, dy);
  state.fPicture = nullptr;  // as above: made where it was
  node.forEachChild([&](AnyNodeRef &child) { shiftSubtree(child, dx, dy); });
}
// Outside a release build, a node laid out by what holds it -- a dialog its
// content, a slide-over its pages -- goes through its table too, as a walk's
// child does: not the typed layout, which made the node's whole subtree
// wherever its holder was, past the program's own boundaries.
template <class N> void layout(N &child, const skia::SkRect &parentBox) {
  if constexpr (kErasedWalks && std::derived_from<N, Node>) {
    AnyNodeRef seen = AnyNodeRef::of(child);
    layout(seen, parentBox);
  } else {
    detail::layoutNode(child, parentBox);
  }
}
template <class N> void draw(N &child, Painting &painting, skia::SkCanvas *canvas, float alpha) {
  if constexpr (kErasedWalks && std::derived_from<N, Node>) {
    AnyNodeRef seen = AnyNodeRef::of(child);
    draw(seen, painting, canvas, alpha);
  } else {
    child.draw(painting, canvas, alpha);
  }
}

// A child's own type, whether seen as itself or through an AnyNode.
template <class N> [[nodiscard]] NodeKind typeOf(N &) { return kindOf<N>(); }
template <class N>
  requires(kSeenThrough<N> || kSeenAsBase<N>)
[[nodiscard]] NodeKind typeOf(N &child) {
  return AnyNodeRef::of(child).kind();
}
[[nodiscard]] inline NodeKind typeOf(AnyNodeRef &child) { return child.kind(); }
inline State &stateOf(AnyNodeRef child) { return sceneOps(child).fState(child.node()); }
inline State &stateOf(AnyNode &node) { return stateOf(node.ref()); }
inline State &stateOf(const AnyNode &node) { return stateOf(node.ref()); }
template <class N>
  requires(kSeenThrough<N> || kSeenAsBase<N>)
State &stateOf(N &child) {
  return stateOf(AnyNodeRef::of(child));
}

// ---- a laid-out tree, told ---------------------------------------------
//
// What a tree came to after layout, one row a node, for a program or a test
// to print where something is not where it should be: what the node is, the
// named view it is in (SKIFF_NAMED), where it was laid out, how it is sized
// along each axis, and whether it sticks out of its parent (as an Overflow
// told). laidOutTree(root) gives the rows, laidOutText(root) the rows as
// text, indented by depth.
struct LaidOutNode {
  std::size_t depth = 0;
  NodeKind kind;
  // The innermost named view it is in -- itself, where it is one (names).
  std::string_view view;
  bool names = false;
  skia::SkRect bounds = skia::SkRect::MakeEmpty();
  Axes relative;
  Axes autoSize;
  Axes grow;
  bool visible = true;
  bool outOfFlow = false;
  bool overflows = false;
  // How opaque it is drawn, as itself: below 1, said in the text.
  float alpha = 1.0f;
};
namespace detail {
template <class N>
  requires(kSeenThrough<N> || kSeenAsBase<N>)
void laidOutRows(N &node, std::size_t depth, std::string_view view, std::vector<LaidOutNode> &rows);
template <class N> void laidOutRows(N &node, std::size_t depth, std::string_view view, std::vector<LaidOutNode> &rows) {
  const State &state = stateOf(node);
  const bool names = state.fViewName != nullptr;
  const std::string_view in = names ? std::string_view(state.fViewName) : view;
  rows.push_back({.depth = depth, .kind = typeOf(node), .view = in, .names = names, .bounds = state.fBounds,
                  .relative = state.fRelativeSizeAxes, .autoSize = state.fAutoSizeAxes, .grow = state.fGrowAxes,
                  .visible = state.visible(), .outOfFlow = state.fOutOfFlow, .overflows = state.fOverflowTold,
                  .alpha = state.fAlpha});
  eachChild(node, [&](auto &child) { laidOutRows(child, depth + 1, in, rows); });
}
// Held as itself in a part, seen through: one row, its content's.
template <class N>
  requires(kSeenThrough<N> || kSeenAsBase<N>)
void laidOutRows(N &node, std::size_t depth, std::string_view view, std::vector<LaidOutNode> &rows) {
  AnyNodeRef seen = AnyNodeRef::of(node);
  laidOutRows(seen, depth, view, rows);
}
inline std::string_view axesText(Axes axes) {
  constexpr std::array<std::string_view, 4> kText{"-", "x", "y", "xy"};
  return kText[(axes.template has<axis::x>() ? 1 : 0) + (axes.template has<axis::y>() ? 2 : 0)];
}
} // namespace detail
template <class N> [[nodiscard]] std::vector<LaidOutNode> laidOutTree(N &root) {
  std::vector<LaidOutNode> rows;
  detail::laidOutRows(root, 0, {}, rows);
  return rows;
}
// One row as text: "Kind [view] x,y w×h relative auto grow", and what else
// it is -- hidden, out of flow, sticking out.
[[nodiscard]] inline std::string laidOutLine(const LaidOutNode &row) {
  return std::format("{:{}}{}{}{}{} at {},{} {}x{} relative {} auto {} grow {}{}{}{}{}", "", row.depth * 2, row.kind.name,
                     row.view.empty() ? "" : (row.names ? " [" : " (in "), row.view,
                     row.view.empty() ? "" : (row.names ? "]" : ")"), row.bounds.fLeft, row.bounds.fTop,
                     row.bounds.width(), row.bounds.height(), detail::axesText(row.relative), detail::axesText(row.autoSize),
                     detail::axesText(row.grow), row.visible ? "" : " hidden", row.outOfFlow ? " out-of-flow" : "",
                     row.alpha < 1.0f ? std::format(" alpha {:.2f}", row.alpha) : std::string(), row.overflows ? " OVERFLOWS" : "");
}
template <class N> [[nodiscard]] std::string laidOutText(N &root) {
  return laidOutTree(root) | std::views::transform([](const LaidOutNode &row) { return laidOutLine(row) + "\n"; }) | std::views::join |
         std::ranges::to<std::string>();
}
inline void layout(AnyNodeRef &child, const skia::SkRect &box) {
  sceneOps(child).fLayout(child.node(), box);
}
inline void draw(AnyNodeRef &child, Painting &painting, skia::SkCanvas *canvas, float alpha) {
  sceneOps(child).fDraw(child.node(), painting, canvas, alpha);
}

namespace walk {
inline void update(AnyNodeRef &c, UpdateContext &context, StyleResolver r,
                   const Style *s, bool all) {
  sceneOps(c).fWalks->fUpdate(c.node(), &sceneOps(c), context, r, s, all);
}
inline bool markDirty(AnyNodeRef &c) { return sceneOps(c).fWalks->fMarkDirty(c.node(), &sceneOps(c)); }
inline skia::SkRect collectDamage(AnyNodeRef &c, bool drawn) {
  return sceneOps(c).fWalks->fCollectDamage(c.node(), &sceneOps(c), drawn);
}
inline bool hasDamage(AnyNodeRef &c) { return sceneOps(c).fWalks->fHasDamage(c.node(), &sceneOps(c)); }
inline void hover(AnyNodeRef &c, float x, float y, bool visible, StyleResolver r,
                  float width, bool hoveredAbove) {
  sceneOps(c).fWalks->fHover(c.node(), &sceneOps(c), x, y, visible, r, width, hoveredAbove);
}
inline bool hitPath(AnyNodeRef &c, float x, float y, Path &path) {
  return sceneOps(c).fWalks->fHitPath(c.node(), &sceneOps(c), x, y, path);
}
inline bool findPath(AnyNodeRef &c, NodeId id, Path &path) {
  return sceneOps(c).fWalks->fFindPath(c.node(), &sceneOps(c), id, path);
}
inline NodeId idAt(AnyNodeRef &c, const Path &path, std::size_t at) {
  return sceneOps(c).fWalks->fIdAt(c.node(), &sceneOps(c), path, at);
}
inline void routePointer(AnyNodeRef &c, const Path &path, std::size_t at,
                         const PointerEvent &e, PointerReply &reply,
                         Routed &routed, bool targetOnly) {
  sceneOps(c).fWalks->fRoutePointer(c.node(), &sceneOps(c), path, at, e, reply, routed, targetOnly);
}
inline void routeKey(AnyNodeRef &c, const Path &path, std::size_t at,
                     const KeyEvent &e, Reply &reply) {
  sceneOps(c).fWalks->fRouteKey(c.node(), &sceneOps(c), path, at, e, reply);
}
inline void routeText(AnyNodeRef &c, const Path &path, std::size_t at,
                      const TextEvent &e, Reply &reply) {
  sceneOps(c).fWalks->fRouteText(c.node(), &sceneOps(c), path, at, e, reply);
}
inline void routeSemantic(AnyNodeRef &c, const Path &path, std::size_t at,
                          const SemanticAction &e, Reply &reply) {
  sceneOps(c).fWalks->fRouteSemantic(c.node(), &sceneOps(c), path, at, e, reply);
}
inline std::optional<NodeInfo> info(AnyNodeRef &c, NodeId id) {
  return sceneOps(c).fWalks->fInfo(c.node(), &sceneOps(c), id);
}
inline bool focusChanged(AnyNodeRef &c, NodeId id, bool focused, StyleResolver r,
                         float width) {
  return sceneOps(c).fWalks->fFocusChanged(c.node(), &sceneOps(c), id, focused, r, width);
}
inline void collectSemantics(AnyNodeRef &c, std::vector<Semantics> &out,
                             int parent, NodeId focused) {
  sceneOps(c).fWalks->fCollectSemantics(c.node(), &sceneOps(c), out, parent, focused);
}
inline void collectFocusable(AnyNodeRef &c, std::vector<NodeId> &out) {
  sceneOps(c).fWalks->fCollectFocusable(c.node(), &sceneOps(c), out);
}
inline bool animating(AnyNodeRef &c) { return sceneOps(c).fWalks->fAnimating(c.node(), &sceneOps(c)); }
inline double wakeAt(AnyNodeRef &c) { return sceneOps(c).fWalks->fWakeAt(c.node(), &sceneOps(c)); }
inline void damageOver(AnyNodeRef &c, NodeId below, const skia::SkRect &rect, bool &passed, skia::SkRect &damage) {
  sceneOps(c).fWalks->fDamageOver(c.node(), &sceneOps(c), below, rect, passed, damage);
}
inline bool clickPath(AnyNodeRef &c, const Path &path, std::size_t at, float x,
                      float y) {
  return sceneOps(c).fWalks->fClickPath(c.node(), &sceneOps(c), path, at, x, y);
}
} // namespace walk

// A node built with a spec applied.
//
//   nodes::Text title = make<nodes::Text>({.fillX = true}, "Log in", 20.0f);
template <class T, class... Args>
  requires std::derived_from<T, Node>
[[nodiscard]] T make(const Spec &spec, Args &&...args) {
  T node(std::forward<Args>(args)...);
  node.fState.apply(spec);
  return node;
}
// The same for a class template, its arguments deduced from the
// constructor's: make<widgets::Button>({}, "Log in", logIn).
template <template <class...> class T, class... Args>
[[nodiscard]] auto make(const Spec &spec, Args &&...args) {
  using Made = decltype(T(std::forward<Args>(args)...));
  return make<Made>(spec, std::forward<Args>(args)...);
}
// A spec applied to a node already built.
template <class T>
  requires std::derived_from<T, Node>
[[nodiscard]] T placed(const Spec &spec, T node) {
  node.fState.apply(spec);
  return node;
}

// ---- the scene ---------------------------------------------------------------

class SceneHandle;

// A tree with a root of type Root, and what a tree needs at its root: which
// node has focus, which holds the pointer, where the pointer is, and the
// viewport. The root is built in place and the scene does not move.
//
// A frame is: update(now), layoutIfNeeded(viewport), draw(canvas),
// finishFrame().
template <class Root> class Scene {
public:
  template <class... Args>
  explicit Scene(std::in_place_t, Args &&...args)
      : fRoot(std::forward<Args>(args)...) {
    work::roots().insert(this->rootState().fId);
  }
  // Explicit class instantiation also instantiates this constructor.
  // An immovable root must use the in-place overload instead.
  explicit Scene(Root root)
    requires std::move_constructible<Root>
      : fRoot(std::move(root)) { work::roots().insert(this->rootState().fId); }
  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  [[nodiscard]] Root &root() noexcept { return fRoot; }
  // The root as the walks see it: outside a release build, through its
  // table -- and through what it holds, where it is only a holder (Held).
  [[nodiscard]] decltype(auto) walked() noexcept {
    if constexpr (kErasedWalks) {
      fSeen.emplace(AnyNodeRef::of(fRoot));
      return (*fSeen);
    } else {
      return (fRoot);
    }
  }
  [[nodiscard]] State &rootState() noexcept {
    if constexpr (kErasedWalks)
      return stateOf(this->walked());
    else
      return fRoot.fState;
  }
  [[nodiscard]] const Root &root() const noexcept { return fRoot; }
  [[nodiscard]] State &state() noexcept { return this->rootState(); }

  template <class Theme> void setStyleSheet() {
    this->rootState().template setStyleSheet<Theme>();
    this->restyleDirty();
  }
  void clearStyleSheet() {
    this->rootState().clearStyleSheet();
    this->restyleDirty();
  }

  void update(double nowMs) {
    fNowMs = nowMs;
    // Only where something ticks or changed -- a node made under a list that
    // changed is come to there -- and everywhere only where one made is still
    // not found: a new row made the whole tree ticked.
    const bool full = work::disabled() || fWalkedGeneration != work::fullGeneration();
    work::tickingFull() = full;
    UpdateContext context{nowMs, fViewport.width(), false};
    walk::update(this->walked(), context, {}, nullptr, false);
    fTickAnimating = context.fAnimating;
    auto &born = work::births();
    const bool lost = std::ranges::any_of(born, [](NodeId id) {
      const work::Entry *here = work::entry(id);
      return here != nullptr && here->unseen;
    });
    born.clear();
    if (!full && lost) {
      work::tickingFull() = true;
      UpdateContext again{nowMs, fViewport.width(), false};
      walk::update(this->walked(), again, {}, nullptr, false);
      fTickAnimating = fTickAnimating || again.fAnimating;
      work::births().clear();
    }
    fTickedBorn = work::bornGeneration();
    auto &pressed = hostWork().pressed;
    for (const void *held : std::exchange(hostWork().due, {})) {
      const auto *one = static_cast<const State *>(held);
      Path way;
      if (walk::findPath(this->walked(), one->fId, way))
        pressed.push_back(std::move(way));
    }
  }

  // Whether this frame's walks go everywhere: where a full walk was asked
  // for since this scene's last, or the work set is off.
  void beginWalks() {
    fWalkGeneration = work::fullGeneration();
    work::walkingFull() = work::disabled() || fWalkedGeneration != fWalkGeneration;
    work::pending().insert(this->rootState().fId);
  }
  // The window's box, as it was last laid out in.
  [[nodiscard]] const skia::SkRect &viewport() const noexcept { return fViewport; }
  bool layoutIfNeeded(const skia::SkRect &viewport) {
    // A window with no room yet -- a phone at its start, its surface not
    // sized: nothing of it is shown, and nothing is laid out in it. Laid
    // out at a width of nothing, every row of a drawer was squeezed past
    // its icon and its text, and each was told as sticking out -- no
    // overflow anyone could see. Laid out when the window has its size.
    if (viewport.isEmpty())
      return false;
    scrollMoves().clear();
    this->beginWalks();
    // The keys, where a node asked for them as it read the model.
    if (const NodeId wanted = std::exchange(work::focusWanted(), 0); wanted != 0)
      this->focus(wanted);
    const bool viewportChanged = viewport != fViewport;
    if (viewportChanged) {
      work::walkingFull() = true;
    }
    fViewport = viewport;
    if (viewportChanged) {
      // Width-constrained selectors are media queries: resolved before
      // layout, so their declarations take part in this pass.
      UpdateContext context{fNowMs, viewport.width(), false, false};
      walk::update(this->walked(), context, {}, nullptr, true);
    }
    const bool dirty = walk::markDirty(this->walked());
    if (!dirty && !viewportChanged) {
      return false;
    }
    ++work::layoutPass();
    scene::layout(fRoot, viewport);
    return true;
  }

  // Drawn with the program's paint: the host's, kept as long as it draws.
  void draw(detail::PaintOf<Root> &paint, skia::SkCanvas *canvas) { scene::draw(fRoot, paint, canvas, 1.0f); }

  [[nodiscard]] FrameResult finishFrame() {
    // Where the field typed into is, for the host to tell the system.
    hostWork().typingAt.reset();
    if (hostWork().typing && fFocus != 0) {
      if (const std::optional<NodeInfo> about = walk::info(this->walked(), fFocus)) {
        hostWork().typingAt = about->fDrawn;
      }
    }
    std::erase_if(detail::liveBackdrops(), [](const auto &one) { return work::entry(one.first) == nullptr; });
    damageFound().clear();
    work::damageOffset() = {};
    work::damageClip().reset();
    ++work::frameNumber();
    skia::SkRect damage = walk::collectDamage(this->walked(), true);
    // Walked as this frame asked: what was marked is done.
    if (work::walkingFull()) {
      fWalkedGeneration = fWalkGeneration;
    }
    work::pending().erase(this->rootState().fId);
    work::settle();
    // What the tick found still moving, marked for the next: after the
    // damage walk, which would clear it.
    for (const NodeId id : std::exchange(work::moving(), {})) {
      if (work::entry(id) != nullptr) {  // not one gone since
        work::mark(id);
      }
    }
    const skia::SkRect &bounds = this->rootState().fBounds;
    if (!bounds.isEmpty() && !damage.isEmpty() && !damage.intersect(bounds)) {
      damage = skia::SkRect::MakeEmpty();
    }
    // What is drawn over each moved view, repainted: its pixels would move.
    std::vector<ScrollMove> moves = std::exchange(scrollMoves(), {});
    for (const ScrollMove &move : moves) {
      bool passed = false;
      skia::SkRect over = skia::SkRect::MakeEmpty();
      walk::damageOver(this->walked(), move.node, move.rect, passed, over);
      if (!over.isEmpty() && over.intersect(move.rect)) {
        damage = joined(damage, over);
        damageFound().push_back(over);
      }
    }
    std::vector<skia::SkRect> pieces = std::exchange(damageFound(), {});
    if (!bounds.isEmpty()) {
      for (skia::SkRect &one : pieces) {
        if (!one.intersect(bounds)) {
          one.setEmpty();
        }
      }
    }
    // Another frame where anything the tick came to still moves, or the
    // walk down the ticking set finds one -- or where nodes were made since
    // the last tick: the next goes everywhere and finds them. Made after it
    // (a message come, its row made as the bindings read the model), one
    // that comes in moving was in no ticking set yet, the frame said it was
    // done, and it stayed where it began -- unseen -- until something else
    // woke the window.
    const bool born = work::bornGeneration() != fTickedBorn;
    return {std::move(moves), damage, born || fTickAnimating || walk::animating(this->walked()), walk::wakeAt(this->walked()),
            fewRects(std::move(pieces))};
  }
  [[nodiscard]] bool hasFrameWork() {
    if (!work::disabled() && fWalkedGeneration == work::fullGeneration() && work::pending().empty()) {
      return walk::animating(this->walked());
    }
    work::walkingFull() = true;
    return walk::markDirty(this->walked()) || walk::hasDamage(this->walked()) ||
           walk::animating(this->walked());
  }

  // -- input
  bool dispatchPointer(const PointerEvent &input) {
    notePointerHeld(input);
    const skia::SkPoint at = where(input);
    spl::visit(spl::overloaded{[&](const pointer::move &) {
                            this->setHover(at.fX, at.fY);
                          },
                          [](const auto &) {}},
               input);
    const bool ending = ends(input);
    Path path;
    bool found = false;
    if (fCapture != 0) {
      found = walk::findPath(this->walked(), fCapture, path);
      if (!found) {
        fCapture = 0; // it went away
      }
    }
    if (!found && ending && fDown != 0) {
      path.clear();
      found = walk::findPath(this->walked(), fDown, path);
      if (!found) {
        fDown = 0;
      }
    }
    if (!found) {
      path.clear();
      found = walk::hitPath(this->walked(), at.fX, at.fY, path);
    }
    const bool press = spl::visit(
        spl::overloaded{[](const pointer::down &) { return true; },
                   [](const auto &) { return false; }},
        input);
    if (!found) {
      // A press on nothing leaves the focus where it is: typing still goes
      // where it went.
      if (ending) {
        fCapture = 0;
        fDown = 0;
      }
      return false;
    }
    const NodeId target = walk::idAt(this->walked(), path, 0);
    if (press) {
      fDown = target;
    }
    PointerReply reply;
    reply.fTarget = target;
    reply.fCaptured = fCapture != 0;
    Routed routed;
    const auto carried = startCarry(fRoot);
    walk::routePointer(this->walked(), path, 0, input, reply, routed, false, carried);
    // A click the target did not take: to the nodes above it, in turn.
    if (reply.fClickAbove) {
      // Said by the target where it is laid out; the walk down from the root
      // takes the point where it is on the screen: the target's and its
      // ancestors' shifts put back -- a click in a scrolled list moved twice.
      const work::Offset shifted = work::drawnOffset(target);
      (void)walk::clickPath(this->walked(), path, 0, reply.fClickAbove->fX + shifted.x, reply.fClickAbove->fY + shifted.y, carried);
    }

    if (routed.fReleaseRequest || ending) {
      fCapture = 0;
    } else if (routed.fCaptureRequest != 0) {
      // A container claiming a drag cancels the control where the press
      // began: it must not stay armed and activate after scrolling. So with
      // one that held the pointer before it -- a text being selected: it
      // would never hear the release, and go on selecting after it.
      const NodeId before = fCapture != 0 ? fCapture : fDown;
      if (before != 0 && before != routed.fCaptureRequest) {
        Path down;
        if (walk::findPath(this->walked(), before, down)) {
          PointerReply cancelled;
          cancelled.fTarget = before;
          Routed ignored;
          walk::routePointer(this->walked(), down, 0,
                             PointerEvent{pointer::cancel{at.fX, at.fY}},
                             cancelled, ignored, true);
        }
      }
      fDown = 0;
      fCapture = routed.fCaptureRequest;
    }
    if (ending) {
      fDown = 0;
    }
    if (reply.fSuppressHover) {
      const float away = -std::numeric_limits<float>::infinity();
      this->setHover(away, away);
    }
    // A press that moves the focus is taken by what it moved it to: a text
    // field pressed answers nothing itself, and has the press all the same.
    const NodeId focusBefore = fFocus;
    if (routed.fFocusRequest != 0) {
      this->focus(routed.fFocusRequest);
    } else if (routed.fTargetDelivered && press && routed.fTargetFocusable) {
      this->focus(target);
      focusVisible() = false;
    }
    this->notePress(path);
    this->restyleDirty();
    return reply.fHandled || (press && fFocus != focusBefore);
  }

  // A click at a point: onClick of the front-most node there that takes
  // input, then of each node above it until one takes it.
  bool click(float x, float y) {
    Path path;
    if (!walk::hitPath(this->walked(), x, y, path)) {
      return false;
    }
    const bool taken = walk::clickPath(this->walked(), path, 0, x, y, startCarry(fRoot));
    this->notePress(path);
    this->restyleDirty();
    return taken;
  }
  // A press its node answered (pressLater) outside the scene's dispatch --
  // an edit given to a field by an engine that edits it (skiff.mirror's
  // typed) -- kept for the host as a dispatched one's is.
  void pressedAlong(const Path &path) { this->notePress(path); }
  // A click on the node at a path, and on each above it until one takes it:
  // where the tree is laid out elsewhere (HostWork::laidOutElsewhere), the
  // engine that laid it out saying which node was pressed.
  bool clickAlong(const Path &path) {
    const bool taken = walk::clickPath(this->walked(), path, 0, 0.0f, 0.0f, startCarry(fRoot));
    this->notePress(path);
    this->restyleDirty();
    return taken;
  }

  bool dispatchKey(const KeyEvent &input) {
    // Tab moves the focus; Shift+Tab back.
    const std::optional<bool> tab = spl::visit(
        spl::overloaded{[](const key::down &press) -> std::optional<bool> {
                     // Ctrl+Tab is not the focus's: a program's, as
                     // tdesktop's to the next chat.
                     if (press.key == keys::kTab && !press.modifiers.has<modifier::control>()) {
                       return press.modifiers.has<modifier::shift>();
                     }
                     return std::nullopt;
                   },
                   [](const auto &) -> std::optional<bool> {
                     return std::nullopt;
                   }},
        input);
    if (tab) {
      this->focusNext(*tab);
      return fFocus != 0;
    }
    Path path;
    if (!this->focusPath(path)) {
      return false;
    }
    Reply reply;
    reply.fTarget = fFocus;
    walk::routeKey(this->walked(), path, 0, input, reply, startCarry(fRoot));
    if (reply.fMoveFocus) {
      focusVisible() = true;
      this->focusNextWithin(reply.fFocusScope, *reply.fMoveFocus);
    }
    this->notePress(path);
    this->restyleDirty();
    return reply.fHandled;
  }

  bool dispatchText(const TextEvent &input) {
    Path path;
    if (!this->focusPath(path)) {
      return false;
    }
    Reply reply;
    reply.fTarget = fFocus;
    walk::routeText(this->walked(), path, 0, input, reply, startCarry(fRoot));
    this->notePress(path);
    this->restyleDirty();
    return reply.fHandled;
  }

  bool dispatchSemantic(NodeId id, const SemanticAction &action) {
    const std::optional<NodeInfo> about = walk::info(this->walked(), id);
    Path path;
    if (!about || !about->fVisible || about->fDisabled ||
        !walk::findPath(this->walked(), id, path)) {
      return false;
    }
    Reply reply;
    reply.fTarget = id;
    walk::routeSemantic(this->walked(), path, 0, action, reply, startCarry(fRoot));
    if (reply.fRequestFocus) {
      this->focus(id);
    }
    this->notePress(path);
    this->restyleDirty();
    return reply.fHandled;
  }

  [[nodiscard]] std::vector<Semantics> semanticsTree() {
    std::vector<Semantics> out;
    walk::collectSemantics(this->walked(), out, -1, fFocus);
    return out;
  }

  // The pointer's shape: that of the node held, else of the node under the
  // pointer or the nearest above it with a shape of its own.
  [[nodiscard]] Cursor cursor() {
    Path path;
    if (fCapture != 0) {
      if (!walk::findPath(this->walked(), fCapture, path)) {
        return cursor::arrow{};
      }
    } else if (!fHoverSeen || !walk::hitPath(this->walked(), fHoverX, fHoverY, path)) {
      return cursor::arrow{};
    }
    if (const std::optional<Cursor> shape = walk::cursorOnPath(this->walked(), path, 0)) {
      return *shape;
    }
    return cursor::arrow{};
  }

  // Gives a node focus, or takes it from all with 0.
  void focus(NodeId id) {
    std::optional<NodeInfo> now;
    if (id != 0) {
      now = walk::info(this->walked(), id);
      if (!now || !now->fFocusable || now->fDisabled || !now->fVisible) {
        id = 0;
        now.reset();
      }
    }
    if (fFocus == id) {
      return;
    }
    const NodeId previous = fFocus;
    fFocus = id;
    hostWork().typing = now && now->fTakesText;
    if (previous != 0) {
      (void)walk::focusChanged(this->walked(), previous, false, {}, fViewport.width());
    }
    if (id != 0) {
      (void)walk::focusChanged(this->walked(), id, true, {}, fViewport.width());
    }
    this->restyleDirty();
  }
  void focus(const Node &node) { this->focus(node.id()); }
  void clearFocus() { this->focus(0); }
  // Everything drawn again: what draws it changed under all of it (the
  // program's paint, say) -- walked whole at the next frame, all damaged.
  void repaintAll() {
    this->rootState().markDamaged();
    ++work::fullGeneration();
  }
  [[nodiscard]] NodeId focusedId() {
    Path ignored;
    return this->focusPath(ignored) ? fFocus : 0;
  }
  [[nodiscard]] bool focusedTakesText() {
    const NodeId id = this->focusedId();
    const std::optional<NodeInfo> about =
        id != 0 ? walk::info(this->walked(), id) : std::nullopt;
    return about && about->fTakesText;
  }
  [[nodiscard]] NodeId capturedId() {
    if (fCapture != 0) {
      Path ignored;
      if (!walk::findPath(this->walked(), fCapture, ignored)) {
        fCapture = 0;
      }
    }
    return fCapture;
  }

  [[nodiscard]] std::vector<NodeId> focusableIds() {
    std::vector<NodeId> out;
    walk::collectFocusable(this->walked(), out);
    return out;
  }

  // As focusNext, among the focusable nodes inside `scope` only.
  void focusNextWithin(NodeId scope, bool backwards) {
    Path inside;
    if (!walk::findPath(this->walked(), scope, inside)) {
      return;
    }
    std::vector<NodeId> nodes;
    for (const NodeId id : this->focusableIds()) {
      Path path;
      if (id != scope && walk::findPath(this->walked(), id, path) &&
          // path begins with inside (std::ranges::starts_with, which the
          // libstdc++ C++23 builds use does not have yet)
          std::ranges::mismatch(inside, path).in1 == inside.end()) {
        nodes.push_back(id);
      }
    }
    if (nodes.empty()) {
      return;
    }
    const auto found = std::ranges::find(nodes, fFocus);
    std::size_t index = found == nodes.end()
                            ? (backwards ? nodes.size() - 1 : 0)
                            : static_cast<std::size_t>(found - nodes.begin());
    if (found != nodes.end()) {
      index = backwards ? (index + nodes.size() - 1) % nodes.size()
                        : (index + 1) % nodes.size();
    }
    this->focus(nodes[index]);
  }

  void focusNext(bool backwards) {
    const std::vector<NodeId> nodes = this->focusableIds();
    if (nodes.empty()) {
      this->focus(0);
      return;
    }
    const auto found = std::ranges::find(nodes, fFocus);
    std::size_t index = found == nodes.end()
                            ? (backwards ? nodes.size() - 1 : 0)
                            : static_cast<std::size_t>(found - nodes.begin());
    if (found != nodes.end()) {
      index = backwards ? (index + nodes.size() - 1) % nodes.size()
                        : (index + 1) % nodes.size();
    }
    this->focus(nodes[index]);
  }

  void setHover(float x, float y) {
    if (x == fHoverX && y == fHoverY && fHoverSeen) {
      return;
    }
    fHoverX = x;
    fHoverY = y;
    fHoverSeen = true;
    walk::hover(this->walked(), x, y, true, {}, fViewport.width(), false);
    this->restyleDirty();
  }

  [[nodiscard]] SceneHandle handle();

private:
  // Styles whose inputs changed, applied now, so what a handler sees next is
  // current.
  void restyleDirty() {
    work::walkingFull() = work::disabled() || fWalkedGeneration != work::fullGeneration();
    UpdateContext context{fNowMs, fViewport.width(), false, false};
    walk::update(this->walked(), context, {}, nullptr, false);
  }

  // A press the dispatch along `path` was answered by (pressLater): kept, by
  // the path to the node that answered -- the routed one, a node above it
  // that the click went on to, or one pressed on another's behalf -- for the
  // program to deliver.
  void notePress(const Path &path) {
    const auto *pressed = static_cast<const State *>(std::exchange(hostWork().pressedNow, nullptr));
    if (pressed == nullptr) {
      return;
    }
    Path way = path;
    while (!way.empty() && walk::idAt(this->walked(), way, 0) != pressed->fId) {
      way.pop_back();
    }
    // Not on the way the event went -- a node pressed on another's behalf,
    // a page's back arrow for Esc: found where it is.
    if (walk::idAt(this->walked(), way, 0) != pressed->fId && !walk::findPath(this->walked(), pressed->fId, way)) {
      return;
    }
    auto &all = hostWork().pressed;
    if (all.empty() || all.back() != way) {
      all.push_back(std::move(way));
    }
  }
  [[nodiscard]] bool focusPath(Path &path) {
    if (fFocus == 0) {
      return false;
    }
    const std::optional<NodeInfo> about = walk::info(this->walked(), fFocus);
    if (!about || about->fDisabled || !about->fVisible ||
        !walk::findPath(this->walked(), fFocus, path)) {
      this->focus(0);
      return false;
    }
    return true;
  }

  Root fRoot;
  // The root as last seen by walked(): a reference to it, or to what it holds.
  std::optional<AnyNodeRef> fSeen;
  // The full walks' count this scene last walked everything at, and the one
  // its current frame goes by.
  std::uint64_t fWalkedGeneration = 0;
  std::uint64_t fWalkGeneration = 0;
  // The births this scene's tick last went everywhere for: at first, those
  // made with it (its root, made just before), which its first frame lays
  // out -- not births a frame has to ask another for.
  std::uint64_t fTickedBorn = work::bornGeneration();
  NodeId fCapture = 0;
  // Whether the last tick came to something still moving.
  bool fTickAnimating = false;
  NodeId fDown = 0;
  NodeId fFocus = 0;
  float fHoverX = 0.0f, fHoverY = 0.0f;
  bool fHoverSeen = false;
  double fNowMs = 0.0;
  skia::SkRect fViewport = skia::SkRect::MakeEmpty();
  std::shared_ptr<int> fAlive = std::make_shared<int>(0);

  friend class SceneHandle;
};

// A scene of any root type, not owned, inert once the scene is destroyed.
class SceneHandle {
public:
  SceneHandle() = default;
  template <class Root>
  explicit SceneHandle(Scene<Root> &scene)
      : fAlive(scene.fAlive), fScene(&scene), fOps(&kOps<Root>) {}

  [[nodiscard]] bool alive() const noexcept {
    return fScene != nullptr && !fAlive.expired();
  }
  [[nodiscard]] explicit operator bool() const noexcept {
    return this->alive();
  }
  [[nodiscard]] bool operator==(const SceneHandle &other) const noexcept {
    return fScene == other.fScene;
  }

  bool pointer(const PointerEvent &input) const {
    return this->alive() && fOps->fPointer(fScene, input);
  }
  bool key(const KeyEvent &input) const {
    return this->alive() && fOps->fKey(fScene, input);
  }
  bool text(const TextEvent &input) const {
    return this->alive() && fOps->fText(fScene, input);
  }
  bool semantic(NodeId id, const SemanticAction &action) const {
    return this->alive() && fOps->fSemantic(fScene, id, action);
  }
  [[nodiscard]] std::vector<Semantics> semantics() const {
    return this->alive() ? fOps->fSemantics(fScene)
                         : std::vector<Semantics>{};
  }
  [[nodiscard]] NodeId captured() const {
    return this->alive() ? fOps->fCaptured(fScene) : 0;
  }
  [[nodiscard]] NodeId focused() const {
    return this->alive() ? fOps->fFocused(fScene) : 0;
  }
  void focus(NodeId id) const {
    if (this->alive()) {
      fOps->fFocus(fScene, id);
    }
  }
  [[nodiscard]] std::vector<NodeId> focusable() const {
    return this->alive() ? fOps->fFocusable(fScene) : std::vector<NodeId>{};
  }

private:
  struct Ops {
    bool (*fPointer)(void *, const PointerEvent &);
    bool (*fKey)(void *, const KeyEvent &);
    bool (*fText)(void *, const TextEvent &);
    bool (*fSemantic)(void *, NodeId, const SemanticAction &);
    std::vector<Semantics> (*fSemantics)(void *);
    NodeId (*fCaptured)(void *);
    NodeId (*fFocused)(void *);
    void (*fFocus)(void *, NodeId);
    std::vector<NodeId> (*fFocusable)(void *);
  };
  template <class Root> [[nodiscard]] static Scene<Root> &as(void *scene) {
    return *static_cast<Scene<Root> *>(scene);
  }
  template <class Root>
  static constexpr Ops kOps{
      +[](void *s, const PointerEvent &e) {
        return as<Root>(s).dispatchPointer(e);
      },
      +[](void *s, const KeyEvent &e) { return as<Root>(s).dispatchKey(e); },
      +[](void *s, const TextEvent &e) { return as<Root>(s).dispatchText(e); },
      +[](void *s, NodeId id, const SemanticAction &e) {
        return as<Root>(s).dispatchSemantic(id, e);
      },
      +[](void *s) { return as<Root>(s).semanticsTree(); },
      +[](void *s) { return as<Root>(s).capturedId(); },
      +[](void *s) { return as<Root>(s).focusedId(); },
      +[](void *s, NodeId id) { as<Root>(s).focus(id); },
      +[](void *s) { return as<Root>(s).focusableIds(); },
  };

  std::weak_ptr<int> fAlive;
  void *fScene = nullptr;
  const Ops *fOps = nullptr;
};

template <class Root> SceneHandle Scene<Root>::handle() {
  return SceneHandle(*this);
}

// Routes between scenes. Layers are back-to-front; the first modal layer
// found owns input even when nothing in it handles it. Covered layers keep
// their hover and focus.
class InputRouter {
public:
  struct Layer {
    SceneHandle fScene;
    bool fModal = false;
  };

  void setLayers(std::span<const Layer> layers) {
    // A capture held by a scene that is gone from the stack, or covered by a
    // modal, is cancelled.
    SceneHandle captured = fCaptured.alive() ? fCaptured : SceneHandle{};
    if (!captured) {
      for (const Layer &layer : fLayers) {
        if (layer.fScene.captured() != 0) {
          captured = layer.fScene;
          break;
        }
      }
    }
    if (!captured) {
      for (const Layer &layer : layers) {
        if (layer.fScene.captured() != 0) {
          captured = layer.fScene;
          break;
        }
      }
    }
    if (captured) {
      const auto at = std::ranges::find_if(layers, [&](const Layer &layer) {
        return layer.fScene == captured;
      });
      const bool covered =
          at != layers.end() &&
          std::ranges::any_of(std::ranges::subrange(at + 1, layers.end()),
                              [](const Layer &layer) {
                                return layer.fModal && layer.fScene.alive();
                              });
      if (at == layers.end() || covered) {
        captured.pointer(pointer::cancel{});
        captured = {};
      }
    }
    fCaptured = captured;
    fLayers.assign(layers.begin(), layers.end());
  }

  bool pointer(const PointerEvent &input) {
    notePointerHeld(input);
    if (fCaptured.alive()) {
      const bool handled = fCaptured.pointer(input);
      if (fCaptured.captured() == 0) {
        fCaptured = {};
      }
      return handled;
    }
    fCaptured = {};
    const bool press = spl::visit(
        spl::overloaded{[](const pointer::down &) { return true; },
                   [](const auto &) { return false; }},
        input);
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      const bool handled = it->fScene.pointer(input);
      if (it->fScene.captured() != 0) {
        fCaptured = it->fScene;
        if (press) {
          this->ownFocus(it->fScene);
        }
        return true;
      }
      if (handled) {
        if (press) {
          this->ownFocus(it->fScene);
        }
        return true;
      }
      if (it->fModal) {
        return true;
      }
    }
    return false;
  }

  bool key(const KeyEvent &input) {
    const std::optional<bool> tab = spl::visit(
        spl::overloaded{[](const key::down &press) -> std::optional<bool> {
                     // Ctrl+Tab is not the focus's: a program's, as
                     // tdesktop's to the next chat.
                     if (press.key == keys::kTab && !press.modifiers.has<modifier::control>()) {
                       return press.modifiers.has<modifier::shift>();
                     }
                     return std::nullopt;
                   },
                   [](const auto &) -> std::optional<bool> {
                     return std::nullopt;
                   }},
        input);
    if (tab) {
      focusVisible() = true;
      return this->focusNext(*tab);
    }
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      if (it->fScene.key(input)) {
        return true;
      }
      if (it->fModal) {
        return true;
      }
    }
    return false;
  }

  bool text(const TextEvent &input) {
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      if (it->fScene.focused() != 0 && it->fScene.text(input)) {
        return true;
      }
      if (it->fModal) {
        return true;
      }
    }
    return false;
  }

  bool semantic(NodeId id, const SemanticAction &action) {
    const bool focusing = spl::visit(
        spl::overloaded{[](const semantic_action::focus &) { return true; },
                   [](const auto &) { return false; }},
        action);
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      if (it->fScene.semantic(id, action)) {
        if (focusing) {
          this->ownFocus(it->fScene);
        }
        return true;
      }
      if (it->fModal) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::vector<Semantics> semantics() const {
    std::vector<Semantics> out;
    for (std::size_t i = this->firstActiveLayer(); i < fLayers.size(); ++i) {
      auto tree = fLayers[i].fScene.semantics();
      const int base = static_cast<int>(out.size());
      for (Semantics &node : tree) {
        if (node.fParent >= 0) {
          node.fParent += base;
        }
        out.push_back(std::move(node));
      }
    }
    return out;
  }

private:
  // Focus is unique inside the active scope; a covered scope keeps its own.
  void ownFocus(const SceneHandle &owner) {
    if (owner.focused() == 0) {
      return;
    }
    for (std::size_t i = this->firstActiveLayer(); i < fLayers.size(); ++i) {
      const SceneHandle &scene = fLayers[i].fScene;
      if (scene.alive() && !(scene == owner)) {
        scene.focus(0);
      }
    }
  }

  [[nodiscard]] std::size_t firstActiveLayer() const {
    for (std::size_t i = fLayers.size(); i > 0; --i) {
      if (fLayers[i - 1].fModal && fLayers[i - 1].fScene.alive()) {
        return i - 1;
      }
    }
    return 0;
  }

  bool focusNext(bool backwards) {
    std::vector<std::pair<SceneHandle, NodeId>> nodes;
    for (std::size_t i = this->firstActiveLayer(); i < fLayers.size(); ++i) {
      const SceneHandle &scene = fLayers[i].fScene;
      for (NodeId id : scene.focusable()) {
        nodes.emplace_back(scene, id);
      }
    }
    if (nodes.empty()) {
      return false;
    }
    const auto focused = std::ranges::find_if(nodes, [](const auto &entry) {
      return entry.first.focused() == entry.second;
    });
    std::size_t index =
        focused == nodes.end()
            ? (backwards ? nodes.size() - 1 : 0)
            : static_cast<std::size_t>(focused - nodes.begin());
    if (focused != nodes.end()) {
      index = backwards ? (index + nodes.size() - 1) % nodes.size()
                        : (index + 1) % nodes.size();
    }
    for (std::size_t i = this->firstActiveLayer(); i < fLayers.size(); ++i) {
      const SceneHandle &scene = fLayers[i].fScene;
      if (scene.alive() && !(scene == nodes[index].first)) {
        scene.focus(0);
      }
    }
    nodes[index].first.focus(nodes[index].second);
    return true;
  }

  std::vector<Layer> fLayers;
  SceneHandle fCaptured;
};

} // namespace skiff::scene
