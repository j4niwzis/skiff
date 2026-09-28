export module skiff.scene;

import std;
import skia;
import skiff.paint;

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

// The overloaded pattern: a visitor made of several callables.
template <class... Fs> struct overloaded : Fs... {
  using Fs::operator()...;
};
template <class... Fs> overloaded(Fs...) -> overloaded<Fs...>;

// What a widget does when it was not given anything to do.
struct NoAction {
  template <class... Args>
  constexpr void operator()(Args &&...) const noexcept {}
};
// Whether an action does anything: false for NoAction, true for anything
// else. Overloads, so a widget asks without a condition on types.
[[nodiscard]] constexpr bool acts(const NoAction &) noexcept { return false; }
template <class Action>
[[nodiscard]] constexpr bool acts(const Action &) noexcept {
  return true;
}

// ---- flags -------------------------------------------------------------------

namespace detail {
// A list of types, and the same list with its duplicates removed: from
// do_let_is (examples/src/type_set.h, MIT), by the same author.
template <class... Ts> struct type_set {
  static constexpr std::size_t size = sizeof...(Ts);
};
template <class... Ts, class... Ts2>
constexpr auto operator+(type_set<Ts...>, type_set<Ts2...>)
    -> type_set<Ts..., Ts2...> {
  return {};
}
struct overload_check_root {
  static consteval void test();
};
template <class T = overload_check_root, class Base = overload_check_root>
struct overload_check : Base {
  using Base::test;
  static consteval void test(type_set<T> value)
    requires true;
  static consteval void test(type_set<T> value)
    requires(requires { Base::test(value); });

  template <class T2> constexpr auto operator|(type_set<T2>) {
    constexpr auto next = overload_check<T2, overload_check>{};
    if constexpr (requires { next.test(type_set<T2>{}); }) {
      return next;
    } else {
      return *this;
    }
  }
};
template <class T> struct overload_check_to_type_set {};
template <class T, class Base>
struct overload_check_to_type_set<overload_check<T, Base>> {
  using type = decltype(type_set<T>{} +
                        typename overload_check_to_type_set<Base>::type{});
};
template <>
struct overload_check_to_type_set<
    overload_check<overload_check_root, overload_check_root>> {
  using type = type_set<>;
};
template <class... Ts> struct get_unique {
  using type = typename overload_check_to_type_set<decltype((
      overload_check<>{} | ... | type_set<Ts>{}))>::type;
};

template <class Tag, class... Ts> consteval std::size_t indexOf() {
  constexpr std::array<bool, sizeof...(Ts)> same{std::is_same_v<Tag, Ts>...};
  for (std::size_t i = 0; i < same.size(); ++i) {
    if (same[i]) {
      return i;
    }
  }
  return sizeof...(Ts);
}
template <std::size_t N>
using FlagBits = std::conditional_t<
    (N <= 8), std::uint8_t,
    std::conditional_t<(N <= 16), std::uint16_t,
                       std::conditional_t<(N <= 32), std::uint32_t,
                                          std::uint64_t>>>;
} // namespace detail

// A set of flags known at compile time: Tag names the family, Ts are the
// flags in the set. It holds nothing; combining two sets of one family gives
// the set of both, duplicates removed, still as a type.
//
//   axes::kX | axes::kY   is   StaticFlags<AxisTag, axis::x, axis::y>
template <class Tag, class... Ts> struct StaticFlags {
  template <class Flag> [[nodiscard]] static constexpr bool has() noexcept {
    return (std::is_same_v<Flag, Ts> || ...);
  }
  template <class... Ts2>
  [[nodiscard]] constexpr auto operator|(StaticFlags<Tag, Ts2...>) const noexcept {
    return []<class... Us>(detail::type_set<Us...>) {
      return StaticFlags<Tag, Us...>{};
    }(typename detail::get_unique<Ts..., Ts2...>::type{});
  }
};

// A set of flags as a value, one bit per flag of the family: Ts are all the
// flags a Tag has. Made from any StaticFlags of the family; a flag the
// family does not have does not compile.
//
//   using Axes = Flags<AxisTag, axis::x, axis::y>;
//   Axes both = axes::kX | axes::kY;
//   both.has<axis::x>()
template <class Tag, class... Ts> class Flags {
  static_assert(sizeof...(Ts) <= 64, "at most 64 flags");
  using Bits = detail::FlagBits<sizeof...(Ts)>;

  template <class Flag> [[nodiscard]] static consteval Bits bit() {
    constexpr std::size_t at = detail::indexOf<Flag, Ts...>();
    static_assert(at < sizeof...(Ts), "not one of this family's flags");
    return static_cast<Bits>(Bits{1} << at);
  }
  constexpr explicit Flags(Bits bits) noexcept : fBits(bits) {}

public:
  constexpr Flags() noexcept = default;
  template <class... Some>
  constexpr Flags(StaticFlags<Tag, Some...>) noexcept // NOLINT: a set is a value
      : fBits(static_cast<Bits>((Bits{0} | ... | bit<Some>()))) {}

  template <class Flag> [[nodiscard]] constexpr bool has() const noexcept {
    return (fBits & bit<Flag>()) != 0;
  }
  template <class Flag> [[nodiscard]] constexpr Flags with() const noexcept {
    return Flags(static_cast<Bits>(fBits | bit<Flag>()));
  }
  template <class Flag> [[nodiscard]] constexpr Flags without() const noexcept {
    return Flags(static_cast<Bits>(fBits & ~bit<Flag>()));
  }
  template <class Flag>
  [[nodiscard]] constexpr Flags with(bool on) const noexcept {
    return on ? this->with<Flag>() : this->without<Flag>();
  }
  [[nodiscard]] friend constexpr Flags operator|(Flags a, Flags b) noexcept {
    return Flags(static_cast<Bits>(a.fBits | b.fBits));
  }
  [[nodiscard]] friend constexpr Flags operator&(Flags a, Flags b) noexcept {
    return Flags(static_cast<Bits>(a.fBits & b.fBits));
  }
  // Every flag of `other` is set here.
  [[nodiscard]] constexpr bool contains(Flags other) const noexcept {
    return (fBits & other.fBits) == other.fBits;
  }
  [[nodiscard]] constexpr bool any(Flags other) const noexcept {
    return (fBits & other.fBits) != 0;
  }
  [[nodiscard]] constexpr bool none() const noexcept { return fBits == 0; }
  friend constexpr bool operator==(Flags, Flags) noexcept = default;

private:
  Bits fBits = 0;
};

