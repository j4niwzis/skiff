// skiff.web: a tree that is a page. Its nodes are the document's elements,
// laid out by the browser -- a stack is a flex box, a text is a span, a box a
// div -- and bound to a skiff.model through skiff.bind as the scene's nodes
// are: the same bound<>, scoped<>, the same read() and onPress(), so a page
// written once is a scene on Skia and a page in a browser. The controls -- a
// toggle that is a checkbox, a slider that is a range input, a choice that is
// a row of tabs or a select -- are skiff.widgets.web's, on this.
//
// Nothing here draws or measures. A Spec becomes CSS on the element, once
// when it is made and again when its state moves; what the browser does with
// it is the browser's. Events come back from the page by the element they
// happened on, to the node that made it, and from there as a press:
// skiff::bind::press delivers it, the binding refreshes what moved, and each
// node that moved writes itself to its element again (sync()). Nothing is
// erased on the way: the page's type is known where an event arrives, as a
// release build keeps everything in skiff.
//
// How a node is an element, each hook found where a node declares it:
//   tag()                 the element's tag; "div" unless said
//   mounted()             the element exists: its attributes, elements of its own
//   sync()                its state written to the element (it was marked dirty)
//   handles(f)            every element it answers events on; its own by default
//   onDom(target, event, value)   an event on one of them, before the press;
//                         false where there is nothing to press for
export module skiff.web;

export import :dom;

import std;
import splice;
import skiff.aggregate;
import skiff.core;
import skiff.model;
import skiff.bind;

export namespace skiff::web {

// ---- CSS, from the words a tree is written in ------------------------------

[[nodiscard]] inline std::string px(float v) {
  char buf[32];
  const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, 2);
  std::string s(buf, end);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return (s.empty() ? std::string("0") : s) + "px";
}
[[nodiscard]] inline std::string number(float v) {
  char buf[32];
  const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, 3);
  std::string s(buf, end);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s.empty() ? std::string("0") : s;
}
// 0xAARRGGBB, as Skia and skiff.core spell a colour.
[[nodiscard]] inline std::string rgba(scene::Colour c) {
  const unsigned a = (c >> 24) & 0xffu, r = (c >> 16) & 0xffu, g = (c >> 8) & 0xffu, b = c & 0xffu;
  return "rgba(" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + "," + number(static_cast<float>(a) / 255.0f) + ")";
}
[[nodiscard]] inline std::string margin(const scene::Margin &m) {
  return px(m.fTop) + " " + px(m.fRight) + " " + px(m.fBottom) + " " + px(m.fLeft);
}
[[nodiscard]] inline std::string_view alignment(scene::Align a) {
  return a.at <= 0.25f ? "flex-start" : a.at >= 0.75f ? "flex-end" : "center";
}

// The declarations a node's element gets, from its Spec and flags.
struct Css {
  std::vector<std::pair<std::string, std::string>> fRules;
  // Attributes the stylesheet's rules select on (pageCss): a hover
  // background, a tail, a part revealed on hover.
  std::vector<std::pair<std::string, std::string>> fAttrs;
  void set(std::string property, std::string value) {
    for (auto &[p, v] : fRules)
      if (p == property) {
        v = std::move(value);
        return;
      }
    fRules.emplace_back(std::move(property), std::move(value));
  }
  void attr(std::string name, std::string value = {}) { fAttrs.emplace_back(std::move(name), std::move(value)); }
};

// The rules the page's own attributes select on: what a Spec says that an
// element's inline style cannot -- a background under the pointer or the
// focus, a bubble's tail, a part shown on hover -- and a stack as the box
// its placed children are placed in. Put into the document with the theme.
[[nodiscard]] inline std::string pageCss() {
  return ".skiff-stack{position:relative}\n"
         "[data-skiff-hover]:hover{background-color:var(--skiff-hover-bg)!important}\n"
         "[data-skiff-focus]:focus-within{background-color:var(--skiff-focus-bg)!important}\n"
         "[data-skiff-reveal]{opacity:0;transition:opacity .12s}\n"
         "*:hover>[data-skiff-reveal],[data-skiff-reveal]:focus-within{opacity:1}\n"
         "[data-skiff-tail]{position:relative}\n"
         "[data-skiff-tail]::after{content:'';position:absolute;bottom:0;width:0;height:0;border-style:solid}\n"
         "[data-skiff-tail=right]::after{right:calc(-1*var(--skiff-tail-w));border-width:0 0 var(--skiff-tail-h) var(--skiff-tail-w);"
         "border-color:transparent transparent transparent var(--skiff-bg)}\n"
         "[data-skiff-tail=left]::after{left:calc(-1*var(--skiff-tail-w));border-width:0 var(--skiff-tail-w) var(--skiff-tail-h) 0;"
         "border-color:transparent var(--skiff-bg) transparent transparent}\n";
}

// A Spec laid over another: what the later says, said; the rest as it was.
inline void overlay(scene::Spec &into, const scene::Spec &from) {
  const auto take = [](auto &mine, const auto &theirs) {
    if (theirs) mine = theirs;
  };
  take(into.place, from.place);
  take(into.anchor, from.anchor);
  take(into.origin, from.origin);
  take(into.x, from.x);
  take(into.y, from.y);
  into.fill = into.fill || from.fill;
  into.fillX = into.fillX || from.fillX;
  into.fillY = into.fillY || from.fillY;
  take(into.width, from.width);
  take(into.height, from.height);
  // A relative size said takes the place of the fills said before: the
  // scene's way to make a filling widget a fixed size is relativeSize kNone.
  if (from.relativeSize) into.fill = into.fillX = into.fillY = false;
  take(into.relativeSize, from.relativeSize);
  take(into.autoSize, from.autoSize);
  take(into.grow, from.grow);
  take(into.shrink, from.shrink);
  take(into.minWidth, from.minWidth);
  take(into.maxWidth, from.maxWidth);
  take(into.minHeight, from.minHeight);
  take(into.maxHeight, from.maxHeight);
  take(into.alignSelf, from.alignSelf);
  take(into.depth, from.depth);
  take(into.margin.given, from.margin.given);
  take(into.padding.given, from.padding.given);
  take(into.cornerRadius, from.cornerRadius);
  take(into.corners, from.corners);
  take(into.tail, from.tail);
  take(into.background, from.background);
  take(into.hoverBackground, from.hoverBackground);
  take(into.selectedBackground, from.selectedBackground);
  take(into.focusBackground, from.focusBackground);
  take(into.gradient, from.gradient);
  take(into.border, from.border);
  take(into.shadow, from.shadow);
  take(into.masking, from.masking);
  take(into.scale, from.scale);
  take(into.alpha, from.alpha);
  take(into.shiftX, from.shiftX);
  take(into.shiftY, from.shiftY);
  take(into.visible, from.visible);
  take(into.revealOnHover, from.revealOnHover);
  into.roles.insert(into.roles.end(), from.roles.begin(), from.roles.end());
  take(into.selected, from.selected);
  take(into.disabled, from.disabled);
}


// ---- a node's state --------------------------------------------------------

struct State {
  scene::Spec fSpec;
  bool fVisible = true;
  bool fSelected = false;
  bool fDisabled = false;
  dom::Handle fHandle = dom::kNone;
  // The element's display when shown: the tag's own (""), a stack's flex.
  // Written on every sync, so that a node shown again is not left display:none.
  std::string fDisplay;
  // Written to its element at the next sync.
  bool fDirty = true;
  // The way of the stack it is in -- a row, or a column -- where it is in
  // one: what a grow means there (along it, a share; across it, its breadth).
  std::optional<bool> fInRow;

  // The element is the state's: it goes with a move and with the node (a row
  // gone from a list takes its element out of the page); a copy has none.
  State() = default;
  State(const State &other)
      : fSpec(other.fSpec), fVisible(other.fVisible), fSelected(other.fSelected), fDisabled(other.fDisabled), fDisplay(other.fDisplay),
        fInRow(other.fInRow) {}
  State &operator=(const State &other) {
    if (this != &other) {
      drop();
      fSpec = other.fSpec;
      fVisible = other.fVisible;
      fSelected = other.fSelected;
      fDisabled = other.fDisabled;
      fDisplay = other.fDisplay;
      fInRow = other.fInRow;
      fDirty = true;
    }
    return *this;
  }
  State(State &&other) noexcept
      : fSpec(std::move(other.fSpec)), fVisible(other.fVisible), fSelected(other.fSelected), fDisabled(other.fDisabled),
        fHandle(std::exchange(other.fHandle, dom::kNone)), fDisplay(std::move(other.fDisplay)), fDirty(other.fDirty), fInRow(other.fInRow) {}
  State &operator=(State &&other) noexcept {
    if (this != &other) {
      drop();
      fSpec = std::move(other.fSpec);
      fVisible = other.fVisible;
      fSelected = other.fSelected;
      fDisabled = other.fDisabled;
      fHandle = std::exchange(other.fHandle, dom::kNone);
      fDisplay = std::move(other.fDisplay);
      fDirty = other.fDirty;
      fInRow = other.fInRow;
    }
    return *this;
  }
  ~State() { drop(); }
  void drop() {
    if (fHandle != dom::kNone) dom::destroy(std::exchange(fHandle, dom::kNone));
  }

  void apply(const scene::Spec &spec) {
    overlay(fSpec, spec);
    if (spec.visible) fVisible = *spec.visible;
    if (spec.selected) fSelected = *spec.selected;
    if (spec.disabled) fDisabled = *spec.disabled;
    fDirty = true;
  }
};

