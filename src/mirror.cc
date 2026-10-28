// skiff.mirror: a scene's tree as a document's elements, laid out and painted
// by an engine rather than by skiff -- the tree a program already has (its
// nodes, its bindings, its presses) shown through skiff.web's dom, whichever
// backend that is: a browser's document, an engine embedded and called
// directly, or the in-memory one a test reads back.
//
// What lays out what is shown is the engine: each node says what it is as
// CSS, from what its State keeps and what kind of node it is --
//   a stack or a flow        a flex box along its way, its gaps and
//                            justification;
//   a text                   its words, in its size, weight and colour;
//   a view that scrolls      a box that scrolls (overflow: auto);
//   a node painting itself   what it paints, as elements: plates, borders,
//                            text, and SVG for paths and arcs;
//   any other node           a grid of one cell: its parts over each other,
//                            each where its anchor puts it, as the scene
//                            places the children of a node that is not a flow.
// Sizes are the State's: a share of the parent's where relative, the
// content's where sized by it, else what it says; shifts and fades are
// transforms and opacity. A press on an element is the node's: found by its
// path, and pressed as the window would press it. What cannot be written as
// the engine's own is said (takeNotNative).
export module skiff.mirror;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.web;

export namespace skiff::mirror {


namespace dom = skiff::web::dom;
namespace css = skiff::web::css;
using skiff::web::Markup;

namespace detail {

using skiff::web::px;
using skiff::web::number;
using skiff::web::rgba;
using skiff::web::cssMargin;
using skiff::web::cssAlignment;

// Whether a flow runs across: its direction, visited.
[[nodiscard]] inline bool isRow(const scene::Direction &d) {
  return spl::visit(spl::overloaded{[](scene::direction::vertical) { return false; }, [](scene::direction::horizontal) { return true; }}, d);
}
// Where in its cell an anchor puts a part.
[[nodiscard]] inline std::string_view placeAlong(float at) {
  return at <= 0.25f ? "start" : at >= 0.75f ? "end" : "center";
}

// What holds a node: how a node lays its children out decides how each
// child is placed in it -- in a flow (a flex box), in a box's one cell (a
// grid: its parts over each other, each where its anchor puts it, the box
// as big as its biggest), or in a view that scrolls.
struct in_flex {
  bool row = false;
};
struct in_cell {};
struct in_scroll {};
using Holder = spl::variant<in_flex, in_cell, in_scroll>;
inline constexpr skiff::web::Part kField{0xfffffff0u}, kPlaceholder{0xfffffff1u};

// The size along one axis, as the State says it.
template <class Property>
inline void sizeOf(Markup &m, float size, bool relative, bool automatic, float margin = 0.0f) {
  if (relative) {
    if (margin == 0.0f) m.style(Property{css::percent{size * 100.0f}});
    else m.style(Property{css::calculated{css::percent{size * 100.0f}, css::pixels{size * margin}}});
  } else if (!automatic && size > 0.0f) {
    m.style(Property{css::pixels{size}});
  }
}

// What every node's element is, from its State: shown or not, its sizes,
// margins and padding, its paint (background, border, corners, shadow), its
// opacity, its shift and scale, and where it is in what holds it.
inline void stateOf(Markup &m, const scene::State &s, const Holder &holder) {
  if (!s.fVisible)
    m.style(css::display{css::none{}});
  const bool relX = s.fRelativeSizeAxes.has<scene::axis::x>(), relY = s.fRelativeSizeAxes.has<scene::axis::y>();
  sizeOf<css::width>(m, s.fWidth, relX, s.fAutoSizeAxes.has<scene::axis::x>(), s.fMargin.totalX());
  sizeOf<css::height>(m, s.fHeight, relY, s.fAutoSizeAxes.has<scene::axis::y>(), s.fMargin.totalY());
  // A grid or flex item's automatic minimum is its content's size. Scene
  // containers may be smaller than their contents (notably a timeline),
  // so that minimum must not push the composer outside the window.
  m.style(css::min_width{css::pixels{s.fMinWidth}});
  m.style(css::min_height{css::pixels{s.fMinHeight}});
  if (s.fMaxWidth > 0.0f) m.style(css::max_width{css::pixels{s.fMaxWidth}});
  if (s.fMaxHeight > 0.0f) m.style(css::max_height{css::pixels{s.fMaxHeight}});
  const scene::Margin none{};
  if (!(s.fMargin == none)) m.style(css::margin{cssMargin(s.fMargin)});
  if (!(s.fPadding == none)) m.style(css::padding{cssMargin(s.fPadding)});
  m.style(css::box_sizing{css::border_box{}});
  if (s.fCorners) {
    const scene::Corners &k = *s.fCorners;
    m.style(css::border_radius{css::sides{css::pixels{k.topLeft}, css::pixels{k.topRight}, css::pixels{k.bottomRight}, css::pixels{k.bottomLeft}}});
  } else if (s.fCornerRadius > 0.0f) {
    m.style(css::border_radius{css::sides{css::pixels{s.fCornerRadius}}});
  }
  const auto background = s.selected() && s.fSelectedBackground ? s.fSelectedBackground : s.fBackground;
  if (background) m.style(css::background_color{css::argb{*background}});
  // What the engine does itself, under the pointer and with the focus: a
  // background, a part shown -- variables on the element, and attributes
  // skiff.web's sheet (pageCss) selects on with :hover and :focus-within.
  if (s.fHoverBackground) {
    m.style(css::hover_background{css::argb{*s.fHoverBackground}});
    m.attr("data-skiff-hover", "");
  }
  if (s.fFocusBackground) {
    m.style(css::focus_background{css::argb{*s.fFocusBackground}});
    m.attr("data-skiff-focus", "");
  }
  if (s.fRevealOnHover) m.attr("data-skiff-reveal", "");
  // A bubble's tail: the sheet's triangle, at its side, in its colour.
  if (s.fTail && s.fTail->shown) {
    m.attr("data-skiff-tail", spl::visit(spl::overloaded{[](scene::tail_side::left) { return "left"; }, [](scene::tail_side::right) { return "right"; }},
                                         s.fTail->side));
    m.style(css::tail_width{css::pixels{s.fTail->width}});
    m.style(css::tail_height{css::pixels{s.fTail->height}});
    if (background) m.style(css::tail_background{css::argb{*background}});
  }
  // The pointer's shape over it.
  spl::visit(spl::overloaded{[](scene::cursor::arrow) {},
                           [&](scene::cursor::text) { m.style(css::cursor{css::text{}}); },
                           [&](scene::cursor::hand) { m.style(css::cursor{css::pointer{}}); },
                           [&](scene::cursor::resize_horizontal) { m.style(css::cursor{css::ew_resize{}}); },
                           [&](scene::cursor::resize_vertical) { m.style(css::cursor{css::ns_resize{}}); }}, s.cursorShape());
  if (s.fGradient) m.style(css::background_image{css::linear_gradient{css::argb{s.fGradient->top}, css::argb{s.fGradient->bottom}}});
  if (s.fBorder) m.style(css::border{css::line{css::pixels{s.fBorder->width}, css::argb{s.fBorder->colour}}});
  if (s.fShadow) m.style(css::box_shadow{css::shadow{css::zero{}, css::pixels{s.fShadow->offsetY}, css::pixels{s.fShadow->offsetY * 2.0f}, css::argb{s.fShadow->colour}}});
  if (s.fAlpha < 1.0f) m.style(css::opacity{s.fAlpha});
  if (s.fMasking) m.style(css::overflow{css::hidden{}});
  if (s.fDepth != 0.0f) m.style(css::z_index{static_cast<int>(std::lround(s.fDepth))});
  // Placed by its anchor, out of the flow it is in: over it, its origin on
  // the anchor.
  css::transforms transform;
  const auto anchored = [&](bool absolute) {
    m.style(absolute ? css::position{css::absolute{}} : css::position{css::relative{}});
    const auto offset = [](float at, float margins) {
      if (at * margins == 0.0f) return css::Length{css::percent{at * 100.0f}};
      return css::Length{css::calculated{css::percent{at * 100.0f}, css::pixels{at * margins}}};
    };
    m.style(css::left{offset(s.fAnchor.x, s.fMargin.totalX())});
    m.style(css::top{offset(s.fAnchor.y, s.fMargin.totalY())});
    if (s.fOrigin.x != 0.0f || s.fOrigin.y != 0.0f)
      transform.values.emplace_back(css::translate{css::percent{-s.fOrigin.x * 100.0f}, css::percent{-s.fOrigin.y * 100.0f}});
  };
  spl::visit(spl::overloaded{
                 [&](const in_flex &flow) {
                   // Along the flow: its own size, or an equal share of what is
                   // left where it grows. Across: where its alignment says.
                   const bool along = flow.row ? s.fGrowAxes.has<scene::axis::x>() || relX
                                               : s.fGrowAxes.has<scene::axis::y>() || relY;
                   const bool grows = flow.row ? s.fGrowAxes.has<scene::axis::x>() : s.fGrowAxes.has<scene::axis::y>();
                   const bool shrinks = flow.row ? s.fShrinkAxes.has<scene::axis::x>() : s.fShrinkAxes.has<scene::axis::y>();
                   m.style(css::flex{css::flex_values{grows ? 1.0f : 0.0f, grows || shrinks ? 1.0f : 0.0f,
                       grows ? css::Size{css::pixels{0}} : css::Size{css::auto_{}}}});
                   if (along && (flow.row ? s.fMinWidth : s.fMinHeight) <= 0.0f)
                     m.style(flow.row ? css::Declaration{css::min_width{css::zero{}}} : css::Declaration{css::min_height{css::zero{}}});
                   const bool across = flow.row ? s.fGrowAxes.has<scene::axis::y>() : s.fGrowAxes.has<scene::axis::x>();
                   if (s.fAlignSelf) m.style(css::align_self{skiff::web::cssAlignment(*s.fAlignSelf)});
                   else if (across) m.style(css::align_self{css::stretch{}});
                   if (s.fOutOfFlow) {
                     anchored(true);
                   }
                 },
                 [&](const in_cell &) {
                   // In the one cell of what holds it, where its anchor is.
                   m.style(css::grid_area{css::cell{1, 1}});
                   m.style(css::justify_self{css::start{}});
                   m.style(css::align_self{css::start{}});
                   anchored(s.fOutOfFlow);
                 },
                 [&](const in_scroll &) {
                   // The contents keep their height; only the viewport is
                   // bounded. Flex's default shrink collapsed long histories.
                   m.style(css::flex{css::flex_values{0, 0, css::auto_{}}});
                 }},
             holder);
  // Its offset, where it is its own: in a flow, a part in the flow is where
  // the flow put it -- skiff's layout writes that place into its offset
  // (State::arrange), and the engine's flex box puts it there already.
  // Written again, every part was moved down a second time: rows twice as
  // far apart, icons above their words.
  const bool flowed = spl::visit(spl::overloaded{[&](const in_flex &) { return !s.fOutOfFlow; }, [](const auto &) { return false; }}, holder);
  // ScrollContainer writes -current() into its contents' shift. The engine
  // already applies that displacement through scrollTop; a transform here
  // scrolled the same contents twice, beneath neighbouring panels.
  const bool scrolled = spl::visit(spl::overloaded{[](const in_scroll &) { return true; }, [](const auto &) { return false; }}, holder);
  const float dx = (flowed ? 0.0f : s.fX) + (scrolled ? 0.0f : s.fShiftX);
  const float dy = (flowed ? 0.0f : s.fY) + (scrolled ? 0.0f : s.fShiftY);
  if (dx != 0.0f || dy != 0.0f) transform.values.emplace_back(css::translate{css::pixels{dx}, css::pixels{dy}});
  if (s.fScale != 1.0f) transform.values.emplace_back(css::scale{s.fScale});
  if (!transform.values.empty()) m.style(css::transform{std::move(transform)});
}

// The element a node is made as: a field the engine's own, else a box.
[[nodiscard]] inline std::string_view tagOf(const scene::Look &look) {
  return spl::visit(spl::overloaded{[](const auto &) { return std::string_view("div"); }},
                    look);
}

// A text's runs, as elements in its own: cut where a style or a link begins
// or ends, each run a <span> -- or an <a>, a link -- drawn as the text
// draws it: strong, slanted, struck, underlined; code in the monospace face
// on a faint plate; a quote in the quote's colour; a stretch pointed at on
// a plate of it; a spoiler hidden under a plate; its own colour and plate;
// a heading's size; a link in the links' colour, a mention on a plate.
template <class PictureOf>
void runsOf(Markup &m, const scene::look::words &words, PictureOf &&pictureOf, std::vector<std::pair<std::uint32_t, const skia::SkImage *>> &pictures) {
  std::vector<std::size_t> cuts{0, words.text.size()};
  for (const scene::TextStyled &one : words.styles) cuts.insert(cuts.end(), {one.first, one.last});
  for (const scene::TextLink &one : words.links) cuts.insert(cuts.end(), {one.first, one.last});
  std::erase_if(cuts, [&](std::size_t at) { return at > words.text.size(); });
  std::ranges::sort(cuts);
  cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());
  const auto covers = [](std::size_t at) { return [at](const auto &one) { return at >= one.first && at < one.last; }; };
  for (const auto [index, pair] : cuts | std::views::adjacent<2> | std::views::enumerate) {
    const auto [from, to] = pair;
    const scene::TextLink *link = nullptr;
    if (const auto found = std::ranges::find_if(words.links, covers(from)); found != words.links.end()) link = &*found;
    // The styles over it, summed as the text sums them (styleAt).
    scene::TextStyled style;
    for (const scene::TextStyled &one : words.styles | std::views::filter(covers(from))) {
      style.strong = style.strong || one.strong;
      style.emphasis = style.emphasis || one.emphasis;
      style.struck = style.struck || one.struck;
      style.underline = style.underline || one.underline;
      style.code = style.code || one.code;
      style.quote = style.quote || one.quote;
      style.marked = style.marked || one.marked;
      style.spoiler = style.spoiler || one.spoiler;
      if (one.colour) style.colour = one.colour;
      if (one.background) style.background = one.background;
      if (one.scale != 1.0f) style.scale = one.scale;
    }
    const auto part = static_cast<std::uint32_t>(index + 1);
    // A custom emoji: its picture, in place of the placeholder its range is,
    // as tall as the text draws it.
    if (link && link->picture) {
      if (const skia::SkImage *picture = pictureOf(*link)) {
        m.inner("img", skiff::web::Part{part}, [&](Markup &img) {
          img.style(css::width{css::pixels{words.size * 1.2f}});
          img.style(css::height{css::pixels{words.size * 1.2f}});
          img.style(css::vertical_align{css::middle{}});
          img.attr("alt", link->plain);
          img.attr("data-skiff-picture", std::to_string(picture->uniqueID()));
        });
        pictures.emplace_back(part, picture);
        continue;
      }
    }
    m.inner(link ? "a" : "span", skiff::web::Part{part}, [&](Markup &run) {
      run.text(words.text.substr(from, to - from));
      const scene::Colour colour = link ? words.linkColour : style.colour.value_or(style.quote ? words.quoteColour : words.colour);
      run.style(css::color{css::argb{colour}});
      if (style.strong) run.style(css::font_weight{css::bold{}});
      if (style.emphasis) run.style(css::font_style{css::italic{}});
      if (style.struck || style.underline)
        run.style(css::text_decoration{css::decoration{style.struck, style.underline}});
      if (style.code) {
        run.style(css::font_family{css::monospace{}});
        run.style(css::background_color{css::argb{(colour & 0x00ffffffu) | 0x1a000000u}});
        run.style(css::border_radius{css::sides{css::pixels{3}}});
      }
      if (style.marked) run.style(css::background_color{css::argb{(words.quoteColour & 0x00ffffffu) | 0x47000000u}});
      if (style.background) run.style(css::background_color{css::argb{*style.background}});
      if (style.spoiler) {
        run.style(css::background_color{css::argb{colour}});
        run.style(css::color{css::transparent{}});
        run.style(css::border_radius{css::sides{css::pixels{3}}});
      }
      if (style.scale != 1.0f) run.style(css::font_size{css::pixels{words.size * style.scale}});
      if (link) {
        run.attr("href", link->target);
        if (link->pill) {
          run.style(css::background_color{css::argb{(words.linkColour & 0x00ffffffu) | 0x29000000u}});
          run.style(css::border_radius{css::sides{css::pixels{999}}});
          run.style(css::padding{css::sides{css::zero{}, css::pixels{6}}});
        }
      }
    });
  }
}

// What kind of node it is, as an element, and how it holds its parts: what
// its look says.
inline void flexOf(Markup &m, const scene::FlowOptions &o) {
  m.style(css::display{css::flex_box{}});
  m.style(css::position{css::relative{}});
  m.style(isRow(o.direction) ? css::flex_direction{css::row{}} : css::flex_direction{css::column{}});
  m.style(o.wrap ? css::flex_wrap{css::wrap{}} : css::flex_wrap{css::nowrap{}});
  if (o.spacingX > 0.0f || o.spacingY > 0.0f) m.style(css::gap{css::gaps{css::pixels{o.spacingY}, css::pixels{o.spacingX}}});
  m.style(css::align_items{skiff::web::cssAlignment(o.crossAlign)});
  m.style(css::justify_content{skiff::web::cssJustification(o.justify)});
}
template <class PictureOf>
[[nodiscard]] Holder kindOf(Markup &m, const scene::Look &look, PictureOf &&pictureOf,
                            std::vector<std::pair<std::uint32_t, const skia::SkImage *>> &pictures) {
  return spl::visit(spl::overloaded{[&](const scene::look::box &box) -> Holder {
                                      m.style(css::display{css::grid{}});
                                      m.style(css::position{css::relative{}});
                                      if (box.colour) m.style(css::background_color{css::argb{*box.colour}});
                                      return in_cell{};
                                    },
                                    [&](const scene::look::flow &flow) -> Holder {
                                      flexOf(m, flow.options);
                                      return in_flex{isRow(flow.options.direction)};
                                    },
                                    [&](const scene::look::words &words) -> Holder {
                                      m.style(css::display{css::block{}});
                                      m.style(css::white_space{css::pre_wrap{}});
                                      m.style(css::font_size{css::pixels{words.size}});
                                      m.style(css::line_height{1.25f});
                                      m.style(css::color{css::argb{words.colour}});
                                      if (words.bold) m.style(css::font_weight{css::bold{}});
                                      if (words.elided) {
                                        m.style(css::white_space{css::nowrap{}});
                                        m.style(css::overflow{css::hidden{}});
                                        m.style(css::text_overflow{css::ellipsis{}});
                                      } else if (words.wrapped == std::optional<bool>(false)) {
                                        m.style(css::white_space{css::pre{}});
                                      }
                                      // Plain: its words. Styled or linked: its runs.
                                      if (words.styles.empty() && words.links.empty())
                                        m.text(words.text);
                                      else
                                        runsOf(m, words, pictureOf, pictures);
                                      return in_cell{};
                                    },
                                    [&](const scene::look::scrolled &) -> Holder {
                                      m.style(css::display{css::flex_box{}});
                                      m.style(css::flex_direction{css::column{}});
                                      m.style(css::overflow_y{css::auto_{}});
                                      m.style(css::position{css::relative{}});
                                      return in_scroll{};
                                    },
                                    [&](const scene::look::drawn &) -> Holder {
                                      m.style(css::display{css::grid{}});
                                      m.style(css::position{css::relative{}});
                                      return in_cell{};
                                    },
                                    [&](const scene::look::field &field) -> Holder {
                                      // The engine's own field: its value and what
                                      // it shows empty; its text in the field's
                                      // size and colour, on nothing of its own --
                                      // the node's plate is its State's.
                                      m.style(css::display{css::grid{}});
                                      m.style(css::position{css::relative{}});
                                      m.style(css::cursor{css::text{}});
                                      m.inner(field.multiline ? "textarea" : "input", kField, [&](Markup &input) {
                                        input.value(field.text);
                                        if (field.selection) {
                                          input.attr("data-skiff-anchor", std::to_string(field.selection->first));
                                          input.attr("data-skiff-caret", std::to_string(field.selection->second));
                                        }
                                        if (!field.placeholder.empty()) input.attr("aria-label", field.placeholder);
                                        if (field.masked) input.attr("type", "password");
                                        input.style(css::grid_area{css::cell{1, 1}});
                                        input.style(css::width{css::percent{100}}); input.style(css::height{css::percent{100}});
                                        input.style(css::min_width{css::zero{}}); input.style(css::min_height{css::zero{}});
                                        input.style(css::box_sizing{css::border_box{}});
                                        input.style(css::padding{css::sides{css::zero{}}}); input.style(css::margin{css::sides{css::zero{}}});
                                        input.style(css::font_size{css::pixels{field.size}});
                                        input.style(css::font_family{css::inherit{}});
                                        input.style(css::color{css::argb{field.colour}});
                                        input.style(css::background{css::transparent{}});
                                        input.style(css::border{css::none{}}); input.style(css::outline{css::none{}});
                                        input.style(css::resize{css::none{}});
                                      });
                                      // Blitz has no native placeholder painting. A sibling
                                      // keeps it out of the editor's actual value and selection.
                                      if (field.text.empty() && !field.placeholder.empty())
                                        m.inner("div", kPlaceholder, [&](Markup &hint) {
                                          hint.text(field.placeholder);
                                          hint.style(css::grid_area{css::cell{1, 1}}); hint.style(css::align_self{css::center{}});
                                          hint.style(css::font_size{css::pixels{field.size}}); hint.style(css::color{css::argb{field.colour}});
                                          hint.style(css::opacity{0.65}); hint.style(css::pointer_events{css::none{}});
                                          hint.style(css::min_width{css::zero{}}); hint.style(css::overflow{css::hidden{}});
                                          hint.style(css::white_space{css::nowrap{}});
                                        });
                                      return in_cell{};
                                    }},
                    look);
}

// What a node that paints itself paints (drawSelf through the painter), as
// elements of its own element: plates, rounded or round, their edges, lines
// across or down, and text, each placed where it is painted -- CSS alone. A
// painter still: drawn into a canvas whose drawing goes nowhere, so that
// whatever it is asked that is not shadowed here is done there and lost --
// and said lost (a path, an arc, a picture), for the node to be painted by
// skiff instead. SvgPainter, below, has paths and arcs too, as SVG.
// What a recording keeps whatever copy of the painter writes to it: the
// parts numbered so far, and what was lost first.
struct Recording {
  std::size_t parts = 0;
  bool lost = false;
  std::string_view lostWhat;
  // The pictures painted, by the part of the <img> each is shown in: their
  // pixels given to the document once it has the element.
  std::vector<std::pair<std::uint32_t, const skia::SkImage *>> pictures;
};
class HtmlPainter : public skiff::paint::Painter {
public:
  HtmlPainter(skia::SkCanvas *discarded, skia::SkFont &font, Markup &into, const skia::SkRect &box, Recording &recording)
      : Painter(discarded, font), fFont(&font), fInto(&into), fLeft(box.fLeft), fTop(box.fTop), fBox(box), fRecording(&recording) {}

