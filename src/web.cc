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
  // Written to its element at the next sync.
  bool fDirty = true;

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
  if (!state.fVisible) css.set("display", "none");
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
  explicit Stack(Look look, Parts... parts) : fParts(std::move(parts)...), fLook(std::move(look)) { fState.apply(fLook.fSpec); }
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
    scene::Path path;
    mountNode(fRoot, into, path);
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
    const auto found = fPaths.find(target);
    if (found == fPaths.end()) return;
    const scene::Path path = found->second;
    bool press = false;
    atPath(fRoot, path, 0, [&](auto &node) { press = node.onDom(target, kind, value); });
    if (press) bind::press(fRoot, *fModel, path);
    refresh();
  }

private:
  template <class N> void mountNode(N &node, dom::Handle parent, scene::Path &path) {
    State &state = node.fState;
    state.fHandle = dom::create(std::remove_cvref_t<N>::tag());
    dom::append(parent, state.fHandle);
    // A named view is named in the document too: the page reads as the source does.
    if constexpr (requires { std::string_view(std::remove_cvref_t<N>::viewName()); })
      dom::attr(state.fHandle, "data-view", std::string_view(std::remove_cvref_t<N>::viewName()));
    node.mounted();
    node.handles([&](dom::Handle h) { fPaths[h] = path; });
    std::uint32_t place = 0;
    detail::eachChild(node, [&](auto &child) {
      path.push_back(place++);
      mountNode(child, state.fHandle, path);
      path.pop_back();
    });
  }
  template <class N> void syncNode(N &node) {
    State &state = node.fState;
    if (state.fDirty) {
      applyCss(state.fHandle, cssOf(state));
      if (state.fDisabled) dom::attr(state.fHandle, "disabled", "");
      else dom::dropAttr(state.fHandle, "disabled");
      node.sync();
      state.fDirty = false;
    }
    detail::eachChild(node, [&](auto &child) { syncNode(child); });
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
  std::unordered_map<dom::Handle, scene::Path> fPaths;
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
