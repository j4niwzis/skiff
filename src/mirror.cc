// skiff.mirror: a scene's tree as a document's elements, laid out and painted
// by an engine rather than by skiff -- the tree a program already has (its
// nodes, its bindings, its presses) shown through skiff.web's dom, whichever
// backend that is: a browser's document, an engine embedded and called
// directly, or the in-memory one a test reads back.
//
// Nothing of the scene's layout runs: each node says what it is as CSS, from
// what its State keeps and what kind of node it is --
//   a stack or a flow        a flex box along its way, its gaps and
//                            justification, where it needs one (a row; a
//                            column some part of which takes a share of what
//                            is left); else blocks, one under another;
//   a text                   its words, in its size, weight and colour;
//   a view that scrolls      a box that scrolls (overflow: auto);
//   any other node           a box: its first child in its flow, the rest
//                            over it, each placed by its anchor, as the scene
//                            places the children of a node that is not a flow
//                            (a flex box and absolute places: what every
//                            engine has, litehtml's without CSS grid too).
// Sizes are the State's: a share of the parent's where relative, the
// content's where sized by it, else what it says. A press on an element is
// the node's: found by its path, and pressed as the window would press it.
export module skiff.mirror;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.web;

export namespace skiff::mirror {

// What the engine behind the document can do, where engines differ: inline
// SVG (a widget's paths and arcs written as SVG), transforms and opacity (a
// shift and a fade written as they are), and flex boxes nested as deep as a
// tree is (every flow a flex box). litehtml has none of them -- the mirror
// writes around them: offsets, hidden, blocks; Blitz has all three.
struct Engine {
  bool svg = false;
  bool transforms = false;
  bool deepFlex = false;
};
inline constexpr Engine kLitehtml{};
inline constexpr Engine kFull{.svg = true, .transforms = true, .deepFlex = true};

namespace dom = skiff::web::dom;
using skiff::web::Markup;

namespace detail {

using skiff::web::px;
using skiff::web::number;
using skiff::web::rgba;

// Whether a flow runs across: its direction, visited.
[[nodiscard]] inline bool isRow(const scene::Direction &d) {
  return spl::visit(spl::overloaded{[](scene::direction::vertical) { return false; }, [](scene::direction::horizontal) { return true; }}, d);
}
[[nodiscard]] inline bool justifiesFromStart(const scene::Justify &j) {
  return spl::visit(spl::overloaded{[](scene::justify::start) { return true; }, [](const auto &) { return false; }}, j);
}

// What holds a node: how a node lays its children out decides how each child
// is placed in it.
//
// Flex only where skiff's layout needs it -- a row, or a column some part of
// which takes a share of what is left -- and blocks everywhere else: an
// engine lays a flex item out two to four times over (to measure it, at its
// share, stretched), each time all of what is in it, so flex boxes nested
// as deep as a program's tree is (mux's, twenty and more) were laid out
// some three times more for each level -- litehtml never finished the first
// layout. Blocks are laid out once.
struct in_flex {
  bool row = false;
};
// In a column laid out as blocks: after the first, the column's gap above;
// across, as the column aligns its parts (or the part itself says).
struct in_column {
  float gap = 0.0f;
  bool first = true;
  scene::Align cross{};
};
// In a box: the first part in its flow, sizing the box; the rest over it,
// each where its anchor puts it.
struct in_cell {
  bool first = true;
};
struct in_scroll {};
using Holder = spl::variant<in_flex, in_column, in_cell, in_scroll>;

// The size along one axis, as the State says it.
inline void sizeOf(Markup &m, std::string_view property, float size, bool relative, bool automatic) {
  if (relative)
    m.style(property, number(size * 100.0f) + "%");
  else if (!automatic && size > 0.0f)
    m.style(property, px(size));
}
[[nodiscard]] inline bool displayIs(const Markup &m, std::string_view value) {
  return std::ranges::contains(m.fStyles, std::pair<std::string, std::string>("display", std::string(value)));
}
// A part in a block's flow, across: as wide as what it holds where it says
// no width of its own (a table: what an engine without fit-content shrinks
// to its content), and where its alignment puts it.
// Words are not shrunk: a table is laid out at its narrowest by litehtml,
// and a text in one was a word a line; they keep the block's width, their
// lines aligned as the part would be. Nor is a part that paints nothing
// round itself: as wide as the block or as what it holds, it looks the same.
inline void acrossInBlock(Markup &m, const scene::State &s, bool relX, float at) {
  const bool sized = relX || (s.fWidth > 0.0f && !s.fAutoSizeAxes.has<scene::axis::x>());
  if (m.fText) {
    if (!relX && at >= 0.75f) m.style("text-align", "right");
    else if (!relX && at > 0.25f) m.style("text-align", "center");
    return;
  }
  const bool painted = s.fBackground || s.fBorder || s.fGradient || s.fShadow ||
                       std::ranges::any_of(m.fStyles, [](const auto &style) { return style.first == "background-color"; });
  if (!sized && painted && displayIs(m, "block")) m.style("display", "table");
  if (relX) return;
  if (at >= 0.75f) {
    m.style("margin-left", "auto");
  } else if (at > 0.25f) {
    m.style("margin-left", "auto");
    m.style("margin-right", "auto");
  }
}

// What every node's element is, from its State: shown or not, its sizes,
// margins and padding, its paint (background, border, corners, shadow), its
// opacity, its shift and scale, and where it is in what holds it.
inline void stateOf(Markup &m, const scene::State &s, const Holder &holder, const Engine &engine) {
  if (!s.fVisible)
    m.style("display", "none");
  const bool relX = s.fRelativeSizeAxes.has<scene::axis::x>(), relY = s.fRelativeSizeAxes.has<scene::axis::y>();
  sizeOf(m, "width", s.fWidth, relX, s.fAutoSizeAxes.has<scene::axis::x>());
  sizeOf(m, "height", s.fHeight, relY, s.fAutoSizeAxes.has<scene::axis::y>());
  if (s.fMinWidth > 0.0f) m.style("min-width", px(s.fMinWidth));
  if (s.fMaxWidth > 0.0f) m.style("max-width", px(s.fMaxWidth));
  if (s.fMinHeight > 0.0f) m.style("min-height", px(s.fMinHeight));
  if (s.fMaxHeight > 0.0f) m.style("max-height", px(s.fMaxHeight));
  const scene::Margin none{};
  if (!(s.fMargin == none)) m.style("margin", skiff::web::margin(s.fMargin));
  if (!(s.fPadding == none)) m.style("padding", skiff::web::margin(s.fPadding));
  m.style("box-sizing", "border-box");
  if (s.fCorners) {
    const scene::Corners &k = *s.fCorners;
    m.style("border-radius", px(k.topLeft) + " " + px(k.topRight) + " " + px(k.bottomRight) + " " + px(k.bottomLeft));
  } else if (s.fCornerRadius > 0.0f) {
    m.style("border-radius", px(s.fCornerRadius));
  }
  if (s.fBackground) m.style("background-color", rgba(*s.fBackground));
  if (s.fGradient) m.style("background-image", "linear-gradient(" + rgba(s.fGradient->top) + "," + rgba(s.fGradient->bottom) + ")");
  if (s.fBorder) m.style("border", px(s.fBorder->width) + " solid " + rgba(s.fBorder->colour));
  if (s.fShadow) m.style("box-shadow", "0 " + px(s.fShadow->offsetY) + " " + px(s.fShadow->offsetY * 2.0f) + " " + rgba(s.fShadow->colour));
  // Faded: as it is, where the engine has opacity; else, faded out whole,
  // not seen, and what is half faded shown whole.
  if (engine.transforms && s.fAlpha < 1.0f) m.style("opacity", number(s.fAlpha));
  else if (s.fAlpha <= 0.0f) m.style("visibility", "hidden");
  if (s.fMasking) m.style("overflow", "hidden");
  if (s.fDepth != 0.0f) m.style("z-index", std::to_string(static_cast<int>(std::lround(s.fDepth))));
  // Moved without a transform -- litehtml has none: its own shift (a drawer
  // slid away, a row swiped), and where it is placed, its origin on its
  // anchor -- by its size as skiff laid it out -- as margins; in a flow, by
  // left and top.
  const float dx = s.fX + s.fShiftX, dy = s.fY + s.fShiftY;
  bool absolute = false;
  const auto placed = [&] {
    absolute = true;
    m.style("position", "absolute");
    m.style("left", number(s.fAnchor.x * 100.0f) + "%");
    m.style("top", number(s.fAnchor.y * 100.0f) + "%");
    if (engine.transforms) return;  // its origin and shift: one transform, below
    const float ox = dx - s.fOrigin.x * s.fBounds.width(), oy = dy - s.fOrigin.y * s.fBounds.height();
    if (ox != 0.0f) m.style("margin-left", px(s.fMargin.fLeft + ox));
    if (oy != 0.0f) m.style("margin-top", px(s.fMargin.fTop + oy));
  };
  spl::visit(spl::overloaded{
                 [&](const in_flex &flow) {
                   // Along the flow: its own size, or an equal share of what is
                   // left where it grows. Across: where its alignment says.
                   const bool along = flow.row ? s.fGrowAxes.has<scene::axis::x>() || relX
                                               : s.fGrowAxes.has<scene::axis::y>() || relY;
                   m.style("flex", along ? "1 1 0%" : (s.fShrinkAxes.has<scene::axis::x>() || s.fShrinkAxes.has<scene::axis::y>()) ? "0 1 auto" : "0 0 auto");
                   if (along) m.style(flow.row ? "min-width" : "min-height", "0");
                   const bool across = flow.row ? s.fGrowAxes.has<scene::axis::y>() : s.fGrowAxes.has<scene::axis::x>();
                   if (s.fAlignSelf) m.style("align-self", std::string(skiff::web::alignment(*s.fAlignSelf)));
                   else if (across) m.style("align-self", "stretch");
                   if (s.fOutOfFlow) placed();
                 },
                 [&](const in_column &column) {
                   if (s.fOutOfFlow) {
                     placed();
                     return;
                   }
                   if (!column.first && column.gap > 0.0f) m.style("margin-top", px(s.fMargin.fTop + column.gap));
                   // A part growing down a column of one: all of its height.
                   if (s.fGrowAxes.has<scene::axis::y>() && !relY) m.style("height", "100%");
                   acrossInBlock(m, s, relX || s.fGrowAxes.has<scene::axis::x>(), (s.fAlignSelf ? *s.fAlignSelf : column.cross).at);
                 },
                 [&](const in_cell &cell) {
                   if (cell.first) {
                     // In the box's flow: across, where its anchor is; down, its
                     // own height or all of the box's.
                     acrossInBlock(m, s, relX, s.fAnchor.x);
                     return;
                   }
                   placed();
                 },
                 [](const in_scroll &) {}},
             holder);
  if (engine.transforms) {
    // Moved by a transform: its origin on its anchor where placed, then its
    // shift and its scale.
    std::string transform;
    if (absolute && (s.fOrigin.x != 0.0f || s.fOrigin.y != 0.0f))
      transform += "translate(" + number(-s.fOrigin.x * 100.0f) + "%," + number(-s.fOrigin.y * 100.0f) + "%) ";
    if (dx != 0.0f || dy != 0.0f) transform += "translate(" + px(dx) + "," + px(dy) + ") ";
    if (s.fScale != 1.0f) transform += "scale(" + number(s.fScale) + ") ";
    if (!transform.empty()) {
      transform.pop_back();
      m.style("transform", transform);
    }
    return;
  }
  if (!absolute && (dx != 0.0f || dy != 0.0f)) {
    if (!std::ranges::contains(m.fStyles, std::pair<std::string, std::string>("position", "relative"))) m.style("position", "relative");
    if (dx != 0.0f) m.style("left", px(dx));
    if (dy != 0.0f) m.style("top", px(dy));
  }
}

// Whether a flow needs a flex box: a row; a column that wraps, is justified
// otherwise than from its start, or holds more than one part with one taking
// a share of what is left. A column of one part that fills it is a block
// whose part is all of its height.
[[nodiscard]] inline bool needsFlex(const scene::FlowOptions &o, const std::vector<const scene::State *> &parts) {
  if (isRow(o.direction) || o.wrap || !justifiesFromStart(o.justify)) return true;
  const auto grows = [](const scene::State *s) {
    return s->fVisible && !s->fOutOfFlow && (s->fGrowAxes.has<scene::axis::y>() || s->fRelativeSizeAxes.has<scene::axis::y>());
  };
  const auto shown = std::ranges::count_if(parts, [](const scene::State *s) { return s->fVisible && !s->fOutOfFlow; });
  return shown > 1 && std::ranges::any_of(parts, grows);
}

// What kind of node it is, as an element, and how it holds its parts: what
// its look says -- a flow as a flex box where it needs one (needsFlex), else
// as blocks.
inline void flexOf(Markup &m, const scene::FlowOptions &o) {
  m.style("display", "flex");
  m.style("position", "relative");
  m.style("flex-direction", isRow(o.direction) ? "row" : "column");
  m.style("flex-wrap", o.wrap ? "wrap" : "nowrap");
  if (o.spacingX > 0.0f || o.spacingY > 0.0f) m.style("gap", px(o.spacingY) + " " + px(o.spacingX));
  m.style("align-items", std::string(skiff::web::alignment(o.crossAlign)));
  m.style("justify-content", spl::visit(spl::overloaded{[](scene::justify::start) { return "flex-start"; },
                                                        [](scene::justify::middle) { return "center"; },
                                                        [](scene::justify::end) { return "flex-end"; },
                                                        [](scene::justify::space_between) { return "space-between"; },
                                                        [](scene::justify::space_around) { return "space-around"; }},
                                        o.justify));
}
[[nodiscard]] inline Holder kindOf(Markup &m, const scene::Look &look, const std::vector<const scene::State *> &parts, const Engine &engine) {
  return spl::visit(spl::overloaded{[&](const scene::look::box &box) -> Holder {
                                      m.style("display", "block");
                                      m.style("position", "relative");
                                      if (box.colour) m.style("background-color", rgba(*box.colour));
                                      return in_cell{};
                                    },
                                    [&](const scene::look::flow &flow) -> Holder {
                                      const scene::FlowOptions &o = flow.options;
                                      if (engine.deepFlex || needsFlex(o, parts)) {
                                        flexOf(m, o);
                                        return in_flex{isRow(o.direction)};
                                      }
                                      m.style("display", "block");
                                      m.style("position", "relative");
                                      return in_column{o.spacingY, true, o.crossAlign};
                                    },
                                    [&](const scene::look::words &words) -> Holder {
                                      m.style("display", "block");
                                      m.style("white-space", "pre-wrap");
                                      m.style("font-size", px(words.size));
                                      m.style("line-height", number(1.25f));
                                      m.style("color", rgba(words.colour));
                                      if (words.bold) m.style("font-weight", "bold");
                                      if (words.elided) {
                                        m.style("white-space", "nowrap");
                                        m.style("overflow", "hidden");
                                        m.style("text-overflow", "ellipsis");
                                      } else if (words.wrapped == std::optional<bool>(false)) {
                                        m.style("white-space", "pre");
                                      }
                                      m.text(words.text);
                                      return in_cell{};
                                    },
                                    [&](const scene::look::scrolled &) -> Holder {
                                      m.style("display", "block");
                                      m.style("overflow-y", "auto");
                                      m.style("position", "relative");
                                      return in_scroll{};
                                    },
                                    [&](const scene::look::drawn &) -> Holder {
                                      m.style("display", "block");
                                      m.style("position", "relative");
                                      m.attr("data-skiff-draw", "1");
                                      return in_cell{};
                                    }},
                    look);
}

// How a part after the first is held: in a box, over the first; in a
// column, below the one before; elsewhere, as the first.
[[nodiscard]] inline Holder after(const Holder &first) {
  return spl::visit(spl::overloaded{[](const in_cell &) -> Holder { return in_cell{false}; },
                                    [](const in_column &column) -> Holder { return in_column{column.gap, false, column.cross}; },
                                    [](const auto &same) -> Holder { return same; }},
                    first);
}

// What a node that paints itself paints (drawSelf through the painter), as
// elements of its own element: plates, rounded or round, their edges, lines
// across or down, and text, each placed where it is painted. A painter
// still: drawn into a canvas whose drawing goes nowhere, so that whatever it
// is asked that is not shadowed here is done there and lost -- and said lost
// (a path, an arc, a picture), for the node to be painted by skiff instead.
class HtmlPainter : public skiff::paint::Painter {
public:
  HtmlPainter(skia::SkCanvas *discarded, skia::SkFont &font, Markup &into, const skia::SkRect &box, bool svg)
      : Painter(discarded, font), fFont(font), fInto(&into), fLeft(box.fLeft), fTop(box.fTop), fBox(box), fSvg(svg) {}