// ---- geometry -------------------------------------------------------------


namespace axis {
struct x {};
struct y {};
} // namespace axis
struct AxisTag {};
using Axes = Flags<AxisTag, axis::x, axis::y>;
namespace axes {
inline constexpr StaticFlags<AxisTag> kNone;
inline constexpr StaticFlags<AxisTag, axis::x> kX;
inline constexpr StaticFlags<AxisTag, axis::y> kY;
inline constexpr auto kBoth = kX | kY;
} // namespace axes
static_assert(decltype(axes::kX | axes::kY | axes::kX)::has<axis::x>() &&
              decltype(axes::kX | axes::kY | axes::kX)::has<axis::y>());
static_assert(Axes(axes::kBoth).has<axis::x>() &&
              !Axes(axes::kX).has<axis::y>());

// A point of a box, as fractions of its width and height: where a node is
// attached to its parent, and which of its own points lands there.
struct Anchor {
  float x = 0.0f;
  float y = 0.0f;
  constexpr bool operator==(const Anchor &) const = default;
};
namespace anchor {
inline constexpr Anchor kTopLeft{0.0f, 0.0f};
inline constexpr Anchor kTopCentre{0.5f, 0.0f};
inline constexpr Anchor kTopRight{1.0f, 0.0f};
inline constexpr Anchor kCentreLeft{0.0f, 0.5f};
inline constexpr Anchor kCentre{0.5f, 0.5f};
inline constexpr Anchor kCentreRight{1.0f, 0.5f};
inline constexpr Anchor kBottomLeft{0.0f, 1.0f};
inline constexpr Anchor kBottomCentre{0.5f, 1.0f};
inline constexpr Anchor kBottomRight{1.0f, 1.0f};
} // namespace anchor

// Where something sits across the axis it is laid out along, as a fraction
// of the room it has there.
struct Align {
  float at = 0.0f;
  constexpr bool operator==(const Align &) const = default;
};
namespace align {
inline constexpr Align kStart{0.0f};
inline constexpr Align kMiddle{0.5f};
inline constexpr Align kEnd{1.0f};
} // namespace align

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
inline std::uint64_t &visitedCount() {
  static std::uint64_t count = 0;
  return count;
}
inline std::uint64_t &drawnCount() {
  static std::uint64_t count = 0;
  return count;
}

// ---- transforms -----------------------------------------------------------

namespace easing {
struct none {};
struct out {};
struct out_quint {};
struct out_elastic_half {};
} // namespace easing
using Easing = std::variant<easing::out_quint, easing::none, easing::out,
                              easing::out_elastic_half>;

