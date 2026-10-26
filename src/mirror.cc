// skiff.mirror: a scene's tree as a document's elements, laid out and painted
// by an engine rather than by skiff -- the tree a program already has (its
// nodes, its bindings, its presses) shown through skiff.web's dom, whichever
// backend that is: a browser's document, an engine embedded and called
// directly, or the in-memory one a test reads back.
//
// Nothing of the scene's layout runs: each node says what it is as CSS, from
// what its State keeps and what kind of node it is --
//   a stack or a flow        a flex box along its way, its gaps and justification;
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
import skiff.scene;
import skiff.bind;
import skiff.web;

export namespace skiff::mirror {

namespace dom = skiff::web::dom;
using skiff::web::Markup;

namespace detail {

using skiff::web::px;
using skiff::web::number;
using skiff::web::rgba;

// Where across a box an anchor puts its first part.
[[nodiscard]] inline std::string_view placeAlong(float at) {
  return at <= 0.25f ? "flex-start" : at >= 0.75f ? "flex-end" : "center";
}

// Whether a flow runs across: its direction, visited.
[[nodiscard]] inline bool isRow(const scene::Direction &d) {
  return spl::visit(spl::overloaded{[](scene::direction::vertical) { return false; }, [](scene::direction::horizontal) { return true; }}, d);
}

// What holds a node: how a node lays its children out decides how each child
// is placed in it.
struct in_flow {
  bool row = false;
};
// In a box: the first part in its flow, sizing the box; the rest over it,
// each where its anchor puts it.
struct in_cell {
  bool first = true;
};
struct in_scroll {};
using Holder = spl::variant<in_flow, in_cell, in_scroll>;

// The size along one axis, as the State says it.
inline void sizeOf(Markup &m, std::string_view property, float size, bool relative, bool automatic) {
  if (relative)
    m.style(property, number(size * 100.0f) + "%");
  else if (!automatic && size > 0.0f)
    m.style(property, px(size));
}

// What every node's element is, from its State: shown or not, its sizes,
// margins and padding, its paint (background, border, corners, shadow), its
// opacity, its shift and scale, and where it is in what holds it.
inline void stateOf(Markup &m, const scene::State &s, const Holder &holder) {
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
  if (s.fAlpha < 1.0f) m.style("opacity", number(s.fAlpha));
  if (s.fMasking) m.style("overflow", "hidden");
  if (s.fDepth != 0.0f) m.style("z-index", std::to_string(static_cast<int>(std::lround(s.fDepth))));
  // Moved: placed over a box, its origin on its anchor; then its own shift and scale.
  std::string transform;
  spl::visit(spl::overloaded{
                 [&](const in_flow &flow) {
                   // Along the flow: its own size, or an equal share of what is
                   // left where it grows. Across: where its alignment says.
                   const bool along = flow.row ? s.fGrowAxes.has<scene::axis::x>() || relX
                                               : s.fGrowAxes.has<scene::axis::y>() || relY;
                   m.style("flex", along ? "1 1 0%" : (s.fShrinkAxes.has<scene::axis::x>() || s.fShrinkAxes.has<scene::axis::y>()) ? "0 1 auto" : "0 0 auto");
                   if (along) m.style(flow.row ? "min-width" : "min-height", "0");
                   const bool across = flow.row ? s.fGrowAxes.has<scene::axis::y>() : s.fGrowAxes.has<scene::axis::x>();
                   if (s.fAlignSelf) m.style("align-self", std::string(skiff::web::alignment(*s.fAlignSelf)));
                   else if (across) m.style("align-self", "stretch");
                   if (s.fOutOfFlow) {
                     m.style("position", "absolute");
                     m.style("left", number(s.fAnchor.x * 100.0f) + "%");
                     m.style("top", number(s.fAnchor.y * 100.0f) + "%");
                   }
                 },
                 [&](const in_cell &cell) {
                   if (cell.first) {
                     // In the box's flow: across, where its anchor is; down, its
                     // own height or all of the box's.
                     m.style("align-self", relX ? "stretch" : placeAlong(s.fAnchor.x));
                     m.style("flex", relY ? "1 1 0%" : "0 0 auto");
                     return;
                   }
                   m.style("position", "absolute");
                   m.style("left", number(s.fAnchor.x * 100.0f) + "%");
                   m.style("top", number(s.fAnchor.y * 100.0f) + "%");
                   if (s.fOrigin.x != 0.0f || s.fOrigin.y != 0.0f)
                     transform += "translate(" + number(-s.fOrigin.x * 100.0f) + "%," + number(-s.fOrigin.y * 100.0f) + "%) ";
                 },
                 [&](const in_scroll &) { m.style("flex", "0 0 auto"); }},
             holder);
  const float dx = s.fX + s.fShiftX, dy = s.fY + s.fShiftY;
  if (dx != 0.0f || dy != 0.0f) transform += "translate(" + px(dx) + "," + px(dy) + ") ";
  if (s.fScale != 1.0f) transform += "scale(" + number(s.fScale) + ") ";
  if (!transform.empty()) {
    transform.pop_back();
    m.style("transform", transform);
  }
}

// What kind of node it is, as an element: what its look says.
inline void kindOfFlow(Markup &m, const scene::FlowOptions &o) {
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
inline void kindOf(Markup &m, const scene::Look &look) {
  spl::visit(spl::overloaded{[&](const scene::look::box &) {
                               m.style("display", "flex");
                               m.style("flex-direction", "column");
                               m.style("position", "relative");
                             },
                             [&](const scene::look::flow &flow) { kindOfFlow(m, flow.options); },
                             [&](const scene::look::words &words) {
                               m.style("display", "block");
                               m.style("white-space", "pre-wrap");
                               m.style("font-size", px(words.size));
                               m.style("line-height", number(1.25f));
                               m.style("color", rgba(words.colour));
                               if (words.bold) m.style("font-weight", "bold");
                               m.text(words.text);
                             },
                             [&](const scene::look::scrolled &) {
                               m.style("display", "flex");
                               m.style("flex-direction", "column");
                               m.style("overflow-y", "auto");
                               m.style("position", "relative");
                             }},
             look);
}

// How a part after a box's first is held: over it; anywhere else, as the first.
[[nodiscard]] inline Holder after(const Holder &first) {
  return spl::visit(spl::overloaded{[](const in_cell &) -> Holder { return in_cell{false}; }, [](const auto &same) -> Holder { return same; }}, first);
}

// How a node holds its children: as its look says.
[[nodiscard]] inline Holder holderOf(const scene::Look &look) {
  return spl::visit(spl::overloaded{[](const scene::look::flow &flow) -> Holder { return in_flow{isRow(flow.options.direction)}; },
                                    [](const scene::look::scrolled &) -> Holder { return in_scroll{}; },
                                    [](const auto &) -> Holder { return in_cell{}; }},
                    look);
}

} // namespace detail

// A scene's tree, mirrored: one element per node, kept by the node's id,
// written again only where what it is changed, made where new, dropped where
// gone. Whatever drives the program refreshes its bindings, then sync()s.
class Mirror {
public:
  explicit Mirror(dom::Handle into = dom::root()) : fInto(into) {}

