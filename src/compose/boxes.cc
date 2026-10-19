// skiff.compose:boxes -- stacks, and the model's kinds made around a node
// (the module is skiff.compose: see compose.cc).
export module skiff.compose:boxes;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import skiff.paint;

export namespace skiff::compose {

// How a stack lays its parts out: which way, the gap between them, and the
// node's own spec.
struct Look {
  bool fHorizontal = false;
  float fGap = 0.0f;
  scene::Spec fSpec{};
  // Where along its way its parts sit, together.
  nodes::Justify fJustify = nodes::justify::start{};
};
inline Look vbox(float gap = 0.0f, scene::Spec spec = {}) {
  return {false, gap, std::move(spec)};
}
inline Look hbox(float gap = 0.0f, scene::Spec spec = {}) {
  return {true, gap, std::move(spec)};
}
// A look with its parts put along its way as said: in the middle, at the end.
inline Look justified(Look look, nodes::Justify justify) {
  look.fJustify = std::move(justify);
  return look;
}

template <class... Parts>
inline constexpr bool kAnyWalks = (bind::detail::kWalks<Parts> || ...);

// A stack laid out as its look says: the base of a node whose parts are
// named (a parts aggregate) rather than listed -- its parts each said with
// their own spec where they are made (styled), nothing set after.
struct Stacked : nodes::Stack {
  explicit Stacked(Look look, scene::Spec spec = {}) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fState.apply(spec);
    this->fStack.justify = look.fJustify;
  }
};

// One of the nodes Shown may be (a variant of them), in a column as wide as
// where it is put and as tall as it is: a node a program's plugin or
// protocol makes of its own, held where it is put. The node is parts.shown;
// made from one of its alternatives, or in place as one of them. Another
// look said first: as tall and as wide as it is, for one.
template <class Shown> struct Holding : Stacked {
  using BindingChildren = std::tuple<Shown>;
  struct parts_t {
    Shown shown;
  } parts;
  template <class Made>
    requires std::constructible_from<Shown, Made>
  explicit Holding(Made made) : Holding(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(made)) {}
  template <class Made>
    requires std::constructible_from<Shown, Made>
  Holding(Look look, Made made) : Stacked(std::move(look)), parts{.shown = Shown(std::move(made))} {}
  template <class Node, class... Args>
  explicit Holding(std::in_place_type_t<Node> which, Args &&...args)
      : Stacked(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY})), parts{.shown = Shown(which, std::forward<Args>(args)...)} {}
};

// A node made in another unit, held here through its table: what a
// program's protocol makes of its own pages, compiled once where they are
// made, never deduced where they are put. What it may ask for is said, not
// seen (Out): its answers are kept by their path, as any erased node's are
// outside a release build. Its node, when it is a T: shown().get<T>().
template <class Out_> struct Erased : Stacked {
  using Out = Out_;
  struct parts_t {
    scene::AnyNode shown;
  } parts;
  template <class Node>
    requires std::derived_from<std::remove_cvref_t<Node>, scene::Node>
  explicit Erased(Node node)
      : Stacked(vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY})), parts{.shown = scene::AnyNode(std::move(node))} {}
  [[nodiscard]] scene::AnyNode &shown() noexcept { return parts.shown; }
};

// What every combinator returns outside a release build: the node it made,
// held through its table, so that no composed type exists there -- the
// table is made where the combinator is called, over children that are
// held already.
//
// Held is not in the tree: the walks see through it (kSeenThrough) to the
// node it holds -- its state, its place, its size, its answers, as they are
// now, whatever the node does to them itself (hides itself, slides in).
//
// A wrapper laid over held content in turn -- onClick over a column, a
// wrapper a combinator makes over what another one returned -- is a node of
// its own, with that content its only child: its base is Held (Self below),
// and it stands where its content stood. As it is erased, the content's
// placement is folded into it (where it stands, its margin, how it is
// aligned, what bounds its size -- not how it is drawn, its alpha, scale
// and shift, which stay the content's, a node drawn under it); its size is
// the content's own, followed axis by axis -- relative to the parent, the
// wrapper is and the content fills it; growing, the wrapper grows and the
// content fills it; any other way, the wrapper is as big as the content. A
// spec given it later goes the same way.
// A type named where it was written over held content (SKIFF_NAMED outside a
// release build): the Held it is, to the walks and to whatever calls it --
// not a wrapper of its own (no table made for it, nothing adopted).
// The view itself (the type SKIFF_NAMED makes, Self), not a wrapper made
// over one: a scope over a named view inherits its NamedOver, and taken for
// the Held it names would lose the scope.
template <class N>
concept NamedOverHeld = requires { typename N::NamedOver; } && std::same_as<typename N::NamedSelf, N> &&
                        std::derived_from<N, Stacked>;
template <class Self>
concept HeldWrapper = !NamedOverHeld<Self>;
struct Held : Stacked {
  // What it holds is not known here: walked, and taken to hold a Local, as
  // any node naming its children itself is.
  using Walked = std::true_type;
  using HasLocal = std::true_type;
  // The content's size as it says it, kept apart from the wrapper's
  // following of it: content made to fill its wrapper is still the size it
  // was made.
  struct own_t {
    float width = 0.0f;
    float height = 0.0f;
    decltype(scene::State::fRelativeSizeAxes) relative{};
    decltype(scene::State::fAutoSizeAxes) autoSize{};
  };
  struct parts_t {
    scene::AnyNode held;
  } parts;
  // A view named over held content: the Held it is, moved -- no wrapper.
  template <class N>
    requires NamedOverHeld<std::remove_cvref_t<N>>
  explicit Held(N &&node) : Held(static_cast<Held &&>(node)) {}
  template <class N>
    requires(std::derived_from<std::remove_cvref_t<N>, scene::Node> && !std::same_as<std::remove_cvref_t<N>, Held> &&
             !NamedOverHeld<std::remove_cvref_t<N>>)
  explicit Held(N &&node) : Stacked(vbox(0.0f)) {
    using T = std::remove_cvref_t<N>;
    // A wrapper over held content, finished: its content's placement its own.
    if constexpr (std::derived_from<T, Held>)
      node.adopt();
    fSpecTo = +[](void *held, const scene::Spec &spec) { static_cast<T *>(held)->apply(spec); };
    parts.held = scene::AnyNode(std::forward<N>(node));
  }
  // Made in place: a node that is never moved (a mount whose content may
  // refer to itself).
  template <class N, class... Args>
    requires(std::derived_from<N, scene::Node> && !std::derived_from<N, Held>)
  explicit Held(std::in_place_type_t<N> which, Args &&...args) : Stacked(vbox(0.0f)) {
    fSpecTo = +[](void *held, const scene::Spec &spec) { static_cast<N *>(held)->apply(spec); };
    parts.held = scene::AnyNode(which, std::forward<Args>(args)...);
  }
  [[nodiscard]] scene::AnyNode &held() noexcept { return parts.held; }
  [[nodiscard]] scene::AnyNode &seenThrough() noexcept { return parts.held; }
  // Along these axes the wrapper says its own size -- a width clamped to
  // what is wanted, a picture fitted -- and its content fills it there,
  // whatever the content would have been: followed again now, and kept
  // through every spec given it later.
  void ownAxes(scene::Axes axes) {
    adopt();
    fOwnAxes = axes;
    scene::State &node = parts.held.state();
    restore(node);
    followSize(node);
    invalidateLayout();
  }