  [[nodiscard]] bool lost() const noexcept { return fLost; }
  [[nodiscard]] std::size_t parts() const noexcept { return fPart; }

  void save() const { fSaved.push_back({fX, fY}); }
  void restore() const {
    if (fSaved.empty()) return;
    std::tie(fX, fY) = fSaved.back();
    fSaved.pop_back();
  }
  void translate(float dx, float dy) const {
    fX += dx;
    fY += dy;
  }
  void rotate(float) const { fLost = true; }
  // Clips: what is drawn is drawn whole (the element's own masking cuts it).
  void clipRect(const skia::SkRect &) const {}
  void clipRounded(const skia::SkRect &, float) const {}
  void clipOval(const skia::SkRect &) const {}
  void clip(const skia::SkRRect &) const {}

  void fillRect(const skia::SkRect &rect, skia::SkColor color, float alpha = 1.0f) const { this->plate(rect, 0.0f, color, alpha); }
  void fillRounded(const skia::SkRect &rect, float radius, skia::SkColor color, float alpha = 1.0f) const {
    this->plate(rect, radius, color, alpha);
  }
  void fill(const skia::SkRRect &shape, skia::SkColor color, float alpha = 1.0f) const {
    this->plate(shape.rect(), shape.radii(skia::SkRRect::kUpperLeft_Corner).fX, color, alpha);
  }
  void circle(float cx, float cy, float r, skia::SkColor color, float alpha = 1.0f) const {
    this->plate(skia::SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), r, color, alpha);
  }
  void strokeRounded(const skia::SkRect &rect, float radius, skia::SkColor color, float width, float alpha = 1.0f) const {
    this->edge(rect, radius, width, color, alpha);
  }
  void stroke(const skia::SkRRect &shape, float width, skia::SkColor color, float alpha = 1.0f) const {
    this->edge(shape.rect(), shape.radii(skia::SkRRect::kUpperLeft_Corner).fX, width, color, alpha);
  }
  void strokeCircle(float cx, float cy, float r, float width, skia::SkColor color, float alpha) const {
    this->edge(skia::SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), r, width, color, alpha);
  }
  // A line across or down: a plate as thick as the line. Slanted: SVG's
  // line, where the engine has SVG; else lost.
  void line(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha, bool round = true) const {
    // Across or down: a plate where SVG is not to be had, or the caps are
    // cut (a round cap's half circles past the ends lost there).
    if (y1 == y2 && (!round || !fSvg))
      this->plate(skia::SkRect::MakeLTRB(std::min(x1, x2), y1 - width * 0.5f, std::max(x1, x2), y1 + width * 0.5f), 0.0f, color, alpha);
    else if (x1 == x2 && (!round || !fSvg))
      this->plate(skia::SkRect::MakeLTRB(x1 - width * 0.5f, std::min(y1, y2), x1 + width * 0.5f, std::max(y1, y2)), 0.0f, color, alpha);
    else
      this->stroked("M" + number(x1) + " " + number(y1) + "L" + number(x2) + " " + number(y2), width, color, alpha, round ? "round" : "butt");
  }
  void line(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha, Cap cap) const {
    this->line(x1, y1, x2, y2, width, color, alpha, cap != Cap::butt);
  }
  void verticalGradient(const skia::SkRect &rect, skia::SkColor top, skia::SkColor, float alpha = 1.0f) const {
    this->plate(rect, 0.0f, top, alpha);
  }
  // Arcs and paths: SVG's, where the engine has SVG; else lost, the node
  // painted by skiff. An SkPath made elsewhere has no path data here: lost.
  void arc(const skia::SkRect &oval, float startDegrees, float sweepDegrees, float width, skia::SkColor color, float alpha) const {
    this->stroked(arcData(oval, startDegrees, sweepDegrees), width, color, alpha, "round");
  }
  void fillArc(const skia::SkRect &oval, float startDegrees, float sweepDegrees, skia::SkColor color, float alpha) const {
    this->filled(arcData(oval, startDegrees, sweepDegrees) + "Z", color, alpha);
  }
  void fill(const Path &path, skia::SkColor color, float alpha) const { this->filled(path.svg(), color, alpha); }
  void fill(const skia::SkPath &, skia::SkColor, float = 1.0f) const { fLost = true; }
  void stroke(const Path &path, float width, skia::SkColor color, float alpha, Cap cap, bool) const {
    this->stroked(path.svg(), width, color, alpha, cap == Cap::butt ? "butt" : cap == Cap::square ? "square" : "round");
  }
  void stroke(const Path &path, float width, skia::SkColor color, float alpha, bool round = true) const {
    this->stroked(path.svg(), width, color, alpha, round ? "round" : "butt");
  }
  void imageFilled(const skia::SkImage *image, const skia::SkRect &, float = 1.0f) const { fLost = fLost || image != nullptr; }
  void image(const skia::SkImage *image, const skia::SkRect &, float = 1.0f, Sampling = Sampling::linear) const { fLost = fLost || image != nullptr; }
  void imagePart(const skia::SkImage *image, const skia::SkRect &, const skia::SkRect &, float = 1.0f, Sampling = Sampling::linear) const {
    fLost = fLost || image != nullptr;
  }
  template <class Shape>
  void imageThrough(const Shape &, const skia::SkImage *image, const skia::SkRect &, float, Sampling = Sampling::linear) const {
    fLost = fLost || image != nullptr;
  }

