export module skiff.core;

import std;
import splice;
import skiff.aggregate;

// What every backend of skiff shares, and no backend is in: the vocabulary a
// tree is written in (Spec, anchors, margins, roles and typed stylesheets),
// the events a tree is sent and what it answers, the semantics it exposes,
// what the host and the program hand each other (HostWork), the names of
// node types, how a node's children are held and walked, and a node of any
// type seen through the table of its type (AnyNode, AnyNodeRef).
//
// skiff.model is below this and skiff.bind above it: a model is bound to a
// tree through what is here, so the binding is written once for the scene
// that draws through Skia and for a page that is HTML. Nothing here knows a
// pixel, a canvas or a font; a Rect is four floats, a Colour is Skia's word.
//
// Everything is in skiff::scene, where it always was: a program reads
// scene::Spec as before, from either module.
export namespace skiff::scene {

// Whether the walks over a tree see each child through an AnyNodeRef: outside
// a release build. Each walk is then made once for AnyNode, not for every
// type of node under it -- a program's tree of hundreds of types made its
// walks most of a build -- and nothing is inlined across a node's edge.
// skiff's CMake says which build this is: C++ cannot see it.
#ifdef SKIFF_ERASED_WALKS
inline constexpr bool kErasedWalks = true;
#else
inline constexpr bool kErasedWalks = false;
#endif
// Whether what each combinator returns is erased (compose::Held): where the
// walks are, unless a build keeps them concrete -- skiff's own tests of each
// widget's behaviour, which call its members on what its combinator made.
#if defined(SKIFF_ERASED_WALKS) && !defined(SKIFF_CONCRETE_COMBINATORS)
inline constexpr bool kErasedCombinators = true;
#else
inline constexpr bool kErasedCombinators = false;
#endif



// ---- colour and geometry, with no backend in them ------------------------------
//
// A colour as Skia keeps one, 0xAARRGGBB in a std::uint32_t: the same type as
// SkColor, so nothing is converted between a scene and this. A point and a
// box spelled as Skia spells them (fX, fY; fLeft, fTop, fRight, fBottom), and
// converting to and from any type spelled so (SkPoint, SkRect): the scene
// hands them to Skia and takes them back without a word, and a backend with
// no Skia reads them as they are.
using Colour = std::uint32_t;
struct Point {
  float fX = 0.0f, fY = 0.0f;
  constexpr Point() = default;
  constexpr Point(float x, float y) : fX(x), fY(y) {}
  template <class P>
    requires(!std::same_as<std::remove_cvref_t<P>, Point> && requires(const P &p) {
      { p.fX } -> std::convertible_to<float>;
      { p.fY } -> std::convertible_to<float>;
    })
  constexpr Point(const P &p) : fX(p.fX), fY(p.fY) {} // NOLINT: converting, as from SkPoint
  [[nodiscard]] static constexpr Point Make(float x, float y) { return {x, y}; }
  [[nodiscard]] constexpr float x() const noexcept { return fX; }
  [[nodiscard]] constexpr float y() const noexcept { return fY; }
  template <class P>
    requires(!std::same_as<P, Point> && requires(float a) { { P::Make(a, a) } -> std::same_as<P>; })
  constexpr operator P() const { return P::Make(fX, fY); } // NOLINT: converting, as to SkPoint
  friend constexpr bool operator==(const Point &, const Point &) = default;
};
struct Rect {
  float fLeft = 0.0f, fTop = 0.0f, fRight = 0.0f, fBottom = 0.0f;
  constexpr Rect() = default;
  constexpr Rect(float l, float t, float r, float b) : fLeft(l), fTop(t), fRight(r), fBottom(b) {}
  template <class R>
    requires(!std::same_as<std::remove_cvref_t<R>, Rect> && requires(const R &r) {
      { r.fLeft } -> std::convertible_to<float>;
      { r.fTop } -> std::convertible_to<float>;
      { r.fRight } -> std::convertible_to<float>;
      { r.fBottom } -> std::convertible_to<float>;
    })
  constexpr Rect(const R &r) : fLeft(r.fLeft), fTop(r.fTop), fRight(r.fRight), fBottom(r.fBottom) {} // NOLINT: as from SkRect
  [[nodiscard]] static constexpr Rect MakeEmpty() { return {}; }
  [[nodiscard]] static constexpr Rect MakeLTRB(float l, float t, float r, float b) { return {l, t, r, b}; }
  [[nodiscard]] static constexpr Rect MakeXYWH(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
  [[nodiscard]] static constexpr Rect MakeWH(float w, float h) { return {0.0f, 0.0f, w, h}; }
  [[nodiscard]] constexpr float x() const noexcept { return fLeft; }
  [[nodiscard]] constexpr float y() const noexcept { return fTop; }
  [[nodiscard]] constexpr float left() const noexcept { return fLeft; }
  [[nodiscard]] constexpr float top() const noexcept { return fTop; }
  [[nodiscard]] constexpr float right() const noexcept { return fRight; }
  [[nodiscard]] constexpr float bottom() const noexcept { return fBottom; }
  [[nodiscard]] constexpr float width() const noexcept { return fRight - fLeft; }
  [[nodiscard]] constexpr float height() const noexcept { return fBottom - fTop; }
  [[nodiscard]] constexpr float centerX() const noexcept { return (fLeft + fRight) * 0.5f; }
  [[nodiscard]] constexpr float centerY() const noexcept { return (fTop + fBottom) * 0.5f; }
  [[nodiscard]] constexpr bool isEmpty() const noexcept { return !(fLeft < fRight && fTop < fBottom); }
  [[nodiscard]] constexpr bool contains(float x, float y) const noexcept {
    return x >= fLeft && x < fRight && y >= fTop && y < fBottom;
  }
  [[nodiscard]] constexpr bool contains(const Rect &r) const noexcept {
    return !r.isEmpty() && !isEmpty() && fLeft <= r.fLeft && fTop <= r.fTop && fRight >= r.fRight && fBottom >= r.fBottom;
  }
  [[nodiscard]] static constexpr bool Intersects(const Rect &a, const Rect &b) noexcept {
    return a.fLeft < b.fRight && b.fLeft < a.fRight && a.fTop < b.fBottom && b.fTop < a.fBottom;
  }
  [[nodiscard]] constexpr bool intersects(const Rect &r) const noexcept { return Intersects(*this, r); }
  constexpr bool intersect(const Rect &r) noexcept {
    if (!Intersects(*this, r)) return false;
    fLeft = std::max(fLeft, r.fLeft); fTop = std::max(fTop, r.fTop);
    fRight = std::min(fRight, r.fRight); fBottom = std::min(fBottom, r.fBottom);
    return true;
  }
  constexpr void join(const Rect &r) noexcept {
    if (r.isEmpty()) return;
    if (isEmpty()) { *this = r; return; }
    fLeft = std::min(fLeft, r.fLeft); fTop = std::min(fTop, r.fTop);
    fRight = std::max(fRight, r.fRight); fBottom = std::max(fBottom, r.fBottom);
  }
  constexpr void offset(float dx, float dy) noexcept { fLeft += dx; fRight += dx; fTop += dy; fBottom += dy; }
  [[nodiscard]] constexpr Rect makeOffset(float dx, float dy) const noexcept { return {fLeft + dx, fTop + dy, fRight + dx, fBottom + dy}; }
  constexpr void inset(float dx, float dy) noexcept { fLeft += dx; fRight -= dx; fTop += dy; fBottom -= dy; }
  [[nodiscard]] constexpr Rect makeInset(float dx, float dy) const noexcept { return {fLeft + dx, fTop + dy, fRight - dx, fBottom - dy}; }
  constexpr void setEmpty() noexcept { *this = Rect{}; }
  constexpr void setLTRB(float l, float t, float r, float b) noexcept { *this = Rect{l, t, r, b}; }
  constexpr void setXYWH(float x, float y, float w, float h) noexcept { *this = MakeXYWH(x, y, w, h); }
  template <class R>
    requires(!std::same_as<R, Rect> && requires(float a) { { R::MakeLTRB(a, a, a, a) } -> std::same_as<R>; })
  constexpr operator R() const { return R::MakeLTRB(fLeft, fTop, fRight, fBottom); } // NOLINT: as to SkRect
  friend constexpr bool operator==(const Rect &, const Rect &) = default;
};
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
  // These flags, but those of `other`.
  [[nodiscard]] constexpr Flags except(Flags other) const noexcept {
    return Flags(static_cast<Bits>(fBits & ~other.fBits));
  }
  friend constexpr bool operator==(Flags, Flags) noexcept = default;

private:
  Bits fBits = 0;
};

// ---- geometry -------------------------------------------------------------


// Each axis says what it is along a box and its margins, so that what is
// written for one is written for both, by the axis, not by asking which.
namespace axis {
struct x {
  static constexpr bool kHorizontal = true;
  [[nodiscard]] static constexpr float length(const auto &box) {
    return box.width();
  }
  [[nodiscard]] static constexpr float margins(const auto &margin) {
    return margin.totalX();
  }
};
struct y {
  static constexpr bool kHorizontal = false;
  [[nodiscard]] static constexpr float length(const auto &box) {
    return box.height();
  }
  [[nodiscard]] static constexpr float margins(const auto &margin) {
    return margin.totalY();
  }
};
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

// A margin a spec may leave out. Braces of four give one -- `{2, 6, 2, 6}`,
// as a Margin's -- and four zeros give zero; nothing leaves the node's as it
// is. Not a std::optional<Margin>: braces of four are its constructor's
// arguments there, and every spec would need a second pair.
struct SpecMargin {
  std::optional<Margin> given;
  constexpr SpecMargin() = default;
  constexpr SpecMargin(float top, float right, float bottom, float left)
      : given(Margin{top, right, bottom, left}) {}
  constexpr SpecMargin(Margin margin) : given(margin) {}
  friend constexpr bool operator==(const SpecMargin &, const SpecMargin &) = default;
};

namespace easing {
struct none {};
struct out {};
struct out_quint {};
struct out_elastic_half {};
} // namespace easing
using Easing = spl::variant<easing::out_quint, easing::none, easing::out,
                              easing::out_elastic_half>;

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
// A background going from one colour at the top to another at the bottom.
struct Gradient {
  Colour top = 0;
  Colour bottom = 0;
  friend bool operator==(const Gradient &, const Gradient &) = default;
};

// A shadow under a node's box: its shape, moved down, in a colour.
struct Shadow {
  Colour colour = 0;
  float offsetY = 3.0f;
  friend bool operator==(const Shadow &, const Shadow &) = default;
};

// A line around a node's box, inside it, following its corner radius.
struct Border {
  Colour colour = 0;
  float width = 1.0f;
  friend bool operator==(const Border &, const Border &) = default;
};

// Each corner's radius, where they differ: a chat bubble's corner on its
// sender's side squared, as the last of a run is.
struct Corners {
  float topLeft = 0.0f, topRight = 0.0f, bottomRight = 0.0f, bottomLeft = 0.0f;
  friend bool operator==(const Corners &, const Corners &) = default;
};

// A chat bubble's tail: out of its box at the bottom, on one side, curving
// from the box's edge down to its tip -- painted in the box's fill as one
// shape with it, so a fill that is see-through is so once, with no seam
// where the two meet. `shown` false takes it off again.
namespace tail_side {
struct left {
  friend bool operator==(left, left) = default;
};
struct right {
  friend bool operator==(right, right) = default;
};
} // namespace tail_side
using TailSide = spl::variant<tail_side::left, tail_side::right>;
struct Tail {
  TailSide side = tail_side::right{};
  float width = 10.0f, height = 12.0f;
  bool shown = true;
  friend bool operator==(const Tail &, const Tail &) = default;
};

// ---- what nodes show, said without a backend ---------------------------------
// How a picture fits its box: filling it, cut at the edges, or whole within it.
namespace fit {
struct cover {};
struct contain {};
} // namespace fit
using Fit = spl::variant<fit::cover, fit::contain>;
// A picture as a page shows it: where it is -- a URL (http, data, blob).
struct Picture {
  std::string url;
  friend bool operator==(const Picture &, const Picture &) = default;
};
// A flow's way, its spacing, whether it wraps, where its children sit across
// its way and how they are justified along it.
namespace direction {
struct vertical {};
struct horizontal {};
} // namespace direction
using Direction = spl::variant<direction::vertical, direction::horizontal>;
namespace justify {
struct start {};
struct middle {};
struct end {};
struct space_between {};
struct space_around {};
} // namespace justify
using Justify = spl::variant<justify::start, justify::middle, justify::end, justify::space_between, justify::space_around>;
struct FlowOptions {
  Direction direction = direction::vertical{};
  float spacingX = 0.0f;
  float spacingY = 0.0f;
  // A horizontal flow breaks into rows at its edge.
  bool wrap = true;
  // Rows centred in the container, as the cards are.
  bool centreRows = false;
  // Where children sit across the axis; a child can say otherwise with alignSelf.
  Align crossAlign = align::kStart;
  Justify justify = justify::start{};
};
// An icon's marks -- lines, circles, arcs, rounded boxes, paths -- about the
// middle of its box, stroked or filled, in the icon's colour or their own.
namespace path_step {
struct move {
  float x, y;
};
struct line {
  float x, y;
};
struct cubic {
  float x1, y1, x2, y2, x, y;
};
struct close {};
} // namespace path_step
using PathStep = spl::variant<path_step::move, path_step::line, path_step::cubic, path_step::close>;
namespace mark {
struct line {
  float x1, y1, x2, y2;
};
struct circle {
  float x, y, radius;
};
struct arc {
  float left, top, right, bottom, start, sweep;
};
struct rect {
  float left, top, right, bottom, radius = 0.0f;
};
struct path {
  std::vector<PathStep> steps;
};
} // namespace mark
using MarkShape = spl::variant<mark::line, mark::circle, mark::arc, mark::rect, mark::path>;
struct Mark {
  MarkShape shape;
  float width = 1.8f;  // the stroke's; ignored where filled
  bool filled = false;
  std::optional<Colour> colour;  // its own, over the icon's
};
struct IconShape {
  std::vector<Mark> marks;
  float rotation = 0.0f;  // degrees, about the middle
};

// ---- what a text says of its stretches, said without a backend --------------
// Links in the text: a range of it, and where it goes. Drawn in their own
// colour, underlined, where they stand; a press on one that does not
// become a selection opens it through skiff::scene::openLink.
struct TextLink {
  std::size_t first = 0;
  std::size_t last = 0;
  std::string target;
  // A pill, as a mention is drawn: a rounded plate behind it and, at its
  // start, a picture the program paints (its text leaves room for it).
  bool pill = false;
  // A picture in the line, in place of its range: a custom emoji. The
  // range is a placeholder the program put in the text for the room it
  // takes (an em space); the picture is the program's, for the target,
  // as the text's Pictures say.
  bool picture = false;
  std::string plain;  // custom emoji label, copied in place of its placeholder
};

// Stretches of the text drawn otherwise: strong, emphasised (slanted),
// struck through, as code (on a tinted plate), as a quote (in the quote's
// colour, a bar at its line's start) -- what a message's HTML says of it.
struct TextStyled {
  std::size_t first = 0;
  std::size_t last = 0;
  bool strong = false;
  bool emphasis = false;
  bool struck = false;
  bool code = false;
  bool quote = false;
  bool underline = false;
  // A spoiler: hidden under a plate of dots until the text is pressed,
  // which shows all of its spoilers.
  bool spoiler = false;
  // How deep in quotes, as styleAt() sums it: a quote inside a quote is 2.
  int depth = 0;
  // Marked: a stretch pointed at -- the part of a message a reply quoted
  // -- on a plate of the quote's colour, apart from what is selected.
  bool marked = false;
  // A block of code (HTML's <pre>), and the language it says it is in: what
  // holds the text may draw it as a block of its own.
  bool block = false;
  std::string language;
  // Its own colour, and a plate behind it in another, where the message's
  // HTML says (<font color>, data-mx-color, data-mx-bg-color): the
  // innermost stretch's where they nest.
  std::optional<Colour> colour;
  std::optional<Colour> background;
  // Its size, as a share of the text: a heading (HTML h1..h6), drawn on
  // lines of its own, each as tall as its size.
  float scale = 1.0f;
};
// What a node is to an engine that lays out and paints a tree itself (a
// browser's, skiff.mirror's): a box holding its children where their
// anchors put them, a flow of them, words, or a view that scrolls what it
// holds. Said by the node (look()), and through the walks' table where the
// node is erased; the words are the node's, read while it is there.
namespace look {
// A box: its own colour where it paints one (a plate behind what it holds).
struct box {
  std::optional<Colour> colour;
};
struct flow {
  FlowOptions options;
};
struct words {
  std::string_view text;
  float size = 0.0f;
  Colour colour = 0;
  bool bold = false;
  // Its runs drawn otherwise (strong, a quote, code, a colour of its own,
  // ...) and its links, as the text keeps them -- read while it is there --
  // and the colours links and quotes are drawn in.
  std::span<const TextStyled> styles;
  std::span<const TextLink> links;
  Colour linkColour = 0;
  Colour quoteColour = 0;
  // Broken across lines always or never, where it is told (else where it
  // does not fit, as CSS's text); cut with an ellipsis where it runs out.
  std::optional<bool> wrapped;
  bool elided = false;
};
struct scrolled {};
// Painted by the node itself (a widget, an icon, a picture): to an engine,
// what it paints, as elements of its own.
struct drawn {};
// A field typed into: one line or many, its text and what it shows empty,
// hidden where it is a password. To an engine, its own field (<input>,
// <textarea>), edited by it; each edit given back (Mirror::typed).
struct field {
  std::string_view text;
  std::string_view placeholder;
  bool multiline = false;
  bool masked = false;
  float size = 0.0f;
  Colour colour = 0;
};
} // namespace look
using Look = spl::variant<look::box, look::flow, look::words, look::scrolled, look::drawn, look::field>;

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
  // CSS's flex-shrink: along these axes, where the flow it is in has not the
  // room for all it holds, it gives way -- down to its minWidth, as CSS's to
  // its min-content. A text that elides gives way on its own.
  std::optional<Axes> shrink{};
  std::optional<float> minWidth{}, maxWidth{};
  std::optional<float> minHeight{}, maxHeight{};
  std::optional<Align> alignSelf{};
  std::optional<float> depth{};

