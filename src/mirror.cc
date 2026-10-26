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
inline void acrossInBlock(Markup &m, const scene::State &s, bool relX, float at) {
  const bool sized = relX || (s.fWidth > 0.0f && !s.fAutoSizeAxes.has<scene::axis::x>());
  if (!sized && displayIs(m, "block")) m.style("display", "table");
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
  // Placed by its anchor, out of what holds it: over it, its origin on the
  // anchor.
  std::string transform;
  const auto placed = [&] {
    m.style("position", "absolute");
    m.style("left", number(s.fAnchor.x * 100.0f) + "%");
    m.style("top", number(s.fAnchor.y * 100.0f) + "%");
    if (s.fOrigin.x != 0.0f || s.fOrigin.y != 0.0f)
      transform += "translate(" + number(-s.fOrigin.x * 100.0f) + "%," + number(-s.fOrigin.y * 100.0f) + "%) ";
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
  const float dx = s.fX + s.fShiftX, dy = s.fY + s.fShiftY;
  if (dx != 0.0f || dy != 0.0f) transform += "translate(" + px(dx) + "," + px(dy) + ") ";
  if (s.fScale != 1.0f) transform += "scale(" + number(s.fScale) + ") ";
  if (!transform.empty()) {
    transform.pop_back();
    m.style("transform", transform);
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
[[nodiscard]] inline Holder kindOf(Markup &m, const scene::Look &look, const std::vector<const scene::State *> &parts) {
  return spl::visit(spl::overloaded{[&](const scene::look::box &) -> Holder {
                                      m.style("display", "block");
                                      m.style("position", "relative");
                                      return in_cell{};
                                    },
                                    [&](const scene::look::flow &flow) -> Holder {
                                      const scene::FlowOptions &o = flow.options;
                                      if (needsFlex(o, parts)) {
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
                                      m.text(words.text);
                                      return in_cell{};
                                    },
                                    [&](const scene::look::scrolled &) -> Holder {
                                      m.style("display", "block");
                                      m.style("overflow-y", "auto");
                                      m.style("position", "relative");
                                      return in_scroll{};
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

// A child's State: its own, or as the walks' table says, where it is erased.
template <class N> [[nodiscard]] const scene::State &stateOfPart(N &node) { return node.fState; }
[[nodiscard]] inline const scene::State &stateOfPart(scene::AnyNodeRef &ref) { return scene::ErasedNode(ref).fState; }

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
    // Its parts' States first: whether it needs a flex box depends on them.
    std::vector<const scene::State *> parts;
    children([&](auto &child) { parts.push_back(&detail::stateOfPart(child)); });
    Markup now;
    const detail::Holder theirs = detail::kindOf(now, look, parts);
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
    std::vector<dom::Handle> handles;
    children([&](auto &child) {
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