  // Text, from its baseline: its line put where the face's ascent says.
  void text(const std::string &str, float x, float y, float size, skia::SkColor color, float alpha = 1.0f, bool bold = false) const {
    this->words(str, x, y, size, color, alpha, bold, std::nullopt);
  }
  void text(const std::string &str, float x, float y, float size, skia::SkColor color, float alpha, skiff::paint::TextFace face) const {
    this->words(str, x, y, size, color, alpha, face.bold, std::nullopt);
  }
  void textClipped(const std::string &str, float x, float y, float maxW, float size, skia::SkColor color, float alpha = 1.0f,
                   bool bold = false) const {
    this->words(str, x, y, size, color, alpha, bold, maxW);
  }
  void textCentered(const std::string &str, float cx, float y, float size, skia::SkColor color, float alpha = 1.0f, bool bold = false) const {
    const float w = this->measure(str, size, bold);
    this->words(str, cx - w * 0.5f, y, size, color, alpha, bold, std::nullopt);
  }
  void textCenteredClipped(const std::string &str, float cx, float y, float maxW, float size, skia::SkColor color, float alpha = 1.0f,
                           bool bold = false) const {
    const float w = std::min(maxW, this->measure(str, size, bold));
    this->words(str, cx - w * 0.5f, y, size, color, alpha, bold, maxW);
  }
  void textElided(const std::string &str, float x, float y, float maxW, float size, skia::SkColor color, float alpha = 1.0f,
                  bool bold = false) const {
    this->words(this->elide(str, maxW, size, bold), x, y, size, color, alpha, bold, maxW);
  }
  void textElidedIn(const skia::SkRect &box, const std::string &str, float size, skia::SkColor color, float alpha = 1.0f, bool bold = false,
                    float inset = 0.0f) const {
    this->textElided(str, box.fLeft + inset, this->middleBaseline(box, size), box.width() - inset * 2.0f, size, color, alpha, bold);
  }
  void textIn(const skia::SkRect &box, const std::string &str, float size, skia::SkColor color, float alpha = 1.0f, bool bold = false,
              float inset = 0.0f) const {
    this->textClipped(str, box.fLeft + inset, this->middleBaseline(box, size), box.width() - inset * 2.0f, size, color, alpha, bold);
  }
  void textCentredIn(const skia::SkRect &box, const std::string &str, float size, skia::SkColor color, float alpha = 1.0f,
                     bool bold = false, float inset = 0.0f) const {
    this->textCenteredClipped(str, box.centerX(), this->middleBaseline(box, size), box.width() - inset * 2.0f, size, color, alpha, bold);
  }
  void textShadowed(const std::string &str, float cx, float y, float size, skia::SkColor color, float alpha = 1.0f) const {
    this->textCentered(str, cx, y, size, color, alpha);
  }

private:
  // Where a box of the painting is in the element: moved as it was
  // translated, from the node's own box.
  [[nodiscard]] skia::SkRect placed(const skia::SkRect &r) const { return r.makeOffset(fX - fLeft, fY - fTop); }
  [[nodiscard]] static std::string colourOf(skia::SkColor c, float alpha) {
    const float a = static_cast<float>((c >> 24) & 0xffu) / 255.0f * std::clamp(alpha, 0.0f, 1.0f);
    const auto byte = static_cast<scene::Colour>(std::lround(a * 255.0f));
    return rgba((byte << 24) | (c & 0x00ffffffu));
  }
  void element(auto &&write) const {
    fInto->inner("div", skiff::web::Part{static_cast<std::uint32_t>(++fPart)}, [&](Markup &e) {
      e.style("position", "absolute");
      write(e);
    });
  }
  void at(Markup &e, const skia::SkRect &r) const {
    e.style("left", px(r.fLeft));
    e.style("top", px(r.fTop));
    e.style("width", px(r.width()));
    e.style("height", px(r.height()));
  }
  void plate(const skia::SkRect &rect, float radius, skia::SkColor color, float alpha) const {
    if (((color >> 24) & 0xffu) == 0 || alpha <= 0.0f || rect.isEmpty()) return;
    this->element([&](Markup &e) {
      this->at(e, this->placed(rect));
      e.style("background-color", colourOf(color, alpha));
      if (radius > 0.0f) e.style("border-radius", px(radius));
    });
  }
  void edge(const skia::SkRect &rect, float radius, float width, skia::SkColor color, float alpha) const {
    if (((color >> 24) & 0xffu) == 0 || alpha <= 0.0f || width <= 0.0f) return;
    // The stroke centred on the box's edge, as Skia strokes it.
    const skia::SkRect outer = this->placed(rect).makeOutset(width * 0.5f, width * 0.5f);
    this->element([&](Markup &e) {
      this->at(e, outer);
      e.style("box-sizing", "border-box");
      e.style("border", px(width) + " solid " + colourOf(color, alpha));
      if (radius > 0.0f) e.style("border-radius", px(radius + width * 0.5f));
    });
  }
  // An arc of the oval in its box, from a start angle through a sweep
  // (degrees, clockwise from three o'clock), as SVG's path data.
  [[nodiscard]] static std::string arcData(const skia::SkRect &oval, float startDegrees, float sweepDegrees) {
    const float rx = oval.width() * 0.5f, ry = oval.height() * 0.5f, cx = oval.centerX(), cy = oval.centerY();
    const auto at = [&](float degrees) {
      const float r = degrees * std::numbers::pi_v<float> / 180.0f;
      return std::pair{cx + rx * std::cos(r), cy + ry * std::sin(r)};
    };
    const auto [sx, sy] = at(startDegrees);
    // A whole turn: two halves, SVG drawing nothing of an arc that ends
    // where it starts.
    if (std::abs(sweepDegrees) >= 359.99f) {
      const auto [mx, my] = at(startDegrees + 180.0f);
      const std::string half = "A" + number(rx) + " " + number(ry) + " 0 1 1 ";
      return "M" + number(sx) + " " + number(sy) + half + number(mx) + " " + number(my) + half + number(sx) + " " + number(sy);
    }
    const auto [ex, ey] = at(startDegrees + sweepDegrees);
    return "M" + number(sx) + " " + number(sy) + "A" + number(rx) + " " + number(ry) + " 0 " + (std::abs(sweepDegrees) > 180.0f ? "1" : "0") +
           " " + (sweepDegrees > 0.0f ? "1" : "0") + " " + number(ex) + " " + number(ey);
  }
  // A shape in SVG, over the node's box in the scene's own units (its
  // viewBox), moved as the painting was translated.
  void shape(auto &&write) const {
    if (!fSvg) {
      fLost = true;
      return;
    }
    fInto->inner("svg", skiff::web::Part{static_cast<std::uint32_t>(++fPart)}, [&](Markup &svg) {
      svg.style("position", "absolute");
      svg.style("left", "0");
      svg.style("top", "0");
      svg.style("overflow", "visible");
      svg.attr("width", number(fBox.width()));
      svg.attr("height", number(fBox.height()));
      svg.attr("viewBox", number(fBox.fLeft - fX) + " " + number(fBox.fTop - fY) + " " + number(fBox.width()) + " " + number(fBox.height()));
      svg.attr("xmlns", "http://www.w3.org/2000/svg");
      svg.inner("path", skiff::web::Part{static_cast<std::uint32_t>(++fPart)}, write);
    });
  }
  void filled(const std::string &data, skia::SkColor color, float alpha) const {
    if (((color >> 24) & 0xffu) == 0 || alpha <= 0.0f || data.empty()) return;
    this->shape([&](Markup &path) {
      path.attr("d", data);
      path.attr("fill", colourOf(color, alpha));
    });
  }
  void stroked(const std::string &data, float width, skia::SkColor color, float alpha, std::string_view cap) const {
    if (((color >> 24) & 0xffu) == 0 || alpha <= 0.0f || data.empty()) return;
    this->shape([&](Markup &path) {
      path.attr("d", data);
      path.attr("fill", "none");
      path.attr("stroke", colourOf(color, alpha));
      path.attr("stroke-width", number(width));
      path.attr("stroke-linecap", cap);
      path.attr("stroke-linejoin", "round");
    });
  }
  void words(const std::string &str, float x, float baseline, float size, skia::SkColor color, float alpha, bool bold,
             std::optional<float> width) const {
    if (str.empty() || ((color >> 24) & 0xffu) == 0 || alpha <= 0.0f) return;
    fFont.setSize(size);
    skia::SkFontMetrics metrics;
    (void)fFont.getMetrics(&metrics);
    this->element([&](Markup &e) {
      e.style("left", px(x + fX - fLeft));
      e.style("top", px(baseline + metrics.fAscent + fY - fTop));
      if (width) {
        e.style("width", px(*width));
        e.style("overflow", "hidden");
      }
      e.style("white-space", "pre");
      e.style("font-size", px(size));
      e.style("color", colourOf(color, alpha));
      if (bold) e.style("font-weight", "bold");
      e.text(str);
    });
  }

