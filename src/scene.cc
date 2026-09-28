export module skiff.scene;

import std;
import skia;
import skiff.paint;

// A retained scene whose type is its tree.
//
// A node is any type with a skiff::scene::State member named fState. It does
// not derive from anything: what it can do -- draw itself, lay out its
// children, take a click, describe itself to a screen reader -- it says by
// having the member function, and the walks here find it with `requires`.
// Its children are its own members, held by value: other nodes, a
// std::variant or std::optional of nodes, a std::unique_ptr to one, or any
// range of them (std::vector<Row>). AnyNode holds a node of any type by value
// for the places where the type cannot be written down.
//
// Nothing in the tree points upwards. A node that changes marks its own
// State; the walks from the root find what changed, lay out what has to be,
// and gather what has to be repainted, clipped by the masking ancestors on
// the way down. Focus, pointer capture and the node a press began on are
// node ids held by the Scene, and routing an event turns an id into a path of
// child positions and recurses along it: the call stack is the path, so a
// capture handler runs on the way down and a bubble handler on the way back.
// A node that went away is simply not found again.
//
// The layout model is osu!framework's: anchors and origins, relative and
// automatic sizes, flows, margins and padding, transforms with easing.
export namespace skiff::scene {

// What a widget does when it was not given anything to do: its action's type
// when none is passed. A widget can tell it apart at compile time -- a toggle
// that does nothing is not offered to input at all.
struct NoAction {
  template <class... Args>
  constexpr void operator()(Args &&...) const noexcept {}
};
template <class Action>
inline constexpr bool kActs =
    !std::same_as<std::remove_cvref_t<Action>, NoAction>;

// ---- geometry -------------------------------------------------------------

enum class Axes : std::uint8_t { kNone, kX, kY, kBoth };

[[nodiscard]] inline bool hasX(Axes a) noexcept {
  return a == Axes::kX || a == Axes::kBoth;
}
[[nodiscard]] inline bool hasY(Axes a) noexcept {
  return a == Axes::kY || a == Axes::kBoth;
}

[[nodiscard]] inline Axes axesUnion(Axes a, Axes b) noexcept {
  const bool x = hasX(a) || hasX(b);
  const bool y = hasY(a) || hasY(b);
  if (x && y)
    return Axes::kBoth;
  if (x)
    return Axes::kX;
  if (y)
    return Axes::kY;
  return Axes::kNone;
}

// The nine positions a node can be anchored to, as in the framework.
enum class Anchor : std::uint8_t {
  kTopLeft,
  kTopCentre,
  kTopRight,
  kCentreLeft,
  kCentre,
  kCentreRight,
  kBottomLeft,
  kBottomCentre,
  kBottomRight
};

[[nodiscard]] inline float anchorX(Anchor a) noexcept {
  switch (a) {
  case Anchor::kTopCentre:
  case Anchor::kCentre:
  case Anchor::kBottomCentre:
    return 0.5f;
  case Anchor::kTopRight:
  case Anchor::kCentreRight:
  case Anchor::kBottomRight:
    return 1.0f;
  default:
    return 0.0f;
  }
}

[[nodiscard]] inline float anchorY(Anchor a) noexcept {
  switch (a) {
  case Anchor::kCentreLeft:
  case Anchor::kCentre:
  case Anchor::kCentreRight:
    return 0.5f;
  case Anchor::kBottomLeft:
  case Anchor::kBottomCentre:
  case Anchor::kBottomRight:
    return 1.0f;
  default:
    return 0.0f;
  }
}

// Where something sits across the axis it is being laid out along.
enum class Align : std::uint8_t { kStart, kMiddle, kEnd };

struct Margin {
  float fTop = 0.0f, fRight = 0.0f, fBottom = 0.0f, fLeft = 0.0f;