  [[nodiscard]] bool lost() const noexcept { return fRecording->lost; }
  // What was lost first, for the mirror to tell: what of the painter it was.
  [[nodiscard]] std::string_view lostWhat() const noexcept { return fRecording->lostWhat; }
  // This painter in the monospace face where `on` (code), else the text's
  // own: a copy writing to the same recording.
  [[nodiscard]] HtmlPainter monospaced(bool on) const {
    HtmlPainter other = *this;
    other.fMonospace = on;
    return other;
  }

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
  void rotate(float) const { this->lose("rotate"); }
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
  void fill(const skia::SkPath &, skia::SkColor, float = 1.0f) const { this->lose("fill(SkPath)"); }
  void fill(const Path &, skia::SkColor, float) const { this->lose("fill(Path)"); }
  void circle(float cx, float cy, float r, skia::SkColor color, float alpha = 1.0f) const {
    this->plate(skia::SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), r, color, alpha);
  }
  void strokeRounded(const skia::SkRect &rect, float radius, skia::SkColor color, float width, float alpha = 1.0f) const {
    this->edge(rect, radius, width, color, alpha);
  }
  void stroke(const skia::SkRRect &shape, float width, skia::SkColor color, float alpha = 1.0f) const {
    this->edge(shape.rect(), shape.radii(skia::SkRRect::kUpperLeft_Corner).fX, width, color, alpha);
  }
  void stroke(const Path &, float, skia::SkColor, float, Cap, bool) const { this->lose("stroke(Path)"); }
  void stroke(const Path &, float, skia::SkColor, float, bool = true) const { this->lose("stroke(Path)"); }
  void strokeCircle(float cx, float cy, float r, float width, skia::SkColor color, float alpha) const {
    this->edge(skia::SkRect::MakeLTRB(cx - r, cy - r, cx + r, cy + r), r, width, color, alpha);
  }
  // A line across or down: a plate as thick as the line. Slanted: lost.
  void line(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha, bool = true) const {
    if (!this->straight(x1, y1, x2, y2, width, color, alpha)) this->lose("a slanted line");
  }
  void line(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha, Cap) const {
    this->line(x1, y1, x2, y2, width, color, alpha, true);
  }
  void arc(const skia::SkRect &, float, float, float, skia::SkColor, float) const { this->lose("arc"); }
  void fillArc(const skia::SkRect &, float, float, skia::SkColor, float) const { this->lose("fillArc"); }
  void verticalGradient(const skia::SkRect &rect, skia::SkColor top, skia::SkColor, float alpha = 1.0f) const {
    this->plate(rect, 0.0f, top, alpha);
  }
  // Pictures: an <img> where each is painted -- filling its box as a cover
  // where it fills it, cut to a rounded box where it is seen through one.
  void imageFilled(const skia::SkImage *image, const skia::SkRect &at, float alpha = 1.0f) const {
    this->picture(image, at, alpha, css::cover{}, 0.0f);
  }
  void image(const skia::SkImage *image, const skia::SkRect &at, float alpha = 1.0f, Sampling = Sampling::linear) const {
    this->picture(image, at, alpha, css::fill{}, 0.0f);
  }
  void imagePart(const skia::SkImage *image, const skia::SkRect &, const skia::SkRect &to, float alpha = 1.0f, Sampling = Sampling::linear) const {
    this->picture(image, to, alpha, css::cover{}, 0.0f);
  }
  void imageThrough(const skia::SkRRect &shape, const skia::SkImage *image, const skia::SkRect &at, float alpha, Sampling = Sampling::linear) const {
    this->picture(image, at, alpha, css::cover{}, shape.radii(skia::SkRRect::kUpperLeft_Corner).fX);
  }
  void imageThrough(const skia::SkPath &, const skia::SkImage *image, const skia::SkRect &, float, Sampling = Sampling::linear) const {
    if (image != nullptr) this->lose("a picture through a path");
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

protected:
  void lose(std::string_view what) const {
    if (!fRecording->lost) fRecording->lostWhat = what;
    fRecording->lost = true;
  }
  void picture(const skia::SkImage *image, const skia::SkRect &at, float alpha, css::Fit fit, float radius) const {
    if (image == nullptr || alpha <= 0.0f) return;
    this->element("img", [&](Markup &e) {
      this->at(e, this->placed(at));
      e.style(css::object_fit{fit});
      if (radius > 0.0f) e.style(css::border_radius{css::sides{css::pixels{radius}}});
      if (alpha < 1.0f) e.style(css::opacity{alpha});
      e.attr("data-skiff-picture", std::to_string(image->uniqueID()));
    });
    fRecording->pictures.emplace_back(static_cast<std::uint32_t>(fRecording->parts), image);
  }
  // A line across or down, as a plate: whether it was one.
  bool straight(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha) const {
    if (y1 == y2)
      this->plate(skia::SkRect::MakeLTRB(std::min(x1, x2), y1 - width * 0.5f, std::max(x1, x2), y1 + width * 0.5f), 0.0f, color, alpha);
    else if (x1 == x2)
      this->plate(skia::SkRect::MakeLTRB(x1 - width * 0.5f, std::min(y1, y2), x1 + width * 0.5f, std::max(y1, y2)), 0.0f, color, alpha);
    else
      return false;
    return true;
  }
  // Where a box of the painting is in the element: moved as it was
  // translated, from the node's own box.
  [[nodiscard]] skia::SkRect placed(const skia::SkRect &r) const { return r.makeOffset(fX - fLeft, fY - fTop); }
  [[nodiscard]] static css::argb cssColour(skia::SkColor c, float alpha) {
    const float a = static_cast<float>((c >> 24) & 0xffu) / 255.0f * std::clamp(alpha, 0.0f, 1.0f);
    const auto byte = static_cast<scene::Colour>(std::lround(a * 255.0f));
    return {(byte << 24) | (c & 0x00ffffffu)};
  }
  [[nodiscard]] static std::string colourOf(skia::SkColor c, float alpha) { return rgba(cssColour(c, alpha).value); }
  [[nodiscard]] static bool unseen(skia::SkColor color, float alpha) { return ((color >> 24) & 0xffu) == 0 || alpha <= 0.0f; }
  void element(std::string_view tag, auto &&write) const {
    fInto->inner(tag, skiff::web::Part{static_cast<std::uint32_t>(++fRecording->parts)}, [&](Markup &e) {
      e.style(css::position{css::absolute{}});
      write(e);
    });
  }
  // A part of an element made here: numbered on from those before.
  [[nodiscard]] skiff::web::Part nextPart() const { return skiff::web::Part{static_cast<std::uint32_t>(++fRecording->parts)}; }
  void at(Markup &e, const skia::SkRect &r) const {
    e.style(css::left{css::pixels{r.fLeft}});
    e.style(css::top{css::pixels{r.fTop}});
    e.style(css::width{css::pixels{r.width()}});
    e.style(css::height{css::pixels{r.height()}});
  }
  void plate(const skia::SkRect &rect, float radius, skia::SkColor color, float alpha) const {
    if (unseen(color, alpha) || rect.isEmpty()) return;
    this->element("div", [&](Markup &e) {
      this->at(e, this->placed(rect));
      e.style(css::background_color{cssColour(color, alpha)});
      if (radius > 0.0f) e.style(css::border_radius{css::sides{css::pixels{radius}}});
    });
  }
  void edge(const skia::SkRect &rect, float radius, float width, skia::SkColor color, float alpha) const {
    if (unseen(color, alpha) || width <= 0.0f) return;
    // The stroke centred on the box's edge, as Skia strokes it.
    const skia::SkRect outer = this->placed(rect).makeOutset(width * 0.5f, width * 0.5f);
    this->element("div", [&](Markup &e) {
      this->at(e, outer);
      e.style(css::box_sizing{css::border_box{}});
      e.style(css::border{css::line{css::pixels{width}, cssColour(color, alpha)}});
      if (radius > 0.0f) e.style(css::border_radius{css::sides{css::pixels{radius + width * 0.5f}}});
    });
  }
  void words(const std::string &str, float x, float baseline, float size, skia::SkColor color, float alpha, bool bold,
             std::optional<float> width) const {
    if (str.empty() || unseen(color, alpha)) return;
    fFont->setSize(size);
    skia::SkFontMetrics metrics;
    (void)fFont->getMetrics(&metrics);
    this->element("div", [&](Markup &e) {
      e.style(css::left{css::pixels{x + fX - fLeft}});
      e.style(css::top{css::pixels{baseline + metrics.fAscent + fY - fTop}});
      if (width) {
        e.style(css::width{css::pixels{*width}});
        e.style(css::overflow{css::hidden{}});
      }
      e.style(css::white_space{css::pre{}});
      if (fMonospace) e.style(css::font_family{css::monospace{}});
      e.style(css::font_size{css::pixels{size}});
      e.style(css::color{cssColour(color, alpha)});
      if (bold) e.style(css::font_weight{css::bold{}});
      e.text(str);
    });
  }

  skia::SkFont *fFont;
  Markup *fInto;
  float fLeft, fTop;
  skia::SkRect fBox;
  Recording *fRecording;
  bool fMonospace = false;
  mutable float fX = 0.0f, fY = 0.0f;
  mutable std::vector<std::pair<float, float>> fSaved;
};

// A path as SVG's path data, as it is made: what SvgPainter's painting asks
// of a path (typename P::Path), in place of the painter's own.
class SvgPath {
public:
  SvgPath &moveTo(float x, float y) { return this->said('M', {x, y}); }
  SvgPath &lineTo(float x, float y) { return this->said('L', {x, y}); }
  SvgPath &quadTo(float x1, float y1, float x, float y) { return this->said('Q', {x1, y1, x, y}); }
  SvgPath &cubicTo(float x1, float y1, float x2, float y2, float x, float y) { return this->said('C', {x1, y1, x2, y2, x, y}); }
  SvgPath &close() {
    fData += 'Z';
    return *this;
  }
  [[nodiscard]] const std::string &data() const noexcept { return fData; }

private:
  SvgPath &said(char command, std::initializer_list<float> numbers) {
    fData += command;
    for (const float n : numbers) {
      char buf[24];
      const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, n, std::chars_format::general, 6);
      fData.append(buf, end);
      fData += ' ';
    }
    return *this;
  }
  std::string fData;
};