  skia::SkFont &fFont;
  Markup *fInto;
  float fLeft, fTop;
  skia::SkRect fBox;
  bool fSvg;
  mutable float fX = 0.0f, fY = 0.0f;
  mutable std::vector<std::pair<float, float>> fSaved;
  mutable std::size_t fPart = 0;
  mutable bool fLost = false;
};

// A child's State: its own, or as the walks' table says, where it is erased.
template <class N> [[nodiscard]] const scene::State &stateOfPart(N &node) { return node.fState; }
[[nodiscard]] inline const scene::State &stateOfPart(scene::AnyNodeRef &ref) { return scene::ErasedNode(ref).fState; }

} // namespace detail

// A scene's tree, mirrored: one element per node, kept by the node's id,
// written again only where what it is changed, made where new, dropped where
// gone. Whatever drives the program refreshes its bindings, then sync()s.
class Mirror {
public:
  explicit Mirror(Engine engine = kLitehtml, dom::Handle into = dom::root()) : fEngine(engine), fInto(into) {}

  template <class Root> void sync(Root &root) {
    ++fPass;
    const dom::Handle top = this->visit(root, detail::in_cell{});
    this->order(fInto, std::vector<dom::Handle>{top});
    std::erase_if(fShown, [&](auto &kept) {
      if (kept.second.seen == fPass) return false;
      fNodeOf.erase(kept.second.handle);
      for (const auto &[part, h] : kept.second.inner) dom::destroy(h);
      dom::destroy(kept.second.handle);
      return true;
    });
  }