  [[nodiscard]] static constexpr Margin all(float v) { return {v, v, v, v}; }
  [[nodiscard]] static constexpr Margin horizontal(float v) {
    return {0, v, 0, v};
  }
  [[nodiscard]] static constexpr Margin vertical(float v) {
    return {v, 0, v, 0};
  }
  [[nodiscard]] constexpr float totalX() const noexcept {
    return fLeft + fRight;
  }
  [[nodiscard]] constexpr float totalY() const noexcept {
    return fTop + fBottom;
  }
  constexpr bool operator==(const Margin &) const = default;
};

struct FrameResult {
  skia::SkRect fDamage = skia::SkRect::MakeEmpty();
  bool fWantsAnotherFrame = false;
};

// The box a thing of that size occupies when its `origin` point is put on the
// `anchor` point of `parent`, offset by dx and dy. What layout does for a
// node, available on its own for what is not a node: a glyph inside a
// control, a bar inside a row.
[[nodiscard]] inline skia::SkRect
anchoredBox(const skia::SkRect &parent, float width, float height,
            Anchor anchor, Anchor origin, float dx = 0.0f, float dy = 0.0f) {
  const float ax = parent.fLeft + parent.width() * anchorX(anchor);
  const float ay = parent.fTop + parent.height() * anchorY(anchor);
  return skia::SkRect::MakeXYWH(ax - width * anchorX(origin) + dx,
                                ay - height * anchorY(origin) + dy, width,
                                height);
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

// How many nodes a frame walked and how many it drew. Counters rather than
// anything cleverer: "why does a frame cost what it costs" has been answered
// by guessing twice now, and guessing is slower than counting.
inline std::uint64_t &visitedCount() {
  static std::uint64_t count = 0;
  return count;
}
inline std::uint64_t &drawnCount() {
  static std::uint64_t count = 0;
  return count;
}

// ---- transforms -----------------------------------------------------------

enum class Easing : std::uint8_t { kNone, kOut, kOutQuint, kOutElasticHalf };

[[nodiscard]] inline float ease(Easing e, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (e) {
  case Easing::kOut:
    return 1.0f - (1.0f - t) * (1.0f - t);
  case Easing::kOutQuint:
    return skiff::paint::outQuint(t);
  case Easing::kOutElasticHalf:
    return skiff::paint::outElasticHalf(t);
  case Easing::kNone:
    break;
  }
  return t;
}

// What a transform animates. A closed set: no allocation and no dispatch per
// property.
enum class Property : std::uint8_t { kAlpha, kX, kY, kWidth, kHeight, kScale };

struct Transform {
  Property fProperty = Property::kAlpha;
  float fFrom = 0.0f;
  float fTo = 0.0f;
  double fStartMs = 0.0;
  double fEndMs = 0.0;
  Easing fEasing = Easing::kNone;
};

// ---- style identity ---------------------------------------------------------

namespace detail {
struct StyleKey {};
template <class Tag> inline constexpr StyleKey styleRoleKey{};
template <class NodeT> inline constexpr StyleKey styleNodeKey{};
template <template <class...> class Template>
inline constexpr StyleKey styleTemplateKey{};

// The class template a node type is a specialisation of, when it is one of
// type parameters only: what lets a rule for widgets::Button match every
// Button<Action>.
template <class T> struct TemplateKeyOf {
  static constexpr const StyleKey *value = nullptr;
};
template <template <class...> class Template, class... Args>
struct TemplateKeyOf<Template<Args...>> {
  static constexpr const StyleKey *value = &styleTemplateKey<Template>;
};
} // namespace detail

// A role is a type-safe name shared by a node and a selector. Applications
// define empty tag types (`struct PrimaryButton;`) and never coordinate
// string spellings.
class StyleRole {
public:
  template <class Tag> [[nodiscard]] static constexpr StyleRole of() {
    return StyleRole(&detail::styleRoleKey<Tag>);
  }

  [[nodiscard]] bool operator==(const StyleRole &) const noexcept = default;

private:
  explicit constexpr StyleRole(const detail::StyleKey *key) : fKey(key) {}
  const detail::StyleKey *fKey;
};

template <class Tag> inline constexpr StyleRole role = StyleRole::of<Tag>();

// ---- the specification ----------------------------------------------------

// Every layout input a node has, gathered into one aggregate so that a node
// can be written down rather than assembled field by field. A field left out
// is not written at all: "zero" and "not mentioned" are different things,
// and `{}` leaves a node as its constructor made it.
//
//   place        anchor and origin at once
//   fill         relative size on both axes at 1.0 -- "as big as my parent"
//   fillX/fillY  the same on one axis
struct Spec {
  std::optional<Anchor> place{};
  std::optional<Anchor> anchor{};
  std::optional<Anchor> origin{};
  std::optional<float> x{};
  std::optional<float> y{};

  bool fill = false;
  bool fillX = false;
  bool fillY = false;
  std::optional<float> width{};
  std::optional<float> height{};
  std::optional<Axes> relativeSize{};
  std::optional<Axes> autoSize{};
  std::optional<Axes> grow{};
  std::optional<float> minWidth{}, maxWidth{};
  std::optional<float> minHeight{}, maxHeight{};
  std::optional<Align> alignSelf{};
  std::optional<float> depth{};

  // Plain, not optional, so that `.padding = {2, 6, 2, 6}` compiles. An
  // all-zero margin reads as "not mentioned".
  Margin margin{};
  Margin padding{};

  std::optional<float> cornerRadius{};
  std::optional<bool> masking{};
  std::optional<float> scale{};
  std::optional<float> alpha{};
  std::optional<bool> visible{};

  std::vector<StyleRole> roles{};
  std::optional<bool> selected{};
  std::optional<bool> disabled{};
};

// ---- declarative styling -------------------------------------------------

enum class StyleState : std::uint8_t {
  kNone = 0,
  kHover = 1 << 0,
  kSelected = 1 << 1,
  kDisabled = 1 << 2,
  kFocus = 1 << 3,
};

[[nodiscard]] constexpr StyleState operator|(StyleState a,
                                             StyleState b) noexcept {
  return static_cast<StyleState>(static_cast<std::uint8_t>(a) |
                                 static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr bool hasState(StyleState states,
                                      StyleState state) noexcept {
  return (static_cast<std::uint8_t>(states) &
          static_cast<std::uint8_t>(state)) != 0;
}

// The declarations every node understands, plus the inheritable visual ones
// Text and Box read. Margins are optional here: a rule must be able to clear
// a margin a less specific rule supplied.
struct Style {
  std::optional<Anchor> anchor{}, origin{};
  std::optional<float> x{}, y{};
  std::optional<float> width{}, height{};
  std::optional<Axes> relativeSize{}, autoSize{}, grow{};
  std::optional<float> minWidth{}, maxWidth{}, minHeight{}, maxHeight{};
  std::optional<Align> alignSelf{};
  std::optional<float> depth{};
  std::optional<Margin> margin{}, padding{};
  std::optional<float> cornerRadius{};
  std::optional<bool> masking{};
  std::optional<float> scale{}, alpha{};
  std::optional<bool> visible{};

  // Foreground colour, font size and weight inherit; background does not.
  std::optional<skia::SkColor> colour{};
  std::optional<skia::SkColor> backgroundColour{};
  std::optional<float> fontSize{};
  std::optional<bool> fontBold{};

  // A state change animates position, size, scale and alpha. Zero or absent
  // is immediate.
  std::optional<double> transitionMs{};
  std::optional<Easing> transitionEasing{};

  void overlay(const Style &other) {
#define SKIFF_OVERLAY(member)                                                  \
  if (other.member)                                                            \
  member = other.member
    SKIFF_OVERLAY(anchor);
    SKIFF_OVERLAY(origin);
    SKIFF_OVERLAY(x);
    SKIFF_OVERLAY(y);
    SKIFF_OVERLAY(width);
    SKIFF_OVERLAY(height);
    SKIFF_OVERLAY(relativeSize);
    SKIFF_OVERLAY(autoSize);
    SKIFF_OVERLAY(grow);
    SKIFF_OVERLAY(minWidth);
    SKIFF_OVERLAY(maxWidth);
    SKIFF_OVERLAY(minHeight);
    SKIFF_OVERLAY(maxHeight);
    SKIFF_OVERLAY(alignSelf);
    SKIFF_OVERLAY(depth);
    SKIFF_OVERLAY(margin);
    SKIFF_OVERLAY(padding);
    SKIFF_OVERLAY(cornerRadius);
    SKIFF_OVERLAY(masking);
    SKIFF_OVERLAY(scale);
    SKIFF_OVERLAY(alpha);
    SKIFF_OVERLAY(visible);
    SKIFF_OVERLAY(colour);
    SKIFF_OVERLAY(backgroundColour);
    SKIFF_OVERLAY(fontSize);
    SKIFF_OVERLAY(fontBold);
    SKIFF_OVERLAY(transitionMs);
    SKIFF_OVERLAY(transitionEasing);
#undef SKIFF_OVERLAY
  }
};

// What a rule is matched against: which type the node is (and which class
// template, when it is a specialisation of one), its roles and its states.
// Plain data, built by the walks, so a sheet never needs the node itself.
struct StyleSubject {
  const detail::StyleKey *fType = nullptr;
  const detail::StyleKey *fTemplate = nullptr;
  std::span<const StyleRole> fRoles;
  StyleState fStates = StyleState::kNone;
  float fViewportWidth = 0.0f;

  [[nodiscard]] bool has(StyleRole role) const noexcept {
    return std::ranges::find(fRoles, role) != fRoles.end();
  }
};

// A selector subject that is a class template rather than a type:
// select<widgets::Button>() matches every Button<Action>.
template <template <class...> class Template> struct OfTemplate {};
// A selector subject that is any node.
struct Anything {};

template <class Subject, class... Roles> class Selector {
public:
  [[nodiscard]] constexpr Selector when(this Selector self,
                                          StyleState state) {
    self.fStates = self.fStates | state;
    return self;
  }
  [[nodiscard]] constexpr Selector atLeastWidth(this Selector self,
                                                 float width) {
    self.fMinViewportWidth = width;
    return self;
  }
  [[nodiscard]] constexpr Selector atMostWidth(this Selector self,
                                                float width) {
    self.fMaxViewportWidth = width;
    return self;
  }

private:
  StyleState fStates = StyleState::kNone;
  std::optional<float> fMinViewportWidth{}, fMaxViewportWidth{};

  template <class, class...> friend struct StyleRule;
};

template <class Subject, class... Roles>
[[nodiscard]] constexpr Selector<Subject, Roles...> select() {
  return {};
}
template <template <class...> class Template, class... Roles>
[[nodiscard]] constexpr Selector<OfTemplate<Template>, Roles...> select() {
  return {};
}
template <class... Roles>
[[nodiscard]] constexpr Selector<Anything, Roles...> selectAny() {
  return {};
}

namespace detail {
template <class T> struct SubjectKey {
  [[nodiscard]] static bool matches(const StyleSubject &subject) {
    return subject.fType == &styleNodeKey<T>;
  }
};
template <template <class...> class Template>
struct SubjectKey<OfTemplate<Template>> {
  [[nodiscard]] static bool matches(const StyleSubject &subject) {
    return subject.fTemplate == &styleTemplateKey<Template>;
  }
};
template <> struct SubjectKey<Anything> {
  [[nodiscard]] static bool matches(const StyleSubject &) { return true; }
};
} // namespace detail

template <class Subject, class... Roles> struct StyleRule {
  Selector<Subject, Roles...> fSelector;
  Style fStyle;

  [[nodiscard]] bool matchesSubject(const StyleSubject &node) const {
    if (!detail::SubjectKey<Subject>::matches(node)) {
      return false;
    }
    if (!(node.has(role<Roles>) && ...)) {
      return false;
    }
    return (!fSelector.fMinViewportWidth ||
            node.fViewportWidth >= *fSelector.fMinViewportWidth) &&
           (!fSelector.fMaxViewportWidth ||
            node.fViewportWidth <= *fSelector.fMaxViewportWidth);
  }
  [[nodiscard]] bool matches(const StyleSubject &node) const {
    const StyleState wanted = fSelector.fStates;
    for (StyleState one : {StyleState::kHover, StyleState::kFocus,
                           StyleState::kSelected, StyleState::kDisabled}) {
      if (hasState(wanted, one) && !hasState(node.fStates, one)) {
        return false;
      }
    }
    return this->matchesSubject(node);
  }
  [[nodiscard]] bool usesState(const StyleSubject &node,
                               StyleState state) const {
    return hasState(fSelector.fStates, state) && this->matchesSubject(node);
  }
};

template <class... Rules> class StaticStyleSheet {
public:
  constexpr StaticStyleSheet() requires(sizeof...(Rules) == 0) = default;
  explicit constexpr StaticStyleSheet(std::tuple<Rules...> rules)
      : fRules(std::move(rules)) {}

  template <class Subject, class... Roles>
  [[nodiscard]] constexpr auto rule(this StaticStyleSheet self,
                                    Selector<Subject, Roles...> selector,
                                    Style style) {
    using Rule = StyleRule<Subject, Roles...>;
    Rule rule{selector, style};
    return StaticStyleSheet<Rules..., Rule>{std::tuple_cat(
        std::move(self.fRules), std::tuple<Rule>{std::move(rule)})};
  }

  // Source order is the cascade: a later matching rule wins.
  [[nodiscard]] Style resolve(const StyleSubject &node) const {
    Style out;
    std::apply(
        [&](const auto &...rules) {
          ((rules.matches(node) ? out.overlay(rules.fStyle) : void()), ...);
        },
        fRules);
    return out;
  }
  [[nodiscard]] bool usesState(const StyleSubject &node,
                               StyleState state) const {
    return std::apply(
        [&](const auto &...rules) {
          return (rules.usesState(node, state) || ... || false);
        },
        fRules);
  }

private:
  std::tuple<Rules...> fRules;
};

[[nodiscard]] constexpr StaticStyleSheet<> makeStyleSheet() { return {}; }

// A sheet as the walks keep it: two functions instantiated for one theme.
struct StyleResolver {
  Style (*fResolve)(const StyleSubject &) = nullptr;
  bool (*fUsesState)(const StyleSubject &, StyleState) = nullptr;

  [[nodiscard]] explicit operator bool() const noexcept {
    return fResolve != nullptr;
  }
  template <class Theme> [[nodiscard]] static StyleResolver of() {
    return {+[](const StyleSubject &node) {
              return Theme::styles.resolve(node);
            },
            +[](const StyleSubject &node, StyleState state) {
              return Theme::styles.usesState(node, state);
            }};
  }
};

// ---- input and accessibility --------------------------------------------

enum class EventPhase : std::uint8_t { kCapture, kTarget, kBubble };
enum class PointerAction : std::uint8_t {
  kMove,
  kDown,
  kUp,
  kCancel,
  kScroll
};
enum class Key : std::uint16_t {
  kUnknown,
  kTab,
  kEnter,
  kSpace,
  kEscape,
  kLeft,
  kRight,
  kUp,
  kDown,
  kHome,
  kEnd,
  kBackspace,
  kDelete
};

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
    if (!fDragging) {
      fTarget = std::clamp(fTarget, fLo, fHi);
    }
  }
  [[nodiscard]] float offset() const noexcept { return fOffset; }
  [[nodiscard]] float target() const noexcept { return fTarget; }
  [[nodiscard]] bool dragging() const noexcept { return fDragging; }
  // Still moving on its own: a flick running out, or an end springing back.
  [[nodiscard]] bool moving() const noexcept {
    return fFlinging || std::abs(fOffset - fTarget) > 0.05f;
  }

  void jumpTo(float value) {
    fOffset = fTarget = std::clamp(value, fLo, fHi);
    fFlinging = false;
    fVelocity = 0.0f;
  }
  void glideTo(float value) {
    fTarget = std::clamp(value, fLo, fHi);
    fFlinging = false;
    fVelocity = 0.0f;
  }
  void wheel(float ticks, float step) {
    fFlinging = false;
    fVelocity = 0.0f;
    fTarget = std::clamp(fTarget - ticks * step, fLo, fHi);
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
    const double dt = std::min(dtMs, 64.0);
    if (fDragging) {
      return false; // the finger owns it
    }
    if (fFlinging) {
      // Friction per millisecond, so thirty frames and two hundred agree on
      // how far a flick travels.
      fVelocity *= std::pow(kFriction, static_cast<float>(dt));
      fOffset += fVelocity * static_cast<float>(dt);
      if (fOffset < fLo || fOffset > fHi ||
          std::abs(fVelocity) < kMinVelocity) {
        fFlinging = false;
        fVelocity = 0.0f;
        fTarget = std::clamp(fOffset, fLo, fHi);
      } else {
        fTarget = fOffset;
      }
    } else {
      fOffset = paint::approach(fOffset, fTarget, tauMs, dt);
      if (std::abs(fOffset - fTarget) < 0.05f) {
        fOffset = fTarget;
      }
    }
    return fOffset != previous;
  }

  // How far a finger travels before this is a drag and not a tap.
  static constexpr float kSlop = 6.0f;

private:
  static constexpr float kFriction = 0.994f;
  static constexpr float kMinVelocity = 0.05f;
  static constexpr float kVelocityMix = 0.35f;
  static constexpr float kOverscroll = 0.4f;

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
};

// A node's id, which is what a scene keeps where it would have kept a
// pointer: a node that is gone is simply not found by it any more.
using NodeId = std::uint64_t;

struct PointerEvent {
  PointerAction fAction = PointerAction::kMove;
  EventPhase fPhase = EventPhase::kTarget;
  float fX = 0.0f, fY = 0.0f;
  float fScrollX = 0.0f, fScrollY = 0.0f;
  int fButton = 0;
  // The node the event is for, and the one it is being delivered to now: an
  // ancestor handling a child's event in the bubble phase tells which child
  // by comparing ids.
  NodeId fTarget = 0;
  NodeId fCurrentTarget = 0;
  // Whether some node already holds the pointer: a scroll container deciding
  // to take a drag has to know whether something else took it first.
  bool fCaptured = false;
  bool fHandled = false;
  bool fCapturePointer = false;
  bool fReleasePointer = false;
  bool fRequestFocus = false;
  bool fDeferClick = false;
  bool fSuppressHover = false;

  void handle() noexcept { fHandled = true; }
  void capturePointer() noexcept { fCapturePointer = true; }
  void releasePointer() noexcept { fReleasePointer = true; }
  void requestFocus() noexcept { fRequestFocus = true; }
  // A gesture-owning ancestor uses this during capture: the target may arm a
  // tap, but must not activate until the ancestor has had a chance to turn
  // the same press into a drag.
  void deferClick() noexcept { fDeferClick = true; }
  // Touch scrolling is not pointing: controls under a finger that is deciding
  // between a tap and a drag do not hover.
  void suppressHover() noexcept { fSuppressHover = true; }
};

struct KeyEvent {
  Key fKey = Key::kUnknown;
  EventPhase fPhase = EventPhase::kTarget;
  bool fPressed = true;
  bool fRepeat = false;
  bool fShift = false, fControl = false, fAlt = false, fSuper = false;
  NodeId fTarget = 0;
  NodeId fCurrentTarget = 0;
  bool fHandled = false;

  void handle() noexcept { fHandled = true; }
};

// Text and composition are distinct: an IME can replace its provisional
// range many times before committing it.
struct TextInputEvent {
  std::string_view fText;
  std::string_view fComposition;
  EventPhase fPhase = EventPhase::kTarget;
  NodeId fTarget = 0;
  NodeId fCurrentTarget = 0;
  int fSelectionStart = 0;
  int fSelectionLength = 0;
  bool fCommit = true;
  bool fHandled = false;

  void handle() noexcept { fHandled = true; }
};

enum class SemanticRole : std::uint8_t {
  kNone,
  kGroup,
  kButton,
  kText,
  kTextBox,
  kSlider,
  kToggle,
  kTab,
  kList,
  kListItem
};

enum class SemanticAction : std::uint8_t {
  kFocus,
  kActivate,
  kIncrement,
  kDecrement,
  kSetValue
};

struct SemanticActionEvent {
  SemanticAction fAction = SemanticAction::kActivate;
  float fValue = 0.0f;
  std::string_view fText;
  EventPhase fPhase = EventPhase::kTarget;
  NodeId fTarget = 0;
  NodeId fCurrentTarget = 0;
  bool fHandled = false;
  bool fRequestFocus = false;

  void handle() noexcept { fHandled = true; }
  void requestFocus() noexcept { fRequestFocus = true; }
};

struct Semantics {
  NodeId fId = 0;
  SemanticRole fRole = SemanticRole::kNone;
  std::string fLabel;
  std::string fValue;
  std::string fHint;
  bool fDisabled = false;
  bool fFocused = false;
  bool fSelected = false;
  skia::SkRect fBounds = skia::SkRect::MakeEmpty();
  int fParent = -1;
  std::vector<SemanticAction> fActions;
};

// Whether whatever holds focus is somewhere text is typed, told to the host
// when it changes: a phone has to raise an on-screen keyboard and sees key
// events, not what they are for. The host keeps what it passes and clears
// the hook before that goes away -- a pointer is kept here, not a copy.
struct TextFocusHook {
  void (*fCall)(void *target, bool text) = nullptr;
  void *fTarget = nullptr;
  explicit operator bool() const noexcept { return fCall != nullptr; }
  void operator()(bool text) const { fCall(fTarget, text); }
};
inline TextFocusHook &textFocusHook() {
  static TextFocusHook hook;
  return hook;
}
template <class Target>
  requires std::invocable<Target &, bool>
void setTextFocusHook(Target &target) {
  textFocusHook() = {+[](void *kept, bool text) {
                       std::invoke(*static_cast<Target *>(kept), text);
                     },
                     &target};
}
inline void clearTextFocusHook() { textFocusHook() = {}; }

// ---- the state every node has --------------------------------------------

// What every node is, beyond what its own type adds: its layout inputs and
// the box they produced, what it is animating, how it is styled, its flags,
// and what it has to repaint. A node holds one as `fState`.
//
// Changing a node goes through the setters here: they mark this State and
// nothing else, and the next frame's walks find the mark.
class State {
public:
  State() : fId(nextId()) {}
  // A node is one thing on the screen: copying it would make two with one
  // identity. Moving keeps the identity, so a node can be built and then
  // put where it lives.
  State(const State &) = delete;
  State &operator=(const State &) = delete;
  State(State &&) noexcept = default;
  State &operator=(State &&) noexcept = default;

  // -- layout inputs
  float fWidth = 0.0f, fHeight = 0.0f;  // absolute, or a fraction if relative
  Axes fRelativeSizeAxes = Axes::kNone; // size is a fraction of the parent
  Axes fAutoSizeAxes = Axes::kNone;     // size follows the children
  // Inside a flow, takes an equal share of what the other children leave
  // along the flow's axis.
  Axes fGrowAxes = Axes::kNone;
  // Bounds on the computed size. Zero means no limit, on the maximums.
  float fMinWidth = 0.0f, fMaxWidth = 0.0f;
  float fMinHeight = 0.0f, fMaxHeight = 0.0f;
  // Overrides the container's cross-axis alignment for this child alone.
  std::optional<Align> fAlignSelf{};
  // Drawn and hit-tested in this order within the parent, low first.
  float fDepth = 0.0f;
  Anchor fAnchor = Anchor::kTopLeft; // point in the parent to attach to
  Anchor fOrigin = Anchor::kTopLeft; // point in this node that lands there
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
  float fCornerRadius = 0.0f;
  bool fVisible = true;

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
  void setPosition(float x, float y) {
    if (x == fX && y == fY) {
      return;
    }
    fX = x;
    fY = y;
    this->invalidateLayout();
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
    this->markDamaged();
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
  }
  void clearStyleSheet() {
    if (!fStyleResolver) {
      return;
    }
    fStyleResolver = {};
    fStyleSheetChanged = true;
  }

  // Writes a spec. What it does not mention is left as it was.
  void apply(const Spec &spec) {
    const auto before = this->commonValues();
    if (spec.place) {
      fAnchor = *spec.place;
      fOrigin = *spec.place;
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
      relative = axesUnion(relative, Axes::kX);
      fWidth = 1.0f;
    }
    if (spec.fill || spec.fillY) {
      relative = axesUnion(relative, Axes::kY);
      fHeight = 1.0f;
    }
    if (spec.relativeSize || spec.fill || spec.fillX || spec.fillY) {
      fRelativeSizeAxes = relative;
    }
    if (spec.autoSize) {
      fAutoSizeAxes = *spec.autoSize;
    }
    if (spec.grow) {
      fGrowAxes = *spec.grow;
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
    if (spec.margin.totalX() != 0.0f || spec.margin.totalY() != 0.0f) {
      fMargin = spec.margin;
    }
    if (spec.padding.totalX() != 0.0f || spec.padding.totalY() != 0.0f) {
      fPadding = spec.padding;
    }
    if (spec.cornerRadius) {
      fCornerRadius = *spec.cornerRadius;
    }
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
    if ((hasX(fRelativeSizeAxes) && hasX(fAutoSizeAxes)) ||
        (hasY(fRelativeSizeAxes) && hasY(fAutoSizeAxes))) {
      std::println(std::cerr,
                   "[scene] relative and automatic sizing on the same axis");
      fAutoSizeAxes = Axes::kNone;
    }
    // A flow writes a grown child's size; anything else claiming the axis
    // would be overwritten every frame.
    if ((hasX(fGrowAxes) && (hasX(fRelativeSizeAxes) || hasX(fAutoSizeAxes))) ||
        (hasY(fGrowAxes) && (hasY(fRelativeSizeAxes) || hasY(fAutoSizeAxes)))) {
      std::println(std::cerr,
                   "[scene] growing and sized another way on the same axis");
      fGrowAxes = Axes::kNone;
    }
    const auto after = this->commonValues();
    if (!sameLayout(before, after)) {
      this->invalidateLayout();
    } else if (before != after) {
      this->markDamaged();
    }
  }

  // -- transforms
  void fadeTo(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kAlpha, fAlpha, target, durationMs, e);
  }
  void moveToX(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kX, fX, target, durationMs, e);
  }
  void moveToY(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kY, fY, target, durationMs, e);
  }
  void resizeWidthTo(float target, double durationMs,
                     Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kWidth, fWidth, target, durationMs, e);
  }
  void resizeHeightTo(float target, double durationMs,
                      Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kHeight, fHeight, target, durationMs, e);
  }
  void scaleTo(float target, double durationMs, Easing e = Easing::kOutQuint) {
    this->transformTo(Property::kScale, fScale, target, durationMs, e);
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
  }
  // Repaints where this node is and where it was drawn last.
  void markDamaged() { fDamaged = true; }

  // Placement by a container, during its layout: writes the position a flow
  // decided without counting as a change somebody made.
  void arrange(float x, float y, Anchor anchor = Anchor::kTopLeft,
               Anchor origin = Anchor::kTopLeft) {
    if (fX == x && fY == y && fAnchor == anchor && fOrigin == origin) {
      return;
    }
    fX = x;
    fY = y;
    fAnchor = anchor;
    fOrigin = origin;
    fLayoutValid = false;
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
    Anchor fAnchor = Anchor::kTopLeft;
    Anchor fOrigin = Anchor::kTopLeft;
    float fX = 0.0f, fY = 0.0f;
    float fWidth = 0.0f, fHeight = 0.0f;
    Axes fRelativeSize = Axes::kNone;
    Axes fAutoSize = Axes::kNone;
    Axes fGrow = Axes::kNone;
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

  [[nodiscard]] Common commonValues() const {
    return {fAnchor,     fOrigin,       fX,           fY,
            fWidth,      fHeight,       fRelativeSizeAxes,
            fAutoSizeAxes, fGrowAxes,   fMinWidth,    fMaxWidth,
            fMinHeight,  fMaxHeight,    fAlignSelf,   fDepth,
            fMargin,     fPadding,      fCornerRadius, fMasking,
            fScale,      fAlpha,        fVisible};
  }
  [[nodiscard]] static bool sameLayout(const Common &a,
                                       const Common &b) noexcept {
    return a.fAnchor == b.fAnchor && a.fOrigin == b.fOrigin && a.fX == b.fX &&
           a.fY == b.fY && a.fWidth == b.fWidth && a.fHeight == b.fHeight &&
           a.fRelativeSize == b.fRelativeSize && a.fAutoSize == b.fAutoSize &&
           a.fGrow == b.fGrow && a.fMinWidth == b.fMinWidth &&
           a.fMaxWidth == b.fMaxWidth && a.fMinHeight == b.fMinHeight &&
           a.fMaxHeight == b.fMaxHeight && a.fAlignSelf == b.fAlignSelf &&
           a.fMargin == b.fMargin && a.fPadding == b.fPadding &&
           a.fScale == b.fScale && a.fVisible == b.fVisible;
  }

  void restyle(bool animate) {
    fStyleDirty = true;
    fStyleAnimate = fStyleAnimate || animate;
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
#define SKIFF_REFRESH_BASE(declaration, member)                               \
  if (!fResolvedStyle.declaration) {                                         \
    fStyleBase.member = current.member;                                      \
  }
    SKIFF_REFRESH_BASE(anchor, fAnchor);
    SKIFF_REFRESH_BASE(origin, fOrigin);
    SKIFF_REFRESH_BASE(x, fX);
    SKIFF_REFRESH_BASE(y, fY);
    SKIFF_REFRESH_BASE(width, fWidth);
    SKIFF_REFRESH_BASE(height, fHeight);
    SKIFF_REFRESH_BASE(relativeSize, fRelativeSize);
    SKIFF_REFRESH_BASE(autoSize, fAutoSize);
    SKIFF_REFRESH_BASE(grow, fGrow);
    SKIFF_REFRESH_BASE(minWidth, fMinWidth);
    SKIFF_REFRESH_BASE(maxWidth, fMaxWidth);
    SKIFF_REFRESH_BASE(minHeight, fMinHeight);
    SKIFF_REFRESH_BASE(maxHeight, fMaxHeight);
    SKIFF_REFRESH_BASE(alignSelf, fAlignSelf);
    SKIFF_REFRESH_BASE(depth, fDepth);
    SKIFF_REFRESH_BASE(margin, fMargin);
    SKIFF_REFRESH_BASE(padding, fPadding);
    SKIFF_REFRESH_BASE(cornerRadius, fCornerRadius);
    SKIFF_REFRESH_BASE(masking, fMasking);
    SKIFF_REFRESH_BASE(scale, fScale);
    SKIFF_REFRESH_BASE(alpha, fAlpha);
    SKIFF_REFRESH_BASE(visible, fVisible);
#undef SKIFF_REFRESH_BASE

    Common target = current;
#define SKIFF_STYLE_TARGET(declaration, member)                               \
  if (style.declaration) {                                                    \
    target.member = *style.declaration;                                      \
  } else if (fResolvedStyle.declaration) {                                   \
    target.member = fStyleBase.member;                                       \
  }
    SKIFF_STYLE_TARGET(anchor, fAnchor);
    SKIFF_STYLE_TARGET(origin, fOrigin);
    SKIFF_STYLE_TARGET(x, fX);
    SKIFF_STYLE_TARGET(y, fY);
    SKIFF_STYLE_TARGET(width, fWidth);
    SKIFF_STYLE_TARGET(height, fHeight);
    SKIFF_STYLE_TARGET(relativeSize, fRelativeSize);
    SKIFF_STYLE_TARGET(autoSize, fAutoSize);
    SKIFF_STYLE_TARGET(grow, fGrow);
    SKIFF_STYLE_TARGET(minWidth, fMinWidth);
    SKIFF_STYLE_TARGET(maxWidth, fMaxWidth);
    SKIFF_STYLE_TARGET(minHeight, fMinHeight);
    SKIFF_STYLE_TARGET(maxHeight, fMaxHeight);
    SKIFF_STYLE_TARGET(alignSelf, fAlignSelf);
    SKIFF_STYLE_TARGET(depth, fDepth);
    SKIFF_STYLE_TARGET(margin, fMargin);
    SKIFF_STYLE_TARGET(padding, fPadding);
    SKIFF_STYLE_TARGET(cornerRadius, fCornerRadius);
    SKIFF_STYLE_TARGET(masking, fMasking);
    SKIFF_STYLE_TARGET(scale, fScale);
    SKIFF_STYLE_TARGET(alpha, fAlpha);
    SKIFF_STYLE_TARGET(visible, fVisible);
#undef SKIFF_STYLE_TARGET

    const bool changed = target != current;
    const bool layoutChanged = !sameLayout(target, current);
    const double duration = style.transitionMs.value_or(0.0);
    const Easing easing = style.transitionEasing.value_or(Easing::kOutQuint);

#define SKIFF_STYLE_DIRECT(declaration, targetMember, liveMember)             \
  if (style.declaration || fResolvedStyle.declaration) {                      \
    liveMember = target.targetMember;                                         \
  }
    SKIFF_STYLE_DIRECT(anchor, fAnchor, fAnchor);
    SKIFF_STYLE_DIRECT(origin, fOrigin, fOrigin);
    SKIFF_STYLE_DIRECT(relativeSize, fRelativeSize, fRelativeSizeAxes);
    SKIFF_STYLE_DIRECT(autoSize, fAutoSize, fAutoSizeAxes);
    SKIFF_STYLE_DIRECT(grow, fGrow, fGrowAxes);
    SKIFF_STYLE_DIRECT(minWidth, fMinWidth, fMinWidth);
    SKIFF_STYLE_DIRECT(maxWidth, fMaxWidth, fMaxWidth);
    SKIFF_STYLE_DIRECT(minHeight, fMinHeight, fMinHeight);
    SKIFF_STYLE_DIRECT(maxHeight, fMaxHeight, fMaxHeight);
    SKIFF_STYLE_DIRECT(alignSelf, fAlignSelf, fAlignSelf);
    SKIFF_STYLE_DIRECT(depth, fDepth, fDepth);
    SKIFF_STYLE_DIRECT(margin, fMargin, fMargin);
    SKIFF_STYLE_DIRECT(padding, fPadding, fPadding);
    SKIFF_STYLE_DIRECT(cornerRadius, fCornerRadius, fCornerRadius);
    SKIFF_STYLE_DIRECT(masking, fMasking, fMasking);
    SKIFF_STYLE_DIRECT(visible, fVisible, fVisible);
#undef SKIFF_STYLE_DIRECT

#define SKIFF_STYLE_ANIMATED(declaration, member, property)                   \
  if (style.declaration || fResolvedStyle.declaration) {                      \
    const float previous = fResolvedStyle.declaration                        \
                               ? fStyledTarget.member                         \
                               : current.member;                              \
    this->setStyledProperty(property, target.member, previous, duration,      \
                            easing, animate);                                 \
  }
    SKIFF_STYLE_ANIMATED(x, fX, Property::kX);
    SKIFF_STYLE_ANIMATED(y, fY, Property::kY);
    SKIFF_STYLE_ANIMATED(width, fWidth, Property::kWidth);
    SKIFF_STYLE_ANIMATED(height, fHeight, Property::kHeight);
    SKIFF_STYLE_ANIMATED(scale, fScale, Property::kScale);
    SKIFF_STYLE_ANIMATED(alpha, fAlpha, Property::kAlpha);
#undef SKIFF_STYLE_ANIMATED

    fStyledTarget = target;
    if (layoutChanged) {
      this->invalidateLayout();
    } else if (changed) {
      this->markDamaged();
    }
  }

  void transformTo(Property property, float from, float to, double durationMs,
                   Easing e) {
    // A new transform on a property replaces whatever was animating it.
    std::erase_if(fTransforms, [property](const Transform &t) {
      return t.fProperty == property;
    });
    if (durationMs <= 0.0) {
      this->applyProperty(property, to);
      return;
    }
    fTransforms.push_back({property, from, to, fPendingStartMs + fDelayMs,
                           fPendingStartMs + fDelayMs + durationMs, e});
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
  // The children, as they were last time: a variant that switched, a row
  // added to a vector. Compared each frame; a different set is laid out and
  // repainted.
  std::size_t fChildSignature = 0;
  skia::SkRect fLastConstraint = skia::SkRect::MakeEmpty();
  bool fDamaged = true;
  skia::SkRect fMovedDamage = skia::SkRect::MakeEmpty();
  skia::SkRect fDrawnBounds = skia::SkRect::MakeEmpty();
  bool fHovered = false;
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

  void setStyledProperty(Property property, float target, float previousTarget,
                         double durationMs, Easing easing, bool animate) {
    if (target == previousTarget) {
      return;
    }
    if (animate && durationMs > 0.0) {
      this->transformTo(property, this->propertyValue(property), target,
                        durationMs, easing);
    } else {
      this->transformTo(property, target, target, 0.0, easing);
    }
  }

  [[nodiscard]] float &propertyRef(Property property) {
    switch (property) {
    case Property::kAlpha:
      return fAlpha;
    case Property::kX:
      return fX;
    case Property::kY:
      return fY;
    case Property::kWidth:
      return fWidth;
    case Property::kHeight:
      return fHeight;
    case Property::kScale:
      break;
    }
    return fScale;
  }
  [[nodiscard]] float propertyValue(Property property) {
    return this->propertyRef(property);
  }

  void applyProperty(Property property, float value) {
    float &current = this->propertyRef(property);
    if (current == value) {
      return;
    }
    current = value;
    if (property == Property::kAlpha) {
      this->markDamaged();
    } else {
      this->invalidateLayout();
    }
  }
};

struct Node;

// ---- children --------------------------------------------------------------

class AnyNode;

namespace detail {
template <class T> struct IsVariant : std::false_type {};
template <class... Ts> struct IsVariant<std::variant<Ts...>> : std::true_type {};
template <class T> struct IsOptional : std::false_type {};
template <class T> struct IsOptional<std::optional<T>> : std::true_type {};
template <class T> struct IsPointer : std::false_type {};
template <class T, class D>
struct IsPointer<std::unique_ptr<T, D>> : std::true_type {};
template <class T> struct IsPointer<std::shared_ptr<T>> : std::true_type {};
template <class T> struct IsReference : std::false_type {};
template <class T>
struct IsReference<std::reference_wrapper<T>> : std::true_type {};
template <class T> struct IsTuple : std::false_type {};
template <class... Ts> struct IsTuple<std::tuple<Ts...>> : std::true_type {};
} // namespace detail

// What can be a child: a way of holding nodes -- a std::variant (its
// alternative, unless that is std::monostate), a std::optional, a pointer, a
// reference_wrapper, a tuple, an AnyNode, a range -- or a node. The walks see
// through the holders to the nodes.
// A node type that is also a range -- it has begin() and end() -- is walked
// as a range of children unless it says it is a node:
//
//   template <> inline constexpr bool skiff::scene::kTreatAsNode<MyList> = true;
template <class T> inline constexpr bool kTreatAsNode = false;

template <class Child, class F> void visitChild(Child &child, F &&f) {
  using C = std::remove_cvref_t<Child>;
  if constexpr (std::same_as<C, std::monostate>) {
    // nothing
  } else if constexpr (detail::IsVariant<C>::value) {
    std::visit([&](auto &alternative) { visitChild(alternative, f); }, child);
  } else if constexpr (detail::IsOptional<C>::value ||
                       detail::IsPointer<C>::value) {
    if (child) {
      visitChild(*child, f);
    }
  } else if constexpr (detail::IsReference<C>::value) {
    visitChild(child.get(), f);
  } else if constexpr (detail::IsTuple<C>::value) {
    std::apply([&](auto &...each) { (visitChild(each, f), ...); }, child);
  } else if constexpr (std::same_as<C, AnyNode>) {
    if (child) {
      f(child);
    }
  } else if constexpr (std::ranges::range<C> && !kTreatAsNode<C>) {
    for (auto &each : child) {
      visitChild(each, f);
    }
  } else {
    // Anything else is a node.
    f(child);
  }
}

// Each child of a node, in the order its forEachChild gives them, holders
// seen through.
template <class T, class F> void eachChild(T &node, F &&f) {
  node.forEachChild([&](auto &child) { visitChild(child, f); });
}

template <class C> [[nodiscard]] State &stateOf(C &child);

// ---- hooks -------------------------------------------------------------------

namespace hook {
template <class T> [[nodiscard]] bool acceptsInput(T &node) {
  return node.acceptsInput();
}
template <class T> [[nodiscard]] bool focusable(T &node) {
  return node.focusable();
}
template <class T> [[nodiscard]] bool hoverChangesAppearance(T &node) {
  return node.hoverChangesAppearance();
}
template <class T> [[nodiscard]] bool focusChangesAppearance(T &node) {
  return node.focusChangesAppearance();
}
template <class T> [[nodiscard]] bool settling(T &node) {
  return node.settling();
}
template <class T> [[nodiscard]] bool onClick(T &node, float x, float y) {
  return node.onClick(x, y);
}
template <class T> [[nodiscard]] bool onScroll(T &node, float ticks) {
  return node.onScroll(ticks);
}
template <class T> [[nodiscard]] Semantics semantics(T &node) {
  return node.semantics();
}
template <class T> [[nodiscard]] bool takesText(T &node) {
  return hook::semantics(node).fRole == SemanticRole::kTextBox;
}
} // namespace hook

// What a node does with a pointer event when it says nothing itself: a
// press is a click, a wheel is a scroll, and a click a gesture-owning
// ancestor deferred waits for the release. A node with its own
// onPointerEvent calls this for what it does not handle.
template <class T>
  requires std::derived_from<T, Node> void defaultPointerEvent(T &node, PointerEvent &event) {
  State &state = node.fState;
  if (event.fPhase != EventPhase::kTarget) {
    return;
  }
  if (event.fAction == PointerAction::kDown && event.fDeferClick) {
    state.fDeferredClick = true;
    event.handle();
  } else if (event.fAction == PointerAction::kUp &&
             std::exchange(state.fDeferredClick, false)) {
    if (state.fBounds.contains(event.fX, event.fY)) {
      (void)hook::onClick(node, event.fX, event.fY);
    }
    // The release belongs to the target where the deferred gesture began,
    // even when it ended outside its bounds.
    event.handle();
  } else if (event.fAction == PointerAction::kCancel) {
    state.fDeferredClick = false;
  } else if (event.fAction == PointerAction::kDown &&
             hook::onClick(node, event.fX, event.fY)) {
    event.handle();
  } else if (event.fAction == PointerAction::kScroll &&
             hook::onScroll(node, event.fScrollY)) {
    event.handle();
  }
}

// Enter and Space activate.
template <class T>
  requires std::derived_from<T, Node> void defaultKeyEvent(T &node, KeyEvent &event) {
  const skia::SkRect &bounds = node.fState.fBounds;
  if (event.fPhase == EventPhase::kTarget && event.fPressed &&
      (event.fKey == Key::kEnter || event.fKey == Key::kSpace) &&
      hook::onClick(node, bounds.centerX(), bounds.centerY())) {
    event.handle();
  }
}

// Focus and activation, which is what assistive technology asks of most
// nodes.
template <class T>
  requires std::derived_from<T, Node>
void defaultSemanticAction(T &node, SemanticActionEvent &event) {
  const skia::SkRect &bounds = node.fState.fBounds;
  if (event.fPhase != EventPhase::kTarget) {
    return;
  }
  if (event.fAction == SemanticAction::kFocus && hook::focusable(node)) {
    event.requestFocus();
    event.handle();
  } else if (event.fAction == SemanticAction::kActivate &&
             hook::onClick(node, bounds.centerX(), bounds.centerY())) {
    event.handle();
  }
}

namespace hook {
template <class T> void pointerEvent(T &node, PointerEvent &event) {
  node.onPointerEvent(event);
}
template <class T> void keyEvent(T &node, KeyEvent &event) {
  node.onKeyEvent(event);
}
template <class T> void textInput(T &node, TextInputEvent &event) {
  node.onTextInput(event);
}
template <class T>
void semanticAction(T &node, SemanticActionEvent &event) {
  node.onSemanticAction(event);
}
template <class T> void focusChanged(T &node, bool focused) {
  node.onFocusChanged(focused);
}
} // namespace hook


// ---- the node ----------------------------------------------------------------

// What every node derives from: its State, and the default of every hook.
// Nothing here is virtual -- the hooks take `this` by deduction, so a call
// made on a node's own type reaches the node's own hook when it has one, and
// this default when it does not. The walks are templates over the real type
// and always call on it.
//
// A node overrides a hook by declaring one with the same name, publicly.
struct Node {
  State fState;

  // -- structure: a leaf has no children
  void forEachChild(this auto &, auto &&) {}

  // -- layout
  // Sets fWidth/fHeight from content before layout uses them.
  void measure(this auto &, const skia::SkRect &) {}
  void layoutChildren(this auto &self) { layoutChildrenInContentBox(self); }

  // -- drawing
  void drawSelf(this auto &, skia::SkCanvas *, float) {}
  // The whole subtree: overridden by a node that draws it another way (a
  // cache), which calls drawDefault for the rest.
  void draw(this auto &self, skia::SkCanvas *canvas, float alpha) {
    drawDefault(self, canvas, alpha);
  }

  // -- time
  void update(this auto &, double) {}
  // Part-way to somewhere by hand -- a hover weight, a knob sliding -- so the
  // next frame differs. Damage says what changed; this says it is still
  // changing.
  [[nodiscard]] bool settling(this const auto &) { return false; }

  // -- input
  [[nodiscard]] bool acceptsInput(this const auto &) { return false; }
  [[nodiscard]] bool focusable(this const auto &self) {
    return self.acceptsInput();
  }
  // Whether the pointer entering or leaving changes the picture. Taking
  // input and drawing hover are separate: a slider need not repaint because
  // the pointer crossed it.
  [[nodiscard]] bool hoverChangesAppearance(this const auto &) {
    return false;
  }
  [[nodiscard]] bool focusChangesAppearance(this const auto &) {
    return false;
  }
  void onFocusChanged(this auto &, bool) {}
  [[nodiscard]] bool onClick(this auto &, float, float) { return false; }
  [[nodiscard]] bool onScroll(this auto &, float) { return false; }
  void onPointerEvent(this auto &self, PointerEvent &event) {
    defaultPointerEvent(self, event);
  }
  void onKeyEvent(this auto &self, KeyEvent &event) {
    defaultKeyEvent(self, event);
  }
  void onTextInput(this auto &, TextInputEvent &) {}
  void onSemanticAction(this auto &self, SemanticActionEvent &event) {
    defaultSemanticAction(self, event);
  }
  [[nodiscard]] Semantics semantics(this const auto &) { return {}; }

  // -- styling: the node's own declarations, colour and fonts
  void applyNodeStyle(this auto &, const Style &, bool) {}

  // -- the common state, reached from the node
  [[nodiscard]] NodeId id() const noexcept { return fState.id(); }
  [[nodiscard]] const skia::SkRect &bounds() const noexcept {
    return fState.bounds();
  }
  [[nodiscard]] bool visible() const noexcept { return fState.visible(); }
  [[nodiscard]] bool hovered() const noexcept { return fState.hovered(); }
  [[nodiscard]] bool focused() const noexcept { return fState.focused(); }
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
  void fadeTo(float target, double ms, Easing e = Easing::kOutQuint) {
    fState.fadeTo(target, ms, e);
  }
  void moveToX(float target, double ms, Easing e = Easing::kOutQuint) {
    fState.moveToX(target, ms, e);
  }
  void moveToY(float target, double ms, Easing e = Easing::kOutQuint) {
    fState.moveToY(target, ms, e);
  }
  void resizeWidthTo(float target, double ms, Easing e = Easing::kOutQuint) {
    fState.resizeWidthTo(target, ms, e);
  }
  void resizeHeightTo(float target, double ms,
                      Easing e = Easing::kOutQuint) {
    fState.resizeHeightTo(target, ms, e);
  }
  void scaleTo(float target, double ms, Easing e = Easing::kOutQuint) {
    fState.scaleTo(target, ms, e);
  }
  void delay(double ms) { fState.delay(ms); }
  void invalidateLayout() { fState.invalidateLayout(); }
  void markDamaged() { fState.markDamaged(); }
};

// A node with no picture of its own that lays its children out in its box:
// a group.
template <class... Children> struct Group : Node {
  explicit Group(Children... children) : fChildren(std::move(children)...) {}
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }
  std::tuple<Children...> fChildren;
};