// HtmlPainter, on an engine with SVG: paths, arcs and slanted lines too, as
// inline SVG over the node's box, in the scene's own units (its viewBox) --
// icons, loaders, marks as markup.
class SvgPainter : public HtmlPainter {
public:
  using HtmlPainter::HtmlPainter;
  explicit SvgPainter(const HtmlPainter &other) : HtmlPainter(other) {}
  using Path = SvgPath;
  [[nodiscard]] SvgPainter monospaced(bool on) const { return SvgPainter(HtmlPainter::monospaced(on)); }
  using HtmlPainter::fill;
  using HtmlPainter::line;
  using HtmlPainter::stroke;

  void fill(const SvgPath &path, skia::SkColor color, float alpha) const { this->filled(path.data(), color, alpha); }
  void stroke(const SvgPath &path, float width, skia::SkColor color, float alpha, Cap cap, bool) const {
    this->stroked(path.data(), width, color, alpha, capOf(cap));
  }
  void stroke(const SvgPath &path, float width, skia::SkColor color, float alpha, bool round = true) const {
    this->stroked(path.data(), width, color, alpha, round ? "round" : "butt");
  }
  // A line: a plate where it is across or down with its ends cut; else SVG's.
  void line(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha, bool round = true) const {
    if (!round && fAngle == 0.0f && this->straight(x1, y1, x2, y2, width, color, alpha)) return;
    this->stroked("M" + number(x1) + " " + number(y1) + "L" + number(x2) + " " + number(y2), width, color, alpha, round ? "round" : "butt");
  }
  void line(float x1, float y1, float x2, float y2, float width, skia::SkColor color, float alpha, Cap cap) const {
    if (cap == Cap::butt && fAngle == 0.0f && this->straight(x1, y1, x2, y2, width, color, alpha)) return;
    this->stroked("M" + number(x1) + " " + number(y1) + "L" + number(x2) + " " + number(y2), width, color, alpha, capOf(cap));
  }
  void arc(const skia::SkRect &oval, float startDegrees, float sweepDegrees, float width, skia::SkColor color, float alpha) const {
    this->stroked(arcData(oval, startDegrees, sweepDegrees), width, color, alpha, "round");
  }
  void fillArc(const skia::SkRect &oval, float startDegrees, float sweepDegrees, skia::SkColor color, float alpha) const {
    this->filled(arcData(oval, startDegrees, sweepDegrees) + "Z", color, alpha);
  }
  // Turned, as an icon is turned about where it stands (the paperclip's
  // 45 degrees, lost before: Storage's row had no icon on a page). What is
  // drawn after it is SVG turned about there -- a plate cannot turn -- and
  // a move after a turn, which SVG's one turn would not follow, is lost.
  void save() const {
    HtmlPainter::save();
    fAngles.push_back(fAngle);
  }
  void restore() const {
    HtmlPainter::restore();
    if (fAngles.empty()) return;
    fAngle = fAngles.back();
    fAngles.pop_back();
  }
  void rotate(float degrees) const { fAngle += degrees; }
  void translate(float dx, float dy) const {
    if (fAngle != 0.0f && (dx != 0.0f || dy != 0.0f)) this->lose("a move after a turn");
    HtmlPainter::translate(dx, dy);
  }
  void fillRect(const skia::SkRect &rect, skia::SkColor color, float alpha = 1.0f) const { this->fillRounded(rect, 0.0f, color, alpha); }
  void fillRounded(const skia::SkRect &rect, float radius, skia::SkColor color, float alpha = 1.0f) const {
    if (fAngle == 0.0f) HtmlPainter::fillRounded(rect, radius, color, alpha);
    else this->filled(roundedData(rect, radius), color, alpha);
  }
  void strokeRounded(const skia::SkRect &rect, float radius, skia::SkColor color, float width, float alpha = 1.0f) const {
    if (fAngle == 0.0f) HtmlPainter::strokeRounded(rect, radius, color, width, alpha);
    else this->stroked(roundedData(rect, radius), width, color, alpha, "round");
  }

private:
  mutable float fAngle = 0.0f;
  mutable std::vector<float> fAngles;
  // A box with rounded corners, as SVG's path data.
  [[nodiscard]] static std::string roundedData(const skia::SkRect &box, float radius) {
    const float r = std::max(0.0f, std::min({radius, box.width() * 0.5f, box.height() * 0.5f}));
    const std::string corner = "A" + number(r) + " " + number(r) + " 0 0 1 ";
    if (r == 0.0f)
      return "M" + number(box.fLeft) + " " + number(box.fTop) + "H" + number(box.fRight) + "V" + number(box.fBottom) + "H" + number(box.fLeft) + "Z";
    return "M" + number(box.fLeft + r) + " " + number(box.fTop) + "H" + number(box.fRight - r) + corner + number(box.fRight) + " " +
           number(box.fTop + r) + "V" + number(box.fBottom - r) + corner + number(box.fRight - r) + " " + number(box.fBottom) + "H" +
           number(box.fLeft + r) + corner + number(box.fLeft) + " " + number(box.fBottom - r) + "V" + number(box.fTop + r) + corner +
           number(box.fLeft + r) + " " + number(box.fTop) + "Z";
  }
  [[nodiscard]] static std::string_view capOf(Cap cap) { return cap == Cap::butt ? "butt" : cap == Cap::square ? "square" : "round"; }
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
    this->element("svg", [&](Markup &svg) {
      svg.style(css::left{css::zero{}});
      svg.style(css::top{css::zero{}});
      svg.style(css::overflow{css::visible{}});
      svg.attr("width", number(fBox.width()));
      svg.attr("height", number(fBox.height()));
      svg.attr("viewBox", number(fBox.fLeft - fX) + " " + number(fBox.fTop - fY) + " " + number(fBox.width()) + " " + number(fBox.height()));
      svg.attr("xmlns", "http://www.w3.org/2000/svg");
      svg.inner("path", this->nextPart(), [&](Markup &path) {
        write(path);
        if (fAngle != 0.0f) path.attr("transform", "rotate(" + number(fAngle) + ")");
      });
    });
  }
  void filled(const std::string &data, skia::SkColor color, float alpha) const {
    if (unseen(color, alpha) || data.empty()) return;
    this->shape([&](Markup &path) {
      path.attr("d", data);
      path.attr("fill", colourOf(color, alpha));
    });
  }
  void stroked(const std::string &data, float width, skia::SkColor color, float alpha, std::string_view cap) const {
    if (unseen(color, alpha) || data.empty()) return;
    this->shape([&](Markup &path) {
      path.attr("d", data);
      path.attr("fill", "none");
      path.attr("stroke", colourOf(color, alpha));
      path.attr("stroke-width", number(width));
      path.attr("stroke-linecap", cap);
      path.attr("stroke-linejoin", "round");
    });
  }
};

} // namespace detail