// How far along an ease is at time t of 1.
[[nodiscard]] inline float ease(const Easing &how, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return std::visit(
      overloaded{
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
using Property = std::variant<property::alpha, property::x, property::y,
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

// A role is a type-safe name shared by a node and a selector.
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

// Every layout input a node has, as one aggregate. A field left out is not
// written: "zero" and "not mentioned" are different things.
//
//   place        anchor and origin at once
//   fill         relative size on both axes at 1.0
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

  // Plain, so that `.padding = {2, 6, 2, 6}` compiles. All zero reads as
  // "not mentioned".
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

// The states a rule can ask for.
namespace state {
struct hover {};
struct selected {};
struct disabled {};
struct focus {};
} // namespace state
struct StateTag {};
using StyleStates = Flags<StateTag, state::hover, state::selected,
                          state::disabled, state::focus>;
namespace states {
inline constexpr StaticFlags<StateTag> kNone;
inline constexpr StaticFlags<StateTag, state::hover> kHover;
inline constexpr StaticFlags<StateTag, state::selected> kSelected;
inline constexpr StaticFlags<StateTag, state::disabled> kDisabled;
inline constexpr StaticFlags<StateTag, state::focus> kFocus;
} // namespace states

// The declarations every node understands, plus the inheritable visual ones.
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

  // A state change animates position, size, scale and alpha.
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

// What a rule is matched against: plain data built by the walks.
struct StyleSubject {
  const detail::StyleKey *fType = nullptr;
  const detail::StyleKey *fTemplate = nullptr;
  std::span<const StyleRole> fRoles;
  StyleStates fStates;
  float fViewportWidth = 0.0f;

  [[nodiscard]] bool has(StyleRole role) const noexcept {
    return std::ranges::find(fRoles, role) != fRoles.end();
  }
};

// Selector subjects: a type, a class template (every specialisation), or
// anything.
template <template <class...> class Template> struct OfTemplate {};
struct Anything {};

template <class Subject, class... Roles> class Selector {
public:
  [[nodiscard]] constexpr Selector when(this Selector self,
                                          StyleStates wanted) {
    self.fStates = self.fStates | wanted;
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
  StyleStates fStates;
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
// Whether a subject is what a selector names: overloads on the selector's
// subject type.
template <class T>
[[nodiscard]] bool isSubject(std::type_identity<T>,
                             const StyleSubject &subject) {
  return subject.fType == &styleNodeKey<T>;
}
template <template <class...> class Template>
[[nodiscard]] bool isSubject(std::type_identity<OfTemplate<Template>>,
                             const StyleSubject &subject) {
  return subject.fTemplate == &styleTemplateKey<Template>;
}
[[nodiscard]] inline bool isSubject(std::type_identity<Anything>,
                                    const StyleSubject &) {
  return true;
}
} // namespace detail

template <class Subject, class... Roles> struct StyleRule {
  Selector<Subject, Roles...> fSelector;
  Style fStyle;

  [[nodiscard]] bool matchesSubject(const StyleSubject &node) const {
    return detail::isSubject(std::type_identity<Subject>{}, node) &&
           (node.has(role<Roles>) && ...) &&
           (!fSelector.fMinViewportWidth ||
            node.fViewportWidth >= *fSelector.fMinViewportWidth) &&
           (!fSelector.fMaxViewportWidth ||
            node.fViewportWidth <= *fSelector.fMaxViewportWidth);
  }
  [[nodiscard]] bool matches(const StyleSubject &node) const {
    return node.fStates.contains(fSelector.fStates) &&
           this->matchesSubject(node);
  }
  [[nodiscard]] bool usesState(const StyleSubject &node,
                               StyleStates which) const {
    return fSelector.fStates.any(which) && this->matchesSubject(node);
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
                               StyleStates which) const {
    return std::apply(
        [&](const auto &...rules) {
          return (rules.usesState(node, which) || ... || false);
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
  bool (*fUsesState)(const StyleSubject &, StyleStates) = nullptr;

  [[nodiscard]] explicit operator bool() const noexcept {
    return fResolve != nullptr;
  }
  template <class Theme> [[nodiscard]] static StyleResolver of() {
    return {+[](const StyleSubject &node) {
              return Theme::styles.resolve(node);
            },
            +[](const StyleSubject &node, StyleStates which) {
              return Theme::styles.usesState(node, which);
            }};
  }
};

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

// Where on the way to its node an event is.
namespace phase {
struct capture {};
struct target {};
struct bubble {};
} // namespace phase

// The pointer.
namespace pointer {
struct move {
  float x = 0.0f, y = 0.0f;
};
struct down {
  float x = 0.0f, y = 0.0f;
  int button = 0;
};
struct up {
  float x = 0.0f, y = 0.0f;
  int button = 0;
};
// The gesture is over without ending where it began: a container took it,
// or a modal covered the scene.
struct cancel {
  float x = 0.0f, y = 0.0f;
};
struct scroll {
  float x = 0.0f, y = 0.0f;
  float dx = 0.0f, dy = 0.0f;
};
} // namespace pointer
using PointerEvent = std::variant<pointer::move, pointer::down, pointer::up,
                               pointer::cancel, pointer::scroll>;

// Where a pointer event is.
[[nodiscard]] inline skia::SkPoint where(const PointerEvent &event) {
  return std::visit(
      [](const auto &one) { return skia::SkPoint::Make(one.x, one.y); },
      event);
}
// Whether it ends a gesture.
[[nodiscard]] inline bool ends(const PointerEvent &event) {
  return std::visit(overloaded{[](const pointer::up &) { return true; },
                               [](const pointer::cancel &) { return true; },
                               [](const auto &) { return false; }},
                    event);
}

// What delivering a pointer event says and asks: which node it is for and
// which has it now, and what the handler wants done.
struct PointerReply {
  NodeId fTarget = 0;
  NodeId fCurrent = 0;
  // Whether some node already holds the pointer.
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
  // A gesture-owning ancestor, in the capture phase: the target may arm a
  // tap but must not activate until the ancestor has had its chance to turn
  // the press into a drag.
  void deferClick() noexcept { fDeferClick = true; }
  // Touch scrolling is not pointing: what is under a finger deciding between
  // a tap and a drag does not hover.
  void suppressHover() noexcept { fSuppressHover = true; }
};

// A key: a code, from whatever the window system calls it.
struct Key {
  std::uint16_t code = 0;
  constexpr bool operator==(const Key &) const = default;
};
namespace keys {
inline constexpr Key kUnknown{0};
inline constexpr Key kTab{1};
inline constexpr Key kEnter{2};
inline constexpr Key kSpace{3};
inline constexpr Key kEscape{4};
inline constexpr Key kLeft{5};
inline constexpr Key kRight{6};
inline constexpr Key kUp{7};
inline constexpr Key kDown{8};
inline constexpr Key kHome{9};
inline constexpr Key kEnd{10};
inline constexpr Key kBackspace{11};
inline constexpr Key kDelete{12};
} // namespace keys

namespace modifier {
struct shift {};
struct control {};
struct alt {};
struct super {};
} // namespace modifier
struct ModifierTag {};
using Modifiers = Flags<ModifierTag, modifier::shift, modifier::control,
                        modifier::alt, modifier::super>;
namespace modifiers {
inline constexpr StaticFlags<ModifierTag> kNone;
inline constexpr StaticFlags<ModifierTag, modifier::shift> kShift;
inline constexpr StaticFlags<ModifierTag, modifier::control> kControl;
inline constexpr StaticFlags<ModifierTag, modifier::alt> kAlt;
inline constexpr StaticFlags<ModifierTag, modifier::super> kSuper;
} // namespace modifiers

namespace key {
struct down {
  Key key;
  Modifiers modifiers;
  bool repeat = false;
};
struct up {
  Key key;
  Modifiers modifiers;
};
} // namespace key
using KeyEvent = std::variant<key::down, key::up>;

// Text and composition are distinct: an IME can replace its provisional
// range many times before committing it.
namespace text {
struct commit {
  std::string_view text;
};
struct compose {
  std::string_view text;
  int start = 0;
  int length = 0;
};
} // namespace text
using TextEvent = std::variant<text::commit, text::compose>;

// What assistive technology asks of a node.
namespace semantic_action {
struct focus {};
struct activate {};
struct increment {};
struct decrement {};
struct set_value {
  float value = 0.0f;
  std::string_view text;
};
} // namespace semantic_action
using SemanticAction =
    std::variant<semantic_action::focus, semantic_action::activate,
                 semantic_action::increment, semantic_action::decrement,
                 semantic_action::set_value>;

// What a node is to a screen reader.
namespace semantic_role {
struct none {};
struct group {};
struct button {};
struct text {};
struct text_box {};
struct slider {};
struct toggle {};
struct tab {};
struct list {};
struct list_item {};
} // namespace semantic_role
using SemanticRole =
    std::variant<semantic_role::none, semantic_role::group,
                 semantic_role::button, semantic_role::text,
                 semantic_role::text_box, semantic_role::slider,
                 semantic_role::toggle, semantic_role::tab,
                 semantic_role::list, semantic_role::list_item>;

// What delivering a key, text or semantic action says and asks.
struct Reply {
  NodeId fTarget = 0;
  NodeId fCurrent = 0;
  bool fHandled = false;
  bool fRequestFocus = false;

  void handle() noexcept { fHandled = true; }
  void requestFocus() noexcept { fRequestFocus = true; }
};

struct Semantics {
  NodeId fId = 0;
  SemanticRole fRole;
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

[[nodiscard]] inline bool isTextBox(const SemanticRole &role) {
  return std::visit(overloaded{[](semantic_role::text_box) { return true; },
                               [](auto) { return false; }},
                    role);
}
[[nodiscard]] inline bool hasRole(const SemanticRole &role) {
  return std::visit(overloaded{[](semantic_role::none) { return false; },
                               [](auto) { return true; }},
                    role);
}

// Whether whatever holds focus is somewhere text is typed, told to the host
// when it changes. The host keeps what it passes and clears the hook before
// that goes away.
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
  Axes fRelativeSizeAxes; // size is a fraction of the parent
  Axes fAutoSizeAxes;     // size follows the children
  // Inside a flow, takes an equal share of what the other children leave
  // along the flow's axis.
  Axes fGrowAxes;
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
    if (fRelativeSizeAxes.any(fAutoSizeAxes)) {
      std::println(std::cerr,
                   "[scene] relative and automatic sizing on the same axis");
      fAutoSizeAxes = axes::kNone;
    }
    // A flow writes a grown child's size; anything else claiming the axis
    // would be overwritten every frame.
    if (fGrowAxes.any(fRelativeSizeAxes | fAutoSizeAxes)) {
      std::println(std::cerr,
                   "[scene] growing and sized another way on the same axis");
      fGrowAxes = axes::kNone;
    }
    const auto after = this->commonValues();
    if (!sameLayout(before, after)) {
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
  }
  // Repaints where this node is and where it was drawn last.
  void markDamaged() { fDamaged = true; }

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
    const Easing how = style.transitionEasing.value_or(easing::out_quint{});

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
                            how, animate);                                    \
  }
    SKIFF_STYLE_ANIMATED(x, fX, property::x{});
    SKIFF_STYLE_ANIMATED(y, fY, property::y{});
    SKIFF_STYLE_ANIMATED(width, fWidth, property::width{});
    SKIFF_STYLE_ANIMATED(height, fHeight, property::height{});
    SKIFF_STYLE_ANIMATED(scale, fScale, property::scale{});
    SKIFF_STYLE_ANIMATED(alpha, fAlpha, property::alpha{});
#undef SKIFF_STYLE_ANIMATED

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
  // Where the children were at the last frame, for when they are gone.
  skia::SkRect fChildArea = skia::SkRect::MakeEmpty();
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
    return std::visit(
        overloaded{[this](property::alpha) -> float & { return fAlpha; },
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
    std::visit(overloaded{[this](property::alpha) { this->markDamaged(); },
                          [this](auto) { this->invalidateLayout(); }},
               property);
  }
};


// ---- children --------------------------------------------------------------

class AnyNode;
struct Node;

// A node type that is also a range -- it has begin() and end() -- is walked
// as a range of children unless it says it is a node:
//
//   template <> inline constexpr bool skiff::scene::kTreatAsNode<MyList> = true;
template <class T> inline constexpr bool kTreatAsNode = false;

// What can be a child: a way of holding nodes -- a std::variant (its
// alternative, unless that is std::monostate), a std::optional, a pointer, a
// reference_wrapper, a tuple, an AnyNode, a range -- or a node. One overload
// per holder; anything else is a node.
template <class F> void visitChild(std::monostate &, F &&);
template <class... Ts, class F> void visitChild(std::variant<Ts...> &, F &&);
template <class T, class F> void visitChild(std::optional<T> &, F &&);
template <class T, class D, class F>
void visitChild(std::unique_ptr<T, D> &, F &&);
template <class T, class F> void visitChild(std::shared_ptr<T> &, F &&);
template <class T, class F> void visitChild(std::reference_wrapper<T> &, F &&);
template <class... Ts, class F> void visitChild(std::tuple<Ts...> &, F &&);
template <class F> void visitChild(AnyNode &, F &&);
template <class R, class F>
  requires(std::ranges::range<R> && !kTreatAsNode<R>)
void visitChild(R &, F &&);
template <class N, class F> void visitChild(N &, F &&);

template <class F> void visitChild(std::monostate &, F &&) {}
template <class... Ts, class F>
void visitChild(std::variant<Ts...> &child, F &&f) {
  std::visit([&](auto &alternative) { visitChild(alternative, f); }, child);
}
template <class T, class F> void visitChild(std::optional<T> &child, F &&f) {
  if (child) {
    visitChild(*child, f);
  }
}
template <class T, class D, class F>
void visitChild(std::unique_ptr<T, D> &child, F &&f) {
  if (child) {
    visitChild(*child, f);
  }
}
template <class T, class F>
void visitChild(std::shared_ptr<T> &child, F &&f) {
  if (child) {
    visitChild(*child, f);
  }
}
template <class T, class F>
void visitChild(std::reference_wrapper<T> &child, F &&f) {
  visitChild(child.get(), f);
}
template <class... Ts, class F>
void visitChild(std::tuple<Ts...> &child, F &&f) {
  std::apply([&](auto &...each) { (visitChild(each, f), ...); }, child);
}
template <class R, class F>
  requires(std::ranges::range<R> && !kTreatAsNode<R>)
void visitChild(R &child, F &&f) {
  for (auto &each : child) {
    visitChild(each, f);
  }
}
template <class N, class F> void visitChild(N &child, F &&f) { f(child); }

// Each child of a node, in the order its forEachChild gives them, holders
// seen through.
template <class T, class F> void eachChild(T &node, F &&f) {
  node.forEachChild([&](auto &child) { visitChild(child, f); });
}

// The State of a child: a node's own, or the one inside an AnyNode.
template <class N> [[nodiscard]] State &stateOf(N &child) {
  return child.fState;
}
[[nodiscard]] State &stateOf(AnyNode &child);

// ---- default handling --------------------------------------------------------
//
// What a node does with an event it has no handler for. Overload sets: the
// general one does nothing, and the ones for particular phases and events
// act. A node's own handler calls these for what it does not handle itself.

template <class T, class Phase, class Input>
void defaultPointer(T &, const Phase &, const Input &, PointerReply &) {}
// A press is a click -- unless a gesture-owning ancestor deferred it, when
// the click waits for the release.
template <class T>
void defaultPointer(T &node, const phase::target &, const pointer::down &press,
                    PointerReply &reply) {
  if (reply.fDeferClick) {
    node.fState.fDeferredClick = true;
    reply.handle();
  } else if (node.onClick(press.x, press.y)) {
    reply.handle();
  }
}
template <class T>
void defaultPointer(T &node, const phase::target &, const pointer::up &release,
                    PointerReply &reply) {
  if (std::exchange(node.fState.fDeferredClick, false)) {
    if (node.fState.fBounds.contains(release.x, release.y)) {
      (void)node.onClick(release.x, release.y);
    }
    // The release belongs to where the deferred gesture began, even when it
    // ended outside.
    reply.handle();
  }
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
void defaultSemantic(T &node, const phase::target &,
                     const semantic_action::activate &, Reply &reply) {
  const skia::SkRect &bounds = node.fState.fBounds;
  if (node.onClick(bounds.centerX(), bounds.centerY())) {
    reply.handle();
  }
}

template <class T> void layoutChildrenInContentBox(T &node);
template <class T> void drawDefault(T &node, skia::SkCanvas *canvas, float alpha);

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
  // The whole subtree, overridden by a node that draws it another way.
  void draw(this auto &self, skia::SkCanvas *canvas, float alpha) {
    drawDefault(self, canvas, alpha);
  }

  // -- time
  void update(this auto &, double) {}
  // Part-way to somewhere by hand, so the next frame differs.
  [[nodiscard]] bool settling(this const auto &) { return false; }

  // -- input
  [[nodiscard]] bool acceptsInput(this const auto &) { return false; }
  [[nodiscard]] bool focusable(this const auto &self) {
    return self.acceptsInput();
  }
  [[nodiscard]] bool hoverChangesAppearance(this const auto &) {
    return false;
  }
  [[nodiscard]] bool focusChangesAppearance(this const auto &) {
    return false;
  }
  void onFocusChanged(this auto &, bool) {}
  [[nodiscard]] bool onClick(this auto &, float, float) { return false; }
  [[nodiscard]] bool onScroll(this auto &, float) { return false; }
  void onPointer(this auto &self, const auto &at, const auto &input,
                 PointerReply &reply) {
    defaultPointer(self, at, input, reply);
  }
  void onKey(this auto &self, const auto &at, const auto &input,
             Reply &reply) {
    defaultKey(self, at, input, reply);
  }
  void onText(this auto &, const auto &, const auto &, Reply &) {}
  void onSemantic(this auto &self, const auto &at, const auto &action,
                  Reply &reply) {
    defaultSemantic(self, at, action, reply);
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
};

// A path from a node to one below it: positions of the children taken, in
// eachChild order.
using Path = std::vector<std::uint32_t>;

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
};

// The children in the order they are drawn: eachChild's order, unless one of
// them has a depth. `f` gets the child and its position in eachChild order.
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

template <class N> void layout(N &child, const skia::SkRect &parentBox);
void layout(AnyNode &child, const skia::SkRect &parentBox);
template <class N> void draw(N &child, skia::SkCanvas *canvas, float alpha);
void draw(AnyNode &child, skia::SkCanvas *canvas, float alpha);

// Lays every child out in the content box: what a node without its own
// layoutChildren does.
template <class T> void layoutChildrenInContentBox(T &node) {
  const skia::SkRect box = node.fState.contentBox();
  eachChild(node, [&](auto &child) { layout(child, box); });
}

namespace detail {
template <class T> void layoutNode(T &node, const skia::SkRect &parentBox) {
  State &state = node.fState;
  // Placed against another node, when asked: a dropdown list belongs to the
  // control that opened it.
  const skia::SkRect parent =
      (state.fFollow != nullptr && !state.fFollow->fBounds.isEmpty())
          ? state.fFollow->fBounds
          : parentBox;
  if (state.fLayoutValid && !state.fSubtreeDirty &&
      parent == state.fLastConstraint) {
    return;
  }
  state.fLastConstraint = parent;
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
    state.fBounds = skia::SkRect::MakeXYWH(room.fLeft, room.fTop,
                                           autoX ? parentW : width,
                                           autoY ? parentH : height);
    node.layoutChildren();
    const skia::SkRect content = childBounds(node);
    if (autoX) {
      width = content.width() + state.fPadding.totalX();
    }
    if (autoY) {
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
    // Moved or resized: repaint where it was and where it is.
    state.fMovedDamage =
        joined(joined(state.fMovedDamage, previous), state.fBounds);
  }
  node.layoutChildren();
  state.fLayoutValid = true;
  state.fSubtreeDirty = false;
}

template <class T>
void drawNode(T &node, skia::SkCanvas *canvas, float inheritedAlpha) {
  State &state = node.fState;
  if (!state.fVisible || state.fAlpha <= 0.001f) {
    return;
  }
  // What lies outside what is being repainted is skipped with its subtree.
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

// Lays a child out in a box: what a container's layoutChildren calls for each
// of its children, after placing it.
template <class N> void layout(N &child, const skia::SkRect &parentBox) {
  detail::layoutNode(child, parentBox);
}
// Draws a node and its subtree the way the scene does; a node that draws its
// subtree another way calls this for what it does not do itself.
template <class T> void drawDefault(T &node, skia::SkCanvas *canvas, float alpha) {
  detail::drawNode(node, canvas, alpha);
}
template <class N> void draw(N &child, skia::SkCanvas *canvas, float alpha) {
  child.draw(canvas, alpha);
}

namespace walk {

// Every walk, for an AnyNode: through its table.
void update(AnyNode &, UpdateContext &, StyleResolver, const Style *, bool);
[[nodiscard]] bool markDirty(AnyNode &);
[[nodiscard]] skia::SkRect collectDamage(AnyNode &, bool);
[[nodiscard]] bool hasDamage(AnyNode &);
void hover(AnyNode &, float, float, bool, StyleResolver, float);
[[nodiscard]] bool hitPath(AnyNode &, float, float, Path &);
[[nodiscard]] bool findPath(AnyNode &, NodeId, Path &);
[[nodiscard]] NodeId idAt(AnyNode &, const Path &, std::size_t);
void routePointer(AnyNode &, const Path &, std::size_t, const PointerEvent &,
                  PointerReply &, Routed &, bool);
void routeKey(AnyNode &, const Path &, std::size_t, const KeyEvent &, Reply &);
void routeText(AnyNode &, const Path &, std::size_t, const TextEvent &, Reply &);
void routeSemantic(AnyNode &, const Path &, std::size_t,
                   const SemanticAction &, Reply &);
[[nodiscard]] std::optional<NodeInfo> info(AnyNode &, NodeId);
[[nodiscard]] bool focusChanged(AnyNode &, NodeId, bool, StyleResolver, float);
void collectSemantics(AnyNode &, std::vector<Semantics> &, int, NodeId);
void collectFocusable(AnyNode &, std::vector<NodeId> &);
[[nodiscard]] bool animating(AnyNode &);
[[nodiscard]] bool clickPath(AnyNode &, const Path &, std::size_t, float,
                             float);

// Transforms, the node's own time, and styles.
template <class N>
void update(N &child, UpdateContext &context, StyleResolver resolver,
            const Style *inherited, bool restyleAll) {
  State &state = child.fState;
  state.updateTransforms(context.fNowMs);
  child.update(context.fNowMs);
  if (!state.fTransforms.empty() || child.settling()) {
    context.fAnimating = true;
  }
  const StyleResolver own = state.fStyleResolver ? state.fStyleResolver
                                                 : resolver;
  const bool restyle =
      restyleAll || state.fStyleDirty || state.fStyleSheetChanged;
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

// What has to be laid out again, found bottom-up: nothing below can tell its
// ancestors, so the frame asks. A node whose set of children changed is laid
// out again and repainted where they were.
template <class N> bool markDirty(N &child) {
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
    state.fMovedDamage = joined(state.fMovedDamage, state.fChildArea);
  }
  state.fSubtreeDirty = below || !state.fLayoutValid;
  return state.fSubtreeDirty;
}

// What has to be repainted, and forgets it. A masking node clips what its
// subtree reports; a hidden one drops it, though its own change counts.
template <class N> skia::SkRect collectDamage(N &child, bool drawnAbove) {
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
  skia::SkRect area = skia::SkRect::MakeEmpty();
  eachChild(child, [&](auto &each) {
    below = joined(below, walk::collectDamage(each, drawn));
    const State &one = stateOf(each);
    area = joined(joined(area, one.fBounds), one.fDrawnBounds);
  });
  // Where the children are, laid out: what is repainted if they go.
  state.fChildArea = area;
  if (!below.isEmpty() && state.fMasking && !below.intersect(state.fBounds)) {
    below = skia::SkRect::MakeEmpty();
  }
  return joined(damage, below);
}

template <class N> bool hasDamage(N &child) {
  const State &state = child.fState;
  if (state.fDamaged || !state.fMovedDamage.isEmpty()) {
    return true;
  }
  bool any = false;
  eachChild(child, [&](auto &each) { any = any || walk::hasDamage(each); });
  return any;
}

// Every node remembers where the pointer is: a control with parts has to
// know which of its parts is under it.
template <class N>
void hover(N &child, float x, float y, bool visibleAbove,
           StyleResolver resolver, float viewportWidth) {
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
                              states::kHover)) {
      state.restyle(true);
    }
    if (child.hoverChangesAppearance()) {
      state.markDamaged();
    }
  }
  const bool childrenVisible =
      visible && (!state.fMasking || state.fBounds.contains(x, y));
  eachChild(child, [&](auto &each) {
    walk::hover(each, x, y, childrenVisible, own, viewportWidth);
  });
}

// The front-most node under a point that takes input: what is drawn last is
// hit first.
template <class N> bool hitPath(N &child, float x, float y, Path &path) {
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
  return child.acceptsInput() && state.fBounds.contains(x, y);
}

template <class N> bool findPath(N &child, NodeId id, Path &path) {
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
template <class N>
void routePointer(N &child, const Path &path, std::size_t at,
                  const PointerEvent &input, PointerReply &reply, Routed &routed,
                  bool targetOnly) {
  State &state = child.fState;
  const auto deliver = [&](const auto &when) {
    reply.fCurrent = state.fId;
    reply.fCapturePointer = false;
    reply.fReleasePointer = false;
    reply.fRequestFocus = false;
    std::visit([&](const auto &event) { child.onPointer(when, event, reply); },
               input);
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
      routed.fTargetFocusable = child.focusable();
      deliver(phase::target{});
    }
    return;
  }
  if (!targetOnly && !reply.fHandled) {
    deliver(phase::capture{});
  }
  childAt(child, path[at], [&](auto &each) {
    walk::routePointer(each, path, at + 1, input, reply, routed, targetOnly);
  });
  if (!targetOnly && !reply.fHandled) {
    deliver(phase::bubble{});
  }
}

// The same for keys, text and semantic actions, which have one kind of reply.
template <class N, class Input, class Deliver>
void route(N &child, const Path &path, std::size_t at, const Input &input,
           Reply &reply, Deliver deliver) {
  const auto phaseOf = [&](const auto &when) {
    reply.fCurrent = child.fState.fId;
    deliver(child, when, input, reply);
  };
  if (at == path.size()) {
    if (!reply.fHandled) {
      phaseOf(phase::target{});
    }
    return;
  }
  if (!reply.fHandled) {
    phaseOf(phase::capture{});
  }
  childAt(child, path[at], [&](auto &each) {
    deliver.next(each, path, at + 1, input, reply);
  });
  if (!reply.fHandled) {
    phaseOf(phase::bubble{});
  }
}

struct KeyDelivery {
  template <class N>
  void operator()(N &node, const auto &when, const KeyEvent &input,
                  Reply &reply) const {
    std::visit([&](const auto &event) { node.onKey(when, event, reply); },
               input);
  }
  template <class C>
  void next(C &child, const Path &path, std::size_t at, const KeyEvent &input,
            Reply &reply) const;
};
struct TextDelivery {
  template <class N>
  void operator()(N &node, const auto &when, const TextEvent &input,
                  Reply &reply) const {
    std::visit([&](const auto &event) { node.onText(when, event, reply); },
               input);
  }
  template <class C>
  void next(C &child, const Path &path, std::size_t at, const TextEvent &input,
            Reply &reply) const;
};
struct SemanticDelivery {
  template <class N>
  void operator()(N &node, const auto &when, const SemanticAction &input,
                  Reply &reply) const {
    std::visit([&](const auto &event) { node.onSemantic(when, event, reply); },
               input);
  }
  template <class C>
  void next(C &child, const Path &path, std::size_t at,
            const SemanticAction &input, Reply &reply) const;
};

template <class N>
void routeKey(N &child, const Path &path, std::size_t at, const KeyEvent &input,
              Reply &reply) {
  route(child, path, at, input, reply, KeyDelivery{});
}
template <class N>
void routeText(N &child, const Path &path, std::size_t at,
               const TextEvent &input, Reply &reply) {
  route(child, path, at, input, reply, TextDelivery{});
}
template <class N>
void routeSemantic(N &child, const Path &path, std::size_t at,
                   const SemanticAction &input, Reply &reply) {
  route(child, path, at, input, reply, SemanticDelivery{});
}
template <class C>
void KeyDelivery::next(C &child, const Path &path, std::size_t at,
                       const KeyEvent &input, Reply &reply) const {
  walk::routeKey(child, path, at, input, reply);
}
template <class C>
void TextDelivery::next(C &child, const Path &path, std::size_t at,
                        const TextEvent &input, Reply &reply) const {
  walk::routeText(child, path, at, input, reply);
}
template <class C>
void SemanticDelivery::next(C &child, const Path &path, std::size_t at,
                            const SemanticAction &input,
                            Reply &reply) const {
  walk::routeSemantic(child, path, at, input, reply);
}

template <class N> std::optional<NodeInfo> info(N &child, NodeId id) {
  const State &state = child.fState;
  if (state.fId == id) {
    return NodeInfo{child.focusable(), state.fDisabled, state.fVisible,
                    isTextBox(child.semantics().fRole)};
  }
  std::optional<NodeInfo> found;
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
    child.onFocusChanged(focused);
    if (own && own.fUsesState(styleSubject(child, viewportWidth),
                              states::kFocus)) {
      state.restyle(true);
    }
    if (child.focusChangesAppearance()) {
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

template <class N> bool animating(N &child) {
  if (!child.fState.fTransforms.empty() || child.settling()) {
    return true;
  }
  bool any = false;
  eachChild(child, [&](auto &each) { any = any || walk::animating(each); });
  return any;
}

// A click told straight to the node at the end of a path and, when it does
// not take it, to each node above in turn.
template <class N>
bool clickPath(N &child, const Path &path, std::size_t at, float x, float y) {
  bool taken = false;
  if (at < path.size()) {
    childAt(child, path[at], [&](auto &each) {
      taken = walk::clickPath(each, path, at + 1, x, y);
    });
  }
  return taken ||
         (child.fState.fBounds.contains(x, y) && child.onClick(x, y));
}

} // namespace walk

// ---- AnyNode ----------------------------------------------------------------

// Any node, held by value, erased the way std::function erases a callable:
// one allocation, and a table of the walks for the node's own type.
class AnyNode {
public:
  AnyNode() = default;
  template <class T>
    requires std::derived_from<std::remove_cvref_t<T>, Node>
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
  template <class T> [[nodiscard]] T *get() noexcept {
    return fOps == &kOps<T> ? static_cast<T *>(fNode) : nullptr;
  }
  [[nodiscard]] State &state() { return fOps->fState(fNode); }

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
    void (*fRoutePointer)(void *, const Path &, std::size_t,
                          const PointerEvent &, PointerReply &, Routed &, bool);
    void (*fRouteKey)(void *, const Path &, std::size_t, const KeyEvent &,
                      Reply &);
    void (*fRouteText)(void *, const Path &, std::size_t, const TextEvent &,
                       Reply &);
    void (*fRouteSemantic)(void *, const Path &, std::size_t,
                           const SemanticAction &, Reply &);
    std::optional<NodeInfo> (*fInfo)(void *, NodeId);
    bool (*fFocusChanged)(void *, NodeId, bool, StyleResolver, float);
    void (*fCollectSemantics)(void *, std::vector<Semantics> &, int, NodeId);
    void (*fCollectFocusable)(void *, std::vector<NodeId> &);
    bool (*fAnimating)(void *);
    bool (*fClickPath)(void *, const Path &, std::size_t, float, float);
  };
  [[nodiscard]] const Ops &ops() const noexcept { return *fOps; }
  [[nodiscard]] void *node() const noexcept { return fNode; }

private:
  template <class T> [[nodiscard]] static T &as(void *node) {
    return *static_cast<T *>(node);
  }

  template <class T>
  static constexpr Ops kOps{
      +[](void *n) { delete static_cast<T *>(n); },
      +[](void *n) -> State & { return as<T>(n).fState; },
      +[](void *n, const skia::SkRect &box) { scene::layout(as<T>(n), box); },
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
      +[](void *n, const Path &path, std::size_t at, const PointerEvent &e,
          PointerReply &reply, Routed &routed, bool targetOnly) {
        walk::routePointer(as<T>(n), path, at, e, reply, routed, targetOnly);
      },
      +[](void *n, const Path &path, std::size_t at, const KeyEvent &e,
          Reply &reply) { walk::routeKey(as<T>(n), path, at, e, reply); },
      +[](void *n, const Path &path, std::size_t at, const TextEvent &e,
          Reply &reply) { walk::routeText(as<T>(n), path, at, e, reply); },
      +[](void *n, const Path &path, std::size_t at,
          const SemanticAction &e, Reply &reply) {
        walk::routeSemantic(as<T>(n), path, at, e, reply);
      },
      +[](void *n, NodeId id) { return walk::info(as<T>(n), id); },
      +[](void *n, NodeId id, bool focused, StyleResolver r, float width) {
        return walk::focusChanged(as<T>(n), id, focused, r, width);
      },
      +[](void *n, std::vector<Semantics> &out, int parent, NodeId focused) {
        walk::collectSemantics(as<T>(n), out, parent, focused);
      },
      +[](void *n, std::vector<NodeId> &out) {
        walk::collectFocusable(as<T>(n), out);
      },
      +[](void *n) { return walk::animating(as<T>(n)); },
      +[](void *n, const Path &path, std::size_t at, float x, float y) {
        return walk::clickPath(as<T>(n), path, at, x, y);
      },
  };

  void *fNode = nullptr;
  const Ops *fOps = nullptr;
};

template <class F> void visitChild(AnyNode &child, F &&f) {
  if (child) {
    f(child);
  }
}
inline State &stateOf(AnyNode &child) { return child.state(); }
inline void layout(AnyNode &child, const skia::SkRect &box) {
  child.ops().fLayout(child.node(), box);
}
inline void draw(AnyNode &child, skia::SkCanvas *canvas, float alpha) {
  child.ops().fDraw(child.node(), canvas, alpha);
}

namespace walk {
inline void update(AnyNode &c, UpdateContext &context, StyleResolver r,
                   const Style *s, bool all) {
  c.ops().fUpdate(c.node(), context, r, s, all);
}
inline bool markDirty(AnyNode &c) { return c.ops().fMarkDirty(c.node()); }
inline skia::SkRect collectDamage(AnyNode &c, bool drawn) {
  return c.ops().fCollectDamage(c.node(), drawn);
}
inline bool hasDamage(AnyNode &c) { return c.ops().fHasDamage(c.node()); }
inline void hover(AnyNode &c, float x, float y, bool visible, StyleResolver r,
                  float width) {
  c.ops().fHover(c.node(), x, y, visible, r, width);
}
inline bool hitPath(AnyNode &c, float x, float y, Path &path) {
  return c.ops().fHitPath(c.node(), x, y, path);
}
inline bool findPath(AnyNode &c, NodeId id, Path &path) {
  return c.ops().fFindPath(c.node(), id, path);
}
inline NodeId idAt(AnyNode &c, const Path &path, std::size_t at) {
  return c.ops().fIdAt(c.node(), path, at);
}
inline void routePointer(AnyNode &c, const Path &path, std::size_t at,
                         const PointerEvent &e, PointerReply &reply,
                         Routed &routed, bool targetOnly) {
  c.ops().fRoutePointer(c.node(), path, at, e, reply, routed, targetOnly);
}
inline void routeKey(AnyNode &c, const Path &path, std::size_t at,
                     const KeyEvent &e, Reply &reply) {
  c.ops().fRouteKey(c.node(), path, at, e, reply);
}
inline void routeText(AnyNode &c, const Path &path, std::size_t at,
                      const TextEvent &e, Reply &reply) {
  c.ops().fRouteText(c.node(), path, at, e, reply);
}
inline void routeSemantic(AnyNode &c, const Path &path, std::size_t at,
                          const SemanticAction &e, Reply &reply) {
  c.ops().fRouteSemantic(c.node(), path, at, e, reply);
}
inline std::optional<NodeInfo> info(AnyNode &c, NodeId id) {
  return c.ops().fInfo(c.node(), id);
}
inline bool focusChanged(AnyNode &c, NodeId id, bool focused, StyleResolver r,
                         float width) {
  return c.ops().fFocusChanged(c.node(), id, focused, r, width);
}
inline void collectSemantics(AnyNode &c, std::vector<Semantics> &out,
                             int parent, NodeId focused) {
  c.ops().fCollectSemantics(c.node(), out, parent, focused);
}
inline void collectFocusable(AnyNode &c, std::vector<NodeId> &out) {
  c.ops().fCollectFocusable(c.node(), out);
}
inline bool animating(AnyNode &c) { return c.ops().fAnimating(c.node()); }
inline bool clickPath(AnyNode &c, const Path &path, std::size_t at, float x,
                      float y) {
  return c.ops().fClickPath(c.node(), path, at, x, y);
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

  void update(double nowMs) {
    fNowMs = nowMs;
    UpdateContext context{nowMs, fViewport.width(), false};
    walk::update(fRoot, context, {}, nullptr, false);
  }

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
  bool dispatchPointer(const PointerEvent &input) {
    const skia::SkPoint at = where(input);
    std::visit(overloaded{[&](const pointer::move &) {
                            this->setHover(at.fX, at.fY);
                          },
                          [](const auto &) {}},
               input);
    const bool ending = ends(input);
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
      found = walk::hitPath(fRoot, at.fX, at.fY, path);
    }
    const bool press = std::visit(
        overloaded{[](const pointer::down &) { return true; },
                   [](const auto &) { return false; }},
        input);
    if (!found) {
      if (press) {
        this->focus(0);
      }
      if (ending) {
        fCapture = 0;
        fDown = 0;
      }
      return false;
    }
    const NodeId target = walk::idAt(fRoot, path, 0);
    if (press) {
      fDown = target;
    }
    PointerReply reply;
    reply.fTarget = target;
    reply.fCaptured = fCapture != 0;
    Routed routed;
    walk::routePointer(fRoot, path, 0, input, reply, routed, false);

    if (routed.fReleaseRequest || ending) {
      fCapture = 0;
    } else if (routed.fCaptureRequest != 0) {
      // A container claiming a drag cancels the control where the press
      // began: it must not stay armed and activate after scrolling.
      if (fDown != 0 && fDown != routed.fCaptureRequest) {
        Path down;
        if (walk::findPath(fRoot, fDown, down)) {
          PointerReply cancelled;
          cancelled.fTarget = fDown;
          Routed ignored;
          walk::routePointer(fRoot, down, 0,
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
    if (routed.fFocusRequest != 0) {
      this->focus(routed.fFocusRequest);
    } else if (routed.fTargetDelivered && press && routed.fTargetFocusable) {
      this->focus(target);
    }
    this->restyleDirty();
    return reply.fHandled;
  }

  // A click at a point: onClick of the front-most node there that takes
  // input, then of each node above it until one takes it.
  bool click(float x, float y) {
    Path path;
    if (!walk::hitPath(fRoot, x, y, path)) {
      return false;
    }
    const bool taken = walk::clickPath(fRoot, path, 0, x, y);
    this->restyleDirty();
    return taken;
  }

  bool dispatchKey(const KeyEvent &input) {
    // Tab moves the focus; Shift+Tab back.
    const std::optional<bool> tab = std::visit(
        overloaded{[](const key::down &press) -> std::optional<bool> {
                     if (press.key == keys::kTab) {
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
    walk::routeKey(fRoot, path, 0, input, reply);
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
    walk::routeText(fRoot, path, 0, input, reply);
    this->restyleDirty();
    return reply.fHandled;
  }

  bool dispatchSemantic(NodeId id, const SemanticAction &action) {
    const std::optional<NodeInfo> about = walk::info(fRoot, id);
    Path path;
    if (!about || !about->fVisible || about->fDisabled ||
        !walk::findPath(fRoot, id, path)) {
      return false;
    }
    Reply reply;
    reply.fTarget = id;
    walk::routeSemantic(fRoot, path, 0, action, reply);
    if (reply.fRequestFocus) {
      this->focus(id);
    }
    this->restyleDirty();
    return reply.fHandled;
  }

  [[nodiscard]] std::vector<Semantics> semanticsTree() {
    std::vector<Semantics> out;
    walk::collectSemantics(fRoot, out, -1, fFocus);
    return out;
  }

  // Gives a node focus, or takes it from all with 0.
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
  void focus(const Node &node) { this->focus(node.id()); }
  void clearFocus() { this->focus(0); }
  [[nodiscard]] NodeId focusedId() {
    Path ignored;
    return this->focusPath(ignored) ? fFocus : 0;
  }
  [[nodiscard]] bool focusedTakesText() {
    const NodeId id = this->focusedId();
    const std::optional<NodeInfo> about =
        id != 0 ? walk::info(fRoot, id) : std::nullopt;
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
  // Styles whose inputs changed, applied now, so what a handler sees next is
  // current.
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
    if (fCaptured.alive()) {
      const bool handled = fCaptured.pointer(input);
      if (fCaptured.captured() == 0) {
        fCaptured = {};
      }
      return handled;
    }
    fCaptured = {};
    const bool press = std::visit(
        overloaded{[](const pointer::down &) { return true; },
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
    const std::optional<bool> tab = std::visit(
        overloaded{[](const key::down &press) -> std::optional<bool> {
                     if (press.key == keys::kTab) {
                       return press.modifiers.has<modifier::shift>();
                     }
                     return std::nullopt;
                   },
                   [](const auto &) -> std::optional<bool> {
                     return std::nullopt;
                   }},
        input);
    if (tab) {
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
    const bool focusing = std::visit(
        overloaded{[](const semantic_action::focus &) { return true; },
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