// ---- style subjects ----------------------------------------------------------

template <class T>
  requires std::derived_from<T, Node>
[[nodiscard]] StyleSubject styleSubject(T &node, float viewportWidth) {
  const State &state = node.fState;
  StyleState states = StyleState::kNone;
  if (state.fHovered)
    states = states | StyleState::kHover;
  if (state.fFocused)
    states = states | StyleState::kFocus;
  if (state.fSelected)
    states = states | StyleState::kSelected;
  if (state.fDisabled)
    states = states | StyleState::kDisabled;
  return {&detail::styleNodeKey<T>, detail::TemplateKeyOf<T>::value,
          state.styleRoles(), states, viewportWidth};
}

// ---- the walks ---------------------------------------------------------------
//
// Each is a function template over the child's type, recursing through
// eachChild. The ones a node's own hooks call -- layout, draw, childBounds --
// are the public ones; the rest are what a Scene runs.

struct UpdateContext {
  double fNowMs = 0.0;
  float fViewportWidth = 0.0f;
  bool fAnimating = false;
};

// A path from a node to one below it: the positions of the children taken,
// in eachChild order.
using Path = std::vector<std::uint32_t>;

// What routing an event found out, for the scene to act on afterwards.
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
};

template <class C> void layout(C &child, const skia::SkRect &parentBox);
template <class C> void draw(C &child, skia::SkCanvas *canvas, float alpha);