// A scene's tree, mirrored: one element per node, kept by the node's id,
// written again only where what it is changed, made where new, dropped where
// gone. Whatever drives the program refreshes its bindings, then sync()s.
class Mirror {
public:
  explicit Mirror(dom::Handle into = dom::root()) : fInto(into) {}

  template <class Root> void sync(Root &root) {
    ++fPass;
    fAtRoot = true;
    const dom::Handle top = this->visit(root, detail::in_cell{});
    this->order(fInto, std::vector<dom::Handle>{top});
    // Only attached, populated elements can scroll: setting scrollTop on
    // an empty or detached viewport was clamped to zero on its first frame.
    for (const auto &[element, offset] : std::exchange(fScrollDue, {}))
      dom::scrollTo(element, offset);
    std::erase_if(fShown, [&](auto &kept) {
      if (kept.second.seen == fPass) return false;
      fNodeOf.erase(kept.second.handle);
      for (const auto &[part, h] : kept.second.inner) { fNodeOf.erase(h); dom::destroy(h); }
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

  // A field edited by the engine: its value now, given to the node as one
  // edit -- what changed, between what both ends kept -- through the field's
  // own way of editing (replaceRange), so that the program is told as it is
  // of typing; its press kept for the host (Scene::pressedAlong). Whether
  // there was such a field.
  template <class S> bool typed(S &scene, dom::Handle element, std::string_view value) {
    const auto found = fNodeOf.find(element);
    if (found == fNodeOf.end()) return false;
    bool edited = false;
    this->editIn(scene.root(), found->second, value, edited);
    if (!edited) return false;
    // What was written of it is the engine's now: not written back.
    if (const auto shown = fShown.find(found->second); shown != fShown.end())
      for (auto &part : shown->second.markup.fInner)
        if (part.fPart == detail::kField) part.fMarkup.value(value);
    scene::Path way;
    if constexpr (scene::kErasedWalks) {
      auto seen = scene::AnyNodeRef::of(scene.root());
      if (!scene::walk::findPath(seen, found->second, way)) return true;
    } else {
      if (!scene::walk::findPath(scene.root(), found->second, way)) return true;
    }
    scene.pressedAlong(way);
    return true;
  }

  // A native editor's caret and selection, already converted to UTF-8.
  template <class S> bool selected(S &scene, dom::Handle element, std::size_t anchor, std::size_t caret) {
    const auto found = fNodeOf.find(element);
    if (found == fNodeOf.end()) return false;
    bool selected = false;
    this->selectIn(scene.root(), found->second, anchor, caret, selected);
    return selected;
  }

  // A key for the program rather than the engine's field -- Enter, which
  // sends or breaks a line as the program says; Escape; a shortcut: given
  // to skiff as the window gives keys, its focus put on the node of
  // `element` first where there is one (the field the engine has focused).
  // Whether it was taken.
  template <class S> bool keyed(S &scene, dom::Handle element, const scene::key::down &press) {
    if (const auto found = fNodeOf.find(element); found != fNodeOf.end()) scene.focus(found->second);
    const bool taken = scene.dispatchKey(press);
    (void)scene.dispatchKey(scene::key::up{press.key, press.modifiers});
    return taken;
  }

  // The engine's scrolling, read back into skiff's views that scroll before
  // a frame: `scrolledOf(element)` how far the engine has an element
  // scrolled. What skiff makes of where its views are -- a list's rows near
  // its edge asked for, what is read -- goes by the engine's scrolling.
  template <class Root, class ScrolledOf> void readScrolled(Root &root, ScrolledOf &&scrolledOf) {
    this->readIn(root, scrolledOf);
  }

  // A point in an element, from its box's corner, where skiff has it: in
  // the node's box as skiff laid it out, moved as skiff draws it (a list's
  // scroll) -- the window's point skiff's own input finds the node at, for
  // what the program does with the pointer (a menu, a drag, a swipe).
  [[nodiscard]] std::optional<skia::SkPoint> toScene(dom::Handle element, float x, float y) const {
    const auto found = fNodeOf.find(element);
    if (found == fNodeOf.end()) return std::nullopt;
    const auto shown = fShown.find(found->second);
    if (shown == fShown.end()) return std::nullopt;
    const scene::work::Offset moved = scene::work::drawnOffset(found->second);
    return skia::SkPoint::Make(shown->second.bounds.fLeft + moved.x + x, shown->second.bounds.fTop + moved.y + y);
  }

  // The element a node is, where it is shown.
  [[nodiscard]] dom::Handle elementOf(const scene::State &state) const {
    const auto found = fShown.find(state.fId);
    return found == fShown.end() ? dom::kNone : found->second.handle;
  }
  [[nodiscard]] dom::Handle ownerOf(dom::Handle element) const {
    const auto found = fNodeOf.find(element);
    if (found == fNodeOf.end()) return dom::kNone;
    const auto shown = fShown.find(found->second);
    return shown == fShown.end() ? dom::kNone : shown->second.handle;
  }
  [[nodiscard]] std::size_t size() const noexcept { return fShown.size(); }
  // What could not be written as the engine's own -- by the node's type and
  // what of it was lost -- each said once, as it is first seen: taken by the
  // host to tell. Nothing left to tell: everything shown is the engine's.
  [[nodiscard]] std::vector<std::string> takeNotNative() { return std::exchange(fNotNativeNew, {}); }
  // How many nodes painting themselves were written as elements, all told.
  [[nodiscard]] std::size_t recorded() const noexcept { return fRecorded; }

private:
  struct Shown {
    dom::Handle handle = dom::kNone;
    Markup markup;
    std::vector<dom::Handle> children;
    std::vector<std::pair<skiff::web::Part, dom::Handle>> inner;
    // Where it was scrolled, as last said either way.
    float scrolled = 0.0f;
    // Where skiff laid the node out, as last written.
    skia::SkRect bounds = skia::SkRect::MakeEmpty();
    // The pictures given, by part: the key of each.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pictures;
    std::uint64_t seen = 0;
  };

  // A node: as its type says, or -- erased, outside a release build -- as the
  // walks' table says; either way its state, its look, whether it takes a
  // press, and its children in their order.
  template <class N> dom::Handle visit(N &node, const detail::Holder &holder) {
    return this->write(node.fState, node.look(), node.acceptsInput(), holder, [&](auto &&each) { scene::eachChild(node, each); },
                       [&](Markup &into, Pictures &pictures) { return this->record(node, into, pictures); }, [&] { return scrolledOf(node); },
                       [&](const scene::TextLink &link) { return pictureOf(node, link); });
  }
  // A text's link's picture (a custom emoji), where the node is a text.
  template <class N> [[nodiscard]] static const skia::SkImage *pictureOf(N &node, const scene::TextLink &link) {
    if constexpr (requires(const N &n) { { n.pictureOf(link) } -> std::convertible_to<const skia::SkImage *>; })
      return node.pictureOf(link);
    else
      return nullptr;
  }
  // How far a view that scrolls is scrolled, as skiff has it: none for what
  // does not scroll.
  template <class N> [[nodiscard]] static std::optional<float> scrolledOf(N &node) {
    if constexpr (requires(const N &n) { { n.current() } -> std::convertible_to<float>; n.extent(); })
      return node.current();
    else
      return std::nullopt;
  }
  dom::Handle visit(scene::AnyNodeRef &ref, const detail::Holder &holder) {
    scene::ErasedNode node(ref);
    return this->write(node.fState, node.look(), node.acceptsInput(), holder, [&](auto &&each) { node.forEachChild(each); },
                       [&](Markup &into, Pictures &pictures) { return this->recordErased(node, into, pictures); },
                       [&] { return node.scrollOffset(); }, [](const scene::TextLink &) -> const skia::SkImage * { return nullptr; });
  }
  // A node that paints itself, recorded as elements where all it paints has
  // one (HtmlPainter); whether it was. Else -- a path, a picture; an erased
  // node -- skiff paints it.
  using Pictures = std::vector<std::pair<std::uint32_t, const skia::SkImage *>>;
  template <class N> bool record(N &node, Markup &into, Pictures &pictures) {
    return this->recordWith<detail::SvgPainter>(node, into, pictures);
  }
  template <class Painter, class N> bool recordWith(N &node, Markup &into, Pictures &pictures) {
    if constexpr (requires(N &n, Painter &p) { n.drawSelf(p, 1.0f); }) {
      // The program's face, for where text sits on its line; a default one
      // where none was loaded (a test).
      const skia::SkFont *font = skiff::paint::defaultFont();
      skia::SkFont face = font != nullptr ? *font : skia::SkFont();
      skia::SkPictureRecorder discarded;
      skia::SkCanvas *canvas = discarded.beginRecording(node.fState.fBounds);
      Markup drawn;
      detail::Recording recording;
      Painter p(canvas, face, drawn, node.fState.fBounds, recording);
      node.drawSelf(p, 1.0f);
      if (p.lost()) {
        this->notNative(scene::nameOf<N>(), p.lostWhat());
        return false;
      }
      into.fInner.append_range(std::move(drawn.fInner));
      pictures = std::move(recording.pictures);
      ++fRecorded;
      return true;
    } else {
      this->notNative(scene::nameOf<N>(), "a drawSelf of its own on a canvas");
      return false;
    }
  }

  // A node seen through the walks' table (outside a release build): what it
  // paints recorded as a node of its type's would be, through a painter the
  // table can name (ErasedPainter) over the mirror's own.
  bool recordErased(scene::ErasedNode &node, Markup &into, Pictures &pictures) {
    const skia::SkFont *font = skiff::paint::defaultFont();
    skia::SkFont face = font != nullptr ? *font : skia::SkFont();
    skia::SkPictureRecorder discarded;
    skia::SkCanvas *canvas = discarded.beginRecording(node.fState.fBounds);
    Markup drawn;
    detail::Recording recording;
    detail::SvgPainter p(canvas, face, drawn, node.fState.fBounds, recording);
    skiff::paint::ErasedPainter through = skiff::paint::ErasedPainter::over(p);
    if (!node.drawThrough(through, 1.0f)) {
      this->notNative("a node seen through the walks' table", "a drawSelf not through a painter");
      return false;
    }
    if (p.lost()) {
      this->notNative("a node seen through the walks' table", p.lostWhat());
      return false;
    }
    into.fInner.append_range(std::move(drawn.fInner));
    pictures = std::move(recording.pictures);
    ++fRecorded;
    return true;
  }

  template <class Children, class Records, class Scrolled, class PictureOf>
  dom::Handle write(const scene::State &state, const scene::Look &look, bool pressable, const detail::Holder &holder,
                    Children &&children, Records &&records, Scrolled &&scrolled, PictureOf &&pictureOf) {
    Markup now;
    Pictures pictures;
    const detail::Holder theirs = detail::kindOf(now, look, pictureOf, pictures);
    detail::stateOf(now, state, holder);
    // Text's pixel size was measured by skiff's font stack. Let this
    // engine measure it: its font metrics and line breaks can differ.
    // Reusing the measured height clipped lines and overlapped later rows.
    spl::visit(spl::overloaded{[&](const scene::look::words &words) {
                                now.eraseStyle<css::height>();
                                if (!state.fRelativeSizeAxes.has<scene::axis::x>() && !state.fGrowAxes.has<scene::axis::x>()) {
                                  now.style(css::width{css::max_content{}});
                                  now.style(state.fMaxWidth > 0.0f ? css::max_width{css::pixels{state.fMaxWidth}} : css::max_width{css::percent{100}});
                                }
                                if (!words.elided && words.wrapped != std::optional(false)) now.style(css::overflow_wrap{css::break_word{}});
                              }, [](const auto &) {}}, look);
    // Scene masking clips the viewport, but overflow:hidden also disables
    // the engine's own scrolling. A scrolling look owns its overflow.
    spl::visit(spl::overloaded{[&](const scene::look::scrolled &) {
                                now.eraseStyle<css::overflow>();
                                now.style(css::overflow_x{css::hidden{}});
                                now.style(css::overflow_y{css::auto_{}});
                              }, [](const auto &) {}}, look);
    // The root: the whole page, whatever its own size says -- the scene gives
    // it the window's box by hand, not by a size of its own, and written as
    // its State has it, it was as big as its content, and everything sized
    // as a share of it nothing.
    // Known as the first element a pass writes -- a node is written before
    // its children -- not by its node's id: in a debug build the root is a
    // wrapper over held content, whose id (stateOf) is its content's, not the
    // wrapper's written here, and the root was never found.
    if (std::exchange(fAtRoot, false)) {
      now.style(css::width{css::percent{100}});
      now.style(css::height{css::percent{100}});
    }
    // A node that paints itself: what it paints, as elements of its own --
    // its size skiff's where it says none. Where something of it has no
    // element (a picture), it is said not native, and that is not shown.
    const bool paints = spl::visit(spl::overloaded{[](const scene::look::drawn &) { return true; }, [](const auto &) { return false; }}, look);
    if (paints) {
      if (!css::find<css::width>(now.fStyles) && state.fBounds.width() > 0.0f) now.style(css::width{css::pixels{state.fBounds.width()}});
      if (!css::find<css::height>(now.fStyles) && state.fBounds.height() > 0.0f) now.style(css::height{css::pixels{state.fBounds.height()}});
      Markup recorded;
      Pictures painted;
      if (records(recorded, painted)) {
        now.fInner = std::move(recorded.fInner);
        pictures.append_range(std::move(painted));
      }
    }
    // A layer over the rest taking no input of its own -- a column's popup
    // layer, filling it, its menus shown at times -- lets presses through
    // to what is under it, as skiff does: an element takes the browser's
    // presses whatever skiff thinks, and the chat list under such a layer
    // could not be clicked (the web smoke test: Mad Tea Party covered).
    // What takes input in it takes them again; a view that scrolls is the
    // browser's to scroll.
    const bool scrolls = spl::visit(spl::overloaded{[](const scene::look::scrolled &) { return true; }, [](const auto &) { return false; }}, look);
    if (pressable) {
      now.listen("click");
      if (!css::find<css::cursor>(now.fStyles)) now.style(css::cursor{css::pointer{}});
      now.style(css::pointer_events{css::auto_{}});
    } else if (state.fOutOfFlow && !scrolls) {
      now.style(css::pointer_events{css::none{}});
    }
    auto [at, made] = fShown.try_emplace(state.fId);
    Shown &shown = at->second;
    if (made) {
      shown.handle = dom::create(detail::tagOf(look));
      fNodeOf.emplace(shown.handle, state.fId);
    }
    // Its handle, on it: which node an element is, in what is written out.
    now.attr("data-skiff-h", std::to_string(shown.handle));
    // A view that scrolls: scrolled by the engine, its offset read back into
    // skiff before the frame (readScrolled); moved by skiff itself (a jump,
    // following its end), written to the engine.
    if (const std::optional<float> offset = scrolled(); offset && std::abs(*offset - shown.scrolled) > 0.5f) {
      fScrollDue.emplace_back(shown.handle, *offset);
      shown.scrolled = *offset;
    }
    shown.seen = fPass;
    shown.bounds = state.fBounds;
    skiff::web::detail::writeOwn(shown.handle, now, shown.markup);
    // The elements it makes in itself (what it paints, as elements): kept by
    // their part, written where they changed, made where new, dropped where
    // gone -- as skiff.web writes a node's.
    std::vector<std::pair<skiff::web::Part, dom::Handle>> kept;
    skiff::web::detail::writeInner(shown.handle, now.fInner, shown.markup.fInner, shown.inner, kept);
    for (const auto &[part, h] : shown.inner)
      if (!std::ranges::contains(kept, h, &std::pair<skiff::web::Part, dom::Handle>::second)) { fNodeOf.erase(h); dom::destroy(h); }
    shown.inner = std::move(kept);
    for (const auto &[part, h] : shown.inner) fNodeOf.insert_or_assign(h, state.fId);
    // Its pictures' pixels, given to the elements they are shown in -- again
    // only where the picture there is another.
    for (const auto &[part, image] : pictures) {
      const auto element = std::ranges::find(shown.inner, skiff::web::Part{part}, &std::pair<skiff::web::Part, dom::Handle>::first);
      if (element == shown.inner.end()) continue;
      const std::uint32_t key = image->uniqueID();
      const auto given = std::ranges::find(shown.pictures, part, &std::pair<std::uint32_t, std::uint32_t>::first);
      if (given != shown.pictures.end() && given->second == key) continue;
      dom::picture(element->second, skia::rgbaPixels(*image), image->width(), image->height(), std::to_string(key));
      if (given != shown.pictures.end()) given->second = key;
      else shown.pictures.emplace_back(part, key);
    }
    shown.markup = std::move(now);
    const dom::Handle handle = shown.handle;
    // Children: each visited, then put in their order where it moved. The
    // map may grow as they are visited: nothing of it held across.
    std::vector<dom::Handle> handles;
    children([&](auto &child) { handles.push_back(this->visit(child, theirs)); });
    this->order(handle, std::move(handles));
    return handle;
  }

  template <class N> void editIn(N &node, scene::NodeId id, std::string_view value, bool &edited) {
    if (edited) return;
    if (node.fState.fId == id) edited = scene::editField(node, value);
    if (edited) return;
    scene::eachChild(node, [&](auto &child) { this->editIn(child, id, value, edited); });
  }
  void editIn(scene::AnyNodeRef &ref, scene::NodeId id, std::string_view value, bool &edited) {
    if (edited) return;
    scene::ErasedNode node(ref);
    if (node.fState.fId == id) edited = node.editField(value);
    if (edited) return;
    node.forEachChild([&](scene::AnyNodeRef &child) { this->editIn(child, id, value, edited); });
  }
  template <class N> void selectIn(N &node, scene::NodeId id, std::size_t anchor, std::size_t caret, bool &selected) {
    if (selected) return;
    if (node.fState.fId == id) selected = scene::selectField(node, anchor, caret);
    if (selected) return;
    scene::eachChild(node, [&](auto &child) { this->selectIn(child, id, anchor, caret, selected); });
  }
  void selectIn(scene::AnyNodeRef &ref, scene::NodeId id, std::size_t anchor, std::size_t caret, bool &selected) {
    if (selected) return;
    scene::ErasedNode node(ref);
    if (node.fState.fId == id) selected = node.selectField(anchor, caret);
    if (selected) return;
    node.forEachChild([&](scene::AnyNodeRef &child) { this->selectIn(child, id, anchor, caret, selected); });
  }
  template <class N, class ScrolledOf> void readIn(N &node, ScrolledOf &scrolledOf) {
    if constexpr (requires(N &n) { n.setCurrent(0.0f); { n.current() } -> std::convertible_to<float>; n.extent(); }) {
      if (const auto found = fShown.find(node.fState.fId); found != fShown.end()) {
        const float engine = scrolledOf(found->second.handle);
        if (std::abs(engine - found->second.scrolled) > 0.5f) {
          found->second.scrolled = engine;
          if (std::abs(engine - node.current()) > 0.5f) node.setCurrent(engine);
        }
      }
    }
    scene::eachChild(node, [&](auto &child) { this->readIn(child, scrolledOf); });
  }
  template <class ScrolledOf> void readIn(scene::AnyNodeRef &ref, ScrolledOf &scrolledOf) {
    scene::ErasedNode node(ref);
    if (const auto offset = node.scrollOffset()) {
      if (const auto found = fShown.find(node.fState.fId); found != fShown.end()) {
        const float engine = scrolledOf(found->second.handle);
        if (std::abs(engine - found->second.scrolled) > 0.5f) {
          found->second.scrolled = engine;
          if (std::abs(engine - *offset) > 0.5f) node.setScrollOffset(engine);
        }
      }
    }
    node.forEachChild([&](scene::AnyNodeRef &child) { this->readIn(child, scrolledOf); });
  }

  void order(dom::Handle parent, std::vector<dom::Handle> children) {
    std::vector<dom::Handle> &was = parent == fInto ? fTop : fShown.at(fNodeOf.at(parent)).children;
    if (was == children) return;
    for (dom::Handle child : children) dom::append(parent, child);
    was = std::move(children);
  }

  dom::Handle fInto;
  std::vector<std::pair<dom::Handle, float>> fScrollDue;
  std::uint64_t fPass = 0;
  // The next element written is the root's (sync): the whole page.
  bool fAtRoot = false;
  std::unordered_map<scene::NodeId, Shown> fShown;
  std::unordered_map<dom::Handle, scene::NodeId> fNodeOf;
  std::vector<dom::Handle> fTop;
  std::size_t fRecorded = 0;
  std::set<std::string> fNotNative;
  std::vector<std::string> fNotNativeNew;
  void notNative(std::string_view type, std::string_view what) {
    std::string said = std::string(type) + ": " + std::string(what);
    if (fNotNative.insert(said).second) fNotNativeNew.push_back(std::move(said));
  }
};

} // namespace skiff::mirror
