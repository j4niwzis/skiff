// skia-lite: the module skia, without Skia.
//
// The scene is laid out, bound, hit-tested, damaged and animated in Skia's
// types -- a rectangle is a SkRect -- and drawn through skiff::paint::Painter.
// This is the same module, written here: the geometry for real; a canvas
// that keeps its matrix and its clip, so what is drawn and what is skipped
// is as it would be, and draws nothing; images and surfaces as buffers of
// pixels that nothing is drawn into; pictures that hold nothing; and faces
// whose glyphs are all there and all of one width, so text is measured --
// the same everywhere, no fonts read. What a build without Skia takes in its
// place (SKIFF_SKIA=OFF): the scene's tests, run where Skia is not.
export module skia;

import std;

export namespace skia {

template <class T> using Sp = std::shared_ptr<T>;

using SkScalar = float;
using SkColor = std::uint32_t;
using SkAlpha = std::uint8_t;
using SkGlyphID = std::uint16_t;
using SkUnichar = std::int32_t;
using SkFourByteTag = std::uint32_t;
using SkFontTableTag = std::uint32_t;

inline constexpr SkColor colorSetARGB(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
  return (static_cast<SkColor>(a) << 24) | (static_cast<SkColor>(r) << 16) | (static_cast<SkColor>(g) << 8) |
         static_cast<SkColor>(b);
}
inline constexpr SkColor kBlack = 0xFF000000u;
inline constexpr SkColor kWhite = 0xFFFFFFFFu;
inline constexpr SkColor kLTGray = 0xFFCCCCCCu;
inline constexpr SkColor kDKGray = 0xFF444444u;
inline constexpr SkColor kGray = 0xFF888888u;
inline constexpr SkColor kRed = 0xFFFF0000u;
inline constexpr SkColor kGreen = 0xFF00FF00u;
inline constexpr SkColor kBlue = 0xFF0000FFu;
inline constexpr SkColor kYellow = 0xFFFFFF00u;
inline constexpr SkColor kCyan = 0xFF00FFFFu;
inline constexpr SkColor kMagenta = 0xFFFF00FFu;

struct SkColor4f {
  float fR = 0.0f, fG = 0.0f, fB = 0.0f, fA = 0.0f;
  [[nodiscard]] static SkColor4f FromColor(SkColor c) noexcept {
    return {static_cast<float>((c >> 16) & 0xFFu) / 255.0f, static_cast<float>((c >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(c & 0xFFu) / 255.0f, static_cast<float>(c >> 24) / 255.0f};
  }
  [[nodiscard]] SkColor toSkColor() const noexcept {
    const auto at = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return colorSetARGB(at(fA), at(fR), at(fG), at(fB));
  }
  friend bool operator==(const SkColor4f &, const SkColor4f &) = default;
};

// ---- geometry ---------------------------------------------------------------

struct SkPoint {
  float fX = 0.0f, fY = 0.0f;
  [[nodiscard]] static constexpr SkPoint Make(float x, float y) noexcept { return {x, y}; }
  [[nodiscard]] constexpr float x() const noexcept { return fX; }
  [[nodiscard]] constexpr float y() const noexcept { return fY; }
  constexpr void set(float x, float y) noexcept { fX = x; fY = y; }
  constexpr void offset(float dx, float dy) noexcept { fX += dx; fY += dy; }
  [[nodiscard]] float length() const noexcept { return std::hypot(fX, fY); }
  friend constexpr SkPoint operator+(SkPoint a, SkPoint b) noexcept { return {a.fX + b.fX, a.fY + b.fY}; }
  friend constexpr SkPoint operator-(SkPoint a, SkPoint b) noexcept { return {a.fX - b.fX, a.fY - b.fY}; }
  friend constexpr bool operator==(const SkPoint &, const SkPoint &) = default;
};
using SkVector = SkPoint;

struct SkIPoint {
  int fX = 0, fY = 0;
  [[nodiscard]] static constexpr SkIPoint Make(int x, int y) noexcept { return {x, y}; }
  friend constexpr bool operator==(const SkIPoint &, const SkIPoint &) = default;
};

struct SkISize {
  int fWidth = 0, fHeight = 0;
  [[nodiscard]] static constexpr SkISize Make(int w, int h) noexcept { return {w, h}; }
  [[nodiscard]] constexpr int width() const noexcept { return fWidth; }
  [[nodiscard]] constexpr int height() const noexcept { return fHeight; }
  [[nodiscard]] constexpr bool isEmpty() const noexcept { return fWidth <= 0 || fHeight <= 0; }
  friend constexpr bool operator==(const SkISize &, const SkISize &) = default;
};

struct SkIRect {
  int fLeft = 0, fTop = 0, fRight = 0, fBottom = 0;
  [[nodiscard]] static constexpr SkIRect MakeEmpty() noexcept { return {}; }
  [[nodiscard]] static constexpr SkIRect MakeWH(int w, int h) noexcept { return {0, 0, w, h}; }
  [[nodiscard]] static constexpr SkIRect MakeSize(SkISize s) noexcept { return {0, 0, s.fWidth, s.fHeight}; }
  [[nodiscard]] static constexpr SkIRect MakeLTRB(int l, int t, int r, int b) noexcept { return {l, t, r, b}; }
  [[nodiscard]] static constexpr SkIRect MakeXYWH(int x, int y, int w, int h) noexcept { return {x, y, x + w, y + h}; }
  [[nodiscard]] constexpr int x() const noexcept { return fLeft; }
  [[nodiscard]] constexpr int y() const noexcept { return fTop; }
  [[nodiscard]] constexpr int left() const noexcept { return fLeft; }
  [[nodiscard]] constexpr int top() const noexcept { return fTop; }
  [[nodiscard]] constexpr int right() const noexcept { return fRight; }
  [[nodiscard]] constexpr int bottom() const noexcept { return fBottom; }
  [[nodiscard]] constexpr int width() const noexcept { return fRight - fLeft; }
  [[nodiscard]] constexpr int height() const noexcept { return fBottom - fTop; }
  [[nodiscard]] constexpr SkISize size() const noexcept { return {width(), height()}; }
  [[nodiscard]] constexpr bool isEmpty() const noexcept { return !(fLeft < fRight && fTop < fBottom); }
  constexpr void setEmpty() noexcept { *this = {}; }
  constexpr void offset(int dx, int dy) noexcept { fLeft += dx; fTop += dy; fRight += dx; fBottom += dy; }
  [[nodiscard]] constexpr SkIRect makeOffset(int dx, int dy) const noexcept {
    return {fLeft + dx, fTop + dy, fRight + dx, fBottom + dy};
  }
  [[nodiscard]] constexpr bool contains(int x, int y) const noexcept {
    return x >= fLeft && x < fRight && y >= fTop && y < fBottom;
  }
  [[nodiscard]] constexpr bool contains(const SkIRect &r) const noexcept {
    return !r.isEmpty() && !isEmpty() && fLeft <= r.fLeft && fTop <= r.fTop && fRight >= r.fRight && fBottom >= r.fBottom;
  }
  constexpr bool intersect(const SkIRect &r) noexcept {
    const SkIRect in{std::max(fLeft, r.fLeft), std::max(fTop, r.fTop), std::min(fRight, r.fRight), std::min(fBottom, r.fBottom)};
    if (in.isEmpty())
      return false;
    *this = in;
    return true;
  }
  friend constexpr bool operator==(const SkIRect &, const SkIRect &) = default;
};

struct SkRect {
  float fLeft = 0.0f, fTop = 0.0f, fRight = 0.0f, fBottom = 0.0f;

  [[nodiscard]] static constexpr SkRect MakeEmpty() noexcept { return {}; }
  [[nodiscard]] static constexpr SkRect MakeWH(float w, float h) noexcept { return {0.0f, 0.0f, w, h}; }
  [[nodiscard]] static constexpr SkRect MakeIWH(int w, int h) noexcept {
    return {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
  }
  [[nodiscard]] static constexpr SkRect MakeSize(SkISize s) noexcept { return MakeIWH(s.fWidth, s.fHeight); }
  [[nodiscard]] static constexpr SkRect MakeLTRB(float l, float t, float r, float b) noexcept { return {l, t, r, b}; }
  [[nodiscard]] static constexpr SkRect MakeXYWH(float x, float y, float w, float h) noexcept {
    return {x, y, x + w, y + h};
  }
  [[nodiscard]] static constexpr SkRect Make(const SkIRect &r) noexcept {
    return {static_cast<float>(r.fLeft), static_cast<float>(r.fTop), static_cast<float>(r.fRight),
            static_cast<float>(r.fBottom)};
  }
  [[nodiscard]] static constexpr SkRect Make(SkISize s) noexcept { return MakeIWH(s.fWidth, s.fHeight); }
  [[nodiscard]] static constexpr bool Intersects(const SkRect &a, const SkRect &b) noexcept {
    return std::max(a.fLeft, b.fLeft) < std::min(a.fRight, b.fRight) &&
           std::max(a.fTop, b.fTop) < std::min(a.fBottom, b.fBottom);
  }

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
  [[nodiscard]] constexpr SkPoint center() const noexcept { return {centerX(), centerY()}; }
  [[nodiscard]] constexpr bool isEmpty() const noexcept { return !(fLeft < fRight && fTop < fBottom); }
  [[nodiscard]] bool isFinite() const noexcept {
    return std::isfinite(fLeft) && std::isfinite(fTop) && std::isfinite(fRight) && std::isfinite(fBottom);
  }
  [[nodiscard]] constexpr bool isSorted() const noexcept { return fLeft <= fRight && fTop <= fBottom; }

  constexpr void setEmpty() noexcept { *this = {}; }
  constexpr void setLTRB(float l, float t, float r, float b) noexcept { *this = {l, t, r, b}; }
  constexpr void setXYWH(float x, float y, float w, float h) noexcept { *this = {x, y, x + w, y + h}; }
  constexpr void setWH(float w, float h) noexcept { *this = {0.0f, 0.0f, w, h}; }
  constexpr void setIWH(int w, int h) noexcept { *this = MakeIWH(w, h); }
  constexpr void set(const SkIRect &r) noexcept { *this = Make(r); }
  constexpr void offset(float dx, float dy) noexcept { fLeft += dx; fTop += dy; fRight += dx; fBottom += dy; }
  constexpr void offset(SkPoint d) noexcept { offset(d.fX, d.fY); }
  constexpr void offsetTo(float x, float y) noexcept { fRight += x - fLeft; fBottom += y - fTop; fLeft = x; fTop = y; }
  constexpr void inset(float dx, float dy) noexcept { fLeft += dx; fTop += dy; fRight -= dx; fBottom -= dy; }
  constexpr void outset(float dx, float dy) noexcept { inset(-dx, -dy); }
  constexpr void sort() noexcept {
    if (fLeft > fRight) std::swap(fLeft, fRight);
    if (fTop > fBottom) std::swap(fTop, fBottom);
  }
  [[nodiscard]] constexpr SkRect makeSorted() const noexcept { SkRect r = *this; r.sort(); return r; }
  [[nodiscard]] constexpr SkRect makeOffset(float dx, float dy) const noexcept {
    return {fLeft + dx, fTop + dy, fRight + dx, fBottom + dy};
  }
  [[nodiscard]] constexpr SkRect makeOffset(SkPoint d) const noexcept { return makeOffset(d.fX, d.fY); }
  [[nodiscard]] constexpr SkRect makeInset(float dx, float dy) const noexcept {
    return {fLeft + dx, fTop + dy, fRight - dx, fBottom - dy};
  }
  [[nodiscard]] constexpr SkRect makeOutset(float dx, float dy) const noexcept { return makeInset(-dx, -dy); }

  [[nodiscard]] constexpr bool contains(float x, float y) const noexcept {
    return x >= fLeft && x < fRight && y >= fTop && y < fBottom;
  }
  [[nodiscard]] constexpr bool contains(const SkRect &r) const noexcept {
    return !r.isEmpty() && !isEmpty() && fLeft <= r.fLeft && fTop <= r.fTop && fRight >= r.fRight && fBottom >= r.fBottom;
  }
  [[nodiscard]] constexpr bool contains(const SkIRect &r) const noexcept { return contains(Make(r)); }
  [[nodiscard]] constexpr bool intersects(const SkRect &r) const noexcept { return Intersects(*this, r); }
  // Where the two meet, kept; where they do not, false and this as it was.
  constexpr bool intersect(const SkRect &r) noexcept {
    const SkRect in{std::max(fLeft, r.fLeft), std::max(fTop, r.fTop), std::min(fRight, r.fRight), std::min(fBottom, r.fBottom)};
    if (in.isEmpty())
      return false;
    *this = in;
    return true;
  }
  constexpr bool intersect(const SkRect &a, const SkRect &b) noexcept {
    SkRect r = a;
    if (!r.intersect(b))
      return false;
    *this = r;
    return true;
  }
  // Both, an empty one counting for nothing.
  constexpr void join(const SkRect &r) noexcept {
    if (r.isEmpty())
      return;
    if (isEmpty()) {
      *this = r;
      return;
    }
    joinNonEmptyArg(r);
  }
  constexpr void joinNonEmptyArg(const SkRect &r) noexcept {
    fLeft = std::min(fLeft, r.fLeft);
    fTop = std::min(fTop, r.fTop);
    fRight = std::max(fRight, r.fRight);
    fBottom = std::max(fBottom, r.fBottom);
  }
  constexpr void joinPossiblyEmptyRect(const SkRect &r) noexcept { joinNonEmptyArg(r); }
  [[nodiscard]] SkIRect round() const noexcept {
    return {static_cast<int>(std::lround(fLeft)), static_cast<int>(std::lround(fTop)), static_cast<int>(std::lround(fRight)),
            static_cast<int>(std::lround(fBottom))};
  }
  [[nodiscard]] SkIRect roundOut() const noexcept {
    return {static_cast<int>(std::floor(fLeft)), static_cast<int>(std::floor(fTop)), static_cast<int>(std::ceil(fRight)),
            static_cast<int>(std::ceil(fBottom))};
  }
  void roundOut(SkIRect *out) const noexcept { *out = roundOut(); }
  void round(SkIRect *out) const noexcept { *out = round(); }
  friend constexpr bool operator==(const SkRect &, const SkRect &) = default;
};

// A box with each corner's radii.
class SkRRect {
public:
  enum Corner { kUpperLeft_Corner, kUpperRight_Corner, kLowerRight_Corner, kLowerLeft_Corner };
  enum Type { kEmpty_Type, kRect_Type, kOval_Type, kSimple_Type, kNinePatch_Type, kComplex_Type };
  [[nodiscard]] static SkRRect MakeEmpty() { return {}; }
  [[nodiscard]] static SkRRect MakeRect(const SkRect &r) { SkRRect o; o.setRect(r); return o; }
  [[nodiscard]] static SkRRect MakeOval(const SkRect &r) { SkRRect o; o.setOval(r); return o; }
  [[nodiscard]] static SkRRect MakeRectXY(const SkRect &r, float x, float y) { SkRRect o; o.setRectXY(r, x, y); return o; }
  void setEmpty() { *this = {}; }
  void setRect(const SkRect &r) { setRectXY(r, 0.0f, 0.0f); }
  void setOval(const SkRect &r) { setRectXY(r, r.width() * 0.5f, r.height() * 0.5f); }
  void setRectXY(const SkRect &r, float x, float y) {
    fRect = r.makeSorted();
    const SkVector each{std::max(0.0f, std::min(x, fRect.width() * 0.5f)), std::max(0.0f, std::min(y, fRect.height() * 0.5f))};
    fRadii = {each, each, each, each};
  }
  void setRectRadii(const SkRect &r, const SkVector radii[4]) {
    fRect = r.makeSorted();
    for (int i = 0; i < 4; ++i)
      fRadii[static_cast<std::size_t>(i)] = {std::max(0.0f, radii[i].fX), std::max(0.0f, radii[i].fY)};
  }
  [[nodiscard]] const SkRect &rect() const noexcept { return fRect; }
  [[nodiscard]] const SkRect &getBounds() const noexcept { return fRect; }
  [[nodiscard]] SkVector radii(Corner c) const noexcept { return fRadii[static_cast<std::size_t>(c)]; }
  [[nodiscard]] float width() const noexcept { return fRect.width(); }
  [[nodiscard]] float height() const noexcept { return fRect.height(); }
  [[nodiscard]] bool isEmpty() const noexcept { return fRect.isEmpty(); }
  [[nodiscard]] bool isRect() const noexcept {
    return std::ranges::all_of(fRadii, [](SkVector v) { return v.fX == 0.0f || v.fY == 0.0f; });
  }
  [[nodiscard]] Type getType() const noexcept { return isEmpty() ? kEmpty_Type : isRect() ? kRect_Type : kComplex_Type; }
  void offset(float dx, float dy) { fRect.offset(dx, dy); }
  [[nodiscard]] SkRRect makeOffset(float dx, float dy) const { SkRRect o = *this; o.offset(dx, dy); return o; }
  void inset(float dx, float dy, SkRRect *dst) const {
    *dst = *this;
    dst->fRect.inset(dx, dy);
    for (SkVector &v : dst->fRadii)
      v = {std::max(0.0f, v.fX - dx), std::max(0.0f, v.fY - dy)};
  }
  void outset(float dx, float dy, SkRRect *dst) const { inset(-dx, -dy, dst); }
  [[nodiscard]] bool contains(const SkRect &r) const noexcept { return fRect.contains(r); }
  friend bool operator==(const SkRRect &, const SkRRect &) = default;

private:
  SkRect fRect{};
  std::array<SkVector, 4> fRadii{};
};

// An affine matrix: [scaleX skewX transX; skewY scaleY transY; 0 0 1].
class SkMatrix {
public:
  enum ScaleToFit { kFill_ScaleToFit, kStart_ScaleToFit, kCenter_ScaleToFit, kEnd_ScaleToFit };
  [[nodiscard]] static SkMatrix I() { return {}; }
  [[nodiscard]] static SkMatrix Translate(float dx, float dy) { SkMatrix m; m.fTX = dx; m.fTY = dy; return m; }
  [[nodiscard]] static SkMatrix Translate(SkVector d) { return Translate(d.fX, d.fY); }
  [[nodiscard]] static SkMatrix Scale(float sx, float sy) { SkMatrix m; m.fSX = sx; m.fSY = sy; return m; }
  [[nodiscard]] static SkMatrix RotateDeg(float degrees) { SkMatrix m; m.setRotate(degrees); return m; }
  [[nodiscard]] static SkMatrix MakeAll(float sx, float kx, float tx, float ky, float sy, float ty, float, float, float) {
    SkMatrix m;
    m.fSX = sx; m.fKX = kx; m.fTX = tx; m.fKY = ky; m.fSY = sy; m.fTY = ty;
    return m;
  }
  [[nodiscard]] static SkMatrix Concat(const SkMatrix &a, const SkMatrix &b) {
    SkMatrix m;
    m.fSX = a.fSX * b.fSX + a.fKX * b.fKY;
    m.fKX = a.fSX * b.fKX + a.fKX * b.fSY;
    m.fTX = a.fSX * b.fTX + a.fKX * b.fTY + a.fTX;
    m.fKY = a.fKY * b.fSX + a.fSY * b.fKY;
    m.fSY = a.fKY * b.fKX + a.fSY * b.fSY;
    m.fTY = a.fKY * b.fTX + a.fSY * b.fTY + a.fTY;
    return m;
  }
  // The matrix that takes `src` onto `dst`, stretched (kFill).
  [[nodiscard]] static SkMatrix RectToRect(const SkRect &src, const SkRect &dst, ScaleToFit = kFill_ScaleToFit) {
    if (src.isEmpty())
      return {};
    const float sx = dst.width() / src.width(), sy = dst.height() / src.height();
    SkMatrix m;
    m.fSX = sx; m.fSY = sy;
    m.fTX = dst.fLeft - src.fLeft * sx;
    m.fTY = dst.fTop - src.fTop * sy;
    return m;
  }
  SkMatrix &reset() { *this = {}; return *this; }
  SkMatrix &setIdentity() { return reset(); }
  SkMatrix &setTranslate(float dx, float dy) { *this = Translate(dx, dy); return *this; }
  SkMatrix &setScale(float sx, float sy) { *this = Scale(sx, sy); return *this; }
  SkMatrix &setRotate(float degrees) {
    const double r = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    const float c = static_cast<float>(std::cos(r)), s = static_cast<float>(std::sin(r));
    *this = {};
    fSX = c; fKX = -s; fKY = s; fSY = c;
    return *this;
  }
  SkMatrix &setRectToRect(const SkRect &src, const SkRect &dst, ScaleToFit fit) {
    *this = RectToRect(src, dst, fit);
    return *this;
  }
  SkMatrix &preConcat(const SkMatrix &other) { *this = Concat(*this, other); return *this; }
  SkMatrix &postConcat(const SkMatrix &other) { *this = Concat(other, *this); return *this; }
  SkMatrix &preTranslate(float dx, float dy) { return preConcat(Translate(dx, dy)); }
  SkMatrix &preScale(float sx, float sy) { return preConcat(Scale(sx, sy)); }
  SkMatrix &preRotate(float degrees) { return preConcat(RotateDeg(degrees)); }
  SkMatrix &postTranslate(float dx, float dy) { return postConcat(Translate(dx, dy)); }
  SkMatrix &postScale(float sx, float sy) { return postConcat(Scale(sx, sy)); }
  [[nodiscard]] float getScaleX() const noexcept { return fSX; }
  [[nodiscard]] float getScaleY() const noexcept { return fSY; }
  [[nodiscard]] float getSkewX() const noexcept { return fKX; }
  [[nodiscard]] float getSkewY() const noexcept { return fKY; }
  [[nodiscard]] float getTranslateX() const noexcept { return fTX; }
  [[nodiscard]] float getTranslateY() const noexcept { return fTY; }
  [[nodiscard]] bool isIdentity() const noexcept { return *this == SkMatrix{}; }
  [[nodiscard]] bool isTranslate() const noexcept { return fSX == 1.0f && fSY == 1.0f && fKX == 0.0f && fKY == 0.0f; }
  [[nodiscard]] bool isScaleTranslate() const noexcept { return fKX == 0.0f && fKY == 0.0f; }
  [[nodiscard]] bool rectStaysRect() const noexcept { return isScaleTranslate() || (fSX == 0.0f && fSY == 0.0f); }
  [[nodiscard]] bool hasPerspective() const noexcept { return false; }
  [[nodiscard]] SkPoint mapXY(float x, float y) const noexcept {
    return {fSX * x + fKX * y + fTX, fKY * x + fSY * y + fTY};
  }
  [[nodiscard]] SkPoint mapPoint(SkPoint p) const noexcept { return mapXY(p.fX, p.fY); }
  void mapPoints(SkPoint dst[], const SkPoint src[], int count) const {
    for (int i = 0; i < count; ++i)
      dst[i] = mapPoint(src[i]);
  }
  void mapPoints(SkPoint pts[], int count) const { mapPoints(pts, pts, count); }
  // The box around where the rectangle's corners go.
  [[nodiscard]] SkRect mapRect(const SkRect &r) const noexcept {
    const SkPoint a = mapXY(r.fLeft, r.fTop), b = mapXY(r.fRight, r.fTop), c = mapXY(r.fRight, r.fBottom),
                  d = mapXY(r.fLeft, r.fBottom);
    return {std::min({a.fX, b.fX, c.fX, d.fX}), std::min({a.fY, b.fY, c.fY, d.fY}), std::max({a.fX, b.fX, c.fX, d.fX}),
            std::max({a.fY, b.fY, c.fY, d.fY})};
  }
  bool mapRect(SkRect *dst, const SkRect &src) const { *dst = mapRect(src); return rectStaysRect(); }
  bool mapRect(SkRect *rect) const { return mapRect(rect, *rect); }
  [[nodiscard]] bool invert(SkMatrix *inverse) const {
    const double det = static_cast<double>(fSX) * fSY - static_cast<double>(fKX) * fKY;
    if (det == 0.0 || !std::isfinite(det))
      return false;
    if (inverse != nullptr) {
      const double inv = 1.0 / det;
      SkMatrix m;
      m.fSX = static_cast<float>(fSY * inv);
      m.fKX = static_cast<float>(-fKX * inv);
      m.fKY = static_cast<float>(-fKY * inv);
      m.fSY = static_cast<float>(fSX * inv);
      m.fTX = static_cast<float>((static_cast<double>(fKX) * fTY - static_cast<double>(fSY) * fTX) * inv);
      m.fTY = static_cast<float>((static_cast<double>(fKY) * fTX - static_cast<double>(fSX) * fTY) * inv);
      *inverse = m;
    }
    return true;
  }
  friend SkMatrix operator*(const SkMatrix &a, const SkMatrix &b) { return Concat(a, b); }
  friend bool operator==(const SkMatrix &, const SkMatrix &) = default;

private:
  float fSX = 1.0f, fKX = 0.0f, fTX = 0.0f;
  float fKY = 0.0f, fSY = 1.0f, fTY = 0.0f;
};

// The kinds in a path's list of numbers, each followed by what it says: a
// move or a line to x y; a quad x1 y1 x y; a cubic x1 y1 x2 y2 x y; a close;
// a rect l t w h; a rounded rect l t w h and each corner's x y radii (upper
// left, upper right, lower right, lower left); an oval cx cy rx ry; an arc
// of one cx cy rx ry start sweep (degrees).
namespace pathkind {
inline constexpr float kMove = 1, kLine = 2, kQuad = 3, kCubic = 4, kClose = 5, kRect = 6, kRRect = 7, kOval = 8, kArc = 9;
} // namespace pathkind

// A path: what it is made of, as the numbers above -- for a canvas that
// draws it -- and its bounds.
class SkPath {
public:
  [[nodiscard]] const SkRect &getBounds() const noexcept { return fBounds; }
  [[nodiscard]] SkRect computeTightBounds() const noexcept { return fBounds; }
  [[nodiscard]] bool isEmpty() const noexcept { return fPoints.empty(); }
  [[nodiscard]] int countPoints() const noexcept { return static_cast<int>(fPoints.size()); }
  [[nodiscard]] bool contains(float x, float y) const noexcept { return fBounds.contains(x, y); }
  [[nodiscard]] SkPath makeOffset(float dx, float dy) const {
    SkPath o = *this;
    for (SkPoint &p : o.fPoints)
      p.offset(dx, dy);
    o.fBounds.offset(dx, dy);
    return o;
  }
  [[nodiscard]] SkPath makeTransform(const SkMatrix &m) const {
    SkPath o;
    for (const SkPoint p : fPoints)
      o.add(m.mapPoint(p));
    return o;
  }
  void add(SkPoint p) {
    if (fPoints.empty())
      fBounds = {p.fX, p.fY, p.fX, p.fY};
    else
      fBounds = {std::min(fBounds.fLeft, p.fX), std::min(fBounds.fTop, p.fY), std::max(fBounds.fRight, p.fX),
                 std::max(fBounds.fBottom, p.fY)};
    fPoints.push_back(p);
  }
  void say(std::initializer_list<float> numbers) { fSaid.insert(fSaid.end(), numbers); }
  [[nodiscard]] const std::vector<float> &said() const noexcept { return fSaid; }

private:
  std::vector<SkPoint> fPoints;
  std::vector<float> fSaid;
  SkRect fBounds{};
};

class SkPathBuilder {
public:
  SkPathBuilder &moveTo(float x, float y) { fPath.add({x, y}); fPath.say({pathkind::kMove, x, y}); return *this; }
  SkPathBuilder &moveTo(SkPoint p) { return moveTo(p.fX, p.fY); }
  SkPathBuilder &lineTo(float x, float y) { fPath.add({x, y}); fPath.say({pathkind::kLine, x, y}); return *this; }
  SkPathBuilder &lineTo(SkPoint p) { return lineTo(p.fX, p.fY); }
  SkPathBuilder &quadTo(float x1, float y1, float x2, float y2) {
    fPath.add({x1, y1}); fPath.add({x2, y2}); fPath.say({pathkind::kQuad, x1, y1, x2, y2});
    return *this;
  }
  SkPathBuilder &conicTo(float x1, float y1, float x2, float y2, float) { return quadTo(x1, y1, x2, y2); }
  SkPathBuilder &cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
    fPath.add({x1, y1}); fPath.add({x2, y2}); fPath.add({x3, y3});
    fPath.say({pathkind::kCubic, x1, y1, x2, y2, x3, y3});
    return *this;
  }
  SkPathBuilder &close() { fPath.say({pathkind::kClose}); return *this; }
  SkPathBuilder &addRect(const SkRect &r) {
    fPath.add({r.fLeft, r.fTop}); fPath.add({r.fRight, r.fBottom});
    fPath.say({pathkind::kRect, r.fLeft, r.fTop, r.width(), r.height()});
    return *this;
  }
  SkPathBuilder &addRRect(const SkRRect &r) {
    const SkRect &b = r.rect();
    fPath.add({b.fLeft, b.fTop}); fPath.add({b.fRight, b.fBottom});
    const SkVector a = r.radii(SkRRect::kUpperLeft_Corner), c = r.radii(SkRRect::kUpperRight_Corner),
                   d = r.radii(SkRRect::kLowerRight_Corner), e = r.radii(SkRRect::kLowerLeft_Corner);
    fPath.say({pathkind::kRRect, b.fLeft, b.fTop, b.width(), b.height(), a.fX, a.fY, c.fX, c.fY, d.fX, d.fY, e.fX, e.fY});
    return *this;
  }
  SkPathBuilder &addOval(const SkRect &r) {
    fPath.add({r.fLeft, r.fTop}); fPath.add({r.fRight, r.fBottom});
    fPath.say({pathkind::kOval, r.centerX(), r.centerY(), r.width() * 0.5f, r.height() * 0.5f});
    return *this;
  }
  SkPathBuilder &addCircle(float x, float y, float radius) { return addOval(SkRect::MakeLTRB(x - radius, y - radius, x + radius, y + radius)); }
  SkPathBuilder &addPath(const SkPath &path) {
    for (const float n : path.said()) fPath.say({n});
    if (!path.isEmpty()) {
      fPath.add({path.getBounds().fLeft, path.getBounds().fTop});
      fPath.add({path.getBounds().fRight, path.getBounds().fBottom});
    }
    return *this;
  }
  SkPathBuilder &arcTo(const SkRect &oval, float start, float sweep, bool) {
    fPath.add({oval.fLeft, oval.fTop}); fPath.add({oval.fRight, oval.fBottom});
    fPath.say({pathkind::kArc, oval.centerX(), oval.centerY(), oval.width() * 0.5f, oval.height() * 0.5f, start, sweep});
    return *this;
  }
  [[nodiscard]] SkRect computeBounds() const { return fPath.getBounds(); }
  [[nodiscard]] SkPath snapshot() const { return fPath; }
  SkPath detach() { return std::exchange(fPath, {}); }
  void reset() { fPath = {}; }

private:
  SkPath fPath;
};

// ---- what is drawn with ------------------------------------------------------

enum class SkClipOp { kDifference, kIntersect };
enum class SkBlendMode { kClear, kSrc, kDst, kSrcOver, kDstOver, kSrcIn, kDstIn, kSrcOut, kDstOut, kSrcATop, kDstATop,
                         kXor, kPlus, kModulate, kScreen, kOverlay, kDarken, kLighten, kMultiply };
enum class SkTileMode { kClamp, kRepeat, kMirror, kDecal };
enum class SkFilterMode { kNearest, kLinear };
enum class SkMipmapMode { kNone, kNearest, kLinear };
enum SkColorType { kUnknown_SkColorType, kRGBA_8888_SkColorType, kBGRA_8888_SkColorType, kRGBA_F32_SkColorType,
                   kN32_SkColorType = kBGRA_8888_SkColorType };
enum SkAlphaType { kUnknown_SkAlphaType, kOpaque_SkAlphaType, kPremul_SkAlphaType, kUnpremul_SkAlphaType };

struct SkSamplingOptions {
  SkFilterMode filter = SkFilterMode::kNearest;
  SkMipmapMode mipmap = SkMipmapMode::kNone;
  constexpr SkSamplingOptions() = default;
  explicit constexpr SkSamplingOptions(SkFilterMode f) : filter(f) {}
  constexpr SkSamplingOptions(SkFilterMode f, SkMipmapMode m) : filter(f), mipmap(m) {}
};

// What paints a shape other than its colour: a linear gradient's points and
// colours, which a canvas that draws takes; anything else (an image's) is
// drawn as the paint's colour.
class SkShader {
public:
  virtual ~SkShader() = default;
  bool linear = false;
  SkPoint from{}, to{};
  std::vector<SkColor4f> colours;
};
class SkImageFilter {
public:
  virtual ~SkImageFilter() = default;
};
class SkColorFilter {
public:
  virtual ~SkColorFilter() = default;
};
namespace SkImageFilters {
inline Sp<SkImageFilter> Blur(float, float, Sp<SkImageFilter> = nullptr, const SkRect * = nullptr) {
  return std::make_shared<SkImageFilter>();
}
inline Sp<SkImageFilter> Blur(float, float, SkTileMode, Sp<SkImageFilter> = nullptr, const SkRect * = nullptr) {
  return std::make_shared<SkImageFilter>();
}
} // namespace SkImageFilters

// A gradient's colours, as a shader takes them; nothing is shaded.
class SkGradient {
public:
  struct Colors {
    std::vector<SkColor4f> colors;
    SkTileMode tile = SkTileMode::kClamp;
    Colors(std::span<const SkColor4f> c, SkTileMode t) : colors(c.begin(), c.end()), tile(t) {}
  };
  struct Interpolation {};
  SkGradient(Colors colors, Interpolation) : fColors(std::move(colors)) {}
  [[nodiscard]] const Colors &colors() const noexcept { return fColors; }

private:
  Colors fColors;
};
inline Sp<SkShader> LinearGradient(const SkPoint points[2], const SkGradient &gradient) {
  auto shader = std::make_shared<SkShader>();
  shader->linear = true;
  shader->from = points[0];
  shader->to = points[1];
  shader->colours = gradient.colors().colors;
  return shader;
}

class SkPaint {
public:
  enum Style { kFill_Style, kStroke_Style, kStrokeAndFill_Style };
  enum Cap { kButt_Cap, kRound_Cap, kSquare_Cap };
  enum Join { kMiter_Join, kRound_Join, kBevel_Join };
  SkPaint() = default;
  explicit SkPaint(const SkColor4f &c) : fColor(c) {}
  void setColor(SkColor c) noexcept { fColor = SkColor4f::FromColor(c); }
  void setColor4f(const SkColor4f &c) noexcept { fColor = c; }
  [[nodiscard]] SkColor getColor() const noexcept { return fColor.toSkColor(); }
  [[nodiscard]] SkColor4f getColor4f() const noexcept { return fColor; }
  void setAlphaf(float a) noexcept { fColor.fA = std::clamp(a, 0.0f, 1.0f); }
  [[nodiscard]] float getAlphaf() const noexcept { return fColor.fA; }
  void setAlpha(std::uint8_t a) noexcept { setAlphaf(static_cast<float>(a) / 255.0f); }
  [[nodiscard]] std::uint8_t getAlpha() const noexcept { return static_cast<std::uint8_t>(std::lround(fColor.fA * 255.0f)); }
  void setARGB(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept { setColor(colorSetARGB(a, r, g, b)); }
  void setAntiAlias(bool on) noexcept { fAntiAlias = on; }
  [[nodiscard]] bool isAntiAlias() const noexcept { return fAntiAlias; }
  void setStyle(Style s) noexcept { fStyle = s; }
  [[nodiscard]] Style getStyle() const noexcept { return fStyle; }
  void setStroke(bool on) noexcept { fStyle = on ? kStroke_Style : kFill_Style; }
  void setStrokeWidth(float w) noexcept { fStrokeWidth = w; }
  [[nodiscard]] float getStrokeWidth() const noexcept { return fStrokeWidth; }
  void setStrokeCap(Cap c) noexcept { fCap = c; }
  [[nodiscard]] Cap getStrokeCap() const noexcept { return fCap; }
  void setStrokeJoin(Join j) noexcept { fJoin = j; }
  [[nodiscard]] Join getStrokeJoin() const noexcept { return fJoin; }
  void setStrokeMiter(float m) noexcept { fMiter = m; }
  void setShader(Sp<SkShader> s) { fShader = std::move(s); }
  [[nodiscard]] SkShader *getShader() const noexcept { return fShader.get(); }
  void setImageFilter(Sp<SkImageFilter> f) { fImageFilter = std::move(f); }
  [[nodiscard]] SkImageFilter *getImageFilter() const noexcept { return fImageFilter.get(); }
  void setColorFilter(Sp<SkColorFilter> f) { fColorFilter = std::move(f); }
  void setBlendMode(SkBlendMode m) noexcept { fBlend = m; }
  [[nodiscard]] SkBlendMode getBlendMode_or(SkBlendMode) const noexcept { return fBlend; }
  void setDither(bool) noexcept {}

private:
  SkColor4f fColor{0.0f, 0.0f, 0.0f, 1.0f};
  bool fAntiAlias = false;
  Style fStyle = kFill_Style;
  float fStrokeWidth = 0.0f, fMiter = 4.0f;
  Cap fCap = kButt_Cap;
  Join fJoin = kMiter_Join;
  Sp<SkShader> fShader;
  Sp<SkImageFilter> fImageFilter;
  Sp<SkColorFilter> fColorFilter;
  SkBlendMode fBlend = SkBlendMode::kSrcOver;
};
using Style = SkPaint::Style;
inline constexpr Style kFillStyle = SkPaint::kFill_Style;
inline constexpr Style kStrokeStyle = SkPaint::kStroke_Style;
inline constexpr Style kStrokeAndFillStyle = SkPaint::kStrokeAndFill_Style;
using Join = SkPaint::Join;
inline constexpr Join kMiterJoin = SkPaint::kMiter_Join;
inline constexpr Join kRoundJoin = SkPaint::kRound_Join;
inline constexpr Join kBevelJoin = SkPaint::kBevel_Join;
using Cap = SkPaint::Cap;
inline constexpr Cap kButtCap = SkPaint::kButt_Cap;
inline constexpr Cap kRoundCap = SkPaint::kRound_Cap;
inline constexpr Cap kSquareCap = SkPaint::kSquare_Cap;

class SkData {
public:
  [[nodiscard]] static Sp<SkData> MakeWithCopy(const void *bytes, std::size_t size) {
    auto made = std::make_shared<SkData>();
    const auto *from = static_cast<const std::uint8_t *>(bytes);
    made->fBytes.assign(from, from + size);
    return made;
  }
  [[nodiscard]] static Sp<SkData> MakeWithoutCopy(const void *bytes, std::size_t size) {
    auto made = std::make_shared<SkData>();
    // Like Skia's API, the owner keeps the supplied bytes alive.
    made->fBorrowed = static_cast<const std::uint8_t *>(bytes);
    made->fBorrowedSize = size;
    return made;
  }
  [[nodiscard]] static Sp<SkData> MakeEmpty() { return std::make_shared<SkData>(); }
  [[nodiscard]] const void *data() const noexcept { return bytes(); }
  [[nodiscard]] const std::uint8_t *bytes() const noexcept { return fBorrowed ? fBorrowed : fBytes.data(); }
  [[nodiscard]] std::size_t size() const noexcept { return fBorrowed ? fBorrowedSize : fBytes.size(); }
  [[nodiscard]] bool isEmpty() const noexcept { return size() == 0; }

private:
  const std::uint8_t *fBorrowed = nullptr;
  std::size_t fBorrowedSize = 0;
  std::vector<std::uint8_t> fBytes;
};

class SkString {
public:
  SkString() = default;
  explicit SkString(const char *s) : fText(s ? s : "") {}
  explicit SkString(std::string_view s) : fText(s) {}
  [[nodiscard]] const char *c_str() const noexcept { return fText.c_str(); }
  [[nodiscard]] std::size_t size() const noexcept { return fText.size(); }
  [[nodiscard]] bool isEmpty() const noexcept { return fText.empty(); }
  [[nodiscard]] bool equals(const char *s) const { return fText == (s ? s : ""); }
  void set(const char *s) { fText = s ? s : ""; }
  void set(std::string_view s) { fText = s; }

private:
  std::string fText;
};

// ---- pixels -------------------------------------------------------------------

struct SkImageInfo {
  int fWidth = 0, fHeight = 0;
  SkColorType fColorType = kUnknown_SkColorType;
  SkAlphaType fAlphaType = kUnknown_SkAlphaType;
  [[nodiscard]] static SkImageInfo Make(int w, int h, SkColorType ct, SkAlphaType at) { return {w, h, ct, at}; }
  [[nodiscard]] static SkImageInfo Make(SkISize s, SkColorType ct, SkAlphaType at) { return {s.fWidth, s.fHeight, ct, at}; }
  [[nodiscard]] static SkImageInfo MakeN32Premul(int w, int h) { return {w, h, kN32_SkColorType, kPremul_SkAlphaType}; }
  [[nodiscard]] static SkImageInfo MakeN32Premul(SkISize s) { return MakeN32Premul(s.fWidth, s.fHeight); }
  [[nodiscard]] static SkImageInfo MakeN32(int w, int h, SkAlphaType at) { return {w, h, kN32_SkColorType, at}; }
  [[nodiscard]] int width() const noexcept { return fWidth; }
  [[nodiscard]] int height() const noexcept { return fHeight; }
  [[nodiscard]] SkISize dimensions() const noexcept { return {fWidth, fHeight}; }
  [[nodiscard]] SkIRect bounds() const noexcept { return SkIRect::MakeWH(fWidth, fHeight); }
  [[nodiscard]] bool isEmpty() const noexcept { return fWidth <= 0 || fHeight <= 0; }
  [[nodiscard]] SkColorType colorType() const noexcept { return fColorType; }
  [[nodiscard]] SkAlphaType alphaType() const noexcept { return fAlphaType; }
  [[nodiscard]] SkImageInfo makeColorType(SkColorType ct) const { SkImageInfo i = *this; i.fColorType = ct; return i; }
  [[nodiscard]] SkImageInfo makeAlphaType(SkAlphaType at) const { SkImageInfo i = *this; i.fAlphaType = at; return i; }
  [[nodiscard]] SkImageInfo makeWH(int w, int h) const { SkImageInfo i = *this; i.fWidth = w; i.fHeight = h; return i; }
  [[nodiscard]] int bytesPerPixel() const noexcept { return fColorType == kRGBA_F32_SkColorType ? 16 : 4; }
  [[nodiscard]] std::size_t minRowBytes() const noexcept { return static_cast<std::size_t>(std::max(0, fWidth)) * bytesPerPixel(); }
  [[nodiscard]] std::size_t computeMinByteSize() const noexcept { return minRowBytes() * static_cast<std::size_t>(std::max(0, fHeight)); }
};

// Pixels as ARGB, each a SkColor: what an image holds and a bitmap is.
struct Pixels {
  int width = 0, height = 0;
  std::vector<SkColor> colours;
  [[nodiscard]] SkColor at(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height ? colours[static_cast<std::size_t>(y) * width + x] : 0u;
  }
};

inline std::atomic<std::uint32_t> &nextImageId() {
  static std::atomic<std::uint32_t> id{0};
  return id;
}

class SkImage {
public:
  explicit SkImage(Pixels pixels) : fPixels(std::move(pixels)), fId(++nextImageId()) {}
  [[nodiscard]] int width() const noexcept { return fPixels.width; }
  [[nodiscard]] int height() const noexcept { return fPixels.height; }
  [[nodiscard]] SkISize dimensions() const noexcept { return {width(), height()}; }
  [[nodiscard]] SkIRect bounds() const noexcept { return SkIRect::MakeWH(width(), height()); }
  [[nodiscard]] std::uint32_t uniqueID() const noexcept { return fId; }
  [[nodiscard]] SkImageInfo imageInfo() const { return SkImageInfo::MakeN32Premul(width(), height()); }
  [[nodiscard]] SkAlphaType alphaType() const noexcept { return isOpaque() ? kOpaque_SkAlphaType : kPremul_SkAlphaType; }
  [[nodiscard]] bool isOpaque() const noexcept {
    return !fPixels.colours.empty() && std::ranges::all_of(fPixels.colours, [](SkColor c) { return (c >> 24) == 0xFFu; });
  }
  [[nodiscard]] Sp<SkShader> makeShader(SkTileMode, SkTileMode, const SkSamplingOptions &, const SkMatrix * = nullptr) const {
    return std::make_shared<SkShader>();
  }
  [[nodiscard]] Sp<SkShader> makeShader(const SkSamplingOptions &, const SkMatrix * = nullptr) const {
    return std::make_shared<SkShader>();
  }
  [[nodiscard]] const Pixels &pixels() const noexcept { return fPixels; }
  [[nodiscard]] SkColor getColor(int x, int y) const { return fPixels.at(x, y); }

private:
  Pixels fPixels;
  std::uint32_t fId;
};

// Pixels seen in place: their shape, and where they are. Nothing here
// draws, so nothing hands out its pixels (peekPixels says so); a pixmap is
// only read for its shape, or given to what takes it whole.
class SkPixmap {
public:
  SkPixmap() = default;
  SkPixmap(const SkImageInfo &info, const void *, std::size_t rowBytes) : fInfo(info), fRowBytes(rowBytes) {}
  [[nodiscard]] const SkImageInfo &info() const noexcept { return fInfo; }
  [[nodiscard]] int width() const noexcept { return fInfo.width(); }
  [[nodiscard]] int height() const noexcept { return fInfo.height(); }
  [[nodiscard]] std::size_t rowBytes() const noexcept { return fRowBytes; }
  [[nodiscard]] void *writable_addr(int, int) const noexcept { return nullptr; }

private:
  SkImageInfo fInfo;
  std::size_t fRowBytes = 0;
};

// An image's pixels, row after row, each red, green, blue and alpha: as
// skia's says them (rgbaPixels), from the colours kept here.
inline std::vector<std::uint8_t> rgbaPixels(const SkImage &image) {
  return image.pixels().colours | std::views::transform([](SkColor c) {
           return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(c >> 16), static_cast<std::uint8_t>(c >> 8), static_cast<std::uint8_t>(c),
                                              static_cast<std::uint8_t>(c >> 24)};
         }) |
         std::views::join | std::ranges::to<std::vector>();
}

class SkBitmap {
public:
  // What is written into it: taken as written -- nothing reads it back.
  bool writePixels(const SkPixmap &) { return true; }
  bool tryAllocPixels(const SkImageInfo &info) {
    if (info.isEmpty() || std::int64_t{info.width()} * info.height() > (std::int64_t{1} << 28))
      return false;
    fInfo = info;
    fPixels = {info.width(), info.height(), std::vector<SkColor>(static_cast<std::size_t>(info.width()) * info.height())};
    return true;
  }
  bool tryAllocN32Pixels(int w, int h, bool opaque = false) {
    return tryAllocPixels(SkImageInfo::MakeN32(w, h, opaque ? kOpaque_SkAlphaType : kPremul_SkAlphaType));
  }
  void allocN32Pixels(int w, int h, bool opaque = false) { (void)tryAllocN32Pixels(w, h, opaque); }
  void allocPixels(const SkImageInfo &info) { (void)tryAllocPixels(info); }
  void eraseColor(SkColor c) { std::ranges::fill(fPixels.colours, c); }
  [[nodiscard]] SkColor getColor(int x, int y) const { return fPixels.at(x, y); }
  [[nodiscard]] Sp<SkImage> asImage() const { return fPixels.colours.empty() ? nullptr : std::make_shared<SkImage>(fPixels); }
  [[nodiscard]] int width() const noexcept { return fPixels.width; }
  [[nodiscard]] int height() const noexcept { return fPixels.height; }
  [[nodiscard]] SkISize dimensions() const noexcept { return {width(), height()}; }
  [[nodiscard]] const SkImageInfo &info() const noexcept { return fInfo; }
  [[nodiscard]] bool isNull() const noexcept { return fPixels.colours.empty(); }
  [[nodiscard]] bool drawsNothing() const noexcept { return isNull(); }
  void setImmutable() noexcept {}
  [[nodiscard]] Pixels &pixels() noexcept { return fPixels; }
  // Its pixels as memory, four bytes each, a row after a row: what a
  // filter written for Skia's bitmap (a blur) reads and writes.
  [[nodiscard]] void *getPixels() noexcept { return fPixels.colours.empty() ? nullptr : fPixels.colours.data(); }
  [[nodiscard]] const void *getPixels() const noexcept { return fPixels.colours.empty() ? nullptr : fPixels.colours.data(); }
  [[nodiscard]] std::size_t rowBytes() const noexcept { return static_cast<std::size_t>(fPixels.width) * sizeof(SkColor); }

private:
  SkImageInfo fInfo;
  Pixels fPixels;
};

inline Sp<SkImage> RasterFromBitmap(const SkBitmap &bitmap) { return bitmap.asImage(); }

// ---- what is drawn and kept ------------------------------------------------------

class SkTypeface;
class SkFont;
class SkTextBlob;
class SkSurface;
class SkPicture;
enum class SkTextEncoding { kUTF8, kUTF16, kUTF32, kGlyphID };

} // namespace skia

// Where a canvas's draws go, other than nowhere: a page's <canvas>, drawn by
// the browser's 2D context (skia_lite_page.cc, under Emscripten); and how wide
// the browser makes a text. Without a browser (skia_lite_none.cc) nothing is
// drawn and nothing measured: the faces' own widths are taken.
namespace skia::lite {
using Target = int;
void draw(Target, const SkMatrix &, const std::vector<float> &shape, const SkPaint &);
void clip(Target, const SkMatrix &, const std::vector<float> &shape);
void save(Target);
void restore(Target);
void layer(Target, float alpha);
void text(Target, const SkMatrix &, std::string_view text, float x, float y, float size, bool bold, const SkPaint &);
void clear(Target, SkColor);
[[nodiscard]] Target attach(std::string_view id, int width, int height, float scale);
[[nodiscard]] std::optional<float> measure(std::string_view text, float size, bool bold);
[[nodiscard]] std::optional<std::pair<float, float>> metrics(float size, bool bold);
// A draw kept for later, as a picture keeps what was drawn into it.
struct Command {
  enum Kind { kDraw, kClip, kSave, kRestore, kLayer, kText, kClear } kind = kDraw;
  SkMatrix matrix;
  std::vector<float> shape;
  SkPaint paint;
  std::string text;
  float x = 0.0f, y = 0.0f, size = 0.0f, alpha = 1.0f;
  bool bold = false;
  SkColor colour = 0;
};
} // namespace skia::lite

export namespace skia {

class SkPicture {
public:
  explicit SkPicture(SkRect cull, std::vector<lite::Command> commands = {}) : fCull(cull), fCommands(std::move(commands)) {}
  [[nodiscard]] SkRect cullRect() const noexcept { return fCull; }
  [[nodiscard]] int approximateOpCount() const noexcept { return static_cast<int>(fCommands.size()); }
  [[nodiscard]] const std::vector<lite::Command> &commands() const noexcept { return fCommands; }

private:
  SkRect fCull;
  std::vector<lite::Command> fCommands;
};

// A canvas that keeps where it is -- its matrix and the box its clips leave,
// saved and restored -- so what is skipped (quickReject), mapped
// (getTotalMatrix) and kept (the save count) is as on Skia's. What it draws
// goes to a page's <canvas> where it is made over one (OnPage), into a
// picture where it records one, and nowhere else. A clear fills the pixels
// it was made over.
class SkCanvas {
public:
  enum SrcRectConstraint { kStrict_SrcRectConstraint, kFast_SrcRectConstraint };
  struct SaveLayerRec {
    const SkRect *fBounds = nullptr;
    const SkPaint *fPaint = nullptr;
    const SkImageFilter *fBackdrop = nullptr;
    std::uint32_t fSaveLayerFlags = 0;
    SaveLayerRec() = default;
    SaveLayerRec(const SkRect *bounds, const SkPaint *paint, const SkImageFilter *backdrop = nullptr, std::uint32_t flags = 0)
        : fBounds(bounds), fPaint(paint), fBackdrop(backdrop), fSaveLayerFlags(flags) {}
  };
  SkCanvas() : SkCanvas(kUnbounded) {}
  SkCanvas(int width, int height) : SkCanvas(SkRect::MakeIWH(width, height)) {}
  explicit SkCanvas(const SkBitmap &bitmap) : SkCanvas(SkRect::MakeIWH(bitmap.width(), bitmap.height())) {
    fTarget = const_cast<SkBitmap &>(bitmap).pixels().colours.empty() ? nullptr : &const_cast<SkBitmap &>(bitmap).pixels();
  }
  explicit SkCanvas(SkRect device) { fStack.push_back({SkMatrix{}, device}); fDevice = device; }
  SkCanvas(const SkCanvas &) = delete;
  SkCanvas &operator=(const SkCanvas &) = delete;
  virtual ~SkCanvas() = default;