// The CSS of a state: the Spec as the browser can take it. Placement by
// hand -- an anchor of the stack, an origin of the node -- is absolute
// position in the stack (position: relative, pageCss); depth is z-index;
// what the browser does on its own under the pointer or the focus is a
// variable on the element and an attribute the page's rules select on.
[[nodiscard]] inline Css cssOf(const State &state) {
  const scene::Spec &s = state.fSpec;
  Css css;
  css.set("display", state.fVisible ? state.fDisplay : "none");
  // Placed: out of the flow, at an anchor of its stack, by an origin of its own.
  const bool placed = s.place || s.anchor;
  scene::Anchor origin = scene::anchor::kTopLeft;
  if (placed) {
    const scene::Anchor at = s.place ? *s.place : *s.anchor;
    origin = s.place ? *s.place : s.origin.value_or(scene::anchor::kTopLeft);
    css.set("position", "absolute");
    css.set("left", number(at.x * 100.0f) + "%");
    css.set("top", number(at.y * 100.0f) + "%");
  } else if (s.depth) {
    css.set("position", "relative");
  }
  if (s.depth) css.set("z-index", std::to_string(static_cast<int>(std::lround(*s.depth))));
  if (s.fill) {
    // An equal share of the stack's way, whatever is in it: a basis of nothing, so two filling
    // parts are two halves, as two filling children of a scene's flow are.
    css.set("flex", "1 1 0%");
    if (!s.minWidth) css.set("min-width", "0");
    css.set("align-self", "stretch");
  }
  if (s.fillX) css.set("width", "100%");
  if (s.fillY) css.set("height", "100%");
  // A size on an axis the node fills, or is relative on, is a share of its parent's -- as on the scene.
  const bool relX = (s.relativeSize && s.relativeSize->has<scene::axis::x>()) || s.fill || s.fillX;
  const bool relY = (s.relativeSize && s.relativeSize->has<scene::axis::y>()) || s.fill || s.fillY;
  if (s.width) css.set("width", relX ? number(*s.width * 100.0f) + "%" : px(*s.width));
  if (s.height) css.set("height", relY ? number(*s.height * 100.0f) + "%" : px(*s.height));
  if (s.minWidth) css.set("min-width", px(*s.minWidth));
  if (s.maxWidth) css.set("max-width", px(*s.maxWidth));
  if (s.minHeight) css.set("min-height", px(*s.minHeight));
  if (s.maxHeight) css.set("max-height", px(*s.maxHeight));
  // Growing along the stack: an equal share of what is left (a basis of
  // nothing), able to shrink -- CSS's `flex: 1`. Across it: the stack's
  // breadth (align-self: stretch), as a scene's flow gives one grown across
  // its way. Where the stack's way is not known (the root), along.
  if (s.grow) {
    const bool x = s.grow->has<scene::axis::x>(), y = s.grow->has<scene::axis::y>();
    const bool along = state.fInRow ? (*state.fInRow ? x : y) : (x || y);
    const bool across = state.fInRow && (*state.fInRow ? y : x);
    // Able to shrink to nothing -- unless it says how narrow it goes, as a
    // grown child of a scene's flow keeps its minWidth: then, where a flow
    // wraps, it wraps rather than squeezing.
    if (along) {
      css.set("flex", "1 1 0%");
      if (!s.minWidth) css.set("min-width", "0");
    }
    if (across && !s.alignSelf && !s.fill) css.set("align-self", "stretch");
  }
  if (s.shrink && (s.shrink->has<scene::axis::x>() || s.shrink->has<scene::axis::y>())) css.set("flex-shrink", "1");
  if (s.alignSelf) css.set("align-self", std::string(alignment(*s.alignSelf)));
  if (s.margin.given) css.set("margin", margin(*s.margin.given));
  if (s.padding.given) css.set("padding", margin(*s.padding.given));
  if (s.padding.given || s.width || s.height || s.border) css.set("box-sizing", "border-box");
  if (s.corners) {
    const scene::Corners &k = *s.corners;
    css.set("border-radius", px(k.topLeft) + " " + px(k.topRight) + " " + px(k.bottomRight) + " " + px(k.bottomLeft));
  } else if (s.cornerRadius) {
    css.set("border-radius", px(*s.cornerRadius));
  }
  std::optional<scene::Colour> background;
  if (state.fSelected && s.selectedBackground) background = *s.selectedBackground;
  else if (s.background) background = *s.background;
  if (background) css.set("background-color", rgba(*background));
  if (s.gradient) css.set("background-image", "linear-gradient(" + rgba(s.gradient->top) + "," + rgba(s.gradient->bottom) + ")");
  if (s.hoverBackground) {
    css.set("--skiff-hover-bg", rgba(*s.hoverBackground));
    css.attr("data-skiff-hover");
  }
  if (s.focusBackground) {
    css.set("--skiff-focus-bg", rgba(*s.focusBackground));
    css.attr("data-skiff-focus");
  }
  if (s.border) css.set("border", px(s.border->width) + " solid " + rgba(s.border->colour));
  if (s.shadow) css.set("box-shadow", "0 " + px(s.shadow->offsetY) + " " + px(s.shadow->offsetY * 2.0f) + " " + rgba(s.shadow->colour));
  if (s.tail && s.tail->shown) {
    css.attr("data-skiff-tail", spl::holds_alternative<scene::tail_side::left>(s.tail->side) ? "left" : "right");
    css.set("--skiff-tail-w", px(s.tail->width));
    css.set("--skiff-tail-h", px(s.tail->height));
    if (background) css.set("--skiff-bg", rgba(*background));
  }
  if (s.revealOnHover && *s.revealOnHover) css.attr("data-skiff-reveal");
  if (s.alpha) css.set("opacity", number(*s.alpha));
  if (s.masking && *s.masking) css.set("overflow", "hidden");
  std::string transform;
  if (placed && (origin.x != 0.0f || origin.y != 0.0f))
    transform += "translate(" + number(-origin.x * 100.0f) + "%," + number(-origin.y * 100.0f) + "%) ";
  const float dx = s.x.value_or(0.0f) + s.shiftX.value_or(0.0f), dy = s.y.value_or(0.0f) + s.shiftY.value_or(0.0f);
  if (dx != 0.0f || dy != 0.0f) transform += "translate(" + px(dx) + "," + px(dy) + ") ";
  if (s.scale && *s.scale != 1.0f) transform += "scale(" + number(*s.scale) + ") ";
  if (!transform.empty()) {
    transform.pop_back();
    css.set("transform", transform);
  }
  return css;
}
inline void applyCss(dom::Handle element, const Css &css) {
  for (const auto &[p, v] : css.fRules) dom::style(element, p, v);
  for (const auto &[n, v] : css.fAttrs) dom::attr(element, n, v);
}

// ---- the node ---------------------------------------------------------------

struct Node : scene::NodeBase {
  State fState;

  // Structure, as the scene's: a leaf, or the members of one aggregate `parts`.
  void forEachChild(this auto &, auto &&) {}
  template <class Self, class F>
    requires requires(Self &self) { self.parts; }
  void forEachChild(this Self &self, F &&f) {
    aggregate::each(self.parts, f);
  }

  // The handlers the binding's kinds re-expose from their base (Bound,
  // Scoped: `using Base::onPointer` and the rest). A page routes nothing
  // through them -- its events are the document's -- so they do nothing.
  template <class... A> void onPointer(A &&...) {}
  template <class... A> void onKey(A &&...) {}
  template <class... A> void onText(A &&...) {}
  template <class... A> void onSemantic(A &&...) {}

  // How it is an element: the defaults, a node declaring its own.
  [[nodiscard]] static constexpr std::string_view tag() { return "div"; }
  void mounted(this auto &) {}
  void sync(this auto &) {}
  // Synced at every sync, not only when something of its own changed: a
  // picture waiting on its source.
  [[nodiscard]] bool wantsSync(this auto &) { return false; }
  template <class F> void handles(this auto &self, F &&f) { f(self.fState.fHandle); }
  [[nodiscard]] bool onDom(this auto &, dom::Handle, std::string_view, std::string_view) { return true; }

  void apply(const scene::Spec &spec) { fState.apply(spec); }
  void markDirty() { fState.fDirty = true; }
  // The scene's words for the same: a node that changed how it looks is
  // written to the page at the next sync; nothing floats over a page.
  void markDamaged() { markDirty(); }
  void setFloats(bool) {}
  // What skiff.bind asks of a list's container when its rows changed: the
  // browser lays the page out, so nothing but the next sync.
  void invalidateLayout() { fState.fDirty = true; }
  [[nodiscard]] dom::Handle element() const noexcept { return fState.fHandle; }
  [[nodiscard]] bool visible() const noexcept { return fState.fVisible; }
  [[nodiscard]] bool selected() const noexcept { return fState.fSelected; }
  [[nodiscard]] bool disabled() const noexcept { return fState.fDisabled; }
  void setVisible(bool visible) {
    if (fState.fVisible != visible) {
      fState.fVisible = visible;
      markDirty();
    }
  }
  void setSelected(bool selected) {
    if (fState.fSelected != selected) {
      fState.fSelected = selected;
      markDirty();
    }
  }
  void setDisabled(bool disabled) {
    if (fState.fDisabled != disabled) {
      fState.fDisabled = disabled;
      markDirty();
    }
  }
};