namespace walk {
template <class C>
void update(C &child, UpdateContext &context, StyleResolver resolver,
            const Style *inherited, bool restyleAll);
template <class C> [[nodiscard]] bool markDirty(C &child);
template <class C>
[[nodiscard]] skia::SkRect collectDamage(C &child, bool drawnAbove);
template <class C>
void hover(C &child, float x, float y, bool visibleAbove,
           StyleResolver resolver, float viewportWidth);
template <class C>
[[nodiscard]] bool hitPath(C &child, float x, float y, Path &path);
template <class C>
[[nodiscard]] bool findPath(C &child, NodeId id, Path &path);
template <class C>
void routePointer(C &child, const Path &path, std::size_t at,
                  PointerEvent &event, Routed &routed, bool targetOnly);
template <class C>
void routeKey(C &child, const Path &path, std::size_t at, KeyEvent &event);
template <class C>
void routeText(C &child, const Path &path, std::size_t at,
               TextInputEvent &event);
template <class C>
[[nodiscard]] std::optional<NodeInfo> info(C &child, NodeId id);
template <class C>
[[nodiscard]] bool focusChanged(C &child, NodeId id, bool focused,
                                StyleResolver resolver, float viewportWidth);
template <class C>
void routeSemantic(C &child, const Path &path, std::size_t at,
                   SemanticActionEvent &event);
template <class C>
void collectSemantics(C &child, std::vector<Semantics> &out, int parent,
                      NodeId focused);
template <class C>
void collectFocusable(C &child, std::vector<NodeId> &out);
template <class C> [[nodiscard]] bool animating(C &child);
} // namespace walk