  // Over the page's <canvas> of this id, `width` by `height` of the page's
  // pixels, `scale` device pixels each: drawn by the browser's 2D context.
  // Without a browser, nowhere.
  [[nodiscard]] static std::unique_ptr<SkCanvas> OnPage(std::string_view id, int width, int height, float scale) {
    auto canvas = std::make_unique<SkCanvas>(SkRect::MakeIWH(width, height));
    canvas->fPage = lite::attach(id, width, height, scale);
    return canvas;
  }
  void attach(Pixels *target, SkSurface *surface = nullptr) { fTarget = target; fSurface = surface; }
  [[nodiscard]] SkSurface *getSurface() const noexcept { return fSurface; }
  void record(std::vector<lite::Command> *into) { fRecord = into; }

  int save() {
    fStack.push_back(fStack.back());
    keep({.kind = lite::Command::kSave});
    return static_cast<int>(fStack.size()) - 1;
  }
  int saveLayer(const SkRect *, const SkPaint *paint) { return saveLayerAlphaf(nullptr, paint ? paint->getAlphaf() : 1.0f); }
  int saveLayer(const SkRect &bounds, const SkPaint *paint) { return saveLayer(&bounds, paint); }
  int saveLayer(const SaveLayerRec &rec) { return saveLayer(rec.fBounds, rec.fPaint); }
  int saveLayerAlphaf(const SkRect *, float alpha) {
    fStack.push_back(fStack.back());
    keep({.kind = lite::Command::kLayer, .alpha = alpha});
    return static_cast<int>(fStack.size()) - 1;
  }
  int saveLayerAlpha(const SkRect *bounds, std::uint32_t alpha) { return saveLayerAlphaf(bounds, static_cast<float>(alpha) / 255.0f); }
  void restore() {
    if (fStack.size() > 1) {
      fStack.pop_back();
      keep({.kind = lite::Command::kRestore});
    }
  }
  void restoreToCount(int count) {
    while (static_cast<int>(fStack.size()) > std::max(1, count))
      restore();
  }
  [[nodiscard]] int getSaveCount() const noexcept { return static_cast<int>(fStack.size()); }