// The core's table of a page's node type: skiff.core asks for it by the
// node's base (skiffOpsOf, found by this namespace), and skiff.bind walks the
// tree through it outside a release build.
namespace detail {
template <class T> [[nodiscard]] T &nodeAs(void *node) { return *static_cast<T *>(node); }
template <class T>
inline constexpr scene::AnyNodeOps kOps{
    +[](void *n) { delete static_cast<T *>(n); },
    scene::kindOf<T>(),
    +[](void *n, void *context, scene::AnyChildVisit visit) {
      scene::eachChild(nodeAs<T>(n), [&](auto &child) { scene::visitAsAny(child, context, visit); });
    },
    +[](void *n, std::uint32_t place, void *context, scene::AnyChildVisit visit) -> bool {
      return scene::childAt(nodeAs<T>(n), place, [&](auto &child) { scene::visitAsAny(child, context, visit); });
    },
    +[](void *) -> bool { return false; },
    scene::BindTableOf<T>::value,
    scene::MarkOf<T>::key,
    &scene::MarkOf<T>::as,
};
} // namespace detail
template <class T>
  requires std::derived_from<T, Node>
[[nodiscard]] const scene::AnyNodeOps &skiffOpsOf(T *) noexcept {
  return detail::kOps<T>;
}

// ---- nodes -------------------------------------------------------------------

// A box: an element with a Spec and nothing of its own -- a plate, a
// spacer -- or, as the scene's Box, a colour with children in it.
template <class... Parts> struct Box : Node {
  std::tuple<Parts...> fParts;
  explicit Box(scene::Spec spec = {})
    requires(sizeof...(Parts) == 0)
  {
    fState.apply(spec);
  }
  explicit Box(scene::Colour colour, Parts... parts) : fParts(std::move(parts)...), fColour(colour) { fState.apply({.background = colour}); }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
  void setColour(scene::Colour colour) {
    fColour = colour;
    fState.apply({.background = colour});
    markDirty();
  }
  [[nodiscard]] scene::Colour colour() const noexcept { return fColour; }
  scene::Colour fColour = 0;
};
template <class... Parts> Box(scene::Colour, Parts...) -> Box<Parts...>;
Box(scene::Spec) -> Box<>;
Box() -> Box<>;

// A stylesheet put into the document, under `into`: what dresses a page's
// elements, written by whoever knows the look -- skiff.widgets.web's theme.
inline dom::Handle mountStylesheet(dom::Handle into, std::string_view css) {
  const dom::Handle style = dom::create("style");
  dom::text(style, css);
  dom::append(into, style);
  return style;
}

// A text: a span whose textContent is the text.
class Text : public Node {
public:
  using Styled = scene::TextStyled;
  using Link = scene::TextLink;
  explicit Text(std::string text, scene::Spec spec = {}) : fText(std::move(text)) { fState.apply(spec); }
  // As the scene's Text is made: its words, their size and colour, bold or not, selectable or not.
  Text(std::string text, float size, scene::Colour colour, bool bold = false, bool selectable = false)
      : fText(std::move(text)), fSize(size), fColour(colour), fBold(bold), fSelectable(selectable) {}
  [[nodiscard]] static constexpr std::string_view tag() { return "span"; }
  void mounted() { dom::attr(fState.fHandle, "class", "skiff-text"); }
  void sync() {
    const dom::Handle h = fState.fHandle;
    if (fSize) dom::style(h, "font-size", px(*fSize));
    if (fColour) dom::style(h, "color", rgba(*fColour));
    dom::style(h, "font-weight", fBold ? "600" : "");
    dom::style(h, "font-family", fMonospace ? "ui-monospace, monospace" : "");
    dom::style(h, "user-select", fSelectable ? "text" : "");
    dom::style(h, "white-space", fWrapped ? "" : "nowrap");
    dom::style(h, "overflow", fElided || fMaxLines ? "hidden" : "");
    dom::style(h, "text-overflow", fElided ? "ellipsis" : "");
    if (fMaxWidth > 0.0f) dom::style(h, "max-width", px(fMaxWidth));
    if (fMaxLines) {
      dom::style(h, "display", "-webkit-box");
      dom::style(h, "-webkit-box-orient", "vertical");
      dom::style(h, "-webkit-line-clamp", std::to_string(*fMaxLines));
    }
    for (const dom::Handle span : fSpans) dom::destroy(span);
    fSpans.clear();
    if (fStyles.empty() && fLinks.empty()) {
      dom::text(h, fText);
      return;
    }
    // Stretches styled or linked: a span (or an a) for each run between the
    // boundaries of the styles and the links, dressed as the scene draws them.
    dom::text(h, "");
    std::vector<std::size_t> cuts{0, fText.size()};
    for (const Styled &one : fStyles) {
      cuts.push_back(std::min(one.first, fText.size()));
      cuts.push_back(std::min(one.last, fText.size()));
    }
    for (const Link &one : fLinks) {
      cuts.push_back(std::min(one.first, fText.size()));
      cuts.push_back(std::min(one.last, fText.size()));
    }
    std::ranges::sort(cuts);
    cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
      const std::size_t from = cuts[i], to = cuts[i + 1];
      const Link *link = nullptr;
      for (const Link &one : fLinks)
        if (one.first <= from && to <= one.last) link = &one;
      const dom::Handle run = dom::create(link ? "a" : "span");
      std::string words = fText.substr(from, to - from);
      if (link) {
        dom::attr(run, "href", link->target);
        dom::style(run, "color", rgba(fLinkColour));
        if (link->pill) {
          dom::attr(run, "class", "skiff-link-pill");
          dom::style(run, "background-color", rgba((fLinkColour & 0x00ffffffu) | 0x30000000u));
          dom::style(run, "border-radius", "6px");
          dom::style(run, "padding", "0 4px");
        }
        if (link->picture) words = link->plain;
      }
      for (const Styled &one : fStyles) {
        if (!(one.first <= from && to <= one.last)) continue;
        if (one.strong) dom::style(run, "font-weight", "600");
        if (one.emphasis) dom::style(run, "font-style", "italic");
        if (one.struck || one.underline)
          dom::style(run, "text-decoration", std::string(one.struck ? "line-through" : "") + (one.struck && one.underline ? " " : "") + (one.underline ? "underline" : ""));
        if (one.code) {
          dom::style(run, "font-family", "ui-monospace, monospace");
          dom::style(run, "background-color", "rgba(255,255,255,0.08)");
          dom::style(run, "border-radius", "3px");
          if (one.block) dom::style(run, "display", "block");
        }
        if (one.quote) {
          dom::style(run, "color", rgba(fQuoteColour));
          if (one.first == from) {
            dom::style(run, "border-left", "2px solid " + rgba(fQuoteColour));
            dom::style(run, "padding-left", "6px");
          }
        }
        if (one.marked) dom::style(run, "background-color", rgba((fQuoteColour & 0x00ffffffu) | 0x40000000u));
        if (one.spoiler) {
          dom::attr(run, "class", "skiff-spoiler");
          dom::style(run, "background-color", "rgba(128,128,128,0.6)");
          dom::style(run, "color", "transparent");
        }
      }
      dom::text(run, words);
      dom::append(h, run);
      fSpans.push_back(run);
    }
  }
  void setText(std::string text) {
    if (text != fText) {
      fText = std::move(text);
      markDirty();
    }
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }
  void setColour(scene::Colour colour) {
    fColour = colour;
    markDirty();
  }
  [[nodiscard]] scene::Colour colour() const noexcept { return fColour.value_or(0xffffffffu); }
  void setFontSize(float size) {
    fSize = size;
    markDirty();
  }
  void setSize(float size) { setFontSize(size); }
  void setBold(bool bold) {
    if (bold != fBold) {
      fBold = bold;
      markDirty();
    }
  }
  [[nodiscard]] bool bold() const noexcept { return fBold; }
  void setMonospace(bool monospace) {
    fMonospace = monospace;
    markDirty();
  }
  void setSelectable(bool selectable) {
    fSelectable = selectable;
    markDirty();
  }
  void setSelectionColour(scene::Colour) {}
  void setStyles(std::vector<Styled> styles, scene::Colour quoteColour) {
    fStyles = std::move(styles);
    fQuoteColour = quoteColour;
    markDirty();
  }
  [[nodiscard]] const std::vector<Styled> &styles() const noexcept { return fStyles; }
  void setLinks(std::vector<Link> links, scene::Colour colour) {
    fLinks = std::move(links);
    fLinkColour = colour;
    markDirty();
  }
  [[nodiscard]] const std::vector<Link> &links() const noexcept { return fLinks; }
  void setMaxWidth(float width) {
    fMaxWidth = width;
    markDirty();
  }
  void setShrinksToLines(bool) {}
  void setWrapped(bool wrapped) {
    fWrapped = wrapped;
    markDirty();
  }
  void setElided(bool elided) {
    fElided = elided;
    markDirty();
  }
  void setMaxLines(std::optional<int> lines) {
    fMaxLines = lines;
    markDirty();
  }
  template <class Pictures> void setPictures(Pictures) {}

private:
  std::string fText;
  std::optional<float> fSize;
  std::optional<scene::Colour> fColour;
  bool fBold = false;
  bool fSelectable = false;
  bool fMonospace = false;
  bool fWrapped = true;
  bool fElided = false;
  std::optional<int> fMaxLines;
  float fMaxWidth = 0.0f;
  std::vector<Styled> fStyles;
  std::vector<Link> fLinks;
  scene::Colour fQuoteColour = 0xff8ab4f8u;
  scene::Colour fLinkColour = 0xff8ab4f8u;
  std::vector<dom::Handle> fSpans;
};