// The children in the order they are drawn: eachChild's order, unless one of
// them has a depth. Sorting is stable, so an untouched tree keeps exactly the
// order it was written in and pays nothing for the feature. `f` gets the
// child and its position in eachChild order.
template <class T, class F> void eachChildInDrawOrder(T &node, F &&f) {
  bool sorted = true;
  std::uint32_t count = 0;
  eachChild(node, [&](auto &child) {
    sorted = sorted && stateOf(child).fDepth == 0.0f;
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
    order.emplace_back(stateOf(child).fDepth, at++);
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

// The child at a position in eachChild order.
template <class T, class F>
void childAt(T &node, std::uint32_t index, F &&f) {
  std::uint32_t seen = 0;
  eachChild(node, [&](auto &child) {
    if (seen++ == index) {
      f(child);
    }
  });
}

// The union of the visible children's boxes: what an auto-sized node is
// sized to.
template <class T>
  requires std::derived_from<T, Node> [[nodiscard]] skia::SkRect childBounds(T &node) {
  skia::SkRect content = skia::SkRect::MakeEmpty();
  eachChild(node, [&](auto &child) {
    const State &state = stateOf(child);
    if (state.fVisible) {
      content = joined(content, state.fBounds);
    }
  });
  return content;
}

// Lays every child out in this node's content box: what a node without a
// layoutChildren does, and what one that has one may call.
template <class T>
  requires std::derived_from<T, Node> void layoutChildrenInContentBox(T &node) {
  const skia::SkRect box = node.fState.contentBox();
  eachChild(node, [&](auto &child) { layout(child, box); });
}

namespace detail {
template <class T>
  requires std::derived_from<T, Node> void layoutChildren(T &node) { node.layoutChildren(); }

template <class T>
  requires std::derived_from<T, Node> void layoutNode(T &node, const skia::SkRect &parentBox) {
  State &state = node.fState;
  // Placed against another node, when asked: a dropdown list belongs to the
  // control that opened it and has to be drawn over everything below it, so
  // it lives high in the tree and is positioned low in it.
  const skia::SkRect parent =
      (state.fFollow != nullptr && !state.fFollow->fBounds.isEmpty())
          ? state.fFollow->fBounds
          : parentBox;
  if (state.fLayoutValid && !state.fSubtreeDirty &&
      parent == state.fLastConstraint) {
    return;
  }
  state.fLastConstraint = parent;

  // A node that knows its own size -- text, mainly -- says so before
  // anything is computed from it, given the box it is going into.
  node.measure(parent);

  // A margin holds a node off whichever edge it is anchored to: the room it
  // is placed in is the parent's box less the margin.
  const skia::SkRect room = inset(parent, state.fMargin);
  const float parentW = room.width();
  const float parentH = room.height();
  float width = hasX(state.fRelativeSizeAxes) ? parentW * state.fWidth
                                              : state.fWidth;
  float height = hasY(state.fRelativeSizeAxes) ? parentH * state.fHeight
                                               : state.fHeight;

  // Auto-sized axes need the children laid out first, in a provisional box.
  if (state.fAutoSizeAxes != Axes::kNone) {
    state.fBounds = skia::SkRect::MakeXYWH(
        room.fLeft, room.fTop, hasX(state.fAutoSizeAxes) ? parentW : width,
        hasY(state.fAutoSizeAxes) ? parentH : height);
    detail::layoutChildren(node);
    const skia::SkRect content = childBounds(node);
    if (hasX(state.fAutoSizeAxes)) {
      width = content.width() + state.fPadding.totalX();
    }
    if (hasY(state.fAutoSizeAxes)) {
      height = content.height() + state.fPadding.totalY();
    }
  }

  width = std::max(width, state.fMinWidth);
  height = std::max(height, state.fMinHeight);
  if (state.fMaxWidth > 0.0f) {
    width = std::min(width, state.fMaxWidth);
  }
  if (state.fMaxHeight > 0.0f) {
    height = std::min(height, state.fMaxHeight);
  }
  width *= state.fScale;
  height *= state.fScale;

  const skia::SkRect previous = state.fBounds;
  state.fBounds = anchoredBox(room, width, height, state.fAnchor,
                              state.fOrigin, state.fX, state.fY);
  if (state.fBounds != previous) {
    // Moved or resized: repaint where it was and where it is. Layout is the
    // only place that knows both.
    state.fMovedDamage =
        joined(joined(state.fMovedDamage, previous), state.fBounds);
  }
  detail::layoutChildren(node);
  state.fLayoutValid = true;
  state.fSubtreeDirty = false;
}

template <class T>
  requires std::derived_from<T, Node>
void drawNode(T &node, skia::SkCanvas *canvas, float inheritedAlpha) {
  State &state = node.fState;
  if (!state.fVisible || state.fAlpha <= 0.001f) {
    return;
  }
  // Whatever lies outside what is being repainted is skipped whole, with its
  // subtree: a repaint of one card does not walk the other two hundred.
  ++visitedCount();
  if (!state.fBounds.isEmpty() && canvas->quickReject(state.fBounds)) {
    return;
  }
  ++drawnCount();
  const float alpha = inheritedAlpha * state.fAlpha;
  const int saved = canvas->save();
  if (state.fMasking) {
    if (state.fCornerRadius > 0.0f) {
      canvas->clipRRect(skia::SkRRect::MakeRectXY(state.fBounds,
                                                  state.fCornerRadius,
                                                  state.fCornerRadius),
                        true);
    } else {
      canvas->clipRect(state.fBounds, true);
    }
  }
  node.drawSelf(canvas, alpha);
  eachChildInDrawOrder(node, [&](auto &child, std::uint32_t) {
    draw(child, canvas, alpha);
  });
  canvas->restoreToCount(saved);
  state.fDrawnBounds = state.fBounds;
}
} // namespace detail

// Lays a child out in a box: what a container's layoutChildren calls for
// each of its children, after placing it.
template <class C> void layout(C &child, const skia::SkRect &parentBox) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.layout(parentBox);
  } else {
    detail::layoutNode(child, parentBox);
  }
}

// Draws a node and its subtree, the way the scene does. A node with its own
// draw -- a cache -- calls drawDefault for what it does not do itself.
template <class T>
  requires std::derived_from<T, Node>
void drawDefault(T &node, skia::SkCanvas *canvas, float alpha) {
  detail::drawNode(node, canvas, alpha);
}

template <class C> void draw(C &child, skia::SkCanvas *canvas, float alpha) {
  child.draw(canvas, alpha);
}

namespace walk {

template <class C>
void update(C &child, UpdateContext &context, StyleResolver resolver,
            const Style *inherited, bool restyleAll) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.update(context, resolver, inherited, restyleAll);
  } else {
    State &state = child.fState;
    state.updateTransforms(context.fNowMs);
    child.update(context.fNowMs);
    if (!state.fTransforms.empty() || hook::settling(child)) {
      context.fAnimating = true;
    }

    const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                   : resolver;
    const bool restyle = restyleAll || state.fStyleDirty ||
                         state.fStyleSheetChanged;
    if (restyle) {
      const bool active = static_cast<bool>(own);
      Style resolved =
          active ? own.fResolve(styleSubject(child, context.fViewportWidth))
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
    eachChild(child, [&](auto &each) {
      walk::update(each, context, own, passed, restyle);
    });
  }
}

// Finds what has to be laid out again, bottom-up, and says so on the way
// back: nothing below can tell its ancestors, so the frame asks. A node whose
// set of children changed -- a variant switched, a row added -- is laid out
// again and repainted whole.
template <class C> bool markDirty(C &child) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.markDirty();
  } else {
    State &state = child.fState;
    bool below = false;
    std::size_t signature = 0;
    eachChild(child, [&](auto &each) {
      below = walk::markDirty(each) || below;
      signature = signature * 1099511628211ull ^
                  static_cast<std::size_t>(stateOf(each).fId);
    });
    if (signature != state.fChildSignature) {
      state.fChildSignature = signature;
      state.fLayoutValid = false;
      state.fDamaged = true;
    }
    state.fSubtreeDirty = below || !state.fLayoutValid;
    return state.fSubtreeDirty;
  }
}