  void translate(float dx, float dy) { top().matrix.preTranslate(dx, dy); }
  void scale(float sx, float sy) { top().matrix.preScale(sx, sy); }
  void rotate(float degrees) { top().matrix.preRotate(degrees); }
  void rotate(float degrees, float px, float py) {
    translate(px, py);
    rotate(degrees);
    translate(-px, -py);
  }
  void concat(const SkMatrix &m) { top().matrix.preConcat(m); }
  void setMatrix(const SkMatrix &m) { top().matrix = m; }
  void resetMatrix() { top().matrix = {}; }
  [[nodiscard]] SkMatrix getTotalMatrix() const { return fStack.back().matrix; }
  [[nodiscard]] SkMatrix getLocalToDeviceAs3x3() const { return getTotalMatrix(); }

  void clipRect(const SkRect &r, SkClipOp op, bool) {
    clipDevice(top().matrix.mapRect(r), op);
    if (op == SkClipOp::kIntersect) keep({.kind = lite::Command::kClip, .shape = {pathkind::kRect, r.fLeft, r.fTop, r.width(), r.height()}});
  }
  void clipRect(const SkRect &r, SkClipOp op) { clipRect(r, op, false); }
  void clipRect(const SkRect &r, bool aa = false) { clipRect(r, SkClipOp::kIntersect, aa); }
  void clipIRect(const SkIRect &r, SkClipOp op = SkClipOp::kIntersect) { clipRect(SkRect::Make(r), op); }
  void clipRRect(const SkRRect &r, SkClipOp op, bool) {
    clipDevice(top().matrix.mapRect(r.rect()), op);
    if (op == SkClipOp::kIntersect) keep({.kind = lite::Command::kClip, .shape = shapeOf(r)});
  }
  void clipRRect(const SkRRect &r, bool aa = false) { clipRRect(r, SkClipOp::kIntersect, aa); }
  void clipPath(const SkPath &p, SkClipOp op, bool) {
    clipDevice(top().matrix.mapRect(p.getBounds()), op);
    if (op == SkClipOp::kIntersect) keep({.kind = lite::Command::kClip, .shape = p.said()});
  }
  void clipPath(const SkPath &p, bool aa = false) { clipPath(p, SkClipOp::kIntersect, aa); }
  [[nodiscard]] bool quickReject(const SkRect &r) const {
    const SkRect device = fStack.back().matrix.mapRect(r);
    return r.isEmpty() || !SkRect::Intersects(device, fStack.back().clip);
  }
  [[nodiscard]] bool quickReject(const SkPath &p) const { return quickReject(p.getBounds()); }
  [[nodiscard]] SkIRect getDeviceClipBounds() const { return fStack.back().clip.roundOut(); }
  [[nodiscard]] SkRect getLocalClipBounds() const {
    SkMatrix inverse;
    if (!fStack.back().matrix.invert(&inverse))
      return SkRect::MakeEmpty();
    return inverse.mapRect(fStack.back().clip);
  }
  [[nodiscard]] bool isClipEmpty() const { return fStack.back().clip.isEmpty(); }
  [[nodiscard]] SkISize getBaseLayerSize() const {
    return {static_cast<int>(fDevice.width()), static_cast<int>(fDevice.height())};
  }
  [[nodiscard]] SkImageInfo imageInfo() const {
    return SkImageInfo::MakeN32Premul(getBaseLayerSize().fWidth, getBaseLayerSize().fHeight);
  }
  [[nodiscard]] Sp<SkSurface> makeSurface(const SkImageInfo &info) const;