  // A press on an element: the node it is, pressed through its path from the
  // root, as the window presses the node under the pointer. Whether one was.
  template <class Root, class Model> bool press(Root &root, Model &model, dom::Handle element) {
    const auto found = fNodeOf.find(element);
    if (found == fNodeOf.end()) return false;
    scene::Path way;
    if constexpr (scene::kErasedWalks) {
      auto seen = scene::AnyNodeRef::of(root);
      if (!scene::walk::findPath(seen, found->second, way)) return false;
    } else {
      if (!scene::walk::findPath(root, found->second, way)) return false;
    }
    return bind::press(root, model, way);
  }

  // A press on an element, as the window gives one: clicked along the path
  // of its node in the scene -- onClick there, and on each node above it
  // until one takes it; what it answers goes as a window's press's does.
  template <class S> bool click(S &scene, dom::Handle element) {
    const auto found = fNodeOf.find(element);
    if (found == fNodeOf.end()) return false;
    scene::Path way;
    if constexpr (scene::kErasedWalks) {
      auto seen = scene::AnyNodeRef::of(scene.root());
      if (!scene::walk::findPath(seen, found->second, way)) return false;
    } else {
      if (!scene::walk::findPath(scene.root(), found->second, way)) return false;
    }
    scene::hostWork().laidOutElsewhere = true;
    return scene.clickAlong(way);
  }