// What has to be repainted in this subtree, and forgets it. A masking node
// clips what its subtree reports; a hidden one drops it, though its own
// change still counts -- hiding is a change.
template <class C> skia::SkRect collectDamage(C &child, bool drawnAbove) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.collectDamage(drawnAbove);
  } else {
    State &state = child.fState;
    skia::SkRect damage = skia::SkRect::MakeEmpty();
    if (drawnAbove) {
      damage = state.fMovedDamage;
      if (state.fDamaged) {
        damage = joined(joined(damage, state.fBounds), state.fDrawnBounds);
      }
    }
    state.fMovedDamage = skia::SkRect::MakeEmpty();
    state.fDamaged = false;
    const bool drawn = drawnAbove && state.fVisible && state.fAlpha > 0.001f;
    skia::SkRect below = skia::SkRect::MakeEmpty();
    eachChild(child, [&](auto &each) {
      below = joined(below, walk::collectDamage(each, drawn));
    });
    if (!below.isEmpty() && state.fMasking &&
        !below.intersect(state.fBounds)) {
      below = skia::SkRect::MakeEmpty();
    }
    return joined(damage, below);
  }
}

// Every node remembers where the pointer is, not only the root: a control
// with parts has to know which of its own parts is under it.
template <class C>
void hover(C &child, float x, float y, bool visibleAbove,
           StyleResolver resolver, float viewportWidth) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.hover(x, y, visibleAbove, resolver, viewportWidth);
  } else {
    State &state = child.fState;
    state.fHoverX = x;
    state.fHoverY = y;
    const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                   : resolver;
    const bool visible = visibleAbove && state.fVisible;
    const bool hovered = visible && state.fBounds.contains(x, y);
    if (hovered != state.fHovered) {
      state.fHovered = hovered;
      if (own && own.fUsesState(styleSubject(child, viewportWidth),
                                StyleState::kHover)) {
        state.restyle(true);
      }
      // Only where hover is drawn: most containers do not light up.
      if (hook::hoverChangesAppearance(child)) {
        state.markDamaged();
      }
    }
    const bool childrenVisible =
        visible && (!state.fMasking || state.fBounds.contains(x, y));
    eachChild(child, [&](auto &each) {
      walk::hover(each, x, y, childrenVisible, own, viewportWidth);
    });
  }
}

// The front-most node under a point that takes input: what is drawn last is
// hit first. `path` receives the positions below this node.
template <class C> bool hitPath(C &child, float x, float y, Path &path) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.hitPath(x, y, path);
  } else {
    State &state = child.fState;
    if (!state.fVisible || state.fAlpha <= 0.001f || state.fDisabled) {
      return false;
    }
    if (state.fMasking && !state.fBounds.contains(x, y)) {
      return false;
    }
    bool found = false;
    Path best;
    eachChildInDrawOrder(child, [&](auto &each, std::uint32_t index) {
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
    return hook::acceptsInput(child) && state.fBounds.contains(x, y);
  }
}

template <class C> bool findPath(C &child, NodeId id, Path &path) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.findPath(id, path);
  } else {
    if (child.fState.fId == id) {
      return true;
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
}

// Capture on the way down, the target at the end of the path, bubble on the
// way back up. `targetOnly` delivers to the end of the path alone -- a
// cancel told to the node a press began on.
template <class C>
void routePointer(C &child, const Path &path, std::size_t at,
                  PointerEvent &event, Routed &routed, bool targetOnly) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.routePointer(path, at, event, routed, targetOnly);
  } else {
    State &state = child.fState;
    const auto deliver = [&](EventPhase phase) {
      event.fPhase = phase;
      event.fCurrentTarget = state.fId;
      event.fCapturePointer = false;
      event.fReleasePointer = false;
      event.fRequestFocus = false;
      hook::pointerEvent(child, event);
      if (event.fCapturePointer) {
        routed.fCaptureRequest = state.fId;
      }
      if (event.fReleasePointer) {
        routed.fReleaseRequest = true;
      }
      if (event.fRequestFocus) {
        routed.fFocusRequest = state.fId;
      }
    };
    if (at == path.size()) {
      if (!event.fHandled) {
        routed.fTargetDelivered = true;
        routed.fTargetFocusable = hook::focusable(child);
        deliver(EventPhase::kTarget);
      }
      return;
    }
    if (!targetOnly && !event.fHandled) {
      deliver(EventPhase::kCapture);
    }
    childAt(child, path[at], [&](auto &each) {
      walk::routePointer(each, path, at + 1, event, routed, targetOnly);
    });
    if (!targetOnly && !event.fHandled) {
      deliver(EventPhase::kBubble);
    }
  }
}

template <class C>
void routeKey(C &child, const Path &path, std::size_t at, KeyEvent &event) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.routeKey(path, at, event);
  } else {
    const auto deliver = [&](EventPhase phase) {
      event.fPhase = phase;
      event.fCurrentTarget = child.fState.fId;
      hook::keyEvent(child, event);
    };
    if (at == path.size()) {
      if (!event.fHandled) {
        deliver(EventPhase::kTarget);
      }
      return;
    }
    if (!event.fHandled) {
      deliver(EventPhase::kCapture);
    }
    childAt(child, path[at], [&](auto &each) {
      walk::routeKey(each, path, at + 1, event);
    });
    if (!event.fHandled) {
      deliver(EventPhase::kBubble);
    }
  }
}

template <class C>
void routeText(C &child, const Path &path, std::size_t at,
               TextInputEvent &event) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.routeText(path, at, event);
  } else {
    const auto deliver = [&](EventPhase phase) {
      event.fPhase = phase;
      event.fCurrentTarget = child.fState.fId;
      hook::textInput(child, event);
    };
    if (at == path.size()) {
      if (!event.fHandled) {
        deliver(EventPhase::kTarget);
      }
      return;
    }
    if (!event.fHandled) {
      deliver(EventPhase::kCapture);
    }
    childAt(child, path[at], [&](auto &each) {
      walk::routeText(each, path, at + 1, event);
    });
    if (!event.fHandled) {
      deliver(EventPhase::kBubble);
    }
  }
}

template <class C> std::optional<NodeInfo> info(C &child, NodeId id) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.info(id);
  } else {
    const State &state = child.fState;
    if (state.fId == id) {
      return NodeInfo{hook::focusable(child), state.fDisabled,
                      state.fVisible, hook::takesText(child)};
    }
    std::optional<NodeInfo> found;
    eachChild(child, [&](auto &each) {
      if (!found) {
        found = walk::info(each, id);
        if (found && (!state.fVisible)) {
          found->fVisible = false;
        }
        if (found && state.fDisabled) {
          found->fDisabled = true;
        }
      }
    });
    return found;
  }
}

template <class C>
bool focusChanged(C &child, NodeId id, bool focused, StyleResolver resolver,
                  float viewportWidth) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.focusChanged(id, focused, resolver, viewportWidth);
  } else {
    State &state = child.fState;
    const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                   : resolver;
    if (state.fId == id) {
      state.fFocused = focused;
      hook::focusChanged(child, focused);
      if (own && own.fUsesState(styleSubject(child, viewportWidth),
                                StyleState::kFocus)) {
        state.restyle(true);
      }
      if (hook::focusChangesAppearance(child)) {
        state.markDamaged();
      }
      return true;
    }
    bool found = false;
    eachChild(child, [&](auto &each) {
      found = found ||
              walk::focusChanged(each, id, focused, own, viewportWidth);
    });
    return found;
  }
}

template <class C>
void routeSemantic(C &child, const Path &path, std::size_t at,
                   SemanticActionEvent &event) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.routeSemantic(path, at, event);
  } else {
    const auto deliver = [&](EventPhase phase) {
      event.fPhase = phase;
      event.fCurrentTarget = child.fState.fId;
      hook::semanticAction(child, event);
    };
    if (at == path.size()) {
      if (!event.fHandled) {
        deliver(EventPhase::kTarget);
      }
      return;
    }
    if (!event.fHandled) {
      deliver(EventPhase::kCapture);
    }
    childAt(child, path[at], [&](auto &each) {
      walk::routeSemantic(each, path, at + 1, event);
    });
    if (!event.fHandled) {
      deliver(EventPhase::kBubble);
    }
  }
}