  void clear(SkColor c) {
    if (fTarget != nullptr)
      std::ranges::fill(fTarget->colours, c);
    keep({.kind = lite::Command::kClear, .colour = c});
  }
  void clear(const SkColor4f &c) { clear(c.toSkColor()); }
  void drawColor(SkColor c, SkBlendMode = SkBlendMode::kSrcOver) { clear(c); }
  void drawPaint(const SkPaint &) {}
  void drawRect(const SkRect &r, const SkPaint &p) { shape({pathkind::kRect, r.fLeft, r.fTop, r.width(), r.height()}, p); }
  void drawIRect(const SkIRect &r, const SkPaint &p) { drawRect(SkRect::Make(r), p); }
  void drawRRect(const SkRRect &r, const SkPaint &p) { shape(shapeOf(r), p); }
  void drawDRRect(const SkRRect &, const SkRRect &, const SkPaint &) {}
  void drawOval(const SkRect &r, const SkPaint &p) {
    shape({pathkind::kOval, r.centerX(), r.centerY(), r.width() * 0.5f, r.height() * 0.5f}, p);
  }
  void drawCircle(float x, float y, float radius, const SkPaint &p) { shape({pathkind::kOval, x, y, radius, radius}, p); }
  void drawCircle(SkPoint c, float radius, const SkPaint &p) { drawCircle(c.fX, c.fY, radius, p); }
  void drawArc(const SkRect &oval, float start, float sweep, bool, const SkPaint &p) {
    shape({pathkind::kArc, oval.centerX(), oval.centerY(), oval.width() * 0.5f, oval.height() * 0.5f, start, sweep}, p);
  }
  void drawLine(float x0, float y0, float x1, float y1, const SkPaint &p) {
    SkPaint stroked = p;
    stroked.setStyle(SkPaint::kStroke_Style);
    shape({pathkind::kMove, x0, y0, pathkind::kLine, x1, y1}, stroked);
  }
  void drawLine(SkPoint a, SkPoint b, const SkPaint &p) { drawLine(a.fX, a.fY, b.fX, b.fY, p); }
  void drawPath(const SkPath &path, const SkPaint &p) { shape(path.said(), p); }
  void drawImage(const SkImage *, float, float) {}
  void drawImage(const SkImage *, float, float, const SkSamplingOptions &, const SkPaint * = nullptr) {}
  void drawImage(const Sp<SkImage> &, float, float) {}
  void drawImage(const Sp<SkImage> &, float, float, const SkSamplingOptions &, const SkPaint * = nullptr) {}
  void drawImageRect(const SkImage *, const SkRect &, const SkSamplingOptions &, const SkPaint * = nullptr) {}
  void drawImageRect(const Sp<SkImage> &, const SkRect &, const SkSamplingOptions &, const SkPaint * = nullptr) {}
  void drawImageRect(const SkImage *, const SkRect &, const SkRect &, const SkSamplingOptions &, const SkPaint *,
                     SrcRectConstraint) {}
  void drawImageRect(const Sp<SkImage> &, const SkRect &, const SkRect &, const SkSamplingOptions &, const SkPaint *,
                     SrcRectConstraint) {}
  // What a picture kept, drawn again here: through this canvas's matrix, and
  // the matrix and the paint's alpha given.
  void drawPicture(const SkPicture *picture, const SkMatrix *matrix, const SkPaint *paint) {
    if (picture == nullptr || (fPage == 0 && fRecord == nullptr))
      return;
    const float alpha = paint ? paint->getAlphaf() : 1.0f;
    if (alpha < 1.0f) saveLayerAlphaf(nullptr, alpha);
    else save();
    if (matrix) concat(*matrix);
    const SkMatrix base = top().matrix;
    for (const lite::Command &one : picture->commands()) {
      lite::Command again = one;
      again.matrix = SkMatrix::Concat(base, one.matrix);
      keep(again, true);
    }
    restore();
  }
  void drawPicture(const SkPicture *picture) { drawPicture(picture, nullptr, nullptr); }
  void drawPicture(const Sp<SkPicture> &picture) { drawPicture(picture.get(), nullptr, nullptr); }
  void drawPicture(const Sp<SkPicture> &picture, const SkMatrix *matrix, const SkPaint *paint) {
    drawPicture(picture.get(), matrix, paint);
  }
  void drawSimpleText(const void *text, std::size_t bytes, SkTextEncoding encoding, float x, float y, const SkFont &font,
                      const SkPaint &paint);
  void drawTextBlob(const SkTextBlob *, float, float, const SkPaint &) {}
  void drawTextBlob(const Sp<SkTextBlob> &, float, float, const SkPaint &) {}

private:
  static constexpr SkRect kUnbounded{-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f};
  struct Level {
    SkMatrix matrix;
    SkRect clip;
  };
  Level &top() { return fStack.back(); }
  void clipDevice(const SkRect &device, SkClipOp op) {
    if (op == SkClipOp::kDifference)
      return;  // what it leaves is not a box: kept as it was, as Skia's bounds do
    SkRect &clip = top().clip;
    if (!clip.intersect(device))
      clip.setEmpty();
  }
  [[nodiscard]] static std::vector<float> shapeOf(const SkRRect &r) {
    const SkRect &b = r.rect();
    const SkVector a = r.radii(SkRRect::kUpperLeft_Corner), c = r.radii(SkRRect::kUpperRight_Corner),
                   d = r.radii(SkRRect::kLowerRight_Corner), e = r.radii(SkRRect::kLowerLeft_Corner);
    return {pathkind::kRRect, b.fLeft, b.fTop, b.width(), b.height(), a.fX, a.fY, c.fX, c.fY, d.fX, d.fY, e.fX, e.fY};
  }
  void shape(std::vector<float> numbers, const SkPaint &paint) {
    if (fPage == 0 && fRecord == nullptr)
      return;
    keep({.kind = lite::Command::kDraw, .shape = std::move(numbers), .paint = paint});
  }
  // A command: kept in the picture being recorded, or drawn on the page --
  // through this canvas's matrix, unless it carries its own (`placed`).
  void keep(lite::Command one, bool placed = false) {
    if (fPage == 0 && fRecord == nullptr)
      return;
    if (!placed)
      one.matrix = top().matrix;
    if (fRecord != nullptr) {
      fRecord->push_back(std::move(one));
      return;
    }
    switch (one.kind) {
    case lite::Command::kDraw: lite::draw(fPage, one.matrix, one.shape, one.paint); break;
    case lite::Command::kClip: lite::clip(fPage, one.matrix, one.shape); break;
    case lite::Command::kSave: lite::save(fPage); break;
    case lite::Command::kRestore: lite::restore(fPage); break;
    case lite::Command::kLayer: lite::layer(fPage, one.alpha); break;
    case lite::Command::kText: lite::text(fPage, one.matrix, one.text, one.x, one.y, one.size, one.bold, one.paint); break;
    case lite::Command::kClear: lite::clear(fPage, one.colour); break;
    }
  }
  friend class SkFont;
  std::vector<Level> fStack;
  SkRect fDevice{};
  SkSurface *fSurface = nullptr;
  Pixels *fTarget = nullptr;
  lite::Target fPage = 0;
  std::vector<lite::Command> *fRecord = nullptr;
};

struct SkSurfaceProps {};

class SkSurface {
public:
  explicit SkSurface(const SkImageInfo &info)
      : fInfo(info), fPixels{info.width(), info.height(), std::vector<SkColor>(info.computeMinByteSize() / 4)},
        fCanvas(std::make_unique<SkCanvas>(SkRect::MakeIWH(info.width(), info.height()))) {
    fCanvas->attach(&fPixels, this);
  }
  SkSurface(const SkSurface &) = delete;
  SkSurface &operator=(const SkSurface &) = delete;
  [[nodiscard]] SkCanvas *getCanvas() noexcept { return fCanvas.get(); }
  [[nodiscard]] Sp<SkImage> makeImageSnapshot() const { return std::make_shared<SkImage>(fPixels); }
  [[nodiscard]] int width() const noexcept { return fInfo.width(); }
  [[nodiscard]] int height() const noexcept { return fInfo.height(); }
  [[nodiscard]] const SkImageInfo &imageInfo() const noexcept { return fInfo; }
  // Its pixels in place: none to hand out, where nothing is drawn.
  bool peekPixels(SkPixmap *) const { return false; }

private:
  SkImageInfo fInfo;
  Pixels fPixels;
  std::unique_ptr<SkCanvas> fCanvas;
};

inline Sp<SkSurface> Raster(const SkImageInfo &info, std::size_t = 0, const SkSurfaceProps * = nullptr) {
  if (info.isEmpty() || std::int64_t{info.width()} * info.height() > (std::int64_t{1} << 28))
    return nullptr;
  return std::make_shared<SkSurface>(info);
}
inline Sp<SkSurface> SkCanvas::makeSurface(const SkImageInfo &info) const { return Raster(info); }
// A surface over pixels the caller has (a window's): one of its own here,
// as nothing is drawn into either.
inline Sp<SkSurface> WrapPixels(const SkImageInfo &info, void *, std::size_t, const SkSurfaceProps * = nullptr) { return Raster(info); }

// What is drawn into its canvas, kept as commands and drawn again where the
// picture is drawn.
class SkPictureRecorder {
public:
  SkCanvas *beginRecording(const SkRect &bounds) {
    fBounds = bounds;
    fCommands.clear();
    fCanvas = std::make_unique<SkCanvas>();
    fCanvas->record(&fCommands);
    return fCanvas.get();
  }
  [[nodiscard]] SkCanvas *getRecordingCanvas() noexcept { return fCanvas.get(); }
  [[nodiscard]] Sp<SkPicture> finishRecordingAsPicture() {
    fCanvas.reset();
    return std::make_shared<SkPicture>(fBounds, std::exchange(fCommands, {}));
  }

private:
  SkRect fBounds{};
  std::vector<lite::Command> fCommands;
  std::unique_ptr<SkCanvas> fCanvas;
};

// ---- faces ---------------------------------------------------------------------
//
// Every face has every glyph, and every glyph one width: a character of
// ASCII is 0.55 of the size across, any other a whole size. Text is
// measured the same on every machine, and nothing is read from a disk.

enum class SkFontHinting { kNone, kSlight, kNormal, kFull };
inline constexpr SkFontHinting kNoHinting = SkFontHinting::kNone;

class SkFontStyle {
public:
  enum Weight { kInvisible_Weight = 0, kThin_Weight = 100, kExtraLight_Weight = 200, kLight_Weight = 300,
                kNormal_Weight = 400, kMedium_Weight = 500, kSemiBold_Weight = 600, kBold_Weight = 700,
                kExtraBold_Weight = 800, kBlack_Weight = 900, kExtraBlack_Weight = 1000 };
  enum Width { kUltraCondensed_Width = 1, kCondensed_Width = 3, kNormal_Width = 5, kExpanded_Width = 7 };
  enum Slant { kUpright_Slant, kItalic_Slant, kOblique_Slant };
  constexpr SkFontStyle() = default;
  constexpr SkFontStyle(int weight, int width, Slant slant) : fWeight(weight), fWidth(width), fSlant(slant) {}
  [[nodiscard]] static constexpr SkFontStyle Normal() { return {}; }
  [[nodiscard]] static constexpr SkFontStyle Bold() { return {kBold_Weight, kNormal_Width, kUpright_Slant}; }
  [[nodiscard]] static constexpr SkFontStyle Italic() { return {kNormal_Weight, kNormal_Width, kItalic_Slant}; }
  [[nodiscard]] static constexpr SkFontStyle BoldItalic() { return {kBold_Weight, kNormal_Width, kItalic_Slant}; }
  [[nodiscard]] constexpr int weight() const noexcept { return fWeight; }
  [[nodiscard]] constexpr int width() const noexcept { return fWidth; }
  [[nodiscard]] constexpr Slant slant() const noexcept { return fSlant; }
  friend constexpr bool operator==(const SkFontStyle &, const SkFontStyle &) = default;

private:
  int fWeight = kNormal_Weight;
  int fWidth = kNormal_Width;
  Slant fSlant = kUpright_Slant;
};

class SkFontArguments {
public:
  struct VariationPosition {
    struct Coordinate {
      SkFourByteTag axis = 0;
      float value = 0.0f;
    };
    const Coordinate *coordinates = nullptr;
    int coordinateCount = 0;
  };
  SkFontArguments &setVariationDesignPosition(VariationPosition position) {
    fPosition = position;
    return *this;
  }
  [[nodiscard]] VariationPosition getVariationDesignPosition() const noexcept { return fPosition; }
  SkFontArguments &setCollectionIndex(int) { return *this; }

private:
  VariationPosition fPosition;
};

struct SkFontMetrics {
  std::uint32_t fFlags = 0;
  float fTop = 0.0f, fAscent = 0.0f, fDescent = 0.0f, fBottom = 0.0f, fLeading = 0.0f;
  float fAvgCharWidth = 0.0f, fMaxCharWidth = 0.0f, fXMin = 0.0f, fXMax = 0.0f;
  float fXHeight = 0.0f, fCapHeight = 0.0f;
  float fUnderlineThickness = 0.0f, fUnderlinePosition = 0.0f, fStrikeoutThickness = 0.0f, fStrikeoutPosition = 0.0f;
};

inline std::uint32_t &nextFaceId() {
  static std::uint32_t id = 0;
  return id;
}

class SkTypeface {
public:
  SkTypeface(std::string family, SkFontStyle style) : fFamily(std::move(family)), fStyle(style), fId(++nextFaceId()) {}
  [[nodiscard]] SkGlyphID unicharToGlyph(SkUnichar cp) const noexcept {
    return cp <= 0 ? SkGlyphID{0} : static_cast<SkGlyphID>(cp % 65535 + 1);
  }
  void unicharsToGlyphs(const SkUnichar uni[], int count, SkGlyphID glyphs[]) const {
    for (int i = 0; i < count; ++i)
      glyphs[i] = unicharToGlyph(uni[i]);
  }
  [[nodiscard]] int countGlyphs() const noexcept { return 65535; }
  [[nodiscard]] int getUnitsPerEm() const noexcept { return 1000; }
  [[nodiscard]] Sp<SkData> copyTableData(SkFontTableTag) const { return nullptr; }
  [[nodiscard]] Sp<SkTypeface> makeClone(const SkFontArguments &) const { return std::make_shared<SkTypeface>(*this); }
  [[nodiscard]] int getVariationDesignPosition(std::span<SkFontArguments::VariationPosition::Coordinate>) const { return 0; }
  void getFamilyName(SkString *name) const { name->set(fFamily); }
  [[nodiscard]] SkFontStyle fontStyle() const noexcept { return fStyle; }
  [[nodiscard]] bool isBold() const noexcept { return fStyle.weight() >= SkFontStyle::kSemiBold_Weight; }
  [[nodiscard]] bool isItalic() const noexcept { return fStyle.slant() != SkFontStyle::kUpright_Slant; }
  [[nodiscard]] bool isFixedPitch() const noexcept { return false; }
  [[nodiscard]] std::uint32_t uniqueID() const noexcept { return fId; }

private:
  std::string fFamily;
  SkFontStyle fStyle;
  std::uint32_t fId;
};

// How wide one character is, at a size of 1.
[[nodiscard]] inline float liteAdvance(SkUnichar cp) noexcept { return cp < 0x80 ? 0.55f : 1.0f; }

class SkFont {
public:
  enum class Edging { kAlias, kAntiAlias, kSubpixelAntiAlias };
  SkFont() = default;
  explicit SkFont(Sp<SkTypeface> face, float size = 12.0f) : fFace(std::move(face)), fSize(size) {}
  SkFont(Sp<SkTypeface> face, float size, float scaleX, float skewX)
      : fFace(std::move(face)), fSize(size), fScaleX(scaleX), fSkewX(skewX) {}
  [[nodiscard]] float getSize() const noexcept { return fSize; }
  void setSize(float size) noexcept { fSize = std::max(0.0f, size); }
  [[nodiscard]] SkTypeface *getTypeface() const noexcept { return fFace.get(); }
  [[nodiscard]] Sp<SkTypeface> refTypeface() const { return fFace; }
  void setTypeface(Sp<SkTypeface> face) { fFace = std::move(face); }
  [[nodiscard]] float getScaleX() const noexcept { return fScaleX; }
  void setScaleX(float s) noexcept { fScaleX = s; }
  [[nodiscard]] float getSkewX() const noexcept { return fSkewX; }
  void setSkewX(float s) noexcept { fSkewX = s; }
  [[nodiscard]] bool isEmbolden() const noexcept { return fEmbolden; }
  void setEmbolden(bool on) noexcept { fEmbolden = on; }
  [[nodiscard]] bool isSubpixel() const noexcept { return fSubpixel; }
  void setSubpixel(bool on) noexcept { fSubpixel = on; }
  [[nodiscard]] bool isLinearMetrics() const noexcept { return fLinear; }
  void setLinearMetrics(bool on) noexcept { fLinear = on; }
  [[nodiscard]] SkFontHinting getHinting() const noexcept { return fHinting; }
  void setHinting(SkFontHinting h) noexcept { fHinting = h; }
  void setEdging(Edging e) noexcept { fEdging = e; }
  [[nodiscard]] Edging getEdging() const noexcept { return fEdging; }
  void setBaselineSnap(bool) noexcept {}
  void setForceAutoHinting(bool) noexcept {}