  // Given or not, like the rest, and still `.padding = {2, 6, 2, 6}`. Given
  // as all zeros it is zero -- once a plain Margin, all zeros read as "not
  // mentioned", and a padding could be set by apply() but never taken off.
  SpecMargin margin{};
  SpecMargin padding{};

  std::optional<float> cornerRadius{};
  // Or each corner its own; given, it is drawn and clipped by, not the one.
  std::optional<Corners> corners{};
  // A tail out of its box, painted with its fill (Tail).
  std::optional<Tail> tail{};
  // What is painted under the node's own drawing and its children, in its
  // box and its corner radius: a background -- another while hovered,
  // another while selected, another while it has the keyboard's focus --
  // and a border. Declared, not drawn by hand. (`fill` above is its size.)
  std::optional<Colour> background{};
  std::optional<Colour> hoverBackground{};
  std::optional<Colour> selectedBackground{};
  std::optional<Colour> focusBackground{};
  std::optional<Gradient> gradient{};  // under the plain background, where there is none
  std::optional<Border> border{};
  std::optional<Shadow> shadow{};
  std::optional<bool> masking{};
  std::optional<float> scale{};
  std::optional<float> alpha{};
  // Drawn moved by this much, where it is laid out not moving: a row
  // swiped, a card dragged. Neither layout nor what is around it changes.
  std::optional<float> shiftX{};
  std::optional<float> shiftY{};
  std::optional<bool> visible{};
  // Shown only while what holds it is under the pointer -- CSS's
  // `.holder:hover .it` -- the hover walk showing and hiding it: a message's
  // time beside a sticker, its buttons on a row.
  std::optional<bool> revealOnHover{};

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
  std::optional<Colour> colour{};
  std::optional<Colour> backgroundColour{};
  std::optional<float> fontSize{};
  std::optional<bool> fontBold{};