template <class C>
void collectSemantics(C &child, std::vector<Semantics> &out, int parent,
                      NodeId focused) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.collectSemantics(out, parent, focused);
  } else {
    const State &state = child.fState;
    if (!state.fVisible || state.fAlpha <= 0.001f) {
      return;
    }
    Semantics own = hook::semantics(child);
    int childParent = parent;
    if (own.fRole != SemanticRole::kNone || !own.fLabel.empty()) {
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
}

template <class C> void collectFocusable(C &child, std::vector<NodeId> &out) {
  if constexpr (std::same_as<C, AnyNode>) {
    child.collectFocusable(out);
  } else {
    const State &state = child.fState;
    if (!state.fVisible || state.fDisabled || state.fAlpha <= 0.001f) {
      return;
    }
    if (hook::focusable(child)) {
      out.push_back(state.fId);
    }
    eachChildInDrawOrder(child, [&](auto &each, std::uint32_t) {
      walk::collectFocusable(each, out);
    });
  }
}

template <class C> bool animating(C &child) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.animating();
  } else {
    if (!child.fState.fTransforms.empty() || hook::settling(child)) {
      return true;
    }
    bool any = false;
    eachChild(child, [&](auto &each) { any = any || walk::animating(each); });
    return any;
  }
}

} // namespace walk

namespace walk {
// What is waiting to be repainted, without forgetting it.
template <class C> [[nodiscard]] bool hasDamage(C &child) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.hasDamage();
  } else {
    const State &state = child.fState;
    if (state.fDamaged || !state.fMovedDamage.isEmpty()) {
      return true;
    }
    bool any = false;
    eachChild(child, [&](auto &each) { any = any || walk::hasDamage(each); });
    return any;
  }
}

// The id of the node at the end of a path.
template <class C>
[[nodiscard]] NodeId idAt(C &child, const Path &path, std::size_t at) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.idAt(path, at);
  } else {
    if (at == path.size()) {
      return child.fState.fId;
    }
    NodeId found = 0;
    childAt(child, path[at],
            [&](auto &each) { found = walk::idAt(each, path, at + 1); });
    return found;
  }
}
} // namespace walk

// ---- AnyNode ----------------------------------------------------------------

// Any node, held by value, erased the way std::function erases a callable:
// one allocation, and a table of the walks for the node's own type. For the
// places where the type cannot be written down -- a list whose rows differ,
// a panel a plugin supplies. Everywhere else, write the type.
class AnyNode {
public:
  AnyNode() = default;
  template <class T>
    requires std::derived_from<std::remove_cvref_t<T>, Node> &&
             (!std::same_as<std::remove_cvref_t<T>, AnyNode>)
  AnyNode(T &&node) // NOLINT: converting, as std::function's is
      : fNode(new std::remove_cvref_t<T>(std::forward<T>(node))),
        fOps(&kOps<std::remove_cvref_t<T>>) {}

  AnyNode(const AnyNode &) = delete;
  AnyNode &operator=(const AnyNode &) = delete;
  AnyNode(AnyNode &&other) noexcept
      : fNode(std::exchange(other.fNode, nullptr)),
        fOps(std::exchange(other.fOps, nullptr)) {}
  AnyNode &operator=(AnyNode &&other) noexcept {
    if (this != &other) {
      this->reset();
      fNode = std::exchange(other.fNode, nullptr);
      fOps = std::exchange(other.fOps, nullptr);
    }
    return *this;
  }
  ~AnyNode() { this->reset(); }

  void reset() {
    if (fNode != nullptr) {
      fOps->fDestroy(fNode);
      fNode = nullptr;
      fOps = nullptr;
    }
  }
  [[nodiscard]] explicit operator bool() const noexcept {
    return fNode != nullptr;
  }
  // The node, when it is a T.
  template <class T>
  requires std::derived_from<T, Node> [[nodiscard]] T *get() noexcept {
    return fOps == &kOps<T> ? static_cast<T *>(fNode) : nullptr;
  }
  [[nodiscard]] State &state() { return fOps->fState(fNode); }

  // The walks, for the node inside.
  void layout(const skia::SkRect &box) { fOps->fLayout(fNode, box); }
  void draw(skia::SkCanvas *canvas, float alpha) {
    fOps->fDraw(fNode, canvas, alpha);
  }
  void update(UpdateContext &context, StyleResolver resolver,
              const Style *inherited, bool restyleAll) {
    fOps->fUpdate(fNode, context, resolver, inherited, restyleAll);
  }
  [[nodiscard]] bool markDirty() { return fOps->fMarkDirty(fNode); }
  [[nodiscard]] skia::SkRect collectDamage(bool drawnAbove) {
    return fOps->fCollectDamage(fNode, drawnAbove);
  }
  [[nodiscard]] bool hasDamage() { return fOps->fHasDamage(fNode); }
  void hover(float x, float y, bool visibleAbove, StyleResolver resolver,
             float viewportWidth) {
    fOps->fHover(fNode, x, y, visibleAbove, resolver, viewportWidth);
  }
  [[nodiscard]] bool hitPath(float x, float y, Path &path) {
    return fOps->fHitPath(fNode, x, y, path);
  }
  [[nodiscard]] bool findPath(NodeId id, Path &path) {
    return fOps->fFindPath(fNode, id, path);
  }
  [[nodiscard]] NodeId idAt(const Path &path, std::size_t at) {
    return fOps->fIdAt(fNode, path, at);
  }
  void routePointer(const Path &path, std::size_t at, PointerEvent &event,
                    Routed &routed, bool targetOnly) {
    fOps->fRoutePointer(fNode, path, at, event, routed, targetOnly);
  }
  void routeKey(const Path &path, std::size_t at, KeyEvent &event) {
    fOps->fRouteKey(fNode, path, at, event);
  }
  void routeText(const Path &path, std::size_t at, TextInputEvent &event) {
    fOps->fRouteText(fNode, path, at, event);
  }
  [[nodiscard]] std::optional<NodeInfo> info(NodeId id) {
    return fOps->fInfo(fNode, id);
  }
  [[nodiscard]] bool focusChanged(NodeId id, bool focused,
                                  StyleResolver resolver,
                                  float viewportWidth) {
    return fOps->fFocusChanged(fNode, id, focused, resolver, viewportWidth);
  }
  void routeSemantic(const Path &path, std::size_t at,
                     SemanticActionEvent &event) {
    fOps->fRouteSemantic(fNode, path, at, event);
  }
  void collectSemantics(std::vector<Semantics> &out, int parent,
                        NodeId focused) {
    fOps->fCollectSemantics(fNode, out, parent, focused);
  }
  void collectFocusable(std::vector<NodeId> &out) {
    fOps->fCollectFocusable(fNode, out);
  }
  [[nodiscard]] bool animating() { return fOps->fAnimating(fNode); }

private:
  struct Ops {
    void (*fDestroy)(void *);
    State &(*fState)(void *);
    void (*fLayout)(void *, const skia::SkRect &);
    void (*fDraw)(void *, skia::SkCanvas *, float);
    void (*fUpdate)(void *, UpdateContext &, StyleResolver, const Style *,
                    bool);
    bool (*fMarkDirty)(void *);
    skia::SkRect (*fCollectDamage)(void *, bool);
    bool (*fHasDamage)(void *);
    void (*fHover)(void *, float, float, bool, StyleResolver, float);
    bool (*fHitPath)(void *, float, float, Path &);
    bool (*fFindPath)(void *, NodeId, Path &);
    NodeId (*fIdAt)(void *, const Path &, std::size_t);
    void (*fRoutePointer)(void *, const Path &, std::size_t, PointerEvent &,
                          Routed &, bool);
    void (*fRouteKey)(void *, const Path &, std::size_t, KeyEvent &);
    void (*fRouteText)(void *, const Path &, std::size_t, TextInputEvent &);
    std::optional<NodeInfo> (*fInfo)(void *, NodeId);
    bool (*fFocusChanged)(void *, NodeId, bool, StyleResolver, float);
    void (*fRouteSemantic)(void *, const Path &, std::size_t,
                           SemanticActionEvent &);
    void (*fCollectSemantics)(void *, std::vector<Semantics> &, int, NodeId);
    void (*fCollectFocusable)(void *, std::vector<NodeId> &);
    bool (*fAnimating)(void *);
  };

  template <class T> [[nodiscard]] static T &as(void *node) {
    return *static_cast<T *>(node);
  }

  template <class T>
  static constexpr Ops kOps{
      +[](void *n) { delete static_cast<T *>(n); },
      +[](void *n) -> State & { return as<T>(n).fState; },
      +[](void *n, const skia::SkRect &box) {
        scene::layout(as<T>(n), box);
      },
      +[](void *n, skia::SkCanvas *canvas, float alpha) {
        scene::draw(as<T>(n), canvas, alpha);
      },
      +[](void *n, UpdateContext &c, StyleResolver r, const Style *s,
          bool all) { walk::update(as<T>(n), c, r, s, all); },
      +[](void *n) { return walk::markDirty(as<T>(n)); },
      +[](void *n, bool drawn) {
        return walk::collectDamage(as<T>(n), drawn);
      },
      +[](void *n) { return walk::hasDamage(as<T>(n)); },
      +[](void *n, float x, float y, bool visible, StyleResolver r,
          float width) { walk::hover(as<T>(n), x, y, visible, r, width); },
      +[](void *n, float x, float y, Path &path) {
        return walk::hitPath(as<T>(n), x, y, path);
      },
      +[](void *n, NodeId id, Path &path) {
        return walk::findPath(as<T>(n), id, path);
      },
      +[](void *n, const Path &path, std::size_t at) {
        return walk::idAt(as<T>(n), path, at);
      },
      +[](void *n, const Path &path, std::size_t at, PointerEvent &e,
          Routed &routed, bool targetOnly) {
        walk::routePointer(as<T>(n), path, at, e, routed, targetOnly);
      },
      +[](void *n, const Path &path, std::size_t at, KeyEvent &e) {
        walk::routeKey(as<T>(n), path, at, e);
      },
      +[](void *n, const Path &path, std::size_t at, TextInputEvent &e) {
        walk::routeText(as<T>(n), path, at, e);
      },
      +[](void *n, NodeId id) { return walk::info(as<T>(n), id); },
      +[](void *n, NodeId id, bool focused, StyleResolver r, float width) {
        return walk::focusChanged(as<T>(n), id, focused, r, width);
      },
      +[](void *n, const Path &path, std::size_t at, SemanticActionEvent &e) {
        walk::routeSemantic(as<T>(n), path, at, e);
      },
      +[](void *n, std::vector<Semantics> &out, int parent, NodeId focused) {
        walk::collectSemantics(as<T>(n), out, parent, focused);
      },
      +[](void *n, std::vector<NodeId> &out) {
        walk::collectFocusable(as<T>(n), out);
      },
      +[](void *n) { return walk::animating(as<T>(n)); },
  };

  void *fNode = nullptr;
  const Ops *fOps = nullptr;
};

template <class C> State &stateOf(C &child) {
  if constexpr (std::same_as<C, AnyNode>) {
    return child.state();
  } else {
    return child.fState;
  }
}

