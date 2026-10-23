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
  void set(std::string property, std::string value) {
    for (auto &[p, v] : fRules)
      if (p == property) {
        v = std::move(value);
        return;
      }
    fRules.emplace_back(std::move(property), std::move(value));
  }
};

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

  // The element is the state's: it goes with a move and with the node (a row
  // gone from a list takes its element out of the page); a copy has none.
  State() = default;
  State(const State &other) : fSpec(other.fSpec), fVisible(other.fVisible), fSelected(other.fSelected), fDisabled(other.fDisabled), fDisplay(other.fDisplay) {}
  State &operator=(const State &other) {
    if (this != &other) {
      drop();
      fSpec = other.fSpec;
      fVisible = other.fVisible;
      fSelected = other.fSelected;
      fDisabled = other.fDisabled;
      fDisplay = other.fDisplay;
      fDirty = true;
    }
    return *this;
  }
  State(State &&other) noexcept
      : fSpec(std::move(other.fSpec)), fVisible(other.fVisible), fSelected(other.fSelected), fDisabled(other.fDisabled),
        fHandle(std::exchange(other.fHandle, dom::kNone)), fDisplay(std::move(other.fDisplay)), fDirty(other.fDirty) {}
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

// The CSS of a state: the Spec as the browser can take it. Anchors, origins
// and depth -- a scene's placement by hand -- are not here: the page is laid
// out by the browser, by stacks.
[[nodiscard]] inline Css cssOf(const State &state) {
  const scene::Spec &s = state.fSpec;
  Css css;
  css.set("display", state.fVisible ? state.fDisplay : "none");
  if (s.fill) {
    // An equal share of the stack's way, whatever is in it: a basis of nothing, so two filling
    // parts are two halves, as two filling children of a scene's flow are.
    css.set("flex", "1 1 0%");
    css.set("min-width", "0");
    css.set("align-self", "stretch");
  }
  if (s.fillX) css.set("width", "100%");
  if (s.fillY) css.set("height", "100%");
  const bool relX = s.relativeSize && s.relativeSize->has<scene::axis::x>();
  const bool relY = s.relativeSize && s.relativeSize->has<scene::axis::y>();
  if (s.width) css.set("width", relX ? number(*s.width * 100.0f) + "%" : px(*s.width));
  if (s.height) css.set("height", relY ? number(*s.height * 100.0f) + "%" : px(*s.height));
  if (s.minWidth) css.set("min-width", px(*s.minWidth));
  if (s.maxWidth) css.set("max-width", px(*s.maxWidth));
  if (s.minHeight) css.set("min-height", px(*s.minHeight));
  if (s.maxHeight) css.set("max-height", px(*s.maxHeight));
  if (s.grow && (s.grow->has<scene::axis::x>() || s.grow->has<scene::axis::y>())) css.set("flex-grow", "1");
  if (s.shrink && (s.shrink->has<scene::axis::x>() || s.shrink->has<scene::axis::y>())) css.set("flex-shrink", "1");
  if (s.alignSelf) css.set("align-self", std::string(alignment(*s.alignSelf)));
  if (s.margin.given) css.set("margin", margin(*s.margin.given));
  if (s.padding.given) css.set("padding", margin(*s.padding.given));
  if (s.padding.given || s.width || s.height) css.set("box-sizing", "border-box");
  if (s.cornerRadius) css.set("border-radius", px(*s.cornerRadius));
  if (state.fSelected && s.selectedBackground) css.set("background-color", rgba(*s.selectedBackground));
  else if (s.background) css.set("background-color", rgba(*s.background));
  if (s.alpha) css.set("opacity", number(*s.alpha));
  if (s.masking && *s.masking) css.set("overflow", "hidden");
  std::string transform;
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
  template <class F> void handles(this auto &self, F &&f) { f(self.fState.fHandle); }
  [[nodiscard]] bool onDom(this auto &, dom::Handle, std::string_view, std::string_view) { return true; }

  void apply(const scene::Spec &spec) { fState.apply(spec); }
  void markDirty() { fState.fDirty = true; }
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

// A box: an element with a Spec and nothing of its own -- a plate, a spacer.
struct Box : Node {
  explicit Box(scene::Spec spec = {}) { fState.apply(spec); }
};

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
  explicit Text(std::string text, scene::Spec spec = {}) : fText(std::move(text)) { fState.apply(spec); }
  [[nodiscard]] static constexpr std::string_view tag() { return "span"; }
  void mounted() { dom::attr(fState.fHandle, "class", "skiff-text"); }
  void sync() { dom::text(fState.fHandle, fText); }
  void setText(std::string text) {
    if (text != fText) {
      fText = std::move(text);
      markDirty();
    }
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

private:
  std::string fText;
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
    if (fLook.fHorizontal) dom::style(fState.fHandle, "align-items", "center");
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
    if (fLook.fHorizontal) dom::style(fState.fHandle, "align-items", "center");
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
    detail::eachChild(node, [&](auto &child) { mountNode(child, state.fHandle); });
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
        if (row.fState.fHandle == dom::kNone) mountNode(row, state.fHandle);
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