  // Whether it is drawn bold: emboldened, or a bold face.
  [[nodiscard]] bool bold() const noexcept { return fEmbolden || (fFace && fFace->isBold()); }
  // How wide the browser draws it, where there is one; else each character's
  // width. None where there is no face, as Skia's empty one.
  float measureText(const void *text, std::size_t bytes, SkTextEncoding encoding, SkRect *bounds = nullptr,
                    const SkPaint * = nullptr) const {
    float width = 0.0f;
    const std::optional<float> browser =
        fFace && encoding == SkTextEncoding::kUTF8
            ? lite::measure(std::string_view(static_cast<const char *>(text), bytes), fSize, bold())
            : std::nullopt;
    if (browser)
      width = *browser * fScaleX;
    else if (fFace)
      forEach(text, bytes, encoding, [&](SkUnichar cp) { width += liteAdvance(cp) * fSize * fScaleX; });
    if (bounds != nullptr)
      *bounds = width > 0.0f ? SkRect::MakeLTRB(0.0f, -0.75f * fSize, width, 0.2f * fSize) : SkRect::MakeEmpty();
    return width;
  }
  float getMetrics(SkFontMetrics *metrics) const {
    if (metrics != nullptr) {
      *metrics = {};
      if (fFace) {
        const auto browser = lite::metrics(fSize, bold());
        metrics->fTop = -0.95f * fSize;
        metrics->fAscent = browser ? browser->first : -0.8f * fSize;
        metrics->fDescent = browser ? browser->second : 0.2f * fSize;
        metrics->fBottom = 0.25f * fSize;
        metrics->fXHeight = 0.5f * fSize;
        metrics->fCapHeight = 0.7f * fSize;
        metrics->fAvgCharWidth = 0.55f * fSize;
        metrics->fMaxCharWidth = fSize;
      }
    }
    return fFace ? fSize : 0.0f;
  }
  [[nodiscard]] float getSpacing() const { return getMetrics(nullptr); }
  int textToGlyphs(const void *text, std::size_t bytes, SkTextEncoding encoding, SkGlyphID glyphs[], int most) const {
    int count = 0;
    forEach(text, bytes, encoding, [&](SkUnichar cp) {
      if (glyphs != nullptr && count < most)
        glyphs[count] = fFace ? fFace->unicharToGlyph(cp) : SkGlyphID{0};
      ++count;
    });
    return count;
  }
  int textToGlyphs(const void *text, std::size_t bytes, SkTextEncoding encoding, std::span<SkGlyphID> glyphs) const {
    return textToGlyphs(text, bytes, encoding, glyphs.data(), static_cast<int>(glyphs.size()));
  }
  [[nodiscard]] SkGlyphID unicharToGlyph(SkUnichar cp) const noexcept { return fFace ? fFace->unicharToGlyph(cp) : SkGlyphID{0}; }
  void getWidths(const SkGlyphID glyphs[], int count, float widths[]) const {
    for (int i = 0; i < count; ++i)
      widths[i] = fFace ? liteAdvance(static_cast<SkUnichar>(glyphs[i] - 1)) * fSize * fScaleX : 0.0f;
  }

private:
  template <class F> static void forEach(const void *text, std::size_t bytes, SkTextEncoding encoding, F &&f) {
    const auto *in = static_cast<const std::uint8_t *>(text);
    switch (encoding) {
    case SkTextEncoding::kUTF8:
      for (std::size_t i = 0; i < bytes;) {
        const std::uint8_t b = in[i];
        const int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
        SkUnichar cp = extra == 0 ? b : b & (0x3F >> extra);
        for (int k = 1; k <= extra && i + static_cast<std::size_t>(k) < bytes; ++k)
          cp = (cp << 6) | (in[i + static_cast<std::size_t>(k)] & 0x3F);
        f(cp);
        i += static_cast<std::size_t>(extra) + 1;
      }
      break;
    case SkTextEncoding::kUTF16:
      for (std::size_t i = 0; i + 1 < bytes; i += 2)
        f(static_cast<SkUnichar>(in[i] | (in[i + 1] << 8)));
      break;
    case SkTextEncoding::kUTF32:
      for (std::size_t i = 0; i + 3 < bytes; i += 4)
        f(static_cast<SkUnichar>(in[i] | (in[i + 1] << 8) | (in[i + 2] << 16) | (in[i + 3] << 24)));
      break;
    case SkTextEncoding::kGlyphID:
      for (std::size_t i = 0; i + 1 < bytes; i += 2)
        f(static_cast<SkUnichar>((in[i] | (in[i + 1] << 8)) - 1));
      break;
    }
  }
  Sp<SkTypeface> fFace;
  float fSize = 12.0f, fScaleX = 1.0f, fSkewX = 0.0f;
  bool fEmbolden = false, fSubpixel = false, fLinear = false;
  SkFontHinting fHinting = SkFontHinting::kNormal;
  Edging fEdging = Edging::kAntiAlias;
};

// Drawn on the page, or kept in the picture: the text, where it is, at the
// font's size and weight, in the paint's colour.
inline void SkCanvas::drawSimpleText(const void *text, std::size_t bytes, SkTextEncoding encoding, float x, float y,
                                     const SkFont &font, const SkPaint &paint) {
  if ((fPage == 0 && fRecord == nullptr) || encoding != SkTextEncoding::kUTF8 || font.getTypeface() == nullptr)
    return;
  keep({.kind = lite::Command::kText, .paint = paint, .text = std::string(static_cast<const char *>(text), bytes), .x = x, .y = y,
        .size = font.getSize(), .bold = font.bold()});
}

class SkFontStyleSet {
public:
  explicit SkFontStyleSet(std::vector<Sp<SkTypeface>> faces) : fFaces(std::move(faces)) {}
  [[nodiscard]] int count() const noexcept { return static_cast<int>(fFaces.size()); }
  void getStyle(int index, SkFontStyle *style, SkString *name) const {
    if (index < 0 || index >= count())
      return;
    if (style != nullptr)
      *style = fFaces[static_cast<std::size_t>(index)]->fontStyle();
    if (name != nullptr)
      name->set("Regular");
  }
  [[nodiscard]] Sp<SkTypeface> createTypeface(int index) const {
    return index >= 0 && index < count() ? fFaces[static_cast<std::size_t>(index)] : nullptr;
  }
  [[nodiscard]] Sp<SkTypeface> matchStyle(const SkFontStyle &) const { return fFaces.empty() ? nullptr : fFaces.front(); }

private:
  std::vector<Sp<SkTypeface>> fFaces;
};

// One family, regular and bold, whatever is asked for.
class SkFontMgr {
public:
  SkFontMgr()
      : fRegular(std::make_shared<SkTypeface>("Lite Sans", SkFontStyle::Normal())),
        fBold(std::make_shared<SkTypeface>("Lite Sans", SkFontStyle::Bold())) {}
  [[nodiscard]] int countFamilies() const noexcept { return 1; }
  void getFamilyName(int, SkString *name) const { name->set("Lite Sans"); }
  [[nodiscard]] Sp<SkFontStyleSet> createStyleSet(int index) const {
    return index == 0 ? std::make_shared<SkFontStyleSet>(std::vector<Sp<SkTypeface>>{fRegular, fBold}) : nullptr;
  }
  [[nodiscard]] Sp<SkFontStyleSet> matchFamily(const char *) const { return createStyleSet(0); }
  [[nodiscard]] Sp<SkTypeface> matchFamilyStyle(const char *, const SkFontStyle &style) const {
    return style.weight() >= SkFontStyle::kSemiBold_Weight ? fBold : fRegular;
  }
  [[nodiscard]] Sp<SkTypeface> matchFamilyStyleCharacter(const char *family, const SkFontStyle &style, const char *[], int,
                                                         SkUnichar) const {
    return matchFamilyStyle(family, style);
  }
  [[nodiscard]] Sp<SkTypeface> legacyMakeTypeface(const char *family, SkFontStyle style) const {
    return matchFamilyStyle(family, style);
  }
  [[nodiscard]] Sp<SkTypeface> makeFromData(Sp<SkData> data, int = 0) const { return data ? fRegular : nullptr; }
  [[nodiscard]] Sp<SkTypeface> makeFromFile(const char *path, int = 0) const { return path ? fRegular : nullptr; }

private:
  Sp<SkTypeface> fRegular, fBold;
};
inline Sp<SkFontMgr> SkFontMgr_New_Custom_Directory(const char *) { return std::make_shared<SkFontMgr>(); }
inline Sp<SkFontMgr> SkFontMgr_New_Custom_Data(std::span<const Sp<SkData>>) { return std::make_shared<SkFontMgr>(); }

class SkTextBlob {
public:
  explicit SkTextBlob(SkRect bounds) : fBounds(bounds) {}
  [[nodiscard]] const SkRect &bounds() const noexcept { return fBounds; }

private:
  SkRect fBounds;
};
class SkTextBlobBuilder {
public:
  struct RunBuffer {
    SkGlyphID *glyphs = nullptr;
    float *pos = nullptr;
    char *utf8text = nullptr;
    std::uint32_t *clusters = nullptr;
  };
  const RunBuffer &allocRunPosH(const SkFont &font, int count, float, const SkRect * = nullptr) {
    fRuns.push_back({std::vector<SkGlyphID>(static_cast<std::size_t>(std::max(0, count))),
                     std::vector<float>(static_cast<std::size_t>(std::max(0, count))), font.getSize()});
    fBuffer = {fRuns.back().glyphs.data(), fRuns.back().xs.data()};
    return fBuffer;
  }
  [[nodiscard]] Sp<SkTextBlob> make() {
    if (fRuns.empty())
      return nullptr;
    SkRect bounds = SkRect::MakeEmpty();
    for (const Run &run : fRuns)
      if (!run.xs.empty())
        bounds.join(SkRect::MakeLTRB(run.xs.front(), -run.size, run.xs.back() + run.size, run.size * 0.25f));
    fRuns.clear();
    return std::make_shared<SkTextBlob>(bounds);
  }

private:
  struct Run {
    std::vector<SkGlyphID> glyphs;
    std::vector<float> xs;
    float size = 0.0f;
  };
  std::deque<Run> fRuns;
  RunBuffer fBuffer;
};

// ---- files of images -------------------------------------------------------------
//
// The browser supplies its codecs; the stand-in outside a browser has none.
// A picture made of pixels the program has is still an image on either host.

inline constexpr std::int64_t kMostPixels = std::int64_t{64} << 20;
struct Frame {
  Sp<SkImage> image;
  int durationMs = 100;
};
namespace lite {
Sp<SkImage> decode(const void *data, std::size_t size, int most);
std::string encode(const SkImage &image, bool jpeg);
}
inline Sp<SkImage> decodeImage(const void *data, std::size_t size) { return lite::decode(data, size, 0); }
inline Sp<SkImage> decodeImageAtMost(const void *data, std::size_t size, int most) { return lite::decode(data, size, most); }
// Browser image decoding returns the first frame, including animated formats.
inline std::vector<Frame> decodeFrames(const void *data, std::size_t size, std::size_t budget = 64u << 20) {
  auto image = decodeImage(data, size);
  if (!image || static_cast<std::size_t>(image->width()) * image->height() > budget / 4) return {};
  return {{std::move(image), 100}};
}
// Four bytes each -- red, green, blue, alpha, not premultiplied -- row after row: copied.
inline Sp<SkImage> imageFromRGBA(int width, int height, const std::uint8_t *rgba) {
  if (width <= 0 || height <= 0 || rgba == nullptr)
    return nullptr;
  Pixels pixels{width, height, std::vector<SkColor>(static_cast<std::size_t>(width) * height)};
  for (std::size_t i = 0; i < pixels.colours.size(); ++i)
    pixels.colours[i] = colorSetARGB(rgba[i * 4 + 3], rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
  return std::make_shared<SkImage>(std::move(pixels));
}
inline std::string encodeImage(const SkImage &image, bool jpeg) { return lite::encode(image, jpeg); }
namespace apng {
inline std::uint32_t word(std::string_view data, std::size_t at) {
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(data[at + static_cast<std::size_t>(i)]);
  return value;
}
inline void word(std::string &data, std::uint32_t value) {
  for (int i = 3; i >= 0; --i) data.push_back(static_cast<char>(value >> (i * 8)));
}
inline std::uint32_t crc(std::string_view bytes) {
  std::uint32_t value = 0xffffffffu;
  for (unsigned char byte : bytes) {
    value ^= byte;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
  }
  return ~value;
}
inline void chunk(std::string &png, std::string_view type, std::string_view data) {
  word(png, static_cast<std::uint32_t>(data.size()));
  const auto at = png.size();
  png += type;
  png += data;
  word(png, crc(std::string_view(png).substr(at)));
}
inline std::optional<std::vector<Frame>> decode(std::string_view, std::size_t) { return std::nullopt; }
} // namespace apng

// Whether this is Skia, or this stand-in: a test of pixels asks.
inline constexpr bool kLite = true;

} // namespace skia