  // A state change animates position, size, scale and alpha.
  std::optional<double> transitionMs{};
  std::optional<Easing> transitionEasing{};

  // Each declaration of `other` that is made, over this one's: the members
  // walked one by one, none listed.
  void overlay(const Style &other) {
    aggregate::eachPair(*this, other, [](auto &mine, const auto &theirs) {
      if (theirs)
        mine = theirs;
    });
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

// Styles shared: what a resolver says of a subject -- its type, template,
// roles, states and the viewport's width -- said once and kept, for every
// node alike to take: the rows of a list, the parts of each message, all
// resolved again one by one. A resolver reads its style sheet and nothing
// else; a program whose styles read more (a theme's colours set at run
// time) forgets them when that changes.
namespace detail {
struct SharedStyleKey {
  std::uintptr_t resolve = 0;
  const StyleKey *type = nullptr, *templated = nullptr;
  std::array<std::uintptr_t, 4> roles{};
  std::size_t roleCount = 0;
  std::array<bool, 4> states{};
  float viewportWidth = 0.0f;
  bool operator==(const SharedStyleKey &) const = default;
};
struct SharedStyleHash {
  std::size_t operator()(const SharedStyleKey &key) const noexcept {
    std::size_t h = std::hash<std::uintptr_t>{}(key.resolve);
    const auto mix = [&](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    mix(std::hash<const void *>{}(key.type));
    mix(std::hash<const void *>{}(key.templated));
    for (std::size_t i = 0; i < key.roleCount; ++i) {
      mix(key.roles[i]);
    }
    for (const bool on : key.states) {
      mix(on ? 1u : 0u);
    }
    mix(std::hash<float>{}(key.viewportWidth));
    return h;
  }
};
inline std::unordered_map<SharedStyleKey, Style, SharedStyleHash> &sharedStyles() {
  static std::unordered_map<SharedStyleKey, Style, SharedStyleHash> kept;
  return kept;
}
} // namespace detail
inline void forgetStyles() { detail::sharedStyles().clear(); }
[[nodiscard]] inline Style resolveShared(const StyleResolver &resolver, const StyleSubject &subject) {
  if (subject.fRoles.size() > 4) {
    return resolver.fResolve(subject);
  }
  detail::SharedStyleKey key;
  key.resolve = std::bit_cast<std::uintptr_t>(resolver.fResolve);
  key.type = subject.fType;
  key.templated = subject.fTemplate;
  key.roleCount = subject.fRoles.size();
  for (std::size_t i = 0; i < key.roleCount; ++i) {
    key.roles[i] = std::bit_cast<std::uintptr_t>(subject.fRoles[i]);
  }
  key.states = {subject.fStates.has<state::hover>(), subject.fStates.has<state::selected>(),
                subject.fStates.has<state::disabled>(), subject.fStates.has<state::focus>()};
  key.viewportWidth = subject.fViewportWidth;
  auto &kept = detail::sharedStyles();
  if (const auto found = kept.find(key); found != kept.end()) {
    return found->second;
  }
  if (kept.size() > 4096) {
    kept.clear();
  }
  Style resolved = resolver.fResolve(subject);
  kept.emplace(key, resolved);
  return resolved;
}

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
// What the text showing the selection has selected, as it is now: copied by
// Ctrl+C wherever the focus is -- a text does not take the focus.
inline std::string &selectedText() {
  static std::string kept;
  return kept;
}
// Inline objects kept beside copied text, without depending on a widget's
// types. Display offsets describe the editable placeholder text.
struct ClipboardAtom {
  std::size_t first = 0, last = 0;
  std::string target, plain;
  bool picture = false;
  friend bool operator==(const ClipboardAtom&, const ClipboardAtom&) = default;
};
// Compared as values: a program's facts that hold one (a menu's over the
// text copied) are compared to know whether what they show changed.
struct ClipboardFragment {
  std::string text;
  std::string display;
  std::vector<ClipboardAtom> atoms;
  friend bool operator==(const ClipboardFragment&, const ClipboardFragment&) = default;
};
inline ClipboardFragment clipboardFragment(std::string display, std::vector<ClipboardAtom> atoms = {}) {
  ClipboardFragment made{display, std::move(display), std::move(atoms)};
  std::ranges::sort(made.atoms, {}, &ClipboardAtom::first);
  for (const auto& atom : made.atoms | std::views::reverse)
    if (atom.last <= made.text.size() && atom.first <= atom.last)
      made.text.replace(atom.first, atom.last - atom.first, atom.plain);
  return made;
}
inline std::optional<ClipboardFragment>& clipboardCandidate() {
  static std::optional<ClipboardFragment> kept;
  return kept;
}
inline std::optional<ClipboardFragment>& clipboardRichText() {
  static std::optional<ClipboardFragment> kept;
  return kept;
}
// Whether a button is held now, as the last press and release said -- to
// whatever scene they went: a release outside a text's own scene still ends
// the press it began.
inline bool &pointerHeld() {
  static bool held = false;
  return held;
}
using PointerEvent = spl::variant<pointer::move, pointer::down, pointer::up,
                               pointer::cancel, pointer::scroll>;
// A pointer event as a node under a shifted one sees it: moved back by the
// shift, into the space the nodes under it are laid out in.
[[nodiscard]] inline PointerEvent shiftedBack(const PointerEvent &input, float dx, float dy) {
  return spl::visit(
      [&](auto event) -> PointerEvent {
        event.x -= dx;
        event.y -= dy;
        return event;
      },
      input);
}
inline void notePointerHeld(const PointerEvent &input) {
  spl::visit(spl::overloaded{[](const pointer::down &) { pointerHeld() = true; },
                                   [](const pointer::up &) { pointerHeld() = false; },
                                   [](const pointer::cancel &) { pointerHeld() = false; },
                                   [](const auto &) {}},
                input);
}

// Where a pointer event is.
[[nodiscard]] inline Point where(const PointerEvent &event) {
  return spl::visit(
      [](const auto &one) { return Point(one.x, one.y); },
      event);
}
// Whether it ends a gesture.
[[nodiscard]] inline bool ends(const PointerEvent &event) {
  return spl::visit(spl::overloaded{[](const pointer::up &) { return true; },
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
  // A click the node under the pointer did not take, where it was: told to
  // each node above it in turn, as a click at a point is.
  std::optional<Point> fClickAbove;
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
// Letters, for the shortcuts made with them: select all, copy, cut, paste,
// find, and the rest.
inline constexpr Key kA{101};
inline constexpr Key kB{102};
inline constexpr Key kC{103};
inline constexpr Key kD{104};
inline constexpr Key kE{105};
inline constexpr Key kF{106};
inline constexpr Key kG{107};
inline constexpr Key kH{108};
inline constexpr Key kI{109};
inline constexpr Key kJ{110};
inline constexpr Key kK{111};
inline constexpr Key kL{112};
inline constexpr Key kM{113};
inline constexpr Key kN{114};
inline constexpr Key kO{115};
inline constexpr Key kP{116};
inline constexpr Key kQ{117};
inline constexpr Key kR{118};
inline constexpr Key kS{119};
inline constexpr Key kT{120};
inline constexpr Key kU{121};
inline constexpr Key kV{122};
inline constexpr Key kW{123};
inline constexpr Key kX{124};
inline constexpr Key kY{125};
inline constexpr Key kZ{126};
// Paging, and the digits of the row above the letters.
inline constexpr Key kPageUp{13};
inline constexpr Key kPageDown{14};
inline constexpr Key k0{130};
inline constexpr Key k1{131};
inline constexpr Key k2{132};
inline constexpr Key k3{133};
inline constexpr Key k4{134};
inline constexpr Key k5{135};
inline constexpr Key k6{136};
inline constexpr Key k7{137};
inline constexpr Key k8{138};
inline constexpr Key k9{139};
// Punctuation that shortcuts take: Ctrl+Shift+. a quote, as tdesktop's.
inline constexpr Key kPeriod{140};
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
using KeyEvent = spl::variant<key::down, key::up>;

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
using TextEvent = spl::variant<text::commit, text::compose>;

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
    spl::variant<semantic_action::focus, semantic_action::activate,
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
    spl::variant<semantic_role::none, semantic_role::group,
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
  // The focus moved on among the focusable nodes inside the node that asks,
  // back where `true`, round from the last to the first: as arrows go
  // through a menu, and never out of it.
  std::optional<bool> fMoveFocus;
  NodeId fFocusScope = 0;

  void handle() noexcept { fHandled = true; }
  void requestFocus() noexcept { fRequestFocus = true; }
  void moveFocus(bool backwards) noexcept {
    fMoveFocus = backwards;
    fFocusScope = fCurrent;
    fHandled = true;
  }
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
  Rect fBounds;
  int fParent = -1;
  std::vector<SemanticAction> fActions;
};

[[nodiscard]] inline bool isTextBox(const SemanticRole &role) {
  return spl::visit(spl::overloaded{[](semantic_role::text_box) { return true; },
                               [](auto) { return false; }},
                    role);
}
// Whether focus is to be shown: it is when it came from the keyboard, and
// not when a press gave it -- a pressed button keeps the focus, so Enter and
// Space still act on it, without being drawn as focused until the keyboard
// moves it. One for the program, as the keyboard is.
inline bool &focusVisible() {
  static bool shown = false;
  return shown;
}

// The pointer's shape over a node: an arrow unless it says otherwise.
namespace cursor {
struct arrow {
  friend bool operator==(arrow, arrow) = default;
};
struct text {
  friend bool operator==(text, text) = default;
};
struct hand {
  friend bool operator==(hand, hand) = default;
};
struct resize_horizontal {
  friend bool operator==(resize_horizontal, resize_horizontal) = default;
};
struct resize_vertical {
  friend bool operator==(resize_vertical, resize_vertical) = default;
};
} // namespace cursor
using Cursor = spl::variant<cursor::arrow, cursor::text, cursor::hand,
                            cursor::resize_horizontal, cursor::resize_vertical>;
[[nodiscard]] inline bool isArrow(const Cursor &shape) {
  return spl::visit(spl::overloaded{[](cursor::arrow) { return true; },
                               [](auto) { return false; }},
                    shape);
}

[[nodiscard]] inline bool hasRole(const SemanticRole &role) {
  return spl::visit(spl::overloaded{[](semantic_role::none) { return false; },
                               [](auto) { return true; }},
                    role);
}

// What the scene has for the host, as data the host reads between events
// -- nothing is called: whether whatever holds focus takes text (the host
// starts and stops its text input as that changes), what was copied, to go
// on the system's clipboard, and the links pressed in texts, to follow.
// A text menu asked for -- by a right press, which a long press on a phone
// is made into -- for the program to read, the last of them its; it drains
// them. A selectable text's: what it offers to copy -- its selection, all of
// it where the press was not on what was selected -- and the link the press
// was on, where it was on one. A field's: what it can do -- Cut and Copy
// where something is selected and it is no password's, Paste, Select All --
// each done by the key the field takes for it, given back (giveKey).
namespace text_menu {
struct of_text {
  std::string text;
  std::optional<std::string> link;
};
struct of_field {
  bool selection = false;
  bool masked = false;
  // The field keeps formats -- bold, a link: its menu has them for what is
  // selected.
  bool formats = false;
  bool undo = false;
  bool redo = false;
  bool text = true;
};
}  // namespace text_menu
using TextMenuAsk = spl::variant<text_menu::of_text, text_menu::of_field>;
inline std::vector<TextMenuAsk> &textMenusAsked() {
  static std::vector<TextMenuAsk> asked;
  return asked;
}
// The link the last right press was on, where its text asked with one.
[[nodiscard]] inline std::optional<std::string> pressedLink() {
  if (textMenusAsked().empty()) {
    return std::nullopt;
  }
  return spl::visit(spl::overloaded{[](const text_menu::of_text &text) { return text.link; },
                                          [](const text_menu::of_field &) { return std::optional<std::string>(); }},
                       textMenusAsked().back());
}
// A type, told by the address of its own: what an erased answer is, outside a
// release build -- one per type, the same wherever it is seen.
template <class T> inline constexpr char kTypeKey = 0;
struct HostWork {
  // Keys for the node with the focus, as if pressed: what the host's own
  // controls stand for. Given, down and up, before the next frame.
  std::vector<key::down> keys;
  bool typing = false;
  // Whether the pointer is a finger, as the host last saw it: a drag in a
  // text field then scrolls it, as a phone's do, rather than selecting.
  bool byTouch = false;
  // Whether the tree is laid out by an engine of its own (skiff.mirror's):
  // its nodes have no bounds of skiff's, and a press along a path reaches
  // the nodes on it wherever the engine put them.
  bool laidOutElsewhere = false;
  // Where what is typed into is, in the window's points, as last drawn: for
  // the host to tell the system, whose keyboard on a screen -- and whose
  // suggestions -- go beside it, not over it.
  std::optional<Rect> typingAt;
  // How much of the window's height an on-screen keyboard took, in points,
  // as the host last saw it: the window shrank by it with its width the
  // same while typing by touch. For a program whose layout follows the
  // window's shape -- a phone's portrait one, which the keyboard up would
  // otherwise turn into a landscape one.
  float keyboardTaken = 0.0f;
  std::optional<std::string> copied;
  std::vector<std::string> links;
  // The node a press is being answered by, while its event is dispatched --
  // one that says what a press does by onPress() -- and, the dispatch over,
  // each such press by the path the scene routed it along: for the program
  // to deliver (skiff::bind::press), when nothing of it runs. Kept by the
  // address of its State, which is the backend's to know -- the core holds
  // the address and the scene reads its own back from it; so is `due`.
  const void *pressedNow = nullptr;
  std::vector<std::vector<std::uint32_t>> pressed;  // each a Path
  // Nodes that came due as they ticked -- a timer: pressed, by the path
  // to each found once the frame's update is over.
  std::vector<const void *> due;
  // What a handler returned that it asks for, where nothing was carried down
  // to send it with -- outside a release build, where the routing is erased:
  // kept, erased (as outside one anything may be), with the path to its
  // node, for skiff::bind to send up the frames it is in once the dispatch
  // is over (skiff::bind::answer). Never in a release build.
  struct KeptAnswer {
    std::vector<std::uint32_t> path;
    std::shared_ptr<const void> answer;
    const void *type = nullptr;  // what it is: kTypeKey<Answer>'s address
  };
  std::shared_ptr<const void> answerNow;
  const void *answerType = nullptr;
  std::vector<KeptAnswer> answers;
};
inline HostWork &hostWork() {
  static HostWork kept;
  return kept;
}
// An action that answers -- returns what a press asks for, as its type says
// (Answer) -- is asked when the press is delivered (a node's onPress(),
// through skiff::bind::press), not where the press is handled: the node
// says it was pressed, once.
template <class A>
concept Answering = requires { typename A::Answer; };
template <class S> void pressLater(S &state) { hostWork().pressedNow = &state; }
// What a routing carries down to its target, for a press to be answered
// where it is made -- in a release build, where the routing is static: what
// binds the tree extends it at each node it binds (skiff::bind's carryInto,
// found by the carried type); nothing by default, nor past an erased node.
struct NoCarry {};
// A click a node took with nothing to ask for: an answer of its own, for a
// node whose onClick returns what a click asks for, perhaps (ClickAnswers),
// to say it was taken all the same. Sending it does nothing.
struct Taken {};
template <class P, class N, class C>
[[nodiscard]] const C &carryInto(P &, N &, const C &carried) {
  return carried;
}
// A press answered where it is made, with what was carried down to it
// (skiff::bind's answerPress): whether it was. By default it is not: it is
// kept, by its path, for the program to deliver.
template <class N, class C>
bool answerPress(N &, const C &) {
  return false;
}
// What a handler returned that it asks for -- its node's Answer, or perhaps
// one: sent at once up the frames carried down to it (skiff::bind's
// answerWith, found by the carried type), else -- nothing carried, as
// outside a release build -- kept for its path to be added as the routing
// leaves it.
// Kept as the node's Answer where it is one (an alternative of it made
// the whole); else as itself -- a node that answers for more than one of
// what it wraps (a named view over keys_for and onKeys) said an Answer of
// one of them, and the other's answer could not be made one.
template <class N, class A>
  requires std::constructible_from<typename N::Answer, const A &>
void keepAnswer(const A &answer) {
  if constexpr (kErasedWalks) {
    hostWork().answerNow = std::make_shared<const typename N::Answer>(answer);
    hostWork().answerType = &kTypeKey<typename N::Answer>;
  }
}
template <class N, class A> void keepAnswer(const A &answer) {
  if constexpr (kErasedWalks) {
    hostWork().answerNow = std::make_shared<const A>(answer);
    hostWork().answerType = &kTypeKey<A>;
  }
}
// Unconstrained, the one answerWith here: skiff::bind's, constrained to
// carried frames, is taken over it wherever frames are carried. (Two of
// them constrained each its own way here, a release build found both and
// bind's equally good: ambiguous.)
template <class N, class A, class C> void answerWith(N &, const A &answer, const C &) { keepAnswer<N>(answer); }
template <class N, class A, class C> void answered(N &node, const A &answer, const C &carried) {
  answerWith(node, answer, carried);
}
template <class N, class A, class C> void answered(N &node, const std::optional<A> &answer, const C &carried) {
  if (answer) {
    answered(node, *answer, carried);
  }
}
// A handler called: one that returns nothing, as ever; one that returns
// what it asks for, that answered.
template <class N, class C, class F>
  requires std::is_void_v<std::invoke_result_t<F &>>
void handled(N &, const C &, F &&call) {
  call();
}
template <class N, class C, class F> void handled(N &node, const C &carried, F &&call) {
  answered(node, call(), carried);
}
// What a routing starts with at the root: what the program says for its
// root's type (startCarry, found by that type); nothing by default.
template <class R>
[[nodiscard]] NoCarry startCarry(R &) {
  return {};
}
// The system's clipboard as the host last read it: what a paste puts in.
inline std::string &clipboardContents() {
  static std::string kept;
  return kept;
}
[[nodiscard]] inline std::string clipboardText() {
  return hostWork().copied ? *hostWork().copied : clipboardContents();
}
inline void setClipboardText(const std::string &text) {
  hostWork().copied = text;
  const auto& candidate = clipboardCandidate();
  clipboardRichText() = candidate && candidate->text == text ? candidate : std::nullopt;
}
inline void openLink(std::string_view target) { hostWork().links.emplace_back(target); }
inline void giveKey(key::down press) { hostWork().keys.push_back(press); }

namespace detail {
template <class T> consteval std::string_view signatureOf() {
#if defined(_MSC_VER) && !defined(__clang__)
  return __FUNCSIG__;
#else
  return __PRETTY_FUNCTION__;
#endif
}
// The type as spelled in the signature.
consteval std::string_view spelledIn(std::string_view signature) {
#if defined(_MSC_VER) && !defined(__clang__)
  const std::size_t from = signature.find("signatureOf<") + 12;
  const std::size_t to = signature.rfind(">(void)");
#else
  const std::size_t from = signature.find("T = ") + 4;
  std::size_t to = signature.size();
  // gcc goes on after it: "[with T = X; std::string_view = ...]".
  int depth = 0;
  for (std::size_t at = from; at < signature.size(); ++at) {
    const char c = signature[at];
    if (c == '<' || c == '(' || c == '[' || c == '{')
      ++depth;
    else if ((c == '>' || c == ')' || c == '}') && depth > 0)
      --depth;
    else if (c == ']' && depth == 0) {
      to = at;
      break;
    } else if (c == ']')
      --depth;
    else if (c == ';' && depth == 0) {
      to = at;
      break;
    }
  }
#endif
  return signature.substr(from, to - from);
}
// Its last part at the outermost level -- past every "::" not inside
// brackets -- with whatever is inside brackets left out.
template <class Out> consteval std::size_t shortened(std::string_view spelled, Out out) {
  std::size_t start = 0;
  int depth = 0;
  for (std::size_t at = 0; at + 1 < spelled.size(); ++at) {
    const char c = spelled[at];
    if (c == '<' || c == '(' || c == '[' || c == '{')
      ++depth;
    else if ((c == '>' || c == ')' || c == ']' || c == '}') && depth > 0)
      --depth;
    else if (depth == 0 && c == ':' && spelled[at + 1] == ':')
      start = at + 2;
  }
  std::size_t length = 0;
  depth = 0;
  for (std::size_t at = start; at < spelled.size(); ++at) {
    const char c = spelled[at];
    if (c == '<' || c == '(' || c == '[' || c == '{') {
      ++depth;
      continue;
    }
    if ((c == '>' || c == ')' || c == ']' || c == '}') && depth > 0) {
      --depth;
      continue;
    }
    if (depth == 0) {
      out(length, c);
      ++length;
    }
  }
  return length;
}
template <class T> consteval std::size_t shortLength() {
  return shortened(spelledIn(signatureOf<T>()), [](std::size_t, char) {});
}
template <class T> consteval std::array<char, shortLength<T>()> shortChars() {
  std::array<char, shortLength<T>()> chars{};
  shortened(spelledIn(signatureOf<T>()), [&](std::size_t at, char c) { chars[at] = c; });
  return chars;
}
template <class T> inline constexpr std::array<char, shortLength<T>()> kShortName = shortChars<T>();
template <class T> inline constexpr char kKindKey = 0;
} // namespace detail
// A type's short name, without RTTI.
template <class T> [[nodiscard]] constexpr std::string_view nameOf() noexcept {
  return {detail::kShortName<T>.data(), detail::kShortName<T>.size()};
}
// Which kind of node: one key per type, and its name -- a view's own, where
// it was named (SKIFF_NAMED: viewName()), else its type's.
struct NodeKind {
  const void *key = nullptr;
  std::string_view name;
  friend constexpr bool operator==(const NodeKind &a, const NodeKind &b) noexcept { return a.key == b.key; }
};
template <class T> [[nodiscard]] constexpr NodeKind kindOf() noexcept {
  if constexpr (requires { std::string_view(T::viewName()); })
    return {&detail::kKindKey<T>, std::string_view(T::viewName())};
  else
    return {&detail::kKindKey<T>, nameOf<T>()};
}

class AnyNode;
class AnyNodeRef;
// The base every node of every backend derives from -- a scene's Node, a web
// page's -- so that what walks a tree tells a node from what holds nodes.
struct NodeBase {};
// A node marked -- a base of its own (MarkedAs) it is found by, wherever
// wrappers have been laid over it by inheritance: its table says the mark
// and how to reach it from the node, so a walk finds the mark through any
// type that derives from it.
template <class T> inline constexpr char kMarkKey = 0;
template <class T> struct MarkOf {
  static constexpr const void *key = nullptr;
  static void *as(void *) { return nullptr; }
};
template <class T>
  requires requires { typename T::MarkedAs; }
struct MarkOf<T> {
  static constexpr const void *key = &kMarkKey<typename T::MarkedAs>;
  static void *as(void *node) { return static_cast<void *>(static_cast<typename T::MarkedAs *>(static_cast<T *>(node))); }
};
// A node type that is not itself in the tree: what it holds is, in its place
// -- seen through, by its seenThrough() (an AnyNode&), in every build. Said
// by specializing this for the type (compose's Held).
template <class T> inline constexpr bool kSeenThrough = false;
// A node type the walks see as another, its base: one table for every stack
// of parts, whatever its parts (compose's Box), rather than one made for
// each. Said by specializing this for the type.
template <class T> inline constexpr bool kSeenAsBase = false;

// A node type that is also a range -- it has begin() and end() -- is walked
// as a range of children unless it says it is a node:
//
//   template <> inline constexpr bool skiff::scene::kTreatAsNode<MyList> = true;
template <class T> inline constexpr bool kTreatAsNode = false;

// What can be a child: a way of holding nodes -- a spl::variant (its
// alternative, unless that is std::monostate), a std::optional, a pointer, a
// reference_wrapper, a tuple, an AnyNode, a range -- or a node. One overload
// per holder; anything else is a node.
template <class F> void visitChild(std::monostate &, F &&);
template <class... Ts, class F> void visitChild(spl::variant<Ts...> &, F &&);
template <class... Ts, class F> void visitChild(std::variant<Ts...> &, F &&);
template <class T, class F> void visitChild(std::optional<T> &, F &&);
template <class T, class D, class F>
void visitChild(std::unique_ptr<T, D> &, F &&);
template <class T, class F> void visitChild(std::shared_ptr<T> &, F &&);
template <class T, class F> void visitChild(std::reference_wrapper<T> &, F &&);
template <class... Ts, class F> void visitChild(std::tuple<Ts...> &, F &&);
template <class F> void visitChild(AnyNode &, F &&);
// A child handed on as an AnyNodeRef: itself where it is one, else a ref to it.
using AnyChildVisit = void (*)(void *context, AnyNodeRef &child);
void visitAsAny(AnyNodeRef &child, void *context, AnyChildVisit visit);
template <class N> void visitAsAny(N &child, void *context, AnyChildVisit visit);
template <class R, class F>
  requires(std::ranges::range<R> && !kTreatAsNode<R>)
void visitChild(R &, F &&);
template <class N, class F> void visitChild(N &, F &&);
template <class N, class F>
  requires(kErasedWalks && std::derived_from<N, NodeBase>)
void visitChild(N &, F &&);
template <class N, class F>
  requires(!kErasedWalks && std::derived_from<N, NodeBase> && (kSeenThrough<N> || kSeenAsBase<N>))
void visitChild(N &, F &&);

template <class F> void visitChild(std::monostate &, F &&) {}
template <class... Ts, class F>
void visitChild(spl::variant<Ts...> &child, F &&f) {
  spl::visit([&](auto &alternative) { visitChild(alternative, f); }, child);
}
// Anything holding one of several and visiting it as C++26's variant does:
// a std::variant, or a program's own, walked as spl::variant is.
template <class V>
concept one_of_several = requires(V &v) {
  v.index();
  v.valueless_by_exception();
};
template <class V, class F>
  requires(one_of_several<V> && !std::derived_from<V, NodeBase>)
void visitChild(V &child, F &&f) {
  child.visit([&](auto &alternative) { visitChild(alternative, f); });
}
// A std::variant by std::visit: its visit member is C++26's, and a C++23
// build has none.
template <class... Ts, class F> void visitChild(std::variant<Ts...> &child, F &&f) {
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

// The child at a place in eachChild's order, handed to `f` as eachChild
// hands it: the parts passed by how many children each holds, a range of
// nodes indexed straight into. Whether there was one. `left` counts down
// the children still to pass.
template <class R>
concept indexed_nodes = std::ranges::random_access_range<R> && std::ranges::sized_range<R> &&
                        std::derived_from<std::ranges::range_value_t<R>, NodeBase>;
template <class F> bool partAt(std::monostate &, std::uint32_t &, F &&);
template <class... Ts, class F> bool partAt(spl::variant<Ts...> &, std::uint32_t &, F &&);
template <class... Ts, class F> bool partAt(std::variant<Ts...> &, std::uint32_t &, F &&);
template <class V, class F>
  requires(one_of_several<V> && !std::derived_from<V, NodeBase>)
bool partAt(V &, std::uint32_t &, F &&);
template <class T, class F> bool partAt(std::optional<T> &, std::uint32_t &, F &&);
template <class T, class D, class F> bool partAt(std::unique_ptr<T, D> &, std::uint32_t &, F &&);
template <class T, class F> bool partAt(std::shared_ptr<T> &, std::uint32_t &, F &&);
template <class T, class F> bool partAt(std::reference_wrapper<T> &, std::uint32_t &, F &&);
template <class... Ts, class F> bool partAt(std::tuple<Ts...> &, std::uint32_t &, F &&);
template <class F> bool partAt(AnyNode &, std::uint32_t &, F &&);
template <class R, class F>
  requires(std::ranges::range<R> && !kTreatAsNode<R> && !indexed_nodes<R>)
bool partAt(R &, std::uint32_t &, F &&);
template <class R, class F>
  requires(indexed_nodes<R> && !kTreatAsNode<R>)
bool partAt(R &, std::uint32_t &, F &&);
template <class N, class F> bool partAt(N &, std::uint32_t &, F &&);

template <class F> bool partAt(std::monostate &, std::uint32_t &, F &&) { return false; }
template <class... Ts, class F> bool partAt(spl::variant<Ts...> &child, std::uint32_t &left, F &&f) {
  return spl::visit([&](auto &alternative) { return partAt(alternative, left, f); }, child);
}
template <class V, class F>
  requires(one_of_several<V> && !std::derived_from<V, NodeBase>)
bool partAt(V &child, std::uint32_t &left, F &&f) {
  bool found = false;
  child.visit([&](auto &alternative) { found = partAt(alternative, left, f); });
  return found;
}
template <class... Ts, class F> bool partAt(std::variant<Ts...> &child, std::uint32_t &left, F &&f) {
  return std::visit([&](auto &alternative) { return partAt(alternative, left, f); }, child);
}
template <class T, class F> bool partAt(std::optional<T> &child, std::uint32_t &left, F &&f) {
  return child && partAt(*child, left, f);
}
template <class T, class D, class F> bool partAt(std::unique_ptr<T, D> &child, std::uint32_t &left, F &&f) {
  return child && partAt(*child, left, f);
}
template <class T, class F> bool partAt(std::shared_ptr<T> &child, std::uint32_t &left, F &&f) {
  return child && partAt(*child, left, f);
}
template <class T, class F> bool partAt(std::reference_wrapper<T> &child, std::uint32_t &left, F &&f) {
  return partAt(child.get(), left, f);
}
template <class... Ts, class F> bool partAt(std::tuple<Ts...> &child, std::uint32_t &left, F &&f) {
  return std::apply([&](auto &...each) { return (partAt(each, left, f) || ...); }, child);
}
template <class F> bool partAt(AnyNode &child, std::uint32_t &left, F &&f) {
  bool held = false;
  visitChild(child, [&](auto &) { held = true; });
  if (!held) {
    return false;
  }
  if (left != 0) {
    --left;
    return false;
  }
  visitChild(child, f);
  return true;
}
template <class R, class F>
  requires(std::ranges::range<R> && !kTreatAsNode<R> && !indexed_nodes<R>)
bool partAt(R &child, std::uint32_t &left, F &&f) {
  for (auto &each : child) {
    if (partAt(each, left, f)) {
      return true;
    }
  }
  return false;
}
template <class R, class F>
  requires(indexed_nodes<R> && !kTreatAsNode<R>)
bool partAt(R &child, std::uint32_t &left, F &&f) {
  const auto size = static_cast<std::uint32_t>(std::ranges::size(child));
  if (left < size) {
    visitChild(std::ranges::begin(child)[left], f);
    return true;
  }
  left -= size;
  return false;
}
template <class N, class F> bool partAt(N &child, std::uint32_t &left, F &&f) {
  if (left != 0) {
    --left;
    return false;
  }
  visitChild(child, f);
  return true;
}
template <class T, class F> bool childAt(T &node, std::uint32_t place, F &&f) {
  bool found = false;
  node.forEachChild([&](auto &part) { found = found || partAt(part, place, f); });
  return found;
}

// A path from a node to one below it: positions of the children taken, in
// eachChild order.
using Path = std::vector<std::uint32_t>;

// What binds a node -- a part of a model shown, a scope, a Local, a list's
// rows, a press answered -- for a debug build's walks made at run time: none,
// unless the binding layer (skiff.bind) says so, by a partial
// specialization of this for the node's kinds. A specialization, not an
// overload found by ADL: a specialization counts wherever the binding layer
// is reachable -- imported anywhere below -- where an overload had to be
// visible, and a node's table made in a unit that did not see it had none,
// the same table made elsewhere had one, and the linker kept either.
template <class T> struct BindTableOf {
  static constexpr const void *value = nullptr;
};


// ---- a node of any type, by value and by reference -----------------------------
//
// What every backend's table of a node type begins with: how the node is
// destroyed, its kind, its children in order and the one at a place, whether
// it is still settling, what binds it (skiff.bind's table), and the mark it
// carries. skiff.bind walks a tree through this and nothing else; a backend's
// own table (the scene's NodeOps) has this as its first member and goes on
// from it, so a table is reached from its core and the core from its table.
struct AnyNodeOps {
  void (*fDestroy)(void *);
  NodeKind fKind;
  void (*fEachChild)(void *, void *context, AnyChildVisit visit);
  bool (*fChildAt)(void *, std::uint32_t, void *context, AnyChildVisit visit);
  bool (*fSettling)(void *);
  // What binds it (skiffBindTable), for the binding's walks at run time.
  const void *fBind;
  // The mark it carries (MarkOf), and the way from it to its mark.
  const void *fMarkKey;
  void *(*fAsMarked)(void *);
};
// A type whose table the program makes in a unit of its own: kOpsElsewhere
// true for it, and opsElsewhere<T>() defined there.
template <class T> inline constexpr bool kOpsElsewhere = false;
template <class T> const AnyNodeOps &opsElsewhere() noexcept;
// The table for a type: the one the program keeps elsewhere, else the one its
// backend makes -- skiffOpsOf(T *), found by the namespaces of T and its
// bases (the scene's, for a scene::Node). Not inline: the table of a type is
// made once, where it is first asked for.
template <class T> [[nodiscard]] const AnyNodeOps *tableOf() noexcept {
  if constexpr (kOpsElsewhere<T>) {
    return &opsElsewhere<T>();
  } else {
    return &skiffOpsOf(static_cast<T *>(nullptr));
  }
}

class AnyNode {
public:
  using Ops = AnyNodeOps;
  AnyNode() = default;
  template <class T>
    requires std::derived_from<std::remove_cvref_t<T>, NodeBase>
  AnyNode(T &&node) // NOLINT: converting, as std::function's is
  {
    if constexpr (kSeenThrough<std::remove_cvref_t<T>>) {
      *this = std::move(node.seenThrough());
    } else if constexpr (kSeenAsBase<std::remove_cvref_t<T>>) {
      *this = AnyNode(static_cast<typename std::remove_cvref_t<T>::SeenAs &&>(node));
    } else {
      fNode = new std::remove_cvref_t<T>(std::forward<T>(node));
      fOps = tableOf<std::remove_cvref_t<T>>();
    }
  }
  template <class T, class... Args>
    requires std::derived_from<T, NodeBase>
  explicit AnyNode(std::in_place_type_t<T>, Args &&...args)
      : fNode(new T(std::forward<Args>(args)...)), fOps(tableOf<T>()) {}

  AnyNode(const AnyNode &) = delete;
  AnyNode &operator=(const AnyNode &) = delete;
  AnyNode(AnyNode &&other) noexcept
      : fNode(std::exchange(other.fNode, nullptr)), fOps(std::exchange(other.fOps, nullptr)) {}
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
  [[nodiscard]] AnyNodeRef ref() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept { return fNode != nullptr; }
  // The node, when it is a T.
  template <class T> [[nodiscard]] T *get() noexcept {
    if (fOps == tableOf<T>())
      return static_cast<T *>(fNode);
    return fOps != nullptr && fOps->fMarkKey == &kMarkKey<T> ? static_cast<T *>(fOps->fAsMarked(fNode)) : nullptr;
  }
  [[nodiscard]] const Ops &ops() const noexcept { return *fOps; }
  [[nodiscard]] void *node() const noexcept { return fNode; }

private:
  void *fNode = nullptr;
  const Ops *fOps = nullptr;
};

class AnyNodeRef {
public:
  AnyNodeRef(void *node, const AnyNodeOps *ops) noexcept : fNode(node), fOps(ops) {}
  template <class T>
    requires std::derived_from<T, NodeBase>
  [[nodiscard]] static AnyNodeRef of(T &node) {
    if constexpr (kSeenThrough<T>) {
      return node.seenThrough().ref();
    } else if constexpr (kSeenAsBase<T>) {
      return of(static_cast<typename T::SeenAs &>(node));
    } else {
      static const AnyNodeOps *volatile table = tableOf<T>();
      return AnyNodeRef(&node, table);
    }
  }
  template <class T> [[nodiscard]] T *get() noexcept {
    if (fOps == tableOf<T>())
      return static_cast<T *>(fNode);
    return fOps->fMarkKey == &kMarkKey<T> ? static_cast<T *>(fOps->fAsMarked(fNode)) : nullptr;
  }
  [[nodiscard]] bool settling() const { return fOps->fSettling(fNode); }
  [[nodiscard]] const AnyNodeOps &ops() const noexcept { return *fOps; }
  [[nodiscard]] void *node() const noexcept { return fNode; }
  [[nodiscard]] NodeKind kind() const { return fOps->fKind; }
  [[nodiscard]] const void *bindTable() const noexcept { return fOps->fBind; }
  template <class F> void forEachChild(F &&f) {
    fOps->fEachChild(fNode, &f, +[](void *context, AnyNodeRef &child) {
      (*static_cast<std::remove_reference_t<F> *>(context))(child);
    });
  }

private:
  void *fNode;
  const AnyNodeOps *fOps;
};
inline AnyNodeRef AnyNode::ref() const noexcept { return AnyNodeRef(fNode, fOps); }

template <class F> void visitChild(AnyNode &child, F &&f) {
  if (child) {
    AnyNodeRef seen = child.ref();
    f(seen);
  }
}
// Outside a release build, every child is seen through an AnyNodeRef.
template <class N, class F>
  requires(kErasedWalks && std::derived_from<N, NodeBase>)
void visitChild(N &child, F &&f) {
  AnyNodeRef seen = AnyNodeRef::of(child);
  f(seen);
}
// In a release build too, one the walks see through, or as its base (a
// Held, a view named over one), is handed on as what they see: its content,
// or its base -- the node whose state stateOf gives for it. Walked as itself
// there, it was laid out by its own state and placed by its content's: 0
// wide, its content twice as far down as its row.
template <class N, class F>
  requires(!kErasedWalks && std::derived_from<N, NodeBase> && (kSeenThrough<N> || kSeenAsBase<N>))
void visitChild(N &child, F &&f) {
  AnyNodeRef seen = AnyNodeRef::of(child);
  f(seen);
}
inline void visitAsAny(AnyNodeRef &child, void *context, AnyChildVisit visit) { visit(context, child); }
template <class N> void visitAsAny(N &child, void *context, AnyChildVisit visit) {
  AnyNodeRef seen = AnyNodeRef::of(child);
  visit(context, seen);
}
} // namespace skiff::scene