// A canvas: an element a node draws on, through a painter, when it is
// synced -- the node's own paint(painter) (not `draw`, which is the scene's
// own hook on every node), as a Box draws itself on the scene. The painter's words are the scene's painter's too (skiff.nodes'
// Canvas), so a drawing is written once for either. A click or a context
// menu on it comes to the node as pressed(Press) -- where, which button --
// and then as its press (onPress), as any control's.
class CanvasPainter {
public:
  // What was drawn stays until drawn over: a node may draw only what changed.
  static constexpr bool kRetains = true;
  explicit CanvasPainter(dom::Handle canvas) : fCanvas(canvas) {}
  void clear(scene::Colour colour) { dom::canvasClear(fCanvas, colour); }
  void fillPolygon(std::span<const scene::Point> points, scene::Colour colour) {
    path(points);
    dom::canvasFill(fCanvas, colour);
  }
  void strokePolygon(std::span<const scene::Point> points, float width, scene::Colour colour) {
    path(points);
    dom::canvasStroke(fCanvas, width, colour);
  }
  void line(scene::Point a, scene::Point b, float width, scene::Colour colour) {
    dom::canvasBegin(fCanvas);
    dom::canvasMoveTo(fCanvas, a.fX, a.fY);
    dom::canvasLineTo(fCanvas, b.fX, b.fY);
    dom::canvasStroke(fCanvas, width, colour);
  }
  void circle(scene::Point centre, float radius, scene::Colour colour) { dom::canvasCircle(fCanvas, centre.fX, centre.fY, radius, colour); }
  void text(scene::Point at, std::string_view text, float size, scene::Colour colour) { dom::canvasText(fCanvas, at.fX, at.fY, size, colour, text); }

private:
  void path(std::span<const scene::Point> points) {
    dom::canvasBegin(fCanvas);
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (i == 0) dom::canvasMoveTo(fCanvas, points[i].fX, points[i].fY);
      else dom::canvasLineTo(fCanvas, points[i].fX, points[i].fY);
    }
    dom::canvasClose(fCanvas);
  }
  dom::Handle fCanvas;
};
struct Canvas : Node {
  struct Press {
    scene::Point at;
    int button = 0;  // 0 the main, 2 the secondary, as the document numbers them
  };
  [[nodiscard]] static constexpr std::string_view tag() { return "canvas"; }
  void mounted() {
    dom::attr(fState.fHandle, "class", "skiff-canvas");
    dom::listen(fState.fHandle, "click");
    dom::listen(fState.fHandle, "contextmenu");
    fResize = true;
  }
  void sync(this auto &self) {
    if (self.fResize) {
      dom::canvasSize(self.fState.fHandle, self.fWidth, self.fHeight);
      self.fResize = false;
    }
    CanvasPainter painter(self.fState.fHandle);
    self.paint(painter);
  }
  // What the document sent for a pointer event: "x,y,button".
  [[nodiscard]] static std::optional<Press> pressOf(std::string_view value) {
    Press press;
    float *fields[] = {&press.at.fX, &press.at.fY};
    std::size_t at = 0;
    for (float *f : fields) {
      const std::size_t comma = value.find(',', at);
      if (comma == std::string_view::npos) return std::nullopt;
      if (std::from_chars(value.data() + at, value.data() + comma, *f).ec != std::errc{}) return std::nullopt;
      at = comma + 1;
    }
    if (std::from_chars(value.data() + at, value.data() + value.size(), press.button).ec != std::errc{}) return std::nullopt;
    return press;
  }
  [[nodiscard]] bool onDom(this auto &self, dom::Handle, std::string_view event, std::string_view value) {
    if (event != "click" && event != "contextmenu") return false;
    const std::optional<Press> press = pressOf(value);
    if (!press) return false;
    if constexpr (requires { self.pressed(*press); }) {
      self.pressed(*press);
      return true;
    }
    return false;
  }
  void setSize(int width, int height) {
    if (width != fWidth || height != fHeight) {
      fWidth = width;
      fHeight = height;
      fResize = true;
      markDirty();
    }
  }
  [[nodiscard]] int width() const noexcept { return fWidth; }
  [[nodiscard]] int height() const noexcept { return fHeight; }
  void redraw() { markDirty(); }

  int fWidth = 1, fHeight = 1;
  bool fResize = true;
};