  // The nodes skiff paints (look::drawn), each where the engine put its
  // element -- `at`, by its handle, in the canvas's space: its whole subtree
  // painted by skiff, moved from where skiff laid it out to there. One walk
  // of the tree, typed where it is; how many were painted.
  template <class Root, class Places>
  std::size_t paintDrawn(Root &root, scene::Painting &painting, skia::SkCanvas *canvas, const Places &at) {
    std::size_t painted = 0;
    this->paintIn(root, painting, canvas, at, painted);
    return painted;
  }

  // The element a node is, where it is shown.
  [[nodiscard]] dom::Handle elementOf(const scene::State &state) const {
    const auto found = fShown.find(state.fId);
    return found == fShown.end() ? dom::kNone : found->second.handle;
  }
  [[nodiscard]] std::size_t size() const noexcept { return fShown.size(); }
  // How many nodes painting themselves were written as elements, all told.
  [[nodiscard]] std::size_t recorded() const noexcept { return fRecorded; }

private:
  struct Shown {
    dom::Handle handle = dom::kNone;
    Markup markup;
    std::vector<dom::Handle> children;
    std::vector<std::pair<skiff::web::Part, dom::Handle>> inner;
    std::uint64_t seen = 0;
  };

  // A node: as its type says, or -- erased, outside a release build -- as the
  // walks' table says; either way its state, its look, whether it takes a
  // press, and its children in their order.
  template <class N> dom::Handle visit(N &node, const detail::Holder &holder) {
    return this->write(node.fState, node.look(), node.acceptsInput(), holder, [&](auto &&each) { scene::eachChild(node, each); },
                       [&](Markup &into) { return this->record(node, into); });
  }
  dom::Handle visit(scene::AnyNodeRef &ref, const detail::Holder &holder) {
    scene::ErasedNode node(ref);
    return this->write(node.fState, node.look(), node.acceptsInput(), holder, [&](auto &&each) { node.forEachChild(each); },
                       [](Markup &) { return false; });
  }
  // A node that paints itself, recorded as elements where all it paints has
  // one (HtmlPainter); whether it was. Else -- a path, a picture; an erased
  // node -- skiff paints it.
  template <class N> bool record(N &node, Markup &into) {
    if constexpr (requires(N &n, detail::HtmlPainter &p) { n.drawSelf(p, 1.0f); }) {
      // The program's face, for where text sits on its line; a default one
      // where none was loaded (a test).
      const skia::SkFont *font = skiff::paint::defaultFont();
      skia::SkFont face = font != nullptr ? *font : skia::SkFont();
      skia::SkPictureRecorder discarded;
      skia::SkCanvas *canvas = discarded.beginRecording(node.fState.fBounds);
      Markup drawn;
      detail::HtmlPainter p(canvas, face, drawn, node.fState.fBounds, fEngine.svg);
      node.drawSelf(p, 1.0f);
      if (p.lost()) return false;
      into.fInner.append_range(std::move(drawn.fInner));
      ++fRecorded;
      return true;
    } else {
      return false;
    }
  }
  template <class Children, class Records>
  dom::Handle write(const scene::State &state, const scene::Look &look, bool pressable, const detail::Holder &holder,
                    Children &&children, Records &&records) {
    // A node that paints itself: as elements, where what it paints has them
    // (recorded below); else a leaf here, of the size skiff laid it out at
    // where it says none -- what is in it, skiff's to paint (paintDrawn).
    const bool paints = spl::visit(spl::overloaded{[](const scene::look::drawn &) { return true; }, [](const auto &) { return false; }}, look);
    Markup recorded;
    const bool drawn = paints && !records(recorded);
    // Its parts' States first: whether it needs a flex box depends on them.
    std::vector<const scene::State *> parts;
    if (!drawn)
      children([&](auto &child) { parts.push_back(&detail::stateOfPart(child)); });
    Markup now;
    const detail::Holder theirs = detail::kindOf(now, look, parts, fEngine);
    detail::stateOf(now, state, holder, fEngine);
    if (paints) {
      const auto unsized = [&](std::string_view property) {
        return std::ranges::none_of(now.fStyles, [&](const auto &style) { return style.first == property; });
      };
      if (unsized("width") && state.fBounds.width() > 0.0f) now.style("width", skiff::web::px(state.fBounds.width()));
      if (unsized("height") && state.fBounds.height() > 0.0f) now.style("height", skiff::web::px(state.fBounds.height()));
    }
    if (paints && !drawn) {
      // Painted as elements: not skiff's to paint.
      std::erase_if(now.fAttrs, [](const auto &one) { return one.first == "data-skiff-draw"; });
      now.fInner = std::move(recorded.fInner);
    }
    if (pressable) {
      now.listen("click");
      now.style("cursor", "pointer");
    }
    auto [at, made] = fShown.try_emplace(state.fId);
    Shown &shown = at->second;
    if (made) {
      shown.handle = dom::create("div");
      fNodeOf.emplace(shown.handle, state.fId);
    }
    // Its handle, on it: what an engine that tells only which element was
    // pressed (litehtml's) is read back by.
    now.attr("data-skiff-h", std::to_string(shown.handle));
    shown.seen = fPass;
    skiff::web::detail::writeOwn(shown.handle, now, shown.markup);
    // The elements it makes in itself (what it paints, as elements): kept by
    // their part, written where they changed, made where new, dropped where
    // gone -- as skiff.web writes a node's.
    std::vector<std::pair<skiff::web::Part, dom::Handle>> kept;
    skiff::web::detail::writeInner(shown.handle, now.fInner, shown.markup.fInner, shown.inner, kept);
    for (const auto &[part, h] : shown.inner)
      if (!std::ranges::contains(kept, h, &std::pair<skiff::web::Part, dom::Handle>::second)) dom::destroy(h);
    shown.inner = std::move(kept);
    shown.markup = std::move(now);
    const dom::Handle handle = shown.handle;
    // Children: each visited, then put in their order where it moved. The
    // map may grow as they are visited: nothing of it held across.
    std::vector<dom::Handle> handles;
    if (!drawn)
      children([&](auto &child) {
        const detail::Holder mine = handles.empty() ? theirs : detail::after(theirs);
        handles.push_back(this->visit(child, mine));
      });
    this->order(handle, std::move(handles));
    return handle;
  }