  // A spec: held, all of it to the node held, as that node takes one
  // (this overload, Held itself); a wrapper over held content, split --
  // placement to the wrapper, the rest to the content, its size followed
  // again (the one below, for any type derived from it).
  void apply(this Held &self, const scene::Spec &spec) { self.fSpecTo(self.parts.held.ref().node(), spec); }
  template <class Self>
    requires HeldWrapper<Self>
  void apply(this Self &self, const scene::Spec &spec) {
    // Adopted first, where it is not yet: a wrapper that says a spec of its
    // own as it is made (appearing's start, faded and shifted) said it
    // before erased() adopted it, and restore() then put back a size never
    // recorded -- nothing: the content lost its fill and was laid out 0
    // wide, a slid-in column with every row's text gone.
    self.adopt();
    scene::State &node = self.parts.held.state();
    // Its own size back first, so the spec and the following start from
    // what it says, not from how it was last made to fill the wrapper.
    self.restore(node);
    self.fSpecTo(self.parts.held.ref().node(), nodeSide(spec));
    // A grow said takes the place of the size the wrapper followed from
    // its content's (State::apply, for a node of its own) -- but not of a
    // fill the content says: a fill stays, and the grow means nothing there.
    // Kept against the relative axis the wrapper followed, it was told as
    // two sizes at once on every wrapper so made.
    scene::Spec placed = placement(spec);
    if (spec.grow) {
      const scene::Axes grow = spec.grow->except(node.fRelativeSizeAxes);
      self.fState.fAutoSizeAxes = self.fState.fAutoSizeAxes.except(grow);
      self.fState.fRelativeSizeAxes = self.fState.fRelativeSizeAxes.except(grow);
      placed.grow = grow;
    }
    self.fState.apply(placed);
    if (spec.visible)
      self.fShown = *spec.visible;
    self.followSize(node);
    self.invalidateLayout();
  }
  template <class Self>
    requires HeldWrapper<Self>
  void setVisible(this Self &self, bool on) { self.apply({.visible = on}); }
  // Laid out as itself, a wrapper over held content (one made around a
  // named view or a Held by a type of the program's -- a mark, a bound part
  // -- that no combinator erased): adopted first, where nothing adopted it,
  // or it is laid out by a state never set, 0 by 0, and its content sticks
  // out of it.
  template <class Self>
    requires HeldWrapper<Self>
  void measure(this Self &self, const skia::SkRect &) { self.adopt(); }
  // The rest of what a node is told by hand: Held itself passes it to its
  // node, a wrapper takes it itself -- never Held's own state, which no walk
  // sees.
  void setVisible(this Held &self, bool on) { self.apply({.visible = on}); }
  void setPosition(this Held &self, float x, float y) { self.apply({.x = x, .y = y}); }
  void setSize(this Held &self, float width, float height) { self.apply({.width = width, .height = height}); }
  void setPadding(this Held &self, scene::Margin padding) { self.apply({.padding = padding}); }
  void setMargin(this Held &self, scene::Margin margin) { self.apply({.margin = margin}); }
  void setScale(this Held &self, float scale) { self.apply({.scale = scale}); }
  void setAlpha(this Held &self, float alpha) { self.apply({.alpha = alpha}); }
  void markDamaged(this Held &self) { self.parts.held.state().markDamaged(); }
  void invalidateLayout(this Held &self) { self.parts.held.state().invalidateLayout(); }
  template <class Self>
    requires HeldWrapper<Self>
  void setPosition(this Self &self, float x, float y) { self.fState.setPosition(x, y); }
  template <class Self>
    requires HeldWrapper<Self>
  void setSize(this Self &self, float width, float height) { self.fState.setSize(width, height); }
  template <class Self>
    requires HeldWrapper<Self>
  void setPadding(this Self &self, scene::Margin padding) { self.fState.setPadding(padding); }
  template <class Self>
    requires HeldWrapper<Self>
  void setMargin(this Self &self, scene::Margin margin) { self.fState.setMargin(margin); }
  template <class Self>
    requires HeldWrapper<Self>
  void setScale(this Self &self, float scale) { self.fState.setScale(scale); }
  template <class Self>
    requires HeldWrapper<Self>
  void setAlpha(this Self &self, float alpha) { self.fState.setAlpha(alpha); }
  template <class Self>
    requires HeldWrapper<Self>
  void markDamaged(this Self &self) { self.fState.markDamaged(); }
  template <class Self>
    requires HeldWrapper<Self>
  void invalidateLayout(this Self &self) { self.fState.invalidateLayout(); }
  // What is asked of it as a node: Held itself is not in the tree, so the
  // node it holds answers -- as the walks see it; a wrapper answers itself.
  [[nodiscard]] const scene::State &state(this const Held &self) { return self.parts.held.ref().state(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] const scene::State &state(this const Self &self) { return self.fState; }
  [[nodiscard]] bool visible(this const Held &self) { return self.state().visible(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] bool visible(this const Self &self) { return self.fState.visible(); }
  [[nodiscard]] const skia::SkRect &bounds(this const Held &self) { return self.state().bounds(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] const skia::SkRect &bounds(this const Self &self) { return self.fState.bounds(); }
  [[nodiscard]] scene::NodeId id(this const Held &self) { return self.state().id(); }
  [[nodiscard]] bool focused(this const Held &self) { return self.state().focused(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] bool focused(this const Self &self) { return self.fState.focused(); }
  [[nodiscard]] bool hovered(this const Held &self) { return self.state().hovered(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] bool hovered(this const Self &self) { return self.fState.hovered(); }
  [[nodiscard]] bool selected(this const Held &self) { return self.state().selected(); }
  [[nodiscard]] bool disabled(this const Held &self) { return self.state().disabled(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] bool disabled(this const Self &self) { return self.fState.disabled(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] bool selected(this const Self &self) { return self.fState.selected(); }
  template <class Self>
    requires HeldWrapper<Self>
  [[nodiscard]] scene::NodeId id(this const Self &self) { return self.fState.id(); }
  // A wrapper as the walks see it: shown while it says it is and its
  // content is -- content that hides itself later (a mount emptied, a part
  // shown as the model says) hides the wrapper with it.
  [[nodiscard]] scene::State &walkedState() noexcept {
    this->fState.fVisible = fShown && (!parts.held || parts.held.state().fVisible);
    return this->fState;
  }

  // The content's placement folded into this wrapper's, and left at rest
  // in it: what the wrapper said of its own is kept, and how both are drawn
  // as a whole -- faded, scaled, shifted -- is what both together made.
  void adopt() {
    // Once: a wrapper made over held content and then erased is adopted as
    // it is made, not again as it is held.
    if (std::exchange(fAdopted, true))
      return;
    scene::State &mine = this->fState;
    scene::State &node = parts.held.state();
    const auto take = [](auto &own, auto &theirs, const auto &rest) {
      const std::remove_cvref_t<decltype(own)> atRest = rest;
      if (own == atRest)
        own = theirs;
      theirs = atRest;
    };
    take(mine.fAnchor, node.fAnchor, scene::anchor::kTopLeft);
    take(mine.fOrigin, node.fOrigin, scene::anchor::kTopLeft);
    mine.fX += std::exchange(node.fX, 0.0f);
    mine.fY += std::exchange(node.fY, 0.0f);
    mine.fOutOfFlow = mine.fOutOfFlow || std::exchange(node.fOutOfFlow, false);
    if (mine.fMargin == scene::Margin{})
      mine.fMargin = node.fMargin;
    node.fMargin = {};
    if (!mine.fAlignSelf)
      mine.fAlignSelf = node.fAlignSelf;
    node.fAlignSelf.reset();
    mine.fDepth += std::exchange(node.fDepth, 0.0f);
    take(mine.fMinWidth, node.fMinWidth, 0.0f);
    take(mine.fMaxWidth, node.fMaxWidth, 0.0f);
    take(mine.fMinHeight, node.fMinHeight, 0.0f);
    take(mine.fMaxHeight, node.fMaxHeight, 0.0f);
    // Hidden as it was made -- visible(false, ...) said before the wrapper
    // was put over it -- the wrapper starts hidden, and what shows it later
    // (shown_if over it, reading the model) shows it: kept the content's,
    // it was hidden under every wrapper for good -- a call button never
    // seen. What the content says of itself after that stays its own,
    // followed as it is seen (walkedState).
    fShown = mine.fVisible && node.fVisible;
    node.fVisible = true;
    // How it is drawn -- its alpha, its scale, its shift -- stays the
    // content's: it is a node of its own under the wrapper, drawn so. Taken
    // over, a fade under way froze: a message coming in -- faded in, then
    // washed for its marks -- had the wash take the fade's start, alpha 0,
    // and the fade then moved an alpha no one drew with. Every message that
    // came stayed unseen, a gap where it was.
    mine.fGrowAxes = mine.fGrowAxes | node.fGrowAxes;
    mine.fShrinkAxes = mine.fShrinkAxes | node.fShrinkAxes;
    this->followSize(node);
  }

private:
  // How the node held takes a spec, as its own type does.
  // How a spec reaches the node held -- named so no wrapper's own member
  // (applied's apply) hides it.
  void (*fSpecTo)(void *, const scene::Spec &) = nullptr;
  // Not a part: nothing walks it.
  own_t fOwn;
  // Shown, as the wrapper itself says.
  bool fShown = true;
  // Its content's placement folded in already.
  bool fAdopted = false;
  // The axes it sizes itself along (ownAxes).
  scene::Axes fOwnAxes{};
  [[nodiscard]] static scene::Spec placement(const scene::Spec &spec) {
    return {.place = spec.place, .anchor = spec.anchor, .origin = spec.origin, .x = spec.x, .y = spec.y,
            .grow = spec.grow, .shrink = spec.shrink, .minWidth = spec.minWidth, .maxWidth = spec.maxWidth,
            .minHeight = spec.minHeight, .maxHeight = spec.maxHeight, .alignSelf = spec.alignSelf, .depth = spec.depth,
            .margin = spec.margin, .scale = spec.scale, .alpha = spec.alpha, .shiftX = spec.shiftX, .shiftY = spec.shiftY,
            .visible = spec.visible, .disabled = spec.disabled};
  }
  [[nodiscard]] static scene::Spec nodeSide(scene::Spec spec) {
    spec.place.reset();
    spec.anchor.reset();
    spec.origin.reset();
    spec.x.reset();
    spec.y.reset();
    spec.grow.reset();
    spec.shrink.reset();
    spec.minWidth.reset();
    spec.maxWidth.reset();
    spec.minHeight.reset();
    spec.maxHeight.reset();
    spec.alignSelf.reset();
    spec.depth.reset();
    spec.margin = {};
    spec.scale.reset();
    spec.alpha.reset();
    spec.shiftX.reset();
    spec.shiftY.reset();
    spec.visible.reset();
    return spec;
  }
  void restore(scene::State &node) const {
    node.fWidth = fOwn.width;
    node.fHeight = fOwn.height;
    node.fRelativeSizeAxes = fOwn.relative;
    node.fAutoSizeAxes = fOwn.autoSize;
  }
  // The holder's size, axis by axis, as the node's says.
  void followSize(scene::State &node) {
    fOwn = {.width = node.fWidth, .height = node.fHeight, .relative = node.fRelativeSizeAxes,
                 .autoSize = node.fAutoSizeAxes};
    this->followAxis<scene::axis::x>(node, node.fWidth, this->fState.fWidth);
    this->followAxis<scene::axis::y>(node, node.fHeight, this->fState.fHeight);
  }
  template <class Axis> void followAxis(scene::State &node, float &nodeSize, float &mySize) {
    scene::State &mine = this->fState;
    // Its own size along the axes it says it on, the content filling it.
    if (fOwnAxes.template has<Axis>()) {
      mine.fRelativeSizeAxes = mine.fRelativeSizeAxes.template without<Axis>();
      mine.fAutoSizeAxes = mine.fAutoSizeAxes.template without<Axis>();
      node.fGrowAxes = node.fGrowAxes.template without<Axis>();
      node.fAutoSizeAxes = node.fAutoSizeAxes.template without<Axis>();
      node.fRelativeSizeAxes = node.fRelativeSizeAxes.template with<Axis>();
      nodeSize = 1.0f;
      return;
    }
    if (node.fRelativeSizeAxes.template has<Axis>()) {
      mine.fRelativeSizeAxes = mine.fRelativeSizeAxes.template with<Axis>();
      mine.fAutoSizeAxes = mine.fAutoSizeAxes.template without<Axis>();
      mySize = nodeSize;
      nodeSize = 1.0f;
    } else if (mine.fGrowAxes.template has<Axis>() || node.fGrowAxes.template has<Axis>()) {
      mine.fGrowAxes = mine.fGrowAxes.template with<Axis>();
      mine.fAutoSizeAxes = mine.fAutoSizeAxes.template without<Axis>();
      node.fGrowAxes = node.fGrowAxes.template without<Axis>();
      node.fRelativeSizeAxes = node.fRelativeSizeAxes.template with<Axis>();
      nodeSize = 1.0f;
    } else {
      mine.fRelativeSizeAxes = mine.fRelativeSizeAxes.template without<Axis>();
      mine.fAutoSizeAxes = mine.fAutoSizeAxes.template with<Axis>();
    }
  }
};
} // namespace skiff::compose
namespace skiff::scene {
template <> inline constexpr bool kSeenThrough<compose::Held> = true;
template <class N>
  requires compose::NamedOverHeld<N>
inline constexpr bool kSeenAsBase<N> = true;
} // namespace skiff::scene
export namespace skiff::compose {
// A node's state as the walks see it: a Held's, its node's; any other node's
// -- a wrapper over held content too -- its own. For a node's own rules over
// its parts (a bubble placing its time), whatever each part is.
template <class N> [[nodiscard]] scene::State &state_of(N &node) { return node.fState; }
template <NamedOverHeld N> [[nodiscard]] scene::State &state_of(N &node) { return static_cast<Held &>(node).held().state(); }
[[nodiscard]] inline scene::State &state_of(Held &held) { return held.held().state(); }
template <class N> [[nodiscard]] const scene::State &state_of(const N &node) { return node.fState; }
template <NamedOverHeld N> [[nodiscard]] const scene::State &state_of(const N &node) {
  return const_cast<Held &>(static_cast<const Held &>(node)).held().state();
}
[[nodiscard]] inline const scene::State &state_of(const Held &held) { return const_cast<Held &>(held).held().state(); }

// A part as a combinator keeps it. A view named where it was written
// (SKIFF_NAMED) is a type of its own -- what the compiler names, what a
// hook is overloaded on -- and, outside a release build, a wrapper over a
// Held; given to a combinator, it is held as a part, so that what keeps it
// is one type for every view and its tables are made once, not once for
// each view it is given. Anything else is kept as it is.
template <class N> struct PartOf {
  using type = N;
};
template <class N>
  requires(scene::kErasedCombinators && std::derived_from<N, Held> && !std::same_as<N, Held> &&
           requires { N::viewName(); })
struct PartOf<N> {
  using type = Held;
};
template <class N> using part_t = typename PartOf<N>::type;
template <class N> part_t<N> as_part(N node) { return part_t<N>(std::move(node)); }
template <class N>
  requires std::same_as<part_t<N>, N>
std::vector<N> as_parts(std::vector<N> nodes) {
  return nodes;
}
template <class N> std::vector<part_t<N>> as_parts(std::vector<N> nodes) {
  return nodes | std::views::as_rvalue | std::views::transform([](N &&one) { return part_t<N>(std::move(one)); }) |
         std::ranges::to<std::vector>();
}

// A wrapper that says its own size along some axes (in its measure):
// over held content, the holder's -- which then fills it there; any other
// node says it in its own state already.
// (Constrained, not a Held& beside a catch-all: a type derived from Held --
// a view named over it -- took the catch-all as the closer match, and
// nothing was owned: its content never filled it, a picture 0 by 0.)
template <class N>
  requires std::derived_from<N, Held>
void sizedHere(N &over, scene::Axes axes) {
  static_cast<Held &>(over).ownAxes(axes);
}
template <class N> void sizedHere(N &, scene::Axes) {}

// A node marked where it is made -- Tag says what it is, and what it
// carries (a link's address) -- for what holds it to find it by its mark,
// as the walks see it (scene::findNode): never named by where it is.
// Over held content, made where it is needed (a mark, an alternative of
// several kinds in one variant), a wrapper takes its content's place at once.
// A node a combinator makes concrete in every build (a scope, a node with
// a model of its own) over what may be held content: outside a release
// build it is then a wrapper over that content, never erased -- adopted
// here, as erased() adopts the others, or it is laid out with none of its
// content's fill and sizing, a 0-by-0 box its content sticks out of.
template <class N>
  requires std::derived_from<N, Held>
N &over_held(N &made) {
  static_cast<Held &>(made).adopt();
  return made;
}
template <class N> N &over_held(N &made) { return made; }

// What a view named where it is written (SKIFF_NAMED) is: what its
// expression made, Base, as a type of its own, Self -- which says its name
// (Self::viewName(), the macro's spelling of it), written into the state of
// what it names: while that is laid out it is the view its parts are in,
// for what is told of them (an overflow).
template <class Self, class Base> struct Named : Base {
  explicit Named(Base made) : Base(std::move(made)) { state_of(static_cast<Base &>(*this)).fViewName = Self::viewName(); }
};
// Over held content (outside a release build): seen as the Held it is.
template <class Self> struct Named<Self, Held> : Held {
  using NamedOver = Held;
  using NamedSelf = Self;
  using SeenAs = Held;
  explicit Named(Held made) : Held(std::move(made)) { this->held().state().fViewName = Self::viewName(); }
};
// Over a view named over held content in turn (a view made of a view of its
// own, named once more): the same Held, named again -- the outer name the
// view's. As a type of its own, deriving from the inner one, it was laid out
// as a wrapper never adopted, 0 by 0, and what it holds in nothing.
template <class Self, class Base>
  requires NamedOverHeld<Base>
struct Named<Self, Base> : Held {
  using NamedOver = Held;
  using NamedSelf = Self;
  using SeenAs = Held;
  explicit Named(Base made) : Held(static_cast<Held &&>(made)) { this->held().state().fViewName = Self::viewName(); }
};

template <class Tag, class N> struct Marked : N {
  // Found by this mark through whatever wraps it (scene::MarkOf).
  using MarkedAs = Marked<Tag, N>;
  Tag fTag;
  explicit Marked(N node, Tag tag = {}) : N(std::move(node)), fTag(std::move(tag)) { over_held(static_cast<N &>(*this)); }
};
template <class Tag, class N> auto marked(Tag tag, N node) { return erased(Marked<Tag, part_t<N>>(as_part(std::move(node)), std::move(tag))); }

// A node made by a combinator where it cannot be moved: made in place, held
// outside a release build; a prvalue in one.
template <class N, class... Args> auto erased_made(Args &&...args) {
  if constexpr (scene::kErasedCombinators)
    return Held(std::in_place_type<N>, std::forward<Args>(args)...);
  else
    return N(std::forward<Args>(args)...);
}
// A node as a combinator returns it: itself in a release build; held,
// outside one.
template <class N> auto erased(N node) {
  if constexpr (scene::kErasedCombinators && !std::same_as<N, Held>)
    return Held(std::move(node));
  else
    return node;
}

// A node of its own drawing or input, its parts placed in it by their own
// specs: its spec said where it is made.
struct Specced : scene::Node {
  explicit Specced(scene::Spec spec) { this->fState.apply(spec); }
};


// A stack whose typed parts are walked in order, in every build.
template <class... Parts> struct Box : nodes::Stack {
  using Base = nodes::Stack;
  using Walked = std::bool_constant<kAnyWalks<Parts...>>;
  using HasLocal = std::bool_constant<(bind::detail::kHasLocal<Parts> || ...)>;
  std::tuple<Parts...> fParts;
  explicit Box(Look look, Parts... parts) : fParts(std::move(parts)...) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
  }
  Box(Box&&) noexcept = default;
  Box(const Box&) = default;
  Box& operator=(Box&&) noexcept = default;
  Box& operator=(const Box&) = default;

  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, self.fParts);
  }

};

// A stack of a list of parts, as many as there are: made from them, in
// their order.
template <class N> struct Many : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<N>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  std::vector<N> fParts;
  Many(Look look, std::vector<N> parts) : fParts(std::move(parts)) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    for (auto &part : self.fParts)
      f(part);
  }
};
template <class N> auto many(Look look, std::vector<N> parts) {
  return erased(Many<part_t<N>>(std::move(look), as_parts(std::move(parts))));
}

// A stack of a list's parts made from what it reads -- a value projected
// from the model or a Local's state, a search's results -- each element made
// into a part by `make`, all of them made again at each read.
template <class Make, class N> struct Listed : Many<N> {
  Make fMake;
  Listed(Look look, Make make) : Many<N>(std::move(look), {}), fMake(std::move(make)) {}
  template <std::ranges::input_range R> void read(const R &items) {
    this->fParts = items | std::views::transform(std::ref(fMake)) | std::ranges::to<std::vector>();
    this->invalidateLayout();
    this->markDamaged();
  }
};
template <class Element, class Make> auto listed(Look look, Make make) {
  // Concrete in every build: what reads its elements for it reads it as itself.
  return Listed<Make, std::invoke_result_t<Make &, const Element &>>(std::move(look), std::move(make));
}

// A node shown, or there but hidden.
template <class N>
  requires std::derived_from<N, scene::Node>
N visible(bool on, N node) {
  node.setVisible(on);
  return node;
}

// Keep a subtree polling while an external deadline is pending. The
// node's own polling, if any, remains active independently.
template <class N> struct Ticking : N {
  bool fTick;
  Ticking(bool tick, N node) : N(std::move(node)), fTick(tick) {}
  bool wantsTick() const {
    if constexpr (requires(const N& node) { node.wantsTick(); })
      return fTick || N::wantsTick();
    else
      return fTick;
  }
};
template <class N> auto keep_ticking(bool on, N node) {
  return erased(Ticking<part_t<N>>(on, as_part(std::move(node))));
}

// A subtree whose drawing can be replayed until it is damaged.
template <class N>
  requires std::derived_from<N, scene::Node>
N recorded(N node, bool on = true) {
  // On the node as the walks see it: a Held's, its node's.
  state_of(node).setRecorded(on);
  return node;
}

// A node showing the pointer as `shape` over it.
template <class N>
  requires std::derived_from<N, scene::Node>
N with_cursor(scene::Cursor shape, N node) {
  // On the node as the walks see it: a Held's, its node's.
  state_of(node).setCursor(shape);
  return node;
}

// Leaf options declared where the node is composed. Keep the concrete
// node type, including a text's picture provider and a widget's actions.
template <class N> requires requires(N& node) { node.setWrapped(true); }
N wrapped(N node, bool on = true) {
  node.setWrapped(on);
  return node;
}
template <class N> requires requires(N& node) { node.setElided(true); }
N elided(N node, bool on = true) {
  node.setElided(on);
  return node;
}
template <class N> requires requires(N& node) { node.setPrimary(true); }
N primary(N node, bool on = true) {
  node.setPrimary(on);
  return node;
}
template <class N> requires requires(N& node) { node.setOnNow(true); }
N on_now(N node, bool on) {
  node.setOnNow(on);
  return node;
}

// A node with its spec applied: a leaf said as it is put in its place.
template <class N>
  requires std::derived_from<N, scene::Node>
N styled(scene::Spec spec, N node) {
  node.apply(spec);
  return node;
}

} // namespace skiff::compose

// Every box, outside a release build, walked as one type. Its table is made
// in the program, not here: its drawing takes the program's paint hooks
// (scene::detail::ProgramPaint, which the program specializes), and a table
// made here had them before the program said them. It is small -- it names
// no part's type -- so made in each unit that walks one.
namespace skiff::scene {
} // namespace skiff::scene

export namespace skiff::compose {
template <class... Parts> auto column(Look look, Parts... parts) {
  look.fHorizontal = false;
  return erased(Box<part_t<Parts>...>(std::move(look), as_part(std::move(parts))...));
}
template <class... Parts>
  requires(std::derived_from<Parts, scene::Node> && ...)
auto column(Parts... parts) {
  return erased(Box<part_t<Parts>...>(vbox(), as_part(std::move(parts))...));
}
template <class... Parts> auto row(Look look, Parts... parts) {
  look.fHorizontal = true;
  return erased(Box<part_t<Parts>...>(std::move(look), as_part(std::move(parts))...));
}
template <class... Parts>
  requires(std::derived_from<Parts, scene::Node> && ...)
auto row(Parts... parts) {
  return erased(Box<part_t<Parts>...>(hbox(), as_part(std::move(parts))...));
}

// A subtree whose presence and constructor arguments are model data. Made
// in place: controls may refer to themselves, so a rendered temporary must
// never be moved into the tree. Make returns the arguments, not a node.
template <class Content, class Facts, class Make> struct Mounted : Specced {
  using BindingChildren = std::tuple<Content>;
  Make fMake;
  std::optional<Content> fContent;
  std::optional<Facts> fLast;
  bool fRead = false;
  explicit Mounted(Make make, scene::Spec spec = {}, bool floats = false)
      : Specced(std::move(spec)), fMake(std::move(make)) {
    this->fState.setFloats(floats);
    this->setVisible(false);
  }
  // Made at once, from its first facts: there before anything reads them.
  Mounted(Make make, const Facts &first, scene::Spec spec = {}, bool floats = false)
      : Mounted(std::move(make), std::move(spec), floats) {
    this->read(std::optional<Facts>(first));
  }
  void read(const std::optional<Facts> &now) {
    if constexpr (requires { fLast == now; })
      if (fRead && fLast == now)
        return;
    fLast = now;
    fRead = true;
    if (now)
      std::apply(
          [&](auto &&...arguments) {
            fContent.emplace(std::forward<decltype(arguments)>(arguments)...);
          },
          fMake(*now));
    else
      fContent.reset();
    this->setVisible(now.has_value());
    this->invalidateLayout();
    this->markDamaged();
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fContent);
  }
  Content *shown() { return fContent ? &*fContent : nullptr; }
  const Content *shown() const { return fContent ? &*fContent : nullptr; }
};
template <class Content, class Facts, class Make>
Mounted<Content, Facts, Make> mount(Make make, scene::Spec spec = {},
                                    bool floats = false) {
  // Concrete in every build: what reads the model for it (bound, derived,
  // projected) reads it as itself.
  return Mounted<Content, Facts, Make>(std::move(make), std::move(spec), floats);
}
template <class Content, class Facts, class Make>
Mounted<Content, Facts, Make> mount_now(Make make, const Facts &first, scene::Spec spec = {}) {
  return Mounted<Content, Facts, Make>(std::move(make), first, std::move(spec));
}

// ---- the model's kinds, around a node ------------------------------------

template <class Want, class N>
  requires std::move_constructible<N>
auto bound(N node) {
  return erased(bind::Bound<Want, part_t<N>>(as_part(std::move(node))));
}
// A node shown what Compute makes of the model, as the parts it reads move.
template <class... Reads, class Compute, class N>
auto derived(Compute compute, N node) {
  return erased(bind::Derived<Compute, part_t<N>, Reads...>(as_part(std::move(node)), std::move(compute)));
}
template <class Within, class Handlers, class N, class... Keys>
bind::Scoped<Within, Handlers, N, Keys...> scoped(Handlers handlers, N node,
                                                  Keys... keys) {
  // Concrete in every build: what moves it (rekeyed_by) reads it as itself.
  bind::Scoped<Within, Handlers, N, Keys...> made(std::move(handlers), std::move(node), std::move(keys)...);
  over_held(made);
  return made;
}
template <class T, class N> auto local(N node, T initial = {}) {
  return erased(bind::Local<T, bind::NoHandlers, part_t<N>>({}, as_part(std::move(node)), std::move(initial)));
}
template <class T, class Handlers, class N>
  requires std::derived_from<N, scene::Node>
auto local(Handlers handlers, N node, T initial = {}) {
  return erased(bind::Local<T, Handlers, part_t<N>>(std::move(handlers), as_part(std::move(node)),
                                     std::move(initial)));
}
template <class T, class Handlers, class N>
auto hidden(Handlers handlers, N node,
                                         T initial = {}) {
  return erased(bind::Local<T, Handlers, part_t<N>, true>(std::move(handlers),
                                           as_part(std::move(node)), std::move(initial)));
}

// A row made by a callable, from the element or from nothing.
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
template <class T, class F>
using RowOf = std::invoke_result_t<const MakeRow<T, F> &, const T &>;

template <class Key, class T, class F>
auto each(F make, Look look = vbox()) {
  using Row = RowOf<T, F>;
  using Container = Box<>;
  return erased(bind::Each<Key, T, Row, Container, MakeRow<T, F>>(
      MakeRow<T, F>{std::move(make)}, Container(std::move(look))));
}

// Handlers written where they are used: handle<Removed>([](auto &here) { ... })
// -- given the place (or the event and the place) -- and several put
// together, handlers(...).
// Each a lambda's own type, kept as it is: no std::function.
template <class E, class F> struct Handle {
  F fAnswer;
  template <class P>
    requires std::invocable<const F &, const E &, const P &>
  auto on(const E &event, const P &here) const {
    return fAnswer(event, here);
  }
  // Or given where it is alone. (Asked before the event alone, each check
  // stopping at the first that holds: a generic handler of its place,
  // asked whether it took the event, was instantiated with it -- an error,
  // not a no.)
  template <class P>
    requires(!std::invocable<const F &, const E &, const P &> && std::invocable<const F &, const P &>)
  auto on(const E &, const P &here) const {
    return fAnswer(here);
  }
  // Or given the event alone: what it answers needs nothing of where it is.
  // (Without this, a handler of the event alone had no on() at all: a
  // Local's never took what it handled.)
  template <class P>
    requires(!std::invocable<const F &, const E &, const P &> && !std::invocable<const F &, const P &> &&
             std::invocable<const F &, const E &>)
  auto on(const E &event, const P &) const {
    return fAnswer(event);
  }
  // Or given nothing: what it answers needs neither.
  template <class P>
    requires(!std::invocable<const F &, const E &, const P &> && !std::invocable<const F &, const P &> &&
             !std::invocable<const F &, const E &> && std::invocable<const F &>)
  auto on(const E &, const P &) const {
    return fAnswer();
  }
};
template <class E, class F> Handle<E, F> handle(F answer) { return {std::move(answer)}; }
template <class... H> struct Handlers : H... {
  using H::on...;
};
template <class... H> Handlers<H...> handlers(H... each) { return {std::move(each)...}; }

// Answers a press with a copy of its event.
template <class E, class N> struct OnClick : N {
  using Walked = std::true_type;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  using Out = model::Types<E>;
  E fEvent;
  std::string fLabel;
  OnClick(E event, N node, std::string label = {})
      : N(std::move(node)), fEvent(std::move(event)), fLabel(std::move(label)) {
    if (!fLabel.empty()) this->fState.setCursor(scene::cursor::hand{});
  }
  bool onClick(float, float) {
    scene::pressLater(this->fState);
    return true;
  }
  E onPress() const { return fEvent; }
  bool acceptsInput() const { return true; }
  scene::Semantics semantics() const {
    scene::Semantics result;
    if constexpr (requires(const N& node) { node.semantics(); })
      result = N::semantics();
    if (!fLabel.empty()) {
      result.fRole = spl::visit(spl::overloaded{
          [](scene::semantic_role::toggle role) -> scene::SemanticRole { return role; },
          [](scene::semantic_role::tab role) -> scene::SemanticRole { return role; },
          [](auto) -> scene::SemanticRole { return scene::semantic_role::button{}; }}, result.fRole);
      result.fLabel = fLabel;
      result.fActions = {scene::semantic_action::focus{}, scene::semantic_action::activate{}};
    }
    return result;
  }
};
template <class E, class N> auto onClick(E event, N node, std::string label = {}) {
  return erased(OnClick<E, part_t<N>>(std::move(event), as_part(std::move(node)), std::move(label)));
}

// Compute a typed answer at press time, for actions whose data can change
// while the node remains mounted -- asked with nothing, or with where the
// node pressed is laid out (its own bounds): a popup opened beside the
// button that opens it.
template <class Make>
struct PressMade : std::type_identity<std::invoke_result_t<Make &>> {};
template <class Make>
  requires(!std::invocable<Make &> && std::invocable<Make &, const skia::SkRect &>)
struct PressMade<Make> : std::type_identity<std::invoke_result_t<Make &, const skia::SkRect &>> {};
template <class Make>
concept MakesPress = (std::invocable<Make &> || std::invocable<Make &, const skia::SkRect &>) &&
                     !std::is_void_v<typename PressMade<Make>::type>;
template <class Make, class N> struct OnPress : OnClick<model::Nothing, N> {
  using Out = model::Types<typename PressMade<Make>::type>;
  Make fMake;
  OnPress(Make make, N node, std::string label)
      : OnClick<model::Nothing, N>({}, std::move(node), std::move(label)), fMake(std::move(make)) {}
  auto onPress()
    requires std::invocable<Make &>
  {
    return std::invoke(fMake);
  }
  auto onPress()
    requires(!std::invocable<Make &>)
  {
    return std::invoke(fMake, std::as_const(this->fState.fBounds));
  }
};
template <class Make, class N>
  requires MakesPress<Make>
auto onPress(Make make, N node, std::string label = {}) {
  return erased(OnPress<Make, part_t<N>>(std::move(make), as_part(std::move(node)), std::move(label)));
}

// What a node says of itself to assistive technology beyond its own
// semantics: what it is (a tab of a segmented control, not a button) and
// whether it is the one chosen -- a radio row, the segment in effect.
// Read from the model where that moves (chosen_for).
template <class N> struct Said : N {
  std::optional<scene::SemanticRole> fRole;
  bool fChosen = false;
  // A name and a hint it is read out with, where its own say none.
  std::string fLabel, fHint;
  Said(std::optional<scene::SemanticRole> role, bool chosen, N node) : N(std::move(node)), fRole(std::move(role)), fChosen(chosen) {}
  void read(bool chosen) { fChosen = chosen; }
  scene::Semantics semantics() const {
    scene::Semantics out;
    if constexpr (requires(const N &node) { node.semantics(); })
      out = N::semantics();
    if (fRole)
      out.fRole = *fRole;
    if (!fLabel.empty())
      out.fLabel = fLabel;
    if (!fHint.empty())
      out.fHint = fHint;
    out.fSelected = out.fSelected || fChosen;
    return out;
  }
};
// A control named for assistive technology, and what it is for: a
// checkbox beside its words.
template <class N> auto named(std::string label, std::string hint, N node) {
  Said<part_t<N>> out(std::nullopt, false, as_part(std::move(node)));
  out.fLabel = std::move(label);
  out.fHint = std::move(hint);
  return erased(std::move(out));
}
template <class N> auto said(std::optional<scene::SemanticRole> role, bool chosen, N node) {
  return erased(Said<part_t<N>>(std::move(role), chosen, as_part(std::move(node))));
}
template <class Want, class Compute, class N> auto chosen_for(Compute compute, std::optional<scene::SemanticRole> role, N node) {
  return projected<Want>(std::move(compute), Said<part_t<N>>(std::move(role), false, as_part(std::move(node))));
}

// Answers a key with a copy of its event: Esc, as a bar is let go.
template <class E, class N, class K> struct OnKey : N {
  using Walked = std::bool_constant<bind::detail::kWalks<N>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  using Answer = E;
  K fKey;
  E fEvent;
  OnKey(K key, E event, N node) : N(std::move(node)), fKey(key), fEvent(std::move(event)) {}
  using N::onKey;
  std::optional<E> onKey(scene::phase::bubble, const scene::key::down &press, scene::Reply &reply) {
    if (press.key != fKey)
      return std::nullopt;
    reply.handle();
    return fEvent;
  }
};
template <class K, class E, class N> auto onKey(K key, E event, N node) {
  return erased(OnKey<E, part_t<N>, K>(key, std::move(event), as_part(std::move(node))));
}

// Answers the keys `ask(press)` makes something of -- Enter and Shift+Enter
// apart, the arrows: what it makes, the key taken; nothing, the key on its
// way up.
template <class Ask, class N> struct OnKeys : N {
  using Walked = std::bool_constant<bind::detail::kWalks<N>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<N>>;
  using Answer = std::remove_cvref_t<decltype(*std::declval<std::invoke_result_t<Ask &, const scene::key::down &>>())>;
  Ask fAsk;
  OnKeys(Ask ask, N node) : N(std::move(node)), fAsk(std::move(ask)) {}
  using N::onKey;
  std::optional<Answer> onKey(scene::phase::bubble, const scene::key::down &press, scene::Reply &reply) {
    auto answer = std::invoke(fAsk, press);
    if (answer)
      reply.handle();
    return answer;
  }
};
template <class Ask, class N> auto onKeys(Ask ask, N node) { return erased(OnKeys<Ask, part_t<N>>(std::move(ask), as_part(std::move(node)))); }

// What a node shows, read from the model: a text made from the parts it
// reads (Reads), and a node shown or hidden as they say -- never set by
// hand. Compute takes the parts' values.
struct TextOf : nodes::Text {
  explicit TextOf(nodes::Text text) : nodes::Text(std::move(text)) {}
  void read(const std::string &now) { this->setText(now); }
};
template <class... Reads, class Compute> auto text_of(Compute compute, nodes::Text text) {
  return erased(bind::Derived<Compute, TextOf, Reads...>(TextOf(std::move(text)), std::move(compute)));
}
template <class N> struct ShownBy : N {
  explicit ShownBy(N node) : N(std::move(node)) {}
  void read(bool on) { this->setVisible(on); }
};
template <class... Reads, class Compute, class N> auto shown_if(Compute compute, N node) {
  return erased(bind::Derived<Compute, ShownBy<part_t<N>>, Reads...>(ShownBy<part_t<N>>(as_part(std::move(node))), std::move(compute)));
}

// Project a bound part into the value a node reads. Unlike derived, this
// works below scopes and local state as well as at the root.
// A projection over a node that reads a part of its own -- shown_for over
// a spec_for, a text_for, a list -- hides that node's read under its own:
// the part it reads is passed on to it too, where it reads the same.
template <class N, class T> void readNested(N &, const T &) {}
template <class N, class T>
  requires bind::IsBound<N> &&
           std::same_as<typename bind::BoundValueT<typename std::remove_cvref_t<decltype(bind::detail::asBound(std::declval<N &>()))>::BoundTo>::type, T>
void readNested(N &node, const T &value) {
  bind::detail::asBound(node).read(value);
}
template <class Compute, class N> struct Projected : N {
  Compute fCompute;
  Projected(Compute compute, N node)
      : N(std::move(node)), fCompute(std::move(compute)) {}
  template <class T> void read(const T &value) {
    N::read(fCompute(value));
    readNested(static_cast<N &>(*this), value);
  }
};
// A function a combinator keeps: itself in a release build; outside one,
// erased to its signature -- every call site that reads the same part the
// same way then makes the same node type, its tables made once, not one
// type for each lambda.
// A closure that cannot be copied -- one holding a unique_ptr -- is kept
// as one that moves only, as release keeps it; the result is the closure's
// own, a reference staying a reference.
template <class Sig> class MovedFn;
template <class R, class... Args> class MovedFn<R(Args...)> {
  struct Base {
    virtual ~Base() = default;
    virtual R call(Args... args) = 0;
  };
  template <class F> struct Of final : Base {
    F fF;
    explicit Of(F f) : fF(std::move(f)) {}
    R call(Args... args) override { return std::invoke(fF, std::forward<Args>(args)...); }
  };
  std::unique_ptr<Base> fOf;

public:
  template <class F> explicit MovedFn(F f) : fOf(std::make_unique<Of<F>>(std::move(f))) {}
  R operator()(Args... args) { return fOf->call(std::forward<Args>(args)...); }
};
template <class Sig, class F> auto erased_fn(F f) {
  if constexpr (!scene::kErasedCombinators)
    return f;
  else if constexpr (std::copy_constructible<F>)
    return std::function<Sig>(std::move(f));
  else
    return MovedFn<Sig>(std::move(f));
}
template <class Want, class Compute, class N>
auto projected(Compute compute, N node) {
  using In = typename bind::BoundValueT<Want>::type;
  using Out = std::invoke_result_t<Compute &, const In &>;
  auto fn = erased_fn<Out(const In &)>(std::move(compute));
  return bound<Want>(
      Projected<decltype(fn), part_t<N>>(std::move(fn), as_part(std::move(node))));
}
// And as a part of the model says they do: `ask(now, press)` with the part
// as last read -- in the phase given: on their way down (capture), before a
// field under it takes them, or on their way up (bubble).
template <class Phase, class K, class Ask, class N> struct KeysFor : N {
  using Answer = std::remove_cvref_t<decltype(*std::declval<std::invoke_result_t<Ask &, const K &, const scene::key::down &>>())>;
  Ask fAsk;
  K fNow{};
  KeysFor(Ask ask, N node) : N(std::move(node)), fAsk(std::move(ask)) {}
  void read(const K &now) { fNow = now; }
  using N::onKey;
  std::optional<Answer> onKey(Phase, const scene::key::down &press, scene::Reply &reply) {
    auto answer = std::invoke(fAsk, std::as_const(fNow), press);
    if (answer)
      reply.handle();
    return answer;
  }
};
template <class Want, class Phase, class Compute, class Ask, class N> auto keys_for(Compute compute, Ask ask, N node) {
  using K = std::remove_cvref_t<std::invoke_result_t<Compute &, const typename bind::BoundValueT<Want>::type &>>;
  return projected<Want>(std::move(compute), KeysFor<Phase, K, Ask, part_t<N>>(std::move(ask), as_part(std::move(node))));
}
// Visibility projected from a bound part, including a scope or local model.
template <class Want, class Compute, class N>
auto shown_for(Compute compute, N node) {
  return projected<Want>(std::move(compute), ShownBy<part_t<N>>(as_part(std::move(node))));
}
// A node shown what it reads by a function said where it is put --
// `apply(node, value)` -- for a leaf with no read of its own: an icon's shape
// and colour, an avatar's person. What it shows is still the model's; only
// how the value lands on the node is said here, once.
template <class Apply, class N> struct Applied : N {
  Apply fApply;
  Applied(Apply apply, N node) : N(std::move(node)), fApply(std::move(apply)) {}
  template <class T> void read(const T &value) {
    std::invoke(fApply, static_cast<N &>(*this), value);
    this->markDamaged();
  }
};
template <class Want, class Compute, class Apply, class N>
auto applied(Compute compute, Apply apply, N node) {
  using In = typename bind::BoundValueT<Want>::type;
  using Out = std::remove_cvref_t<std::invoke_result_t<Compute &, const In &>>;
  auto fn = erased_fn<void(N &, const Out &)>(std::move(apply));
  return projected<Want>(std::move(compute), Applied<decltype(fn), N>(std::move(fn), std::move(node)));
}
// A field given the keys as the model says: each new request -- a count the
// model raises for it, `compute(value)` -- asks its scene to focus it at its
// next layout; 0 asks nothing. A field made again (the window remade in
// another theme) takes the keys back where the last request was its own.
template <class N> struct FocusedBy : N {
  std::uint64_t fSeen = 0;
  explicit FocusedBy(N node) : N(std::move(node)) {}
  void read(std::uint64_t asked) {
    // The node that takes the keys: over held content, what it holds -- the
    // field itself -- not this wrapper.
    if (asked != 0 && asked != fSeen)
      scene::work::focusWanted() = state_of(static_cast<N &>(*this)).fId;
    fSeen = asked;
  }
};
template <class Want, class Compute, class N>
auto focused_by(Compute compute, N node) {
  return projected<Want>(std::move(compute), FocusedBy<part_t<N>>(as_part(std::move(node))));
}
template <class N> struct SpecOf : N {
  explicit SpecOf(N node) : N(std::move(node)) {}
  void read(const scene::Spec &spec) { this->apply(spec); }
};
template <class Want, class Compute, class N>
auto spec_for(Compute compute, N node) {
  return projected<Want>(std::move(compute), SpecOf<part_t<N>>(as_part(std::move(node))));
}
template <class Want, class Compute>
auto text_for(Compute compute, nodes::Text text) {
  return projected<Want>(std::move(compute), TextOf(std::move(text)));
}
// A scope at the part what it reads says -- the chat chosen, as what is
// shown has it: Compute makes the scope's keys of the value read, and the
// scope is moved to them (rekey), shown that part by the next refresh of
// its model that walks it.
template <class Compute, class N> struct Rekeyed : N {
  Compute fCompute;
  Rekeyed(Compute compute, N node) : N(std::move(node)), fCompute(std::move(compute)) {}
  template <class T> void read(const T &value) {
    std::apply([&](auto &&...keys) { this->rekey(std::forward<decltype(keys)>(keys)...); }, fCompute(value));
  }
};
template <class Want, class Compute, class N>
auto rekeyed_by(Compute compute, N node) {
  return bound<Want>(Rekeyed<Compute, part_t<N>>(std::move(compute), as_part(std::move(node))));
}

// What sections show: their parts, one under another, the tabs of every
// one there is -- the same, unless what is shown is a search's results,
// when the tabs stay every group's and none is lit -- and whether the tab of
// the part at the top is lit.
template <class Element> struct SectionsShown {
  std::vector<Element> parts;
  std::vector<Element> tabs;
  bool lit = true;
};

// The strips sections put their tabs in, made for the tabs' type by the
// maker sections are given. A strip takes the tabs, each with its element,
// lights one, and is the node they are shown in.
//
// All of them in rows that wrap, in a view of its own height that scrolls to
// the one lit; shown where there is more than one.
template <class Element, class Button> struct WrappedStrip : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<Button>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<Button>>;
  nodes::ScrollContainer<Many<Button>> fView;
  WrappedStrip(Look view, Look row) : fView(Many<Button>(std::move(row), {})) {
    this->fState.apply(view.fSpec);
    std::get<0>(fView.fChildren).fStack.wrap = true;
    fView.apply({.fill = true});
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) { f(self.fView); }
  [[nodiscard]] std::vector<Button> &buttons() { return std::get<0>(fView.fChildren).fParts; }
  void put(const std::vector<Element> &, std::vector<Button> made) {
    buttons() = std::move(made);
    this->setVisible(buttons().size() > 1);
    fView.invalidateLayout();
    fView.scrollToStart();
  }
  // Lit, or none (an index past them all).
  void light(std::size_t at) {
    for (auto &&[index, tab] : buttons() | std::views::enumerate) {
      const bool on = static_cast<std::size_t>(index) == at;
      if (on == tab.fState.selected())
        continue;
      tab.fState.apply({.selected = on});
      if (on && !tab.bounds().isEmpty()) {
        const auto shown = fView.toView(tab.bounds());
        if (shown.fTop < fView.bounds().fTop || shown.fBottom > fView.bounds().fBottom)
          fView.scrollTo(tab.bounds().fTop - fView.bounds().fTop);
      }
    }
  }
};
template <class Element> struct Wrapped {
  Look fView, fRow;
  template <class Button> WrappedStrip<Element, Button> operator()(std::type_identity<Button>) const { return {fView, fRow}; }
};
template <class Element> Wrapped<Element> wrapped(Look view, Look row) { return {std::move(view), std::move(row)}; }