  template <class Root> void sync(Root &root) {
    ++fPass;
    const dom::Handle top = this->visit(root, detail::in_cell{});
    this->order(fInto, std::vector<dom::Handle>{top});
    std::erase_if(fShown, [&](auto &kept) {
      if (kept.second.seen == fPass) return false;
      fNodeOf.erase(kept.second.handle);
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

  // The element a node is, where it is shown.
  [[nodiscard]] dom::Handle elementOf(const scene::State &state) const {
    const auto found = fShown.find(state.fId);
    return found == fShown.end() ? dom::kNone : found->second.handle;
  }
  [[nodiscard]] std::size_t size() const noexcept { return fShown.size(); }

private:
  struct Shown {
    dom::Handle handle = dom::kNone;
    Markup markup;
    std::vector<dom::Handle> children;
    std::uint64_t seen = 0;
  };

  // A node: as its type says, or -- erased, outside a release build -- as the
  // walks' table says; either way its state, its look, whether it takes a
  // press, and its children in their order.
  template <class N> dom::Handle visit(N &node, const detail::Holder &holder) {
    return this->write(node.fState, node.look(), node.acceptsInput(), holder,
                       [&](auto &&each) { scene::eachChild(node, each); });
  }
  dom::Handle visit(scene::AnyNodeRef &ref, const detail::Holder &holder) {
    scene::ErasedNode node(ref);
    return this->write(node.fState, node.look(), node.acceptsInput(), holder,
                       [&](auto &&each) { node.forEachChild(each); });
  }
  template <class Children>
  dom::Handle write(const scene::State &state, const scene::Look &look, bool pressable, const detail::Holder &holder,
                    Children &&children) {
    Markup now;
    detail::kindOf(now, look);
    detail::stateOf(now, state, holder);
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
    shown.markup = std::move(now);
    const dom::Handle handle = shown.handle;
    // Children: each visited, then put in their order where it moved. The
    // map may grow as they are visited: nothing of it held across.
    const detail::Holder theirs = detail::holderOf(look);
    std::vector<dom::Handle> handles;
    children([&](auto &child) {
      // In a box, the parts after the first are placed over it.
      const detail::Holder mine = handles.empty() ? theirs : detail::after(theirs);
      handles.push_back(this->visit(child, mine));
    });
    this->order(handle, std::move(handles));
    return handle;
  }

  void order(dom::Handle parent, std::vector<dom::Handle> children) {
    std::vector<dom::Handle> &was = parent == fInto ? fTop : fShown.at(fNodeOf.at(parent)).children;
    if (was == children) return;
    for (dom::Handle child : children) dom::append(parent, child);
    was = std::move(children);
  }

  dom::Handle fInto;
  std::uint64_t fPass = 0;
  std::unordered_map<scene::NodeId, Shown> fShown;
  std::unordered_map<dom::Handle, scene::NodeId> fNodeOf;
  std::vector<dom::Handle> fTop;
};

} // namespace skiff::mirror