// A node built with a spec applied: for members and for the arguments of a
// container, where there is no parent to add to.
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
  requires std::derived_from<T, Node> [[nodiscard]] T placed(const Spec &spec, T node) {
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
template <class Root>
  requires std::derived_from<Root, Node> class Scene {
public:
  template <class... Args>
  explicit Scene(std::in_place_t, Args &&...args)
      : fRoot(std::forward<Args>(args)...) {}
  explicit Scene(Root root) : fRoot(std::move(root)) {}
  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  [[nodiscard]] Root &root() noexcept { return fRoot; }
  [[nodiscard]] const Root &root() const noexcept { return fRoot; }
  [[nodiscard]] State &state() noexcept { return fRoot.fState; }

  template <class Theme> void setStyleSheet() {
    fRoot.fState.template setStyleSheet<Theme>();
    this->restyleDirty();
  }
  void clearStyleSheet() {
    fRoot.fState.clearStyleSheet();
    this->restyleDirty();
  }

  // Advances transforms and the nodes' own animation, and applies styles.
  void update(double nowMs) {
    fNowMs = nowMs;
    UpdateContext context{nowMs, fViewport.width(), false};
    walk::update(fRoot, context, {}, nullptr, false);
  }

  // Lays out what changed, and everything when the viewport did.
  bool layoutIfNeeded(const skia::SkRect &viewport) {
    const bool viewportChanged = viewport != fViewport;
    fViewport = viewport;
    if (viewportChanged) {
      // Width-constrained selectors are media queries: resolved before
      // layout, so their declarations take part in this pass.
      UpdateContext context{fNowMs, viewport.width(), false};
      walk::update(fRoot, context, {}, nullptr, true);
    }
    const bool dirty = walk::markDirty(fRoot);
    if (!dirty && !viewportChanged) {
      return false;
    }
    scene::layout(fRoot, viewport);
    return true;
  }

  void draw(skia::SkCanvas *canvas) { scene::draw(fRoot, canvas, 1.0f); }

  // What changed this frame and whether the next can differ, together: kept
  // apart, damage was consumed while an ease still needed frames.
  [[nodiscard]] FrameResult finishFrame() {
    skia::SkRect damage = walk::collectDamage(fRoot, true);
    const skia::SkRect &bounds = fRoot.fState.fBounds;
    if (!bounds.isEmpty() && !damage.isEmpty() && !damage.intersect(bounds)) {
      damage = skia::SkRect::MakeEmpty();
    }
    return {damage, walk::animating(fRoot)};
  }
  [[nodiscard]] bool hasFrameWork() {
    return walk::markDirty(fRoot) || walk::hasDamage(fRoot) ||
           walk::animating(fRoot);
  }

  // -- input
  bool dispatchPointer(PointerEvent event) {
    if (event.fAction == PointerAction::kMove) {
      this->setHover(event.fX, event.fY);
    }
    const bool ending = event.fAction == PointerAction::kUp ||
                        event.fAction == PointerAction::kCancel;
    Path path;
    bool found = false;
    if (fCapture != 0) {
      found = walk::findPath(fRoot, fCapture, path);
      if (!found) {
        fCapture = 0; // it went away
      }
    }
    if (!found && ending && fDown != 0) {
      path.clear();
      found = walk::findPath(fRoot, fDown, path);
      if (!found) {
        fDown = 0;
      }
    }
    if (!found) {
      path.clear();
      found = walk::hitPath(fRoot, event.fX, event.fY, path);
    }
    if (!found) {
      if (event.fAction == PointerAction::kDown) {
        this->focus(0);
      }
      if (ending) {
        fCapture = 0;
        fDown = 0;
      }
      return false;
    }
    const NodeId target = walk::idAt(fRoot, path, 0);
    if (event.fAction == PointerAction::kDown) {
      fDown = target;
    }
    event.fCaptured = fCapture != 0;
    event.fTarget = target;
    Routed routed;
    walk::routePointer(fRoot, path, 0, event, routed, false);

    if (routed.fReleaseRequest || ending) {
      fCapture = 0;
    } else if (routed.fCaptureRequest != 0) {
      // A container claiming a drag cancels the control where the press
      // began: it must not stay armed and activate after scrolling.
      if (fDown != 0 && fDown != routed.fCaptureRequest) {
        Path down;
        if (walk::findPath(fRoot, fDown, down)) {
          PointerEvent cancel = event;
          cancel.fAction = PointerAction::kCancel;
          cancel.fHandled = false;
          cancel.fTarget = fDown;
          Routed ignored;
          walk::routePointer(fRoot, down, 0, cancel, ignored, true);
        }
      }
      fDown = 0;
      fCapture = routed.fCaptureRequest;
    }
    if (ending) {
      fDown = 0;
    }
    if (event.fSuppressHover) {
      const float away = -std::numeric_limits<float>::infinity();
      this->setHover(away, away);
    }
    if (routed.fFocusRequest != 0) {
      this->focus(routed.fFocusRequest);
    } else if (routed.fTargetDelivered &&
               event.fAction == PointerAction::kDown &&
               routed.fTargetFocusable) {
      this->focus(target);
    }
    this->restyleDirty();
    return event.fHandled;
  }

  bool dispatchPointer(PointerAction action, float x, float y,
                       float scrollX = 0.0f, float scrollY = 0.0f,
                       int button = 0) {
    PointerEvent event;
    event.fAction = action;
    event.fX = x;
    event.fY = y;
    event.fScrollX = scrollX;
    event.fScrollY = scrollY;
    event.fButton = button;
    return this->dispatchPointer(event);
  }

  bool dispatchKey(KeyEvent event) {
    if (event.fPressed && event.fKey == Key::kTab) {
      this->focusNext(event.fShift);
      return fFocus != 0;
    }
    Path path;
    if (!this->focusPath(path)) {
      return false;
    }
    event.fTarget = fFocus;
    walk::routeKey(fRoot, path, 0, event);
    this->restyleDirty();
    return event.fHandled;
  }

  bool dispatchText(TextInputEvent event) {
    Path path;
    if (!this->focusPath(path)) {
      return false;
    }
    event.fTarget = fFocus;
    walk::routeText(fRoot, path, 0, event);
    this->restyleDirty();
    return event.fHandled;
  }

  bool dispatchSemantic(NodeId id, SemanticActionEvent event) {
    const std::optional<NodeInfo> about = walk::info(fRoot, id);
    Path path;
    if (!about || !about->fVisible || about->fDisabled ||
        !walk::findPath(fRoot, id, path)) {
      return false;
    }
    event.fTarget = id;
    walk::routeSemantic(fRoot, path, 0, event);
    if (event.fRequestFocus) {
      this->focus(id);
    }
    this->restyleDirty();
    return event.fHandled;
  }

  [[nodiscard]] std::vector<Semantics> semanticsTree() {
    std::vector<Semantics> out;
    walk::collectSemantics(fRoot, out, -1, fFocus);
    return out;
  }

  // Gives a node focus, or takes it from all with 0. A node that cannot
  // take it -- not focusable, disabled, hidden, gone -- does not get it.
  void focus(NodeId id) {
    std::optional<NodeInfo> now;
    if (id != 0) {
      now = walk::info(fRoot, id);
      if (!now || !now->fFocusable || now->fDisabled || !now->fVisible) {
        id = 0;
        now.reset();
      }
    }
    if (fFocus == id) {
      return;
    }
    const NodeId previous = fFocus;
    const std::optional<NodeInfo> before =
        previous != 0 ? walk::info(fRoot, previous) : std::nullopt;
    fFocus = id;
    if (auto &hook = textFocusHook(); hook) {
      const bool wasText = before && before->fTakesText;
      const bool isText = now && now->fTakesText;
      if (wasText != isText) {
        hook(isText);
      }
    }
    if (previous != 0) {
      (void)walk::focusChanged(fRoot, previous, false, {}, fViewport.width());
    }
    if (id != 0) {
      (void)walk::focusChanged(fRoot, id, true, {}, fViewport.width());
    }
    this->restyleDirty();
  }
  void focus(const State &node) { this->focus(node.id()); }
  void clearFocus() { this->focus(0); }
  [[nodiscard]] NodeId focusedId() {
    Path ignored;
    return this->focusPath(ignored) ? fFocus : 0;
  }
  [[nodiscard]] bool focusedTakesText() {
    const NodeId id = this->focusedId();
    if (id == 0) {
      return false;
    }
    const std::optional<NodeInfo> about = walk::info(fRoot, id);
    return about && about->fTakesText;
  }
  [[nodiscard]] NodeId capturedId() {
    if (fCapture != 0) {
      Path ignored;
      if (!walk::findPath(fRoot, fCapture, ignored)) {
        fCapture = 0;
      }
    }
    return fCapture;
  }

  [[nodiscard]] std::vector<NodeId> focusableIds() {
    std::vector<NodeId> out;
    walk::collectFocusable(fRoot, out);
    return out;
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

  // Walked only when the pointer moved: hover cannot change by itself.
  void setHover(float x, float y) {
    if (x == fHoverX && y == fHoverY && fHoverSeen) {
      return;
    }
    fHoverX = x;
    fHoverY = y;
    fHoverSeen = true;
    walk::hover(fRoot, x, y, true, {}, fViewport.width());
    this->restyleDirty();
  }

  [[nodiscard]] SceneHandle handle();

private:
  // Styles whose inputs changed -- a hover, a focus, a role -- applied now
  // rather than at the next frame, so what a handler sees is current.
  void restyleDirty() {
    UpdateContext context{fNowMs, fViewport.width(), false};
    walk::update(fRoot, context, {}, nullptr, false);
  }

  [[nodiscard]] bool focusPath(Path &path) {
    if (fFocus == 0) {
      return false;
    }
    const std::optional<NodeInfo> about = walk::info(fRoot, fFocus);
    if (!about || about->fDisabled || !about->fVisible ||
        !walk::findPath(fRoot, fFocus, path)) {
      this->focus(0);
      return false;
    }
    return true;
  }

  Root fRoot;
  NodeId fCapture = 0;
  NodeId fDown = 0;
  NodeId fFocus = 0;
  float fHoverX = 0.0f, fHoverY = 0.0f;
  bool fHoverSeen = false;
  double fNowMs = 0.0;
  skia::SkRect fViewport = skia::SkRect::MakeEmpty();
  std::shared_ptr<int> fAlive = std::make_shared<int>(0);

  friend class SceneHandle;
};

// A scene of any root type, not owned, that goes inert when the scene is
// destroyed: what a router keeps across screen changes.
class SceneHandle {
public:
  SceneHandle() = default;
  template <class Root>
  requires std::derived_from<Root, Node>
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

  bool pointer(PointerEvent event) const {
    return this->alive() && fOps->fPointer(fScene, event);
  }
  bool key(KeyEvent event) const {
    return this->alive() && fOps->fKey(fScene, event);
  }
  bool text(TextInputEvent event) const {
    return this->alive() && fOps->fText(fScene, event);
  }
  bool semantic(NodeId id, SemanticActionEvent event) const {
    return this->alive() && fOps->fSemantic(fScene, id, event);
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
    bool (*fPointer)(void *, PointerEvent);
    bool (*fKey)(void *, KeyEvent);
    bool (*fText)(void *, TextInputEvent);
    bool (*fSemantic)(void *, NodeId, SemanticActionEvent);
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
      +[](void *s, PointerEvent e) { return as<Root>(s).dispatchPointer(e); },
      +[](void *s, KeyEvent e) { return as<Root>(s).dispatchKey(e); },
      +[](void *s, TextInputEvent e) { return as<Root>(s).dispatchText(e); },
      +[](void *s, NodeId id, SemanticActionEvent e) {
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

template <class Root>
  requires std::derived_from<Root, Node> SceneHandle Scene<Root>::handle() {
  return SceneHandle(*this);
}

// Routes between scenes. Layers are back-to-front; the first modal layer
// found owns input even when nothing in it handles it. Covered layers keep
// their hover and focus, so closing a modal restores where the user was.
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
        PointerEvent cancel;
        cancel.fAction = PointerAction::kCancel;
        captured.pointer(cancel);
        captured = {};
      }
    }
    fCaptured = captured;
    fLayers.assign(layers.begin(), layers.end());
  }

  bool pointer(PointerEvent event) {
    if (fCaptured.alive()) {
      const bool handled = fCaptured.pointer(event);
      if (fCaptured.captured() == 0) {
        fCaptured = {};
      }
      return handled;
    }
    fCaptured = {};
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      const bool handled = it->fScene.pointer(event);
      if (it->fScene.captured() != 0) {
        fCaptured = it->fScene;
        if (event.fAction == PointerAction::kDown) {
          this->ownFocus(it->fScene);
        }
        return true;
      }
      if (handled) {
        if (event.fAction == PointerAction::kDown) {
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

  bool key(KeyEvent event) {
    if (event.fPressed && event.fKey == Key::kTab) {
      return this->focusNext(event.fShift);
    }
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      if (it->fScene.key(event)) {
        return true;
      }
      if (it->fModal) {
        return true;
      }
    }
    return false;
  }

  bool text(TextInputEvent event) {
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      if (it->fScene.focused() != 0 && it->fScene.text(event)) {
        return true;
      }
      if (it->fModal) {
        return true;
      }
    }
    return false;
  }

  bool semantic(NodeId id, SemanticActionEvent event) {
    for (auto it = fLayers.rbegin(); it != fLayers.rend(); ++it) {
      if (!it->fScene.alive()) {
        continue;
      }
      if (it->fScene.semantic(id, event)) {
        if (event.fAction == SemanticAction::kFocus) {
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