// All of them in one row, as it is laid out -- each as wide as it says,
// growing to fill it where it says so: a panel's category row.
template <class Element, class Button> struct RowStrip : Many<Button> {
  explicit RowStrip(Look look) : Many<Button>(std::move(look), {}) {}
  void put(const std::vector<Element> &, std::vector<Button> made) {
    this->fParts = std::move(made);
    this->invalidateLayout();
  }
  void light(std::size_t at) {
    for (auto &&[index, tab] : this->fParts | std::views::enumerate)
      tab.fState.apply({.selected = static_cast<std::size_t>(index) == at});
  }
};
template <class Element> struct InRow {
  Look fLook;
  template <class Button> RowStrip<Element, Button> operator()(std::type_identity<Button>) const { return RowStrip<Element, Button>(fLook); }
};
template <class Element> InRow<Element> in_row(Look look) { return {std::move(look)}; }

// As many as its width holds, a page of them at a time, between the arrows
// that page back and on -- each arrow off where there is no page that way --
// paged to the one lit; shown where there is any.
template <class Element, class Button, class MakeArrow> struct PagedStrip : nodes::Stack {
  using Arrow = std::invoke_result_t<MakeArrow &, bool>;
  // An arrow's press: the page before, or the next.
  struct Page {
    PagedStrip *of = nullptr;
    bool next = false;
    scene::Taken operator()() const {
      of->page(next);
      return {};
    }
  };
  using ArrowButton = OnPress<Page, Arrow>;
  using Walked = std::bool_constant<kAnyWalks<Button, Arrow>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<Button> || bind::detail::kHasLocal<Arrow>>;
  MakeArrow fMakeArrow;
  float fTabWidth = 30.0f, fArrowsWidth = 60.0f;
  std::string fBackLabel, fOnLabel;
  std::optional<ArrowButton> fBack, fOn;
  Many<Button> fRow;
  std::size_t fFirst = 0, fHolds = 1;
  PagedStrip(Look look, Look row, float tabWidth, float arrowsWidth, MakeArrow makeArrow, std::string backLabel, std::string onLabel)
      : fMakeArrow(std::move(makeArrow)), fTabWidth(tabWidth), fArrowsWidth(arrowsWidth), fBackLabel(std::move(backLabel)),
        fOnLabel(std::move(onLabel)), fRow(std::move(row), {}) {
    this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    if (self.fBack)
      f(*self.fBack);
    f(self.fRow);
    if (self.fOn)
      f(*self.fOn);
  }
  [[nodiscard]] std::vector<Button> &buttons() { return fRow.fParts; }
  void put(const std::vector<Element> &, std::vector<Button> made) {
    // The arrows made where it is kept: they point at it.
    if (!fBack) {
      fBack.emplace(Page{this, false}, fMakeArrow(false), fBackLabel);
      fOn.emplace(Page{this, true}, fMakeArrow(true), fOnLabel);
    }
    buttons() = std::move(made);
    fFirst = 0;
    this->setVisible(!buttons().empty());
    this->invalidateLayout();
  }
  void page(bool next) {
    if (next && fFirst + fHolds < buttons().size())
      fFirst += fHolds;
    else if (!next)
      fFirst = fFirst > fHolds ? fFirst - fHolds : 0;
    this->fState.relayoutQuietly();
  }
  void light(std::size_t at) {
    for (auto &&[index, tab] : buttons() | std::views::enumerate) {
      const bool on = static_cast<std::size_t>(index) == at;
      if (on && !tab.fState.selected() && !tab.visible()) {
        fFirst = static_cast<std::size_t>(index) / fHolds * fHolds;
        this->fState.relayoutQuietly();
      }
      tab.fState.apply({.selected = on});
    }
  }
  void layoutChildren() {
    fHolds = static_cast<std::size_t>(std::max(1.0f, std::floor((this->fState.contentBox().width() - fArrowsWidth) / fTabWidth)));
    fFirst = buttons().empty() ? 0 : std::min(fFirst / fHolds, (buttons().size() - 1) / fHolds) * fHolds;
    for (auto &&[index, tab] : buttons() | std::views::enumerate)
      tab.setVisible(static_cast<std::size_t>(index) >= fFirst && static_cast<std::size_t>(index) < fFirst + fHolds);
    if (fBack) {
      fBack->apply({.disabled = fFirst == 0});
      fOn->apply({.disabled = fFirst + fHolds >= buttons().size()});
    }
    this->nodes::Stack::layoutChildren();
  }
};
template <class Element, class MakeArrow> struct Paged {
  Look fLook, fRow;
  float fTabWidth, fArrowsWidth;
  MakeArrow fMakeArrow;
  std::string fBackLabel, fOnLabel;
  template <class Button> PagedStrip<Element, Button, MakeArrow> operator()(std::type_identity<Button>) const {
    return {fLook, fRow, fTabWidth, fArrowsWidth, fMakeArrow, fBackLabel, fOnLabel};
  }
};
template <class Element, class MakeArrow>
Paged<Element, MakeArrow> paged(Look look, Look row, float tabWidth, float arrowsWidth, MakeArrow makeArrow,
                                std::string backLabel = "Previous page", std::string onLabel = "Next page") {
  return {std::move(look), std::move(row), tabWidth, arrowsWidth, std::move(makeArrow), std::move(backLabel), std::move(onLabel)};
}