// A stack: a flex box of its parts, down or across, with a gap between them.
struct Look {
  bool fHorizontal = false;
  float fGap = 0.0f;
  scene::Spec fSpec{};
};
[[nodiscard]] inline Look vbox(float gap = 0.0f, scene::Spec spec = {}) { return {false, gap, std::move(spec)}; }
[[nodiscard]] inline Look hbox(float gap = 0.0f, scene::Spec spec = {}) { return {true, gap, std::move(spec)}; }
template <class... Parts> struct Stack : Node {
  std::tuple<Parts...> fParts;
  Look fLook;
  explicit Stack(Look look, Parts... parts) : fParts(std::move(parts)...), fLook(std::move(look)) {
    fState.fDisplay = "flex";
    fState.apply(fLook.fSpec);
  }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
  void mounted() {
    dom::attr(fState.fHandle, "class", "skiff-stack");
    dom::style(fState.fHandle, "display", "flex");
    dom::style(fState.fHandle, "flex-direction", fLook.fHorizontal ? "row" : "column");
    if (fLook.fGap > 0.0f) dom::style(fState.fHandle, "gap", px(fLook.fGap));
    // Across its way, a child is its own size -- as in a scene's flow -- unless it fills or grows there.
    dom::style(fState.fHandle, "align-items", fLook.fHorizontal ? "center" : "flex-start");
  }
  void setGap(float gap) {
    fLook.fGap = gap;
    if (fState.fHandle != dom::kNone) dom::style(fState.fHandle, "gap", gap > 0.0f ? px(gap) : std::string());
  }
  void setHorizontal() {
    fLook.fHorizontal = true;
    if (fState.fHandle != dom::kNone) dom::style(fState.fHandle, "flex-direction", "row");
  }
};
template <class... Parts> [[nodiscard]] Stack<Parts...> column(Look look, Parts... parts) {
  if (look.fHorizontal) look.fHorizontal = false;
  return Stack<Parts...>(std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires(sizeof...(Parts) > 0 && !std::same_as<std::tuple_element_t<0, std::tuple<Parts...>>, Look>)
[[nodiscard]] Stack<Parts...> column(Parts... parts) {
  return Stack<Parts...>(vbox(), std::move(parts)...);
}
template <class... Parts> [[nodiscard]] Stack<Parts...> row(Look look, Parts... parts) {
  look.fHorizontal = true;
  return Stack<Parts...>(std::move(look), std::move(parts)...);
}
template <class... Parts>
  requires(sizeof...(Parts) > 0 && !std::same_as<std::tuple_element_t<0, std::tuple<Parts...>>, Look>)
[[nodiscard]] Stack<Parts...> row(Parts... parts) {
  return Stack<Parts...>(hbox(), std::move(parts)...);
}

// ---- flows, scrolling, pictures, icons ------------------------------------------

[[nodiscard]] inline std::string_view justified(const scene::Justify &how) {
  return spl::visit(spl::overloaded{[](const scene::justify::start &) { return std::string_view("flex-start"); },
                                    [](const scene::justify::middle &) { return std::string_view("center"); },
                                    [](const scene::justify::end &) { return std::string_view("flex-end"); },
                                    [](const scene::justify::space_between &) { return std::string_view("space-between"); },
                                    [](const scene::justify::space_around &) { return std::string_view("space-around"); }},
                    how);
}

// A flow: its children along a way, wrapped at its edge where they do not
// fit, spaced, aligned across the way and justified along it -- the
// browser's flexbox, told by the scene's FlowOptions.
template <class... Children> struct Flow : Node {
  std::tuple<Children...> fParts;
  scene::FlowOptions fOptions;
  Look fLook;  // its way, for its children: what a grow means in it
  explicit Flow(scene::FlowOptions options, Children... children) : fParts(std::move(children)...), fOptions(options) {
    fState.fDisplay = "flex";
    fLook.fHorizontal = spl::holds_alternative<scene::direction::horizontal>(fOptions.direction);
  }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
  void setOptions(scene::FlowOptions options) {
    fOptions = options;
    fLook.fHorizontal = spl::holds_alternative<scene::direction::horizontal>(fOptions.direction);
    markDirty();
  }
  void setSpacing(float x, float y) {
    fOptions.spacingX = x;
    fOptions.spacingY = y;
    markDirty();
  }
  void setWrap(bool wrap) {
    fOptions.wrap = wrap;
    markDirty();
  }
  void setCrossAlign(scene::Align at) {
    fOptions.crossAlign = at;
    markDirty();
  }
  void setJustify(scene::Justify how) {
    fOptions.justify = how;
    markDirty();
  }
  void mounted() { dom::attr(fState.fHandle, "class", "skiff-flow"); }
  void sync() {
    const dom::Handle h = fState.fHandle;
    dom::style(h, "flex-direction", fLook.fHorizontal ? "row" : "column");
    dom::style(h, "flex-wrap", fOptions.wrap ? "wrap" : "nowrap");
    dom::style(h, "column-gap", px(fOptions.spacingX));
    dom::style(h, "row-gap", px(fOptions.spacingY));
    dom::style(h, "align-items", std::string(alignment(fOptions.crossAlign)));
    dom::style(h, "justify-content", fOptions.centreRows ? std::string("center") : std::string(justified(fOptions.justify)));
    dom::style(h, "position", "relative");
  }
};
template <class... Children> Flow(scene::FlowOptions, Children...) -> Flow<Children...>;

// A scroll container: its children in a column -- or a row -- scrolled by
// the browser, the offset it is at read back from its scroll events; told
// to scroll to an offset or to its end. The scene's names, for a page
// written against them; what the browser does by itself is nothing here.
template <class... Children> struct ScrollContainer : Node {
  std::tuple<Children...> fParts;
  Look fLook;
  explicit ScrollContainer(Children... children) : fParts(std::move(children)...) { fState.fDisplay = "flex"; }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
  void setHorizontal() {
    fLook.fHorizontal = true;
    markDirty();
  }
  void mounted() {
    dom::attr(fState.fHandle, "class", "skiff-scroll");
    dom::listen(fState.fHandle, "scroll");
  }
  void sync() {
    const dom::Handle h = fState.fHandle;
    dom::style(h, "flex-direction", fLook.fHorizontal ? "row" : "column");
    dom::style(h, "overflow-x", fLook.fHorizontal ? "auto" : "hidden");
    dom::style(h, "overflow-y", fLook.fHorizontal ? "hidden" : "auto");
    dom::style(h, "position", "relative");
    if (fWanted) {
      dom::scrollTo(h, *fWanted);
      fWanted.reset();
    }
  }
  void scrollTo(float offset) {
    fWanted = offset;
    fCurrent = offset;
    markDirty();
  }
  void setCurrent(float offset) { scrollTo(offset); }
  void seeAt(float offset) { scrollTo(offset); }
  void scrollToStart() { scrollTo(0.0f); }
  void scrollToEnd(bool = true) {
    fWanted = -1.0f;
    markDirty();
  }
  [[nodiscard]] float current() const noexcept { return fCurrent; }
  [[nodiscard]] float extent() const noexcept { return fExtent; }
  [[nodiscard]] bool moving() const noexcept { return false; }
  [[nodiscard]] bool atRest() const noexcept { return true; }
  [[nodiscard]] bool glidingToEnd() const noexcept { return fWanted && *fWanted < 0.0f; }
  void setCopiesOnScroll(bool) {}
  void setHoldsInView(bool) {}
  void setDrawsAhead(bool) {}
  // Scrolled by the user: where it is now -- not a press.
  bool onDom(dom::Handle, std::string_view event, std::string_view value) {
    if (event != "scroll") return false;
    fCurrent = std::strtof(std::string(value).c_str(), nullptr);
    return false;
  }
  std::optional<float> fWanted;
  float fCurrent = 0.0f;
  float fExtent = 0.0f;
};
template <class... Children> ScrollContainer(Children...) -> ScrollContainer<Children...>;

// A picture's source: what has it, asked as the page is synced -- a Picture
// where it is, nothing where it is not (yet).
template <class Source>
concept PictureSource = std::copy_constructible<Source> && requires(const Source &source) {
  { source() } -> std::convertible_to<const scene::Picture *>;
};
// A picture in its box, fitting it as said (object-fit): an img, its source
// asked at every sync until it has one.
template <PictureSource Source> struct Image : Node {
  explicit Image(Source source, scene::Fit how = scene::fit::cover{}) : fSource(std::move(source)), fFit(how) {}
  [[nodiscard]] static constexpr std::string_view tag() { return "img"; }
  void setSource(Source source) {
    fSource = std::move(source);
    markDirty();
  }
  void keepBox(bool kept = true) { fBoxKept = kept; }
  [[nodiscard]] const Source &source() const { return fSource; }
  [[nodiscard]] const scene::Picture *picture() const {
    const scene::Picture *found = fSource();
    return found && !found->url.empty() ? found : nullptr;
  }
  [[nodiscard]] bool wantsSync() const { return fShown.empty(); }
  void mounted() { dom::attr(fState.fHandle, "class", "skiff-image"); }
  void sync() {
    const scene::Picture *found = picture();
    const std::string url = found ? found->url : std::string();
    if (url != fShown) {
      fShown = url;
      if (url.empty()) dom::dropAttr(fState.fHandle, "src");
      else dom::attr(fState.fHandle, "src", url);
    }
    dom::style(fState.fHandle, "object-fit", spl::holds_alternative<scene::fit::contain>(fFit) ? "contain" : "cover");
  }

private:
  Source fSource;
  scene::Fit fFit;
  std::string fShown;
  bool fBoxKept = false;
};

// An icon: its marks as an SVG's elements about the middle of its box (the
// viewBox is centred, as the scene draws them), in the icon's colour or a
// mark's own; rotated about the middle.
struct Icon : Node {
  explicit Icon(scene::IconShape shape, scene::Colour colour = 0xffffffffu, scene::Spec spec = {}) : fShape(std::move(shape)), fColour(colour) {
    fState.apply(spec);
  }
  [[nodiscard]] static constexpr std::string_view tag() { return "svg"; }
  void setShape(scene::IconShape shape) {
    fShape = std::move(shape);
    markDirty();
  }
  void setColour(scene::Colour colour) {
    if (colour != fColour) {
      fColour = colour;
      markDirty();
    }
  }
  [[nodiscard]] scene::Colour colour() const noexcept { return fColour; }
  void mounted() {
    dom::attr(fState.fHandle, "class", "skiff-icon");
    fGroup = dom::create("g");
    dom::append(fState.fHandle, fGroup);
  }
  void sync() {
    const float w = fState.fSpec.width.value_or(24.0f), h = fState.fSpec.height.value_or(24.0f);
    dom::attr(fState.fHandle, "width", number(w));
    dom::attr(fState.fHandle, "height", number(h));
    dom::attr(fState.fHandle, "viewBox", number(-w / 2.0f) + " " + number(-h / 2.0f) + " " + number(w) + " " + number(h));
    if (fShape.rotation != 0.0f) dom::attr(fGroup, "transform", "rotate(" + number(fShape.rotation) + ")");
    else dom::dropAttr(fGroup, "transform");
    for (const dom::Handle old : fMarks) dom::destroy(old);
    fMarks.clear();
    for (const scene::Mark &one : fShape.marks) {
      const dom::Handle made = spl::visit([&](const auto &shape) { return markElement(shape); }, one.shape);
      const std::string colour = rgba(one.colour.value_or(fColour));
      if (one.filled) {
        dom::attr(made, "fill", colour);
        dom::attr(made, "stroke", "none");
      } else {
        dom::attr(made, "fill", "none");
        dom::attr(made, "stroke", colour);
        dom::attr(made, "stroke-width", number(one.width));
        dom::attr(made, "stroke-linecap", "round");
        dom::attr(made, "stroke-linejoin", "round");
      }
      dom::append(fGroup, made);
      fMarks.push_back(made);
    }
  }

private:
  static dom::Handle markElement(const scene::mark::line &one) {
    const dom::Handle h = dom::create("line");
    dom::attr(h, "x1", number(one.x1));
    dom::attr(h, "y1", number(one.y1));
    dom::attr(h, "x2", number(one.x2));
    dom::attr(h, "y2", number(one.y2));
    return h;
  }
  static dom::Handle markElement(const scene::mark::circle &one) {
    const dom::Handle h = dom::create("circle");
    dom::attr(h, "cx", number(one.x));
    dom::attr(h, "cy", number(one.y));
    dom::attr(h, "r", number(one.radius));
    return h;
  }
  static dom::Handle markElement(const scene::mark::rect &one) {
    const dom::Handle h = dom::create("rect");
    dom::attr(h, "x", number(one.left));
    dom::attr(h, "y", number(one.top));
    dom::attr(h, "width", number(one.right - one.left));
    dom::attr(h, "height", number(one.bottom - one.top));
    if (one.radius > 0.0f) {
      dom::attr(h, "rx", number(one.radius));
      dom::attr(h, "ry", number(one.radius));
    }
    return h;
  }
  // An arc of the oval in its box, from a start angle through a sweep (degrees, clockwise), as Skia draws one.
  static dom::Handle markElement(const scene::mark::arc &one) {
    const float rx = (one.right - one.left) / 2.0f, ry = (one.bottom - one.top) / 2.0f;
    const float cx = (one.left + one.right) / 2.0f, cy = (one.top + one.bottom) / 2.0f;
    const float sweep = std::clamp(one.sweep, -359.99f, 359.99f);
    const float toRadians = std::numbers::pi_v<float> / 180.0f;
    const float a0 = one.start * toRadians, a1 = (one.start + sweep) * toRadians;
    const dom::Handle h = dom::create("path");
    dom::attr(h, "d",
              "M" + number(cx + rx * std::cos(a0)) + " " + number(cy + ry * std::sin(a0)) + " A" + number(rx) + " " + number(ry) + " 0 " +
                  (std::abs(sweep) > 180.0f ? "1" : "0") + " " + (sweep > 0.0f ? "1" : "0") + " " + number(cx + rx * std::cos(a1)) + " " +
                  number(cy + ry * std::sin(a1)));
    return h;
  }
  static dom::Handle markElement(const scene::mark::path &one) {
    std::string d;
    for (const scene::PathStep &step : one.steps) {
      spl::visit(spl::overloaded{[&](const scene::path_step::move &at) { d += "M" + number(at.x) + " " + number(at.y); },
                                 [&](const scene::path_step::line &at) { d += "L" + number(at.x) + " " + number(at.y); },
                                 [&](const scene::path_step::cubic &at) {
                                   d += "C" + number(at.x1) + " " + number(at.y1) + " " + number(at.x2) + " " + number(at.y2) + " " + number(at.x) +
                                        " " + number(at.y);
                                 },
                                 [&](const scene::path_step::close &) { d += "Z"; }},
                 step);
    }
    const dom::Handle h = dom::create("path");
    dom::attr(h, "d", d);
    return h;
  }
  scene::IconShape fShape;
  scene::Colour fColour;
  dom::Handle fGroup = dom::kNone;
  std::vector<dom::Handle> fMarks;
};

// ---- figures: shapes as nodes ----------------------------------------------
//
// A figure is an <svg> of shapes, each a node of its own -- a polygon, a
// polyline, a line, a circle -- with the points, the fill, the stroke it has
// now, written to its element when they move; where the figure's stylesheet
// says so, the browser transitions them. A shape is bound to a model as any
// node is, and a list of them (group) is as many as the model has. The
// scene has the same shapes (skiff.nodes' Figure), drawn by Skia.
[[nodiscard]] inline std::string hexOf(scene::Colour c) {
  if (((c >> 24) & 0xffu) == 0) return "none";
  return std::format("#{:02x}{:02x}{:02x}", (c >> 16) & 0xffu, (c >> 8) & 0xffu, c & 0xffu);
}
[[nodiscard]] inline std::string pointsOf(std::span<const scene::Point> points) {
  std::string out;
  for (const scene::Point &p : points) out += number(p.fX) + "," + number(p.fY) + " ";
  if (!out.empty()) out.pop_back();
  return out;
}
// What every shape has: a fill, a stroke of a width, an opacity, a class
// for the stylesheet to know it by. Written as attributes, which the
// stylesheet's transitions apply to as they change.
struct Shape : Node {
  scene::Colour fFill = 0xffffffffu, fStroke = 0;
  float fStrokeWidth = 0.0f, fOpacity = 1.0f;
  std::string fClass;
  void setFill(scene::Colour fill) { change(fFill, fill); }
  void setStroke(scene::Colour stroke, float width) {
    change(fStroke, stroke);
    change(fStrokeWidth, width);
  }
  void setOpacity(float opacity) { change(fOpacity, opacity); }
  void setClass(std::string name) { change(fClass, std::move(name)); }
  // A style of the shape's own, beyond its fill and stroke: an offset-path,
  // a dash, a transition -- written as given.
  void style(std::string name, std::string value) {
    for (auto &[n, v] : fStyles)
      if (n == name) {
        if (v != value) {
          v = std::move(value);
          markDirty();
        }
        return;
      }
    fStyles.emplace_back(std::move(name), std::move(value));
    markDirty();
  }
  std::vector<std::pair<std::string, std::string>> fStyles;
  void syncShape() {
    for (const auto &[n, v] : fStyles) dom::style(fState.fHandle, n, v);
    dom::attr(fState.fHandle, "fill", hexOf(fFill));
    dom::attr(fState.fHandle, "stroke", hexOf(fStroke));
    dom::attr(fState.fHandle, "stroke-width", number(fStrokeWidth));
    if (fOpacity < 1.0f) dom::attr(fState.fHandle, "opacity", number(fOpacity));
    else dom::dropAttr(fState.fHandle, "opacity");
    if (!fClass.empty()) dom::attr(fState.fHandle, "class", fClass);
    else dom::dropAttr(fState.fHandle, "class");
  }
  template <class V> void change(V &field, V value) {
    if (field != value) {
      field = std::move(value);
      markDirty();
    }
  }
};
class Polygon : public Shape {
public:
  explicit Polygon(std::vector<scene::Point> points = {}, scene::Colour fill = 0xffffffffu) : fPoints(std::move(points)) { fFill = fill; }
  [[nodiscard]] static constexpr std::string_view tag() { return "polygon"; }
  void setPoints(std::vector<scene::Point> points) { change(fPoints, std::move(points)); }
  [[nodiscard]] const std::vector<scene::Point> &points() const noexcept { return fPoints; }
  void sync() {
    dom::attr(fState.fHandle, "points", pointsOf(fPoints));
    syncShape();
  }

private:
  std::vector<scene::Point> fPoints;
};
class Polyline : public Shape {
public:
  explicit Polyline(std::vector<scene::Point> points = {}, scene::Colour stroke = 0xffffffffu, float width = 1.0f) : fPoints(std::move(points)) {
    fFill = 0;
    fStroke = stroke;
    fStrokeWidth = width;
  }
  [[nodiscard]] static constexpr std::string_view tag() { return "polyline"; }
  void setPoints(std::vector<scene::Point> points) { change(fPoints, std::move(points)); }
  [[nodiscard]] const std::vector<scene::Point> &points() const noexcept { return fPoints; }
  void sync() {
    dom::attr(fState.fHandle, "points", pointsOf(fPoints));
    syncShape();
  }

private:
  std::vector<scene::Point> fPoints;
};
class Line : public Shape {
public:
  Line(scene::Point a = {}, scene::Point b = {}, scene::Colour stroke = 0xffffffffu, float width = 1.0f) : fA(a), fB(b) {
    fFill = 0;
    fStroke = stroke;
    fStrokeWidth = width;
  }
  [[nodiscard]] static constexpr std::string_view tag() { return "line"; }
  void setEnds(scene::Point a, scene::Point b) {
    if (a.fX != fA.fX || a.fY != fA.fY || b.fX != fB.fX || b.fY != fB.fY) {
      fA = a;
      fB = b;
      markDirty();
    }
  }
  void sync() {
    dom::attr(fState.fHandle, "x1", number(fA.fX));
    dom::attr(fState.fHandle, "y1", number(fA.fY));
    dom::attr(fState.fHandle, "x2", number(fB.fX));
    dom::attr(fState.fHandle, "y2", number(fB.fY));
    syncShape();
  }

private:
  scene::Point fA, fB;
};
class Circle : public Shape {
public:
  Circle(scene::Point centre = {}, float radius = 1.0f, scene::Colour fill = 0xffffffffu) : fCentre(centre), fRadius(radius) { fFill = fill; }
  [[nodiscard]] static constexpr std::string_view tag() { return "circle"; }
  void setCentre(scene::Point centre) {
    if (centre.fX != fCentre.fX || centre.fY != fCentre.fY) {
      fCentre = centre;
      markDirty();
    }
  }
  void setRadius(float radius) { change(fRadius, radius); }
  [[nodiscard]] scene::Point centre() const noexcept { return fCentre; }
  void sync() {
    dom::attr(fState.fHandle, "cx", number(fCentre.fX));
    dom::attr(fState.fHandle, "cy", number(fCentre.fY));
    dom::attr(fState.fHandle, "r", number(fRadius));
    syncShape();
  }

private:
  scene::Point fCentre;
  float fRadius;
};
// A layer: shapes together as one node -- an SVG group of its parts -- for
// a row of a list that is more than one shape, or for shapes shown and
// hidden together.
template <class... Parts> struct Layer : Node {
  std::tuple<Parts...> fParts;
  explicit Layer(Parts... parts) : fParts(std::move(parts)...) {}
  [[nodiscard]] static constexpr std::string_view tag() { return "g"; }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
};
template <class... Parts> [[nodiscard]] Layer<Parts...> layer(Parts... parts) { return Layer<Parts...>(std::move(parts)...); }

// The figure: an svg of its parts, of a size in CSS pixels, its own
// coordinates the same.
template <class... Parts> struct Figure : Node {
  using Press = Canvas::Press;  // where and which button, as a Canvas has it
  std::tuple<Parts...> fParts;
  float fWidth, fHeight;
  Figure(float width, float height, Parts... parts) : fParts(std::move(parts)...), fWidth(width), fHeight(height) {}
  [[nodiscard]] static constexpr std::string_view tag() { return "svg"; }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
  void setSize(float width, float height) {
    if (width != fWidth || height != fHeight) {
      fWidth = width;
      fHeight = height;
      markDirty();
    }
  }
  // Its class, for a stylesheet to know it by: "skiff-figure" and what the figure adds.
  void setClass(std::string name) {
    if (name != fClass) {
      fClass = std::move(name);
      markDirty();
    }
  }
  std::string fClass = "skiff-figure";
  void mounted() {
    dom::attr(fState.fHandle, "class", fClass);
    dom::listen(fState.fHandle, "click");
    dom::listen(fState.fHandle, "contextmenu");
  }
  // A click or a context menu on the figure: pressed(Press) on the figure
  // that has it -- where, in its own coordinates, and which button -- then
  // its press (onPress), as a Canvas has it.
  [[nodiscard]] bool onDom(this auto &self, dom::Handle, std::string_view event, std::string_view value) {
    if (event != "click" && event != "contextmenu") return false;
    const std::optional<Canvas::Press> press = Canvas::pressOf(value);
    if (!press) return false;
    if constexpr (requires { self.pressed(*press); }) {
      self.pressed(*press);
      return true;
    }
    return false;
  }
  void sync() {
    dom::attr(fState.fHandle, "class", fClass);
    dom::attr(fState.fHandle, "width", number(fWidth));
    dom::attr(fState.fHandle, "height", number(fHeight));
    dom::attr(fState.fHandle, "viewBox", "0 0 " + number(fWidth) + " " + number(fHeight));
  }
};
template <class... Parts> [[nodiscard]] Figure<Parts...> figure(float width, float height, Parts... parts) {
  return Figure<Parts...>(width, height, std::move(parts)...);
}

// ---- a list's rows ------------------------------------------------------------
//
// A row per element of a model's list (model::Keyed), made by the make given
// as the elements come, kept while they stay, gone with them: skiff.bind's
// Each, with a container of the page's own. The rows are its children, as
// the list has them now; the page mounts the new ones and keeps them in the
// list's order.
struct Rows : Node {
  Look fLook;
  std::string_view fTag = "div";
  bool fFlex = true;
  std::vector<dom::Handle> fShownRows;  // the rows' elements as last appended, in order
  explicit Rows(Look look, std::string_view tag = "div", bool flex = true) : fLook(std::move(look)), fTag(tag), fFlex(flex) {
    if (fFlex) fState.fDisplay = "flex";
    fState.apply(fLook.fSpec);
  }
  [[nodiscard]] std::string_view tag() const noexcept { return fTag; }
  void forEachChild(this auto &self, auto &&f) {
    if constexpr (requires { self.fRows; }) f(self.fRows);
  }
  void mounted() {
    if (!fFlex) return;
    dom::attr(fState.fHandle, "class", "skiff-stack");
    dom::style(fState.fHandle, "display", "flex");
    dom::style(fState.fHandle, "flex-direction", fLook.fHorizontal ? "row" : "column");
    if (fLook.fGap > 0.0f) dom::style(fState.fHandle, "gap", px(fLook.fGap));
    // Across its way, a child is its own size -- as in a scene's flow -- unless it fills or grows there.
    dom::style(fState.fHandle, "align-items", fLook.fHorizontal ? "center" : "flex-start");
  }
};
namespace detail {
// How a row is made for an element: from the element where the make takes
// one, else from nothing. As skiff.compose has it.
template <class T, class F> struct MakeRow {
  F fMake;
  auto operator()(const T &value) const
    requires std::invocable<const F &, const T &>
  {
    return fMake(value);
  }
  auto operator()(const T &) const
    requires(!std::invocable<const F &, const T &>)
  {
    return fMake();
  }
};
template <class T, class F> using RowOf = std::invoke_result_t<const MakeRow<T, F> &, const T &>;
} // namespace detail
// The rows of a list as a stack, down (vbox) or across.
template <class Key, class T, class F> [[nodiscard]] auto each(F make, Look look = vbox()) {
  using Row = detail::RowOf<T, F>;
  return bind::Each<Key, T, Row, Rows, detail::MakeRow<T, F>>(detail::MakeRow<T, F>{std::move(make)}, Rows(std::move(look)));
}
// The rows of a list as an SVG group: for the shapes of a figure.
template <class Key, class T, class F> [[nodiscard]] auto group(F make) {
  using Row = detail::RowOf<T, F>;
  return bind::Each<Key, T, Row, Rows, detail::MakeRow<T, F>>(detail::MakeRow<T, F>{std::move(make)}, Rows(vbox(), "g", false));
}

// ---- the model's kinds, around a node --------------------------------------
//
// As skiff.compose spells them for a scene, for a page: the kinds themselves
// are skiff.bind's, written once for every backend.

template <class Want, class N>
  requires std::move_constructible<N>
[[nodiscard]] bind::Bound<Want, N> bound(N node) {
  return bind::Bound<Want, N>(std::move(node));
}
template <class Within, class Handlers, class N, class... Keys>
[[nodiscard]] bind::Scoped<Within, Handlers, N, Keys...> scoped(Handlers handlers, N node, Keys... keys) {
  return bind::Scoped<Within, Handlers, N, Keys...>(std::move(handlers), std::move(node), std::move(keys)...);
}
template <class T, class Handlers, class N>
[[nodiscard]] bind::Local<T, Handlers, N> local(Handlers handlers, N node, T initial = {}) {
  return bind::Local<T, Handlers, N>(std::move(handlers), std::move(node), std::move(initial));
}
// A node with its spec applied: a leaf said as it is put in its place.
template <class N>
  requires std::derived_from<N, Node>
[[nodiscard]] N styled(scene::Spec spec, N node) {
  node.apply(spec);
  return node;
}
// A node read through a function of the part: what the part is, made into
// what the node reads. As skiff.compose spells it for a scene.
template <class Compute, class N> struct Projected : N {
  Compute fCompute;
  Projected(Compute compute, N node) : N(std::move(node)), fCompute(std::move(compute)) {}
  template <class T> void read(const T &value) { N::read(fCompute(value)); }
};
template <class Want, class Compute, class N>
[[nodiscard]] auto projected(Compute compute, N node) {
  return bound<Want>(Projected<Compute, N>(std::move(compute), std::move(node)));
}
// A node whose Spec is read from the part: shown, sized, coloured by it.
template <class N> struct SpecOf : N {
  explicit SpecOf(N node) : N(std::move(node)) {}
  void read(const scene::Spec &spec) { this->apply(spec); }
};
template <class Want, class Compute, class N>
[[nodiscard]] auto spec_for(Compute compute, N node) {
  return projected<Want>(std::move(compute), SpecOf<N>(std::move(node)));
}
// A text read from the part: its words.
struct TextOf : Text {
  using Text::Text;
  explicit TextOf(Text text) : Text(std::move(text)) {}
  void read(const std::string &words) { this->setText(words); }
};
template <class Want, class Compute>
[[nodiscard]] auto text_for(Compute compute, Text text) {
  return projected<Want>(std::move(compute), TextOf(std::move(text)));
}
// A node reading what is computed from parts: computed again only where
// one of them moved (skiff.bind's Derived).
template <class... Reads, class Compute, class N>
[[nodiscard]] auto derived(Compute compute, N node) {
  return bind::Derived<Compute, N, Reads...>(std::move(node), std::move(compute));
}
// The scene's erasure of a node, where a page has none: the node itself.
template <class N> [[nodiscard]] N erased(N node) { return node; }
// A swipe the scene takes on a node: a page has no swipes of its own (the
// browser's are the browser's), so the node stands as it is.
template <class Act, class N> [[nodiscard]] N flicked(float, Act, N node) { return node; }

// What a scope answers an event with: the handler for E, given the event
// and the scope's place, either, or neither.
template <class E, class F> struct Handle {
  F fAnswer;
  template <class P>
    requires std::invocable<const F &, const E &, const P &>
  auto on(const E &event, const P &place) const {
    return fAnswer(event, place);
  }
  template <class P>
    requires(!std::invocable<const F &, const E &, const P &> && std::invocable<const F &, const P &>)
  auto on(const E &, const P &place) const {
    return fAnswer(place);
  }
  template <class P>
    requires(!std::invocable<const F &, const E &, const P &> && !std::invocable<const F &, const P &> && std::invocable<const F &, const E &>)
  auto on(const E &event, const P &) const {
    return fAnswer(event);
  }
  template <class P>
    requires(!std::invocable<const F &, const E &, const P &> && !std::invocable<const F &, const P &> && !std::invocable<const F &, const E &> &&
             std::invocable<const F &>)
  auto on(const E &, const P &) const {
    return fAnswer();
  }
};
template <class E, class F> [[nodiscard]] Handle<E, F> handle(F answer) { return {std::move(answer)}; }
template <class... H> struct Handlers : H... {
  using H::on...;
};
template <class... H> [[nodiscard]] Handlers<H...> handlers(H... each) { return {std::move(each)...}; }
// A part of the model kept by the view it is made over: a scope over it,
// with the handlers that answer what is sent within -- and the model the
// page is bound to holds it because the view says so (bind::ModelOf). As
// skiff.compose's.
template <class T, class Handlers, class N>
  requires(!std::derived_from<Handlers, Node>)
[[nodiscard]] bind::Stated<T, Handlers, N> state(Handlers handlers, N node) {
  return bind::Stated<T, Handlers, N>(std::move(handlers), std::move(node));
}
template <class T, class N>
  requires std::derived_from<N, Node>
[[nodiscard]] auto state(N node) {
  return state<T>(Handlers<>{}, std::move(node));
}

// A view named where it is written (SKIFF_NAMED): a type of its own over
// what the expression made, read, bound and walked the same, and written to
// the page under its name (data-view) so that the document reads as the
// source does. Its content's handlers are declared again at its level, as
// the binding's kinds declare them.
template <class Self, class Base> struct Named : Base {
  using Base::onKey;
  using Base::onPointer;
  using Base::onText;
  using Base::onSemantic;
  // What it names: where the parts of the model its views keep are found
  // (bind::RootOf).
  using NamedContent = Base;
  explicit Named(Base made) : Base(std::move(made)) {}
};

// ---- walking a page's tree, typed ---------------------------------------------
//
// A page's tree is written down whole, so the page walks it by type in every
// build -- where the core's walks, outside a release build, see each child
// through an AnyNodeRef. The children are counted as skiff.bind counts them
// along a path: a holder seen through, a node one.
namespace detail {
template <class F> void visitTyped(std::monostate &, F &&);
template <class... Ts, class F> void visitTyped(spl::variant<Ts...> &, F &&);
template <class... Ts, class F> void visitTyped(std::variant<Ts...> &, F &&);
template <class T, class F> void visitTyped(std::optional<T> &, F &&);
template <class T, class D, class F> void visitTyped(std::unique_ptr<T, D> &, F &&);
template <class T, class F> void visitTyped(std::shared_ptr<T> &, F &&);
template <class T, class F> void visitTyped(std::reference_wrapper<T> &, F &&);
template <class... Ts, class F> void visitTyped(std::tuple<Ts...> &, F &&);
template <class R, class F>
  requires(std::ranges::range<R> && !scene::kTreatAsNode<R>)
void visitTyped(R &, F &&);
template <class N, class F> void visitTyped(N &, F &&);

template <class F> void visitTyped(std::monostate &, F &&) {}
template <class... Ts, class F> void visitTyped(spl::variant<Ts...> &held, F &&f) {
  spl::visit([&](auto &one) { visitTyped(one, f); }, held);
}
template <class... Ts, class F> void visitTyped(std::variant<Ts...> &held, F &&f) {
  std::visit([&](auto &one) { visitTyped(one, f); }, held);
}
template <class T, class F> void visitTyped(std::optional<T> &held, F &&f) {
  if (held) visitTyped(*held, f);
}
template <class T, class D, class F> void visitTyped(std::unique_ptr<T, D> &held, F &&f) {
  if (held) visitTyped(*held, f);
}
template <class T, class F> void visitTyped(std::shared_ptr<T> &held, F &&f) {
  if (held) visitTyped(*held, f);
}
template <class T, class F> void visitTyped(std::reference_wrapper<T> &held, F &&f) { visitTyped(held.get(), f); }
template <class... Ts, class F> void visitTyped(std::tuple<Ts...> &held, F &&f) {
  std::apply([&](auto &...each) { (visitTyped(each, f), ...); }, held);
}
template <class R, class F>
  requires(std::ranges::range<R> && !scene::kTreatAsNode<R>)
void visitTyped(R &held, F &&f) {
  for (auto &each : held) visitTyped(each, f);
}
template <class N, class F> void visitTyped(N &node, F &&f) { f(node); }

template <class N, class F> void eachChild(N &node, F &&f) {
  node.forEachChild([&](auto &part) { visitTyped(part, f); });
}
template <class N, class F> bool childAt(N &node, std::uint32_t place, F &&f) {
  std::uint32_t at = 0;
  bool found = false;
  detail::eachChild(node, [&](auto &child) {
    if (!found && at++ == place) {
      found = true;
      f(child);
    }
  });
  return found;
}
} // namespace detail

// ---- the page -----------------------------------------------------------------
//
// A tree mounted into an element: each node made an element in eachChild
// order, so that the path to a node is the one skiff.bind delivers a press
// along. An event on an element goes to the node that made it (onDom), then
// down the tree as a press; what the press changed, the binding refreshes;
// what the refresh moved, sync() writes to the page.
template <class Root, class Model> class Page {
public:
  Page(Root root, Model &model) : fRoot(std::move(root)), fModel(&model) {}
  Page(const Page &) = delete;
  Page &operator=(const Page &) = delete;

  void mount(dom::Handle into = dom::root()) {
    mountNode(fRoot, into);
    fBinding.refresh(fRoot, *fModel);
    sync();
  }
  // A page written beforehand taken over -- the markup this tree made where
  // it was mounted natively and dumped: the tree read as the model says, then
  // walked over the document's elements in the order they were written, each
  // node taking its own and saying its listeners, nothing made again; then
  // written as the model says now. `first` is the place of the tree's root
  // among the parent's elements (after the stylesheets written before it). A
  // node with no nodes under it makes what it holds itself (a text's runs, a
  // control's parts): it is emptied and makes it again. An element not there
  // is made, as mount() would.
  void hydrate(dom::Handle into = dom::root(), std::size_t first = 0) {
    fBinding.refresh(fRoot, *fModel);
    hydrateNode(fRoot, into, first);
    sync();
  }
  // What the model says, written to the page: after an edit made elsewhere
  // than by the page's own events.
  void refresh() {
    fBinding.refresh(fRoot, *fModel);
    sync();
  }
  void sync() { syncNode(fRoot); }
  [[nodiscard]] Root &root() noexcept { return fRoot; }
  [[nodiscard]] const Root &root() const noexcept { return fRoot; }
  [[nodiscard]] Model &model() noexcept { return *fModel; }

  // An event on one of the page's elements: from the document's listener,
  // through the program's skiff_web_event; from a test, through web::fire.
  void event(dom::Handle target, std::string_view kind, std::string_view value) {
    // The node that answers on the element, found down the tree with the
    // path to it -- the one a press is delivered along. Walked rather than
    // kept, as a list's rows come and go.
    scene::Path path;
    bool press = false, found = false;
    findNode(fRoot, target, path, [&](auto &node) {
      found = true;
      press = node.onDom(target, kind, value);
    });
    if (!found) return;
    if (press) bind::press(fRoot, *fModel, path);
    refresh();
  }

private:
  template <class N> void mountNode(N &node, dom::Handle parent) {
    State &state = node.fState;
    state.fHandle = dom::create(node.tag());
    dom::append(parent, state.fHandle);
    // A named view is named in the document too: the page reads as the source does.
    if constexpr (requires { std::string_view(std::remove_cvref_t<N>::viewName()); })
      dom::attr(state.fHandle, "data-view", std::string_view(std::remove_cvref_t<N>::viewName()));
    node.mounted();
    detail::eachChild(node, [&](auto &child) {
      if constexpr (requires { node.fLook.fHorizontal; }) child.fState.fInRow = node.fLook.fHorizontal;
      mountNode(child, state.fHandle);
    });
  }
  template <class N> void hydrateNode(N &node, dom::Handle parent, std::size_t index) {
    State &state = node.fState;
    const dom::Handle found = dom::child(parent, index);
    if (found == dom::kNone) {
      mountNode(node, parent);
      return;
    }
    state.fHandle = found;
    std::size_t under = 0;
    detail::eachChild(node, [&](auto &) { ++under; });
    if (under == 0) dom::clear(found);
    node.mounted();
    std::size_t at = 0;
    detail::eachChild(node, [&](auto &child) {
      if constexpr (requires { node.fLook.fHorizontal; }) child.fState.fInRow = node.fLook.fHorizontal;
      hydrateNode(child, found, at++);
    });
    // A list's rows are the ones it shows now: not appended again.
    if constexpr (requires { node.fRows; node.fShownRows; }) {
      node.fShownRows.clear();
      for (auto &row : node.fRows) node.fShownRows.push_back(row.fState.fHandle);
    }
    state.fDirty = true;
  }
  template <class N> void syncNode(N &node) {
    State &state = node.fState;
    // A list's rows: the new ones mounted, all of them in the list's order.
    if constexpr (requires { node.fRows; node.fShownRows; }) {
      // The rows as they were, then new ones at the end: those are mounted
      // (appended) and that is all. Rows gone from the middle or moved: all
      // appended again, in the list's order.
      std::size_t same = 0;
      while (same < node.fShownRows.size() && same < node.fRows.size() && node.fShownRows[same] == node.fRows[same].fState.fHandle) ++same;
      const bool reorder = same != node.fShownRows.size();
      for (std::size_t i = same; i < node.fRows.size(); ++i) {
        auto &row = node.fRows[i];
        if (row.fState.fHandle == dom::kNone) {
          if constexpr (requires { node.fLook.fHorizontal; }) row.fState.fInRow = node.fLook.fHorizontal;
          mountNode(row, state.fHandle);
        }
        else if (!reorder) dom::append(state.fHandle, row.fState.fHandle);
      }
      if (reorder)
        for (auto &row : node.fRows) dom::append(state.fHandle, row.fState.fHandle);
      if (reorder || same != node.fRows.size()) {
        node.fShownRows.clear();
        for (auto &row : node.fRows) node.fShownRows.push_back(row.fState.fHandle);
      }
    }
    if (state.fDirty) {
      applyCss(state.fHandle, cssOf(state));
      if (state.fDisabled) dom::attr(state.fHandle, "disabled", "");
      else dom::dropAttr(state.fHandle, "disabled");
      node.sync();
      state.fDirty = false;
    } else if (node.wantsSync()) {
      node.sync();
    }
    detail::eachChild(node, [&](auto &child) { syncNode(child); });
  }
  template <class N, class F> static bool findNode(N &node, dom::Handle target, scene::Path &path, F &&f) {
    bool mine = false;
    node.handles([&](dom::Handle h) { mine = mine || h == target; });
    if (mine) {
      f(node);
      return true;
    }
    std::uint32_t place = 0;
    bool found = false;
    detail::eachChild(node, [&](auto &child) {
      if (found) return;
      path.push_back(place++);
      found = findNode(child, target, path, f);
      if (!found) path.pop_back();
    });
    return found;
  }
  template <class N, class F> static void atPath(N &node, const scene::Path &path, std::size_t at, F &&f) {
    if (at == path.size()) {
      f(node);
      return;
    }
    detail::childAt(node, path[at], [&](auto &child) { atPath(child, path, at + 1, f); });
  }

  Root fRoot;
  Model *fModel;
  bind::Binding<Model> fBinding;
};
template <class Root, class Model> Page(Root, Model &) -> Page<Root, Model>;

// An event made by hand and delivered: what a test does, where no document
// is listening -- the element done to as the document would have, then the
// page told, as its listener would have told it.
template <class Root, class Model>
void fire(Page<Root, Model> &page, dom::Handle target, std::string_view event, std::string_view value) {
  dom::fire(target, event, value);
  page.event(target, event, value);
}

} // namespace skiff::web