  template <class Places>
  bool paintAt(const scene::State &state, skia::SkCanvas *canvas, const Places &at, auto &&draw) {
    const auto shown = fShown.find(state.fId);
    if (shown == fShown.end()) return false;
    const auto placed = at.find(shown->second.handle);
    if (placed == at.end()) return false;
    const int saved = canvas->save();
    canvas->translate(placed->second.fLeft - state.fBounds.fLeft, placed->second.fTop - state.fBounds.fTop);
    draw();
    canvas->restoreToCount(saved);
    return true;
  }
  template <class N, class Places>
  void paintIn(N &node, scene::Painting &painting, skia::SkCanvas *canvas, const Places &at, std::size_t &painted) {
    if (this->paintAt(node.fState, canvas, at, [&] { scene::draw(node, painting, canvas, 1.0f); })) {
      ++painted;
      return;
    }
    scene::eachChild(node, [&](auto &child) { this->paintIn(child, painting, canvas, at, painted); });
  }
  template <class Places>
  void paintIn(scene::AnyNodeRef &ref, scene::Painting &painting, skia::SkCanvas *canvas, const Places &at, std::size_t &painted) {
    scene::ErasedNode node(ref);
    if (this->paintAt(node.fState, canvas, at, [&] { scene::draw(ref, painting, canvas, 1.0f); })) {
      ++painted;
      return;
    }
    node.forEachChild([&](scene::AnyNodeRef &child) { this->paintIn(child, painting, canvas, at, painted); });
  }

  void order(dom::Handle parent, std::vector<dom::Handle> children) {
    std::vector<dom::Handle> &was = parent == fInto ? fTop : fShown.at(fNodeOf.at(parent)).children;
    if (was == children) return;
    for (dom::Handle child : children) dom::append(parent, child);
    was = std::move(children);
  }

  Engine fEngine;
  dom::Handle fInto;
  std::uint64_t fPass = 0;
  std::unordered_map<scene::NodeId, Shown> fShown;
  std::unordered_map<dom::Handle, scene::NodeId> fNodeOf;
  std::vector<dom::Handle> fTop;
  std::size_t fRecorded = 0;
};

} // namespace skiff::mirror