// Two strips one over the other, each tab in the first or the second as its
// element says (`second(element)`): an emoji panel's groups over its packs.
template <class Element, class Button, class Second, class First, class Then> struct SplitStrip : nodes::Stack {
  using Walked = std::bool_constant<kAnyWalks<First, Then>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<First> || bind::detail::kHasLocal<Then>>;
  Second fSecond;
  First fFirst;
  Then fThen;
  // Where each tab went: in the second, and its place there.
  std::vector<std::pair<bool, std::size_t>> fWhere;
  SplitStrip(Look look, Second second, First first, Then then)
      : fSecond(std::move(second)), fFirst(std::move(first)), fThen(std::move(then)) {
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fFirst);
    f(self.fThen);
  }
  void put(const std::vector<Element> &elements, std::vector<Button> made) {
    std::vector<Element> firstElements, thenElements;
    std::vector<Button> firstMade, thenMade;
    fWhere.clear();
    for (auto &&[element, button] : std::views::zip(elements, made)) {
      const bool second = fSecond(element);
      fWhere.emplace_back(second, second ? thenMade.size() : firstMade.size());
      (second ? thenElements : firstElements).push_back(element);
      (second ? thenMade : firstMade).push_back(std::move(button));
    }
    fFirst.put(firstElements, std::move(firstMade));
    fThen.put(thenElements, std::move(thenMade));
  }
  void light(std::size_t at) {
    const auto none = std::numeric_limits<std::size_t>::max();
    const bool known = at < fWhere.size();
    fFirst.light(known && !fWhere[at].first ? fWhere[at].second : none);
    fThen.light(known && fWhere[at].first ? fWhere[at].second : none);
  }
};
template <class Element, class Second, class MakeFirst, class MakeThen> struct Split {
  Look fLook;
  Second fSecond;
  MakeFirst fMakeFirst;
  MakeThen fMakeThen;
  template <class Button> auto operator()(std::type_identity<Button> button) const {
    using First = decltype(fMakeFirst(button));
    using Then = decltype(fMakeThen(button));
    return SplitStrip<Element, Button, Second, First, Then>(fLook, fSecond, fMakeFirst(button), fMakeThen(button));
  }
};
template <class Element, class Second, class MakeFirst, class MakeThen>
Split<Element, Second, MakeFirst, MakeThen> split(Look look, Second second, MakeFirst first, MakeThen then) {
  return {std::move(look), std::move(second), std::move(first), std::move(then)};
}

// A list of sections, one under another in a view that scrolls, and a strip
// of their tabs: a tab pressed asks its owner what that does first --
// leaving a search, say -- and brings its section to the top of the view,
// once it is laid out; the tab of the section at the top is lit, where what
// is shown says so. Made from what it reads (SectionsShown), each element's tab
// made by `makeTab`, its section by `makePart`, the strip by `makeStrip`:
// a picker's packs, an emoji panel's groups.
template <class Element, class MakeTab, class MakePart, class MakeStrip, class OnTab> struct Sections : nodes::Stack {
  using Tab = std::invoke_result_t<MakeTab &, const Element &>;
  using Part = std::invoke_result_t<MakePart &, const Element &>;
  // A tab's press: what its owner says, and its section brought.
  struct Bring {
    Sections *of = nullptr;
    std::size_t at = 0;
    std::invoke_result_t<const OnTab &, std::size_t> operator()() const {
      of->fBringing = at;
      return std::invoke(of->fOnTab, at);
    }
  };
  using TabButton = OnPress<Bring, Tab>;
  using Strip = std::invoke_result_t<const MakeStrip &, std::type_identity<TabButton>>;
  using List = nodes::ScrollContainer<Many<Part>>;
  using Walked = std::bool_constant<kAnyWalks<Part, Tab>>;
  using HasLocal = std::bool_constant<bind::detail::kHasLocal<Part> || bind::detail::kHasLocal<Tab>>;
  MakeTab fMakeTab;
  MakePart fMakePart;
  OnTab fOnTab;
  List fList;
  Strip fStrip;
  bool fLit = true;
  std::optional<std::size_t> fBringing;

  Sections(Look look, Look parts, MakeTab makeTab, MakePart makePart, const MakeStrip &makeStrip, OnTab onTab)
      : fMakeTab(std::move(makeTab)), fMakePart(std::move(makePart)), fOnTab(std::move(onTab)),
        fList(Many<Part>(std::move(parts), {})), fStrip(makeStrip(std::type_identity<TabButton>{})) {
    if (look.fHorizontal)
      this->setHorizontal();
    this->setGap(look.fGap);
    this->fState.apply(look.fSpec);
    this->fStack.justify = look.fJustify;
    fList.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(fList.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) {
    f(self.fList);
    f(self.fStrip);
  }
  [[nodiscard]] std::vector<Part> &parts() { return std::get<0>(fList.fChildren).fParts; }

  // The parts made again, the view at its start; the tabs too, where they
  // are others.
  void read(const SectionsShown<Element> &shown) {
    parts() = shown.parts | std::views::transform(std::ref(fMakePart)) | std::ranges::to<std::vector>();
    fStrip.put(shown.tabs, shown.tabs | std::views::enumerate | std::views::transform([&](const auto &one) {
                             const auto &[at, element] = one;
                             return TabButton(Bring{this, static_cast<std::size_t>(at)}, fMakeTab(element), {});
                           }) | std::ranges::to<std::vector>());
    fLit = shown.lit;
    fList.invalidateLayout();
    fList.scrollTo(0.0f);
  }
  // A section brought to the top of the view.
  void bring(std::size_t at) {
    if (at >= parts().size() || parts()[at].bounds().isEmpty())
      return;
    fList.scrollTo(std::max(0.0f, fList.current() + (fList.toView(parts()[at].bounds()).fTop - fList.bounds().fTop)));
  }
  // The section a tab asked for, brought once it is laid out; the tab of
  // the section at the top lit.
  [[nodiscard]] bool settling() const { return fBringing.has_value(); }
  void update(double) {
    if (fBringing && fBringing < parts().size() && !parts()[*fBringing].bounds().isEmpty())
      bring(*std::exchange(fBringing, std::nullopt));
    const float top = fList.bounds().fTop + 1.0f;
    // The last of those whose top has reached the view's: they come in order.
    const std::size_t lit = std::ranges::fold_left(
        std::views::iota(std::size_t{0}, parts().size()) | std::views::filter([&](std::size_t at) {
          return !parts()[at].bounds().isEmpty() && fList.toView(parts()[at].bounds()).fTop <= top;
        }),
        std::size_t{0}, [](std::size_t, std::size_t at) { return at; });
    fStrip.light(fLit ? lit : std::numeric_limits<std::size_t>::max());
  }
};
// A tab's press, where it asks nothing of its owner first.
struct OnlyBrings {
  scene::Taken operator()(std::size_t) const { return {}; }
};
template <class Element, class MakeTab, class MakePart, class MakeStrip, class OnTab = OnlyBrings>
auto sections(Look look, Look parts, MakeTab makeTab, MakePart makePart, const MakeStrip &makeStrip, OnTab onTab = {}) {
  // Concrete in every build: what reads the model for it reads it as itself.
  return Sections<Element, MakeTab, MakePart, MakeStrip, OnTab>(std::move(look), std::move(parts), std::move(makeTab),
                                                                std::move(makePart), makeStrip, std::move(onTab));
}

// A picture sized as a chat sizes one: its own size, or its picture's own
// proportions once that has come (`ratio` of the node), fitted into `most`
// by `most` and into the room there is, its proportions kept; no side under
// `least` where the room allows. A cell of an album: as wide and as tall as
// it is said (cell), whatever its picture.
struct Fit {
  float width = 0.0f, height = 0.0f;  // as the message says it; none: 320 by 240
  float most = 430.0f, least = 100.0f;
  std::optional<std::pair<float, float>> cell;
};
template <class Ratio, class N> struct Fitted : N {
  Fit fFit;
  Ratio fRatio;
  Fitted(Fit fit, Ratio ratio, N node) : N(std::move(node)), fFit(fit), fRatio(std::move(ratio)) {
    sizedHere(static_cast<N &>(*this), scene::axes::kBoth);
  }
  void measure(const skia::SkRect &parent) {
    if (fFit.cell) {
      this->fState.fWidth = fFit.cell->first;
      this->fState.fHeight = fFit.cell->second;
      return;
    }
    float w = fFit.width > 0.0f ? fFit.width : 320.0f;
    float h = fFit.height > 0.0f ? fFit.height : 240.0f;
    // The picture's own proportions, where the message said none or others.
    if (const std::optional<float> ratio = std::invoke(fRatio, static_cast<N &>(*this));
        ratio && (fFit.width <= 0.0f || fFit.height <= 0.0f || std::abs(w / h - *ratio) > 0.01f))
      h = w / *ratio;
    const float room = parent.width() > 0.0f ? parent.width() : fFit.most;
    const float scale = std::min({1.0f, fFit.most / w, fFit.most / h, room / w});
    w *= scale;
    h *= scale;
    if (w < fFit.least && h < fFit.least) {
      const float up = std::min(fFit.least / std::max(w, h), room / w);
      w *= up;
      h *= up;
    }
    this->fState.fWidth = std::floor(w);
    this->fState.fHeight = std::floor(h);
  }
};
template <class Ratio, class N> auto fitted(Fit fit, Ratio ratio, N node) {
  return erased(Fitted<Ratio, N>(fit, std::move(ratio), std::move(node)));
}

// A picture and what turns over it while it comes: the second shown while
// the first has no picture yet, and gone as it comes -- looked at only while
// it is coming.
template <class Picture, class Loader> struct WhileComing : Box<Picture, Loader> {
  WhileComing(Look look, Picture picture, Loader loader) : Box<Picture, Loader>(std::move(look), std::move(picture), std::move(loader)) {}
  [[nodiscard]] bool wantsTick() const { return std::get<1>(this->fParts).visible(); }
  void update(double) {
    auto &[picture, loader] = this->fParts;
    const bool coming = picture.visible() && picture.image() == nullptr;
    if (coming != loader.visible())
      loader.setVisible(coming);
  }
};
template <class Picture, class Loader> auto while_coming(Look look, Picture picture, Loader loader) {
  return erased(WhileComing<Picture, Loader>(std::move(look), std::move(picture), std::move(loader)));
}

// Laid out at most as wide as its room, its natural width `wide`: narrower,
// the share it is laid out at told to `fit(node, share)` -- an album's cells
// all shrunk alike, a gap `gap` kept -- once each time the share changes.
template <class Fit_, class N> struct Shrunk : N {
  float fWide, fGap, fShare = 1.0f;
  Fit_ fFit;
  Shrunk(float wide, float gap, Fit_ fit, N node) : N(std::move(node)), fWide(wide), fGap(gap), fFit(std::move(fit)) {}
  void measure(const skia::SkRect &parent) {
    const float share = parent.width() > 0.0f ? std::min(1.0f, (parent.width() - fGap) / (fWide - fGap)) : 1.0f;
    if (share <= 0.0f || share == fShare)
      return;
    fShare = share;
    std::invoke(fFit, static_cast<N &>(*this), share);
  }
};
template <class Fit_, class N> auto shrunk(float wide, float gap, Fit_ fit, N node) {
  return erased(Shrunk<Fit_, N>(wide, gap, std::move(fit), std::move(node)));
}

// Placed at a point of the window a part of the model says -- in window
// coordinates, as what asked for it saw them -- out of its parent's flow,
// kept inside its parent's box; shown where there is one: a popover over
// the cell it is for.
template <class N> struct PlacedAt : N {
  std::optional<skia::SkPoint> fAt;
  explicit PlacedAt(N node) : N(std::move(node)) {
    this->fState.apply({.place = scene::anchor::kTopLeft});
    this->setVisible(false);
  }
  void read(const std::optional<skia::SkPoint> &at) {
    fAt = at;
    this->setVisible(at.has_value());
    this->invalidateLayout();
  }
  // Where it is laid out, set from where it is drawn: its parent's box, as
  // its layout last gave it, and the shift of all above it.
  [[nodiscard]] bool settling() const { return fAt.has_value() && !placed(); }
  void update(double) {
    if (!fAt || placed())
      return;
    const skia::SkRect box = this->fState.fLastConstraint;
    const auto shift = scene::work::drawnOffset(this->fState.fId);
    const skia::SkRect &self = this->fState.fBounds;
    const float x = std::clamp(fAt->fX - shift.x, box.fLeft, std::max(box.fLeft, box.fRight - self.width()));
    const float y = std::clamp(fAt->fY - shift.y, box.fTop, std::max(box.fTop, box.fBottom - self.height()));
    this->setPosition(x - box.fLeft, y - box.fTop);
  }

private:
  [[nodiscard]] bool placed() const {
    const skia::SkRect box = this->fState.fLastConstraint;
    if (box.isEmpty() || this->fState.fBounds.isEmpty())
      return false;
    const auto shift = scene::work::drawnOffset(this->fState.fId);
    const float x = std::clamp(fAt->fX - shift.x, box.fLeft, std::max(box.fLeft, box.fRight - this->fState.fBounds.width()));
    const float y = std::clamp(fAt->fY - shift.y, box.fTop, std::max(box.fTop, box.fBottom - this->fState.fBounds.height()));
    return std::abs(this->fState.fBounds.fLeft - x) < 0.5f && std::abs(this->fState.fBounds.fTop - y) < 0.5f;
  }
};
template <class Want, class Compute, class N> auto placed_at(Compute compute, N node) {
  return projected<Want>(std::move(compute), PlacedAt<part_t<N>>(as_part(std::move(node))));
}

// Where a popover opens, in window coordinates: at a point (a menu, at the
// pointer) -- going down and right, up where it would pass its parent's
// bottom, left where it would pass its right -- or beside a box (a row's
// list, opening from it), over its edge by `overlap`, to its right where
// there is room, else its left, its top at the box's or higher where it
// would pass the bottom. Kept `edge` inside its parent either way.
struct PopAt {
  skia::SkRect from = skia::SkRect::MakeEmpty();
  bool beside = false;
  float overlap = 0.0f;
  friend bool operator==(const PopAt &, const PopAt &) = default;
};
template <class N> struct Popped : N {
  std::optional<PopAt> fAt;
  bool fPlaced = false;
  float fEdge;
  Popped(float edge, N node) : N(std::move(node)), fEdge(edge) {
    this->fState.apply({.place = scene::anchor::kTopLeft});
    this->setVisible(false);
  }
  // Opened: unseen, and ticked, until it is where it goes -- laid out first
  // where its place is not known yet (its size is not), it was drawn there,
  // in the top left corner, and said it was settled: moved only when
  // something else ticked it.
  void read(const std::optional<PopAt> &at) {
    if (fAt == at)
      return;
    fAt = at;
    fPlaced = false;
    this->setVisible(at.has_value());
    if (at)
      this->apply({.alpha = 0.0f});
    this->invalidateLayout();
    scene::work::mark(this->fState.fId);
  }
  [[nodiscard]] bool settling() const {
    return (fAt.has_value() && (!fPlaced || this->target() != this->now())) || N::settling();
  }
  [[nodiscard]] bool wantsTick() const {
    if constexpr (requires(const N &node) { node.wantsTick(); })
      return (fAt.has_value() && !fPlaced) || N::wantsTick();
    else
      return fAt.has_value() && !fPlaced;
  }
  void update(double nowMs) {
    N::update(nowMs);
    if (!fAt || this->fState.fBounds.isEmpty())
      return;
    const skia::SkPoint want = this->target();
    if (want != this->now()) {
      const skia::SkRect box = this->fState.fLastConstraint;
      this->setPosition(want.fX - box.fLeft, want.fY - box.fTop);
      return;
    }
    if (!std::exchange(fPlaced, true))
      this->apply({.alpha = 1.0f});
  }

private:
  [[nodiscard]] skia::SkPoint now() const {
    return {std::round(this->fState.fBounds.fLeft), std::round(this->fState.fBounds.fTop)};
  }
  // Where it goes, in its parent's coordinates as laid out.
  [[nodiscard]] skia::SkPoint target() const {
    const skia::SkRect box = this->fState.fLastConstraint;
    const skia::SkRect &self = this->fState.fBounds;
    if (box.isEmpty() || self.isEmpty())
      return this->now();
    const auto shift = scene::work::drawnOffset(this->fState.fId);
    const skia::SkRect from = fAt->from.makeOffset(-shift.x, -shift.y);
    const float w = self.width(), h = self.height();
    float x = 0.0f, y = 0.0f;
    if (fAt->beside) {
      x = from.fRight - fAt->overlap;
      if (x + w > box.fRight - fEdge)
        x = from.fLeft + fAt->overlap - w;
      y = std::min(from.fTop, box.fBottom - fEdge - h);
    } else {
      x = from.fLeft + w > box.fRight - fEdge ? from.fLeft - w : from.fLeft;
      y = from.fTop + h > box.fBottom - fEdge ? from.fTop - h : from.fTop;
    }
    x = std::clamp(x, box.fLeft + fEdge, std::max(box.fLeft + fEdge, box.fRight - w - fEdge));
    y = std::clamp(y, box.fTop + fEdge, std::max(box.fTop + fEdge, box.fBottom - h - fEdge));
    return {std::round(x), std::round(y)};
  }
};
template <class Want, class Compute, class N> auto popped_at(Compute compute, float edge, N node) {
  return projected<Want>(std::move(compute), Popped<part_t<N>>(edge, as_part(std::move(node))));
}


// Pages in a view that scrolls, one up at a time, chosen by what it reads:
// a page made by `show` from the facts -- which says whether it is another
// page than the one up -- comes in sliding from the side `side` says and
// fading in; the same page made again is made where it was scrolled to, at
// the next frame, without a slide. Where `pin(facts)` gives a page a header
// -- its first part, looking as the spec it gives says -- the header stays
// at the top while the rest scrolls, drawn over it; found as the walks see
// the page, not by any type of its parts.
//
//   page_view<Facts, Page>(show, same, side, pin, first_page, spec)
template <class Facts, class Page, class Show, class Same, class Side, class Pin> struct PageView : Specced {
  using BindingChildren = std::tuple<Page>;
  Show fShow;
  Same fSame;
  Side fSide;
  Pin fPin;
  nodes::ScrollContainer<Page> fScroll;
  paint::Tween fSwap{1.0f, 200.0f, paint::movement::subtle{}};
  float fFrom = 1.0f;
  std::optional<float> fOffsetDue;
  std::optional<Facts> fShown, fDue;
  PageView(Show show, Same same, Side side, Pin pin, Page first, scene::Spec spec)
      : Specced(spec), fShow(std::move(show)), fSame(std::move(same)), fSide(std::move(side)), fPin(std::move(pin)),
        fScroll(std::move(first)) {
    fScroll.apply({.fill = true});
    fScroll.setCopiesOnScroll(false);
    fit();
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) { f(self.fScroll); }
  [[nodiscard]] Page &page() { return std::get<0>(fScroll.fChildren); }
  // The facts it shows now: another page slid in, the same one made again
  // in place at the next frame.
  void read(const Facts &now) {
    if (fShown && fSame(*fShown, now)) {
      fDue = now;
      scene::work::mark(this->fState.fId);
      return;
    }
    fShow(page(), now);
    scene::work::subtreeMade() = true;
    fShown = now;
    fDue.reset();
    this->lookPinned();
    fFrom = fSide(now);
    fit();
    keepOffset(0.0f);
    fSwap.jump(0.0f);
    fSwap.setTarget(1.0f);
    this->invalidateLayout();
  }
  // Where it is scrolled to, and kept there as it is laid out again.
  [[nodiscard]] float offset() const { return fScroll.current(); }
  // The same, as a view says it (kept_scroll).
  [[nodiscard]] float current() const { return this->offset(); }
  void setCurrent(float at) { this->keepOffset(at); }
  void keepOffset(float at) {
    fOffsetDue = at;
    this->invalidateLayout();
  }
  [[nodiscard]] bool settling() const { return fSwap.moving() || fDue.has_value(); }
  [[nodiscard]] bool wantsTick() const { return settling(); }
  void update(double nowMs) {
    if (auto due = std::exchange(fDue, std::nullopt)) {
      const float at = fScroll.current();
      fShow(page(), *due);
      scene::work::subtreeMade() = true;
      fShown = std::move(due);
      this->lookPinned();
      fit();
      keepOffset(at);
    }
    if (fSwap.step(nowMs))
      this->invalidateLayout();
  }
  void draw(scene::Painting &painting, skia::SkCanvas *canvas, float alpha) {
    if (fShown && fPin(*fShown))
      this->header([&](scene::State &header) { header.setShift(0.0f, -fScroll.contentsShift()); });
    scene::drawDefault(*this, painting, canvas, alpha);
  }
  void layoutChildren() {
    const skia::SkRect box = this->fState.contentBox();
    // The offset kept through measuring and the box's resizing: a view
    // otherwise follows its end when a short page becomes one that scrolls.
    if (auto at = std::exchange(fOffsetDue, std::nullopt);
        at || fScroll.bounds().width() != box.width() || fScroll.bounds().height() != box.height())
      fScroll.setCurrent(at.value_or(fScroll.current()));
    fScroll.fState.arrange(0.0f, 0.0f);
    const float value = fSwap.value();
    fScroll.fState.setAlpha(value);
    scene::layout(fScroll, skia::SkRect::MakeXYWH(box.fLeft + (1.0f - value) * 32.0f * fFrom, box.fTop, box.width(), box.height()));
  }

 private:
  // A page fills the view across, as tall as what it holds.
  void fit() {
    spl::visit([](auto &one) { one.apply({.relativeSize = scene::axes::kX, .autoSize = scene::axes::kY}); }, page());
  }
  // The page's header -- its first part, as the walks see it -- given to f.
  // A page held whole by a node of one part -- its own state over it
  // (local), and outside a release build what holds that -- is gone into to
  // its own parts: the node of one part taken for the header, the whole page
  // was pinned, shifted back as far as it scrolled -- the bar moved, the
  // page stood still.
  template <class F> void header(F &&f) {
    spl::visit([&](auto &one) {
      bool first = true;
      if constexpr (scene::kErasedWalks) {
        auto seen = scene::AnyNodeRef::of(one);
        for (;;) {
          std::size_t count = 0;
          std::optional<scene::AnyNodeRef> only;
          seen.forEachChild([&](scene::AnyNodeRef &part) {
            if (count++ == 0)
              only.emplace(part);
          });
          if (count != 1)
            break;
          seen = *only;
        }
        seen.forEachChild([&](scene::AnyNodeRef &part) {
          if (std::exchange(first, false))
            f(part.state());
        });
      } else {
        scene::eachChild(one, [&](auto &part) {
          if (std::exchange(first, false))
            f(scene::stateOf(part));
        });
      }
    }, page());
  }
  // The header looking as the facts shown say, where they give the page one.
  void lookPinned() {
    if (const std::optional<scene::Spec> look = fPin(*fShown))
      this->header([&](scene::State &header) { header.apply(*look); });
  }
};
template <class Facts, class Page, class Show, class Same, class Side, class Pin>
PageView<Facts, Page, Show, Same, Side, Pin> page_view(Show show, Same same, Side side, Pin pin, Page first, scene::Spec spec = {}) {
  // Concrete in every build: what keeps its offset (kept_scroll) reads it.
  return {std::move(show), std::move(same), std::move(side), std::move(pin), std::move(first), std::move(spec)};
}

} // namespace skiff::compose
