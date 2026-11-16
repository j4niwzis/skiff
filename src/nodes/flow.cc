export module skiff.nodes.flow;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;



export namespace skiff::nodes {

// Along which axis a flow lays its children.
namespace direction {
using skiff::scene::direction::vertical;
using skiff::scene::direction::horizontal;
} // namespace direction
using skiff::scene::Direction;
namespace justify {
using skiff::scene::justify::start;
using skiff::scene::justify::middle;
using skiff::scene::justify::end;
using skiff::scene::justify::space_between;
using skiff::scene::justify::space_around;
} // namespace justify
using skiff::scene::Justify;
using skiff::scene::FlowOptions;

// Where a line starts and how much goes between its items, given how much
// room it did not use. Pure arithmetic, usable at compile time.
struct Spread {
  float fStart = 0.0f;
  float fBetween = 0.0f;
  constexpr bool operator==(const Spread &) const = default;
};
[[nodiscard]] constexpr Spread spread(const Justify &how, float room,
                                      float used, int count) {
  const float slack = std::max(0.0f, room - used);
  const auto gaps = static_cast<float>(std::max(0, count - 1));
  const auto items = static_cast<float>(count);
  return spl::visit(
      spl::overloaded{
          [](justify::start) { return Spread{}; },
          [&](justify::middle) { return Spread{slack * 0.5f, 0.0f}; },
          [&](justify::end) { return Spread{slack, 0.0f}; },
          [&](justify::space_between) {
            return Spread{0.0f, gaps > 0.0f ? slack / gaps : 0.0f};
          },
          [&](justify::space_around) {
            return count > 0 ? Spread{slack / items * 0.5f, slack / items}
                             : Spread{};
          }},
      how);
}
static_assert(spread(justify::end{}, 100.0f, 60.0f, 3) == Spread{40.0f, 0.0f});
static_assert(spread(justify::space_between{}, 100.0f, 60.0f, 3) ==
              Spread{0.0f, 20.0f});

// Where each item of a line goes along it, from its extent: the line's
// arithmetic, apart from the tree.
template <std::size_t N>
[[nodiscard]] constexpr std::array<float, N>
stack(const std::array<float, N> &extents, float spacing, Spread spread) {
  std::array<float, N> at{};
  float position = spread.fStart;
  for (std::size_t i = 0; i < N; ++i) {
    at[i] = position;
    position += extents[i] + spacing + spread.fBetween;
  }
  return at;
}
static_assert(stack<3>({10.0f, 20.0f, 30.0f}, 4.0f, {}) ==
              std::array<float, 3>{0.0f, 14.0f, 38.0f});

// A flow: children laid end to end, wrapping when they run out of
// room: every list, every row of filters.
namespace detail {
// How a flow lays out a node's children -- the ones its forEachChild names --
// with its options: Flow's own, and any node's that says it is laid out so.
template <class N> struct Flowing {
  N &fNode;
  const FlowOptions &fOptions;

  // What a child is laid out in: the flow's box -- with no height, for a
  // child whose height is not a share of it. An auto-sized flow is laid out
  // twice, provisionally and then at its size, and a box that changed with
  // it had every child laid out again at each: every row of a long list,
  // twice at each level of auto-sizing. A child's own height, its content's
  // or the one a flow gives it, never came from that box.
  //
  // And in a row, a child sized by its content is given the width its
  // fixed siblings leave rather than the row's: a bubble beside an avatar's
  // room shrinks with the row instead of running past its end.
  [[nodiscard]] skia::SkRect boxFor(const skiff::scene::State &child, const skia::SkRect &box) const {
    const float width = fAutoRoom >= 0.0f && child.fAutoSizeAxes.template has<skiff::scene::axis::x>()
                            ? std::min(box.width(), fAutoRoom)
                            : box.width();
    if (child.fRelativeSizeAxes.template has<skiff::scene::axis::y>()) {
      return skia::SkRect::MakeXYWH(box.fLeft, box.fTop, width, box.height());
    }
    return skia::SkRect::MakeXYWH(box.fLeft, box.fTop, width, 0.0f);
  }
  // A row's room for what sizes itself: its width, less its fixed-size
  // children and the gaps between them all. Negative where not worked out.
  float fAutoRoom = -1.0f;
  // Whether a child is laid out in the flow: shown, and not placed by its
  // anchor instead.
  [[nodiscard]] static bool inFlow(const skiff::scene::State &state) { return state.fVisible && !state.fOutOfFlow; }

  void workOutAutoRoom(const skia::SkRect &box) {
    float fixed = 0.0f;
    int shown = 0;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      const skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!inFlow(state)) {
        return;
      }
      ++shown;
      fixed += state.fMargin.totalX();
      if (state.fGrowAxes.template has<skiff::scene::axis::x>() ||
          state.fAutoSizeAxes.template has<skiff::scene::axis::x>()) {
        return;
      }
      fixed += state.fRelativeSizeAxes.template has<skiff::scene::axis::x>() ? box.width() * state.fWidth
                                                                          : state.fWidth;
    });
    fixed += fOptions.spacingX * static_cast<float>(std::max(0, shown - 1));
    fAutoRoom = std::max(0.0f, box.width() - fixed);
  }

  void layout() {
    spl::visit([this](const auto &along) { lay(along); }, fOptions.direction);
  }

  // A scrolled list's rows out of view while its width moves -- a column
  // dragged wider, at every frame of the drag: not laid out again at each
  // width, every bubble's text wrapped again, but kept as they were laid
  // out and only moved; laid out at the width once it holds (the list's
  // scroll view asks for that frame). Rows in view, and a screen above and
  // below, are laid out as ever.
  bool fMayWait = false;
  std::vector<const skiff::scene::State *> fWaiting;
  [[nodiscard]] bool waits(const skiff::scene::State &child, const skia::SkRect &childBox) const {
    const skiff::scene::State &self = skiff::scene::stateOf(fNode);
    return fMayWait && self.fInView && child.fLayoutValid && !child.fSubtreeDirty && !child.fBounds.isEmpty() &&
           child.fLastConstraint.width() != childBox.width() && !skia::SkRect::Intersects(child.fBounds, *self.fInView);
  }
  // Whether a child is one of those waiting: asked by place() of the
  // children in their order, as measured() listed them -- the next one
  // listed, or none. A search of the whole list for each child made a
  // width change of a 10,000-row list 10,000 x 9,000 compares (43 ms).
  std::size_t fWaitingAt = 0;
  [[nodiscard]] bool waiting(const skiff::scene::State &child) {
    if (fWaitingAt < fWaiting.size() && fWaiting[fWaitingAt] == &child) {
      ++fWaitingAt;
      return true;
    }
    return false;
  }

  // The children that show, laid out at their own size in the box; what the
  // placing is worked out from.
  // Whether a child shown reads the box's height: sized or grown as a share
  // of it, or placed by its anchor in it -- found as measured() passes them.
  bool fReadsHeight = false;
  // How many children it has, and whether all are in the flow, shown, of no
  // depth (State::fFlowOrdered) -- found as measured() passes them.
  std::uint32_t fCount = 0;
  bool fAllInFlow = true;
  std::vector<skiff::scene::State *> measured(const skia::SkRect &box) {
    std::vector<skiff::scene::State *> shown;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      ++fCount;
      fAllInFlow = fAllInFlow && inFlow(state) && state.fDepth == 0.0f;
      if (state.fVisible && (state.fOutOfFlow || state.fRelativeSizeAxes.template has<skiff::scene::axis::y>() ||
                             state.fGrowAxes.template has<skiff::scene::axis::y>()))
        fReadsHeight = true;
      state.fInFlowAtLayout = inFlow(state);
      skiff::scene::work::record(state.fId, skiff::scene::stateOf(fNode).fId);
      if (inFlow(state)) {
        if (const skia::SkRect childBox = boxFor(state, box); this->waits(state, childBox))
          fWaiting.push_back(&state);
        else
          skiff::scene::layout(child, childBox);
        shown.push_back(&state);
      }
    });
    return shown;
  }

  // And placed: a child that did not move keeps its layout. A child out
  // of the flow is laid out in the box by its anchor.
  void place(const skia::SkRect &box,
             const std::vector<std::pair<float, float>> &places) {
    std::size_t at = 0;
    fWaitingAt = 0;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (state.fVisible && state.fOutOfFlow) {
        skiff::scene::layout(child, box);
        return;
      }
      if (!state.fVisible) {
        return;
      }
      state.arrange(places[at].first, places[at].second);
      // Where it was, as it was, in the same box: measured() has just laid
      // it out, and laying it out again only finds that -- once for each of
      // a long list's rows, at every keystroke in one of them.
      if (!state.fPlacementDirty && state.fLayoutValid && !state.fSubtreeDirty && state.fFollow == nullptr &&
          boxFor(state, box) == state.fLastConstraint) {
        ++at;
        return;
      }
      if (this->waiting(state)) {
        // Moved where it goes, as it is: laid out at the width later.
        const skia::SkRect to = boxFor(state, box);
        const float dx = to.fLeft + state.fMargin.fLeft + places[at].first - state.fBounds.fLeft;
        const float dy = to.fTop + state.fMargin.fTop + places[at].second - state.fBounds.fTop;
        state.fPlacementDirty = false;
        // Where it was already -- above what changed, or placed so by the
        // provisional pass of a list sized by its rows: nothing to move. Its
        // whole subtree was walked for nothing, and every recording in it
        // let go.
        if (dx != 0.0f || dy != 0.0f) {
          skiff::scene::work::record(state.fId, skiff::scene::stateOf(fNode).fId);
          skiff::scene::shiftSubtree(child, dx, dy);
        }
        ++at;
        return;
      }
      skiff::scene::layout(child, boxFor(state, box));
      ++at;
    });
  }

  [[nodiscard]] float crossOffset(const skiff::scene::State &child,
                                  float line, float own) const {
    return (line - own) * child.fAlignSelf.value_or(fOptions.crossAlign).at;
  }

  void lay(direction::vertical) {
    const skia::SkRect box = skiff::scene::stateOf(fNode).contentBox();
    if (this->redoChanged(box))
      return;
    grow<skiff::scene::axis::y>(box, fOptions.spacingY);
    skiff::scene::State &self = skiff::scene::stateOf(fNode);
    if (self.fFlowPass != skiff::scene::work::layoutPass()) {
      self.fFlowPass = skiff::scene::work::layoutPass();
      self.fFlowMoved = self.fInView.has_value() && self.fFlowWidth >= 0.0f && self.fFlowWidth != box.width();
      self.fFlowWidth = box.width();
    }
    fMayWait = self.fFlowMoved;
    const auto shown = measured(box);
    self.fChildrenWaiting = !fWaiting.empty();
    float used = 0.0f;
    for (const skiff::scene::State *state : shown) {
      used += state->fBounds.height() + state->fMargin.totalY();
    }
    const int count = static_cast<int>(shown.size());
    const Spread gaps = spread(
        fOptions.justify, box.height(),
        used + fOptions.spacingY * static_cast<float>(std::max(0, count - 1)),
        count);
    std::vector<std::pair<float, float>> places(shown.size());
    // Across a column sized by its content, its widest child is the line
    // to align against, not the room it may take: aligned to that, a child
    // at the end pulled the column out to its largest.
    float line = box.width();
    if (skiff::scene::stateOf(fNode).fAutoSizeAxes.template has<skiff::scene::axis::x>()) {
      line = 0.0f;
      for (const skiff::scene::State *state : shown) {
        line = std::max(line, state->fBounds.width() + state->fMargin.totalX());
      }
    }
    float y = gaps.fStart;
    for (std::size_t i = 0; i < shown.size(); ++i) {
      const skiff::scene::State &state = *shown[i];
      places[i] = {crossOffset(state, line,
                                     state.fBounds.width() +
                                         state.fMargin.totalX()),
                   y};
      y += state.fBounds.height() + state.fMargin.totalY() +
           fOptions.spacingY + gaps.fBetween;
    }
    place(box, places);
    told(box);
    self.fHeightFree = this->freeOfHeight(true);
    // What a later layout of only what changed starts from: this box, and
    // whether this one can be redone in part; the height the rows came to.
    self.fFlowBox = box;
    self.fFlowSimple = self.fHeightFree && !fAnyGrows;
    self.fFlowOrdered = fAllInFlow;
    self.fFlowCount = fCount;
    if (self.fFlowSimple) {
      self.fContentHeight = shown.empty() ? 0.0f : y - fOptions.spacingY - gaps.fBetween;
      self.fContentKnown = true;
    }
  }

  // Laid out again only for what changed under it (State::fBelowOnly), in
  // the box of its last whole layout, that one simple (fFlowSimple): only
  // the children the frame marked are laid out again -- found by the hints
  // to them, as the walks find them -- and those after the first whose
  // height changed moved by as much, one write each (moveLaidOut); none of
  // the others gone into. A word typed into one row of 10,000 laid out and
  // placed all 10,000, twice. Whether it did: else the whole is laid out.
  bool redoChanged(const skia::SkRect &box) {
    namespace scene = skiff::scene;
    scene::State &self = scene::stateOf(fNode);
    if (!self.fBelowOnly || !self.fFlowSimple || self.fFlowBox != box || self.fAutoSizeAxes.template has<scene::axis::x>())
      return false;
    if (scene::work::walkingFull() || scene::work::disabled() || scene::work::reshaped().contains(self.fId))
      return false;
    const scene::NodeId *head = scene::work::hintHeads().find(self.fId);
    if (head == nullptr)
      return false;
    // The marked children, by their places -- each still there, still in
    // the flow as it was at the last layout (not shown or hidden since).
    std::array<std::pair<std::uint32_t, scene::NodeId>, scene::work::kFewHints> marked{};
    std::size_t count = 0;
    for (std::uint32_t at = static_cast<std::uint32_t>(*head); at != 0;) {
      const scene::work::Hint hint = scene::work::hints()[at - 1];
      at = hint.fNext;
      if (count == marked.size())
        return false;
      marked[count++] = {hint.fPlace, hint.fChild};
    }
    std::ranges::sort(marked.begin(), marked.begin() + static_cast<std::ptrdiff_t>(count));
    bool still = true;
    for (const auto &[place, id] : std::span(marked).first(count)) {
      const bool found = scene::childAt(fNode, place, [&](auto &child) {
        const scene::State &state = scene::stateOf(child);
        still = still && state.fId == id && inFlow(state) == state.fInFlowAtLayout && !state.fOutOfFlow;
      });
      still = still && found;
    }
    if (!still)
      return false;
    // From the first marked on: each marked one moved by what changed above
    // it and laid out again; each after, moved by what changed above it.
    const std::optional<skia::SkRect> &view = self.fInView;
    std::size_t next = 0;
    std::uint32_t at = 0;
    float delta = 0.0f;
    scene::eachChild(fNode, [&](auto &child) {
      const std::uint32_t place = at++;
      if (place < marked[0].first || (next == count && delta == 0.0f))
        return;
      scene::State &state = scene::stateOf(child);
      if (next < count && place == marked[next].first) {
        ++next;
        if (!inFlow(state))
          return;
        const float before = state.fBounds.height() + state.fMargin.totalY();
        const float beforeWidth = state.fBounds.width() + state.fMargin.totalX();
        state.arrange(state.fX, state.fY + delta);
        scene::layout(child, boxFor(state, box));
        // Across, as a whole layout places it: where its width changed.
        const float width = state.fBounds.width() + state.fMargin.totalX();
        if (width != beforeWidth) {
          state.arrange(crossOffset(state, box.width(), width), state.fY);
          scene::layout(child, boxFor(state, box));
        }
        delta += state.fBounds.height() + state.fMargin.totalY() - before;
        return;
      }
      if (delta == 0.0f || !inFlow(state))
        return;
      // Moved as it is: placed where a whole layout would put it, its
      // subtree moved when gone into; reached by the damage walk where it
      // is in view, to be repainted where it was and is.
      scene::work::record(state.fId, self.fId);
      scene::detail::placedAgain(child, 0.0f, delta);
      if (!view || skia::SkRect::Intersects(scene::joined(state.fBounds, state.fReach), *view))
        scene::work::reach(state.fId);
    });
    self.fContentHeight += delta;
    self.fContentKnown = true;
    self.fHeightFree = true;
    return true;
  }

  // Whether this layout was free of the box's height (State::fHeightFree):
  // down a column, from its start, or across a row sized by what it holds;
  // no child sized or grown as a share of the height, none placed by its
  // anchor in the box (as measured() found them).
  [[nodiscard]] bool freeOfHeight(bool vertical) {
    const skiff::scene::State &self = skiff::scene::stateOf(fNode);
    const bool fromStart = spl::visit(spl::overloaded{[](justify::start) { return true; }, [](const auto &) { return false; }}, fOptions.justify);
    if (vertical ? !fromStart : !fOptions.wrap && !self.fAutoSizeAxes.template has<skiff::scene::axis::y>())
      return false;
    return !fReadsHeight;
  }

  void lay(direction::horizontal) {
    const skia::SkRect box = skiff::scene::stateOf(fNode).contentBox();
    if (!fOptions.wrap) {
      this->workOutAutoRoom(box);
      grow<skiff::scene::axis::x>(box, fOptions.spacingX);
      // Measured as they would be with room, then given way where there is
      // not: what a row of the last layout asked of them asks nothing now.
      skiff::scene::eachChild(fNode, [](auto &child) { shrink_to(skiff::scene::stateOf(child), 0.0f); });
    }
    const auto shown = measured(box);
    if (!fOptions.wrap && !skiff::scene::stateOf(fNode).fAutoSizeAxes.template has<skiff::scene::axis::x>()) {
      this->shrink_to_fit(box);
    }
    // Rows broken at the edge...
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    {
      std::size_t rowStart = 0;
      float rowWidth = 0.0f;
      for (std::size_t i = 0; i < shown.size(); ++i) {
        const float width = this->basis(*shown[i]);
        // Half a pixel of slack: four quarters add up to the width.
        if (fOptions.wrap && i > rowStart && rowWidth + fOptions.spacingX + width > box.width() + 0.5f) {
          lines.emplace_back(rowStart, i);
          rowStart = i;
          rowWidth = 0.0f;
        }
        rowWidth += i == rowStart ? width : fOptions.spacingX + width;
      }
      if (rowStart < shown.size())
        lines.emplace_back(rowStart, shown.size());
    }
    // ...what grows grown along its own...
    if (fOptions.wrap)
      this->growWrapped(box, shown, lines);
    // ...then placed.
    std::vector<std::pair<float, float>> places(shown.size());
    float y = 0.0f;
    for (const auto &[rowStart, end] : lines) {
      float rowWidth = 0.0f;
      float rowHeight = 0.0f;
      for (std::size_t i = rowStart; i < end; ++i) {
        rowWidth += (i == rowStart ? 0.0f : fOptions.spacingX) + shown[i]->fBounds.width() + shown[i]->fMargin.totalX();
        rowHeight = std::max(rowHeight, shown[i]->fBounds.height() + shown[i]->fMargin.totalY());
      }
      // What a child is aligned against across the row: a row not wrapped
      // is the whole box across, as a column is (unless the box takes its
      // height from what it holds); wrapped rows, each its tallest child.
      const float line =
          !fOptions.wrap && !skiff::scene::stateOf(fNode).fAutoSizeAxes.template has<skiff::scene::axis::y>()
              ? std::max(rowHeight, box.height())
              : rowHeight;
      const Spread gaps = fOptions.centreRows ? Spread{(box.width() - rowWidth) * 0.5f, 0.0f}
                                              : spread(fOptions.justify, box.width(), rowWidth, static_cast<int>(end - rowStart));
      float x = gaps.fStart;
      for (std::size_t i = rowStart; i < end; ++i) {
        const skiff::scene::State &state = *shown[i];
        places[i] = {x, y + crossOffset(state, line, state.fBounds.height() + state.fMargin.totalY())};
        x += state.fBounds.width() + state.fMargin.totalX() + fOptions.spacingX + gaps.fBetween;
      }
      y += rowHeight + fOptions.spacingY;
    }
    place(box, places);
    told(box);
    skiff::scene::stateOf(fNode).fHeightFree = this->freeOfHeight(false);
    skiff::scene::stateOf(fNode).fFlowSimple = false;
    skiff::scene::stateOf(fNode).fFlowOrdered = false;
  }

  // What a child takes on its line, wrapped: what it is -- or, where it
  // grows, the least it takes, CSS's flex-basis of nothing held up by its
  // min-width -- whatever it was given last: its width given again only
  // where its share moved, so a flow laid out again for something else does
  // not lay out, nor repaint, what it holds.
  [[nodiscard]] float basis(const skiff::scene::State &state) const {
    if (fOptions.wrap && state.fGrowAxes.template has<skiff::scene::axis::x>())
      return state.fMinWidth + state.fMargin.totalX();
    return state.fBounds.width() + state.fMargin.totalX();
  }

  // CSS's flex-grow on wrapped lines: on each line, what grows takes an
  // equal share of what it and the room left come to -- none less than it
  // takes already (its min-width): one that would is kept at that, and the
  // rest share what is left. Laid out at its share, before the line's
  // height is known: its words wrap anew.
  void growWrapped(const skia::SkRect &box, const std::vector<skiff::scene::State *> &shown,
                   const std::vector<std::pair<std::size_t, std::size_t>> &lines) {
    std::vector<float> to(shown.size(), -1.0f);
    bool any = false;
    for (const auto &[from, end] : lines) {
      float used = fOptions.spacingX * static_cast<float>(end - from - 1);
      float growing = 0.0f;
      int growers = 0;
      for (std::size_t i = from; i < end; ++i) {
        const float width = this->basis(*shown[i]);
        used += width;
        if (shown[i]->fGrowAxes.template has<skiff::scene::axis::x>()) {
          growing += width;
          ++growers;
        }
      }
      const float room = std::max(0.0f, box.width() - used);
      if (growers == 0)
        continue;
      std::vector<bool> kept(end - from, false);
      float pool = growing + room;
      int open = growers;
      for (bool again = true; again && open > 0;) {
        again = false;
        const float share = pool / static_cast<float>(open);
        for (std::size_t i = from; i < end; ++i) {
          const float width = this->basis(*shown[i]);
          if (shown[i]->fGrowAxes.template has<skiff::scene::axis::x>() && !kept[i - from] && width > share) {
            kept[i - from] = true;
            pool -= width;
            --open;
            again = true;
          }
        }
      }
      if (open == 0)
        continue;
      const float share = pool / static_cast<float>(open);
      for (std::size_t i = from; i < end; ++i) {
        if (!shown[i]->fGrowAxes.template has<skiff::scene::axis::x>())
          continue;
        const float width = kept[i - from] ? this->basis(*shown[i]) : share;
        to[i] = std::max(0.0f, width - shown[i]->fMargin.totalX());
        any = true;
      }
    }
    if (!any)
      return;
    std::size_t at = 0;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!inFlow(state))
        return;
      if (to[at] >= 0.0f && std::abs(state.fWidth - to[at]) > 0.01f) {
        state.arrangeAxisSize(true, to[at]);
        skiff::scene::layout(child, boxFor(state, box));
      }
      ++at;
    });
  }

  // What sticks out of the box, told: along an axis the box does not take
  // from what it holds, where it does not clip what it holds. A menu of a
  // set width whose row of buttons was wider than it put its last button
  // past its edge, and nothing said so.
  // Not made at all without the layout's reports (SKIFF_LAYOUT_REPORTS).
  void told(const skia::SkRect &box) {
    if constexpr (skiff::scene::kLayoutReports)
      toldNow(box);
  }
  void toldNow(const skia::SkRect &box) {
    if (skiff::scene::stateOf(fNode).fMasking) {
      return;
    }
    const bool freeX = !skiff::scene::stateOf(fNode).fAutoSizeAxes.template has<skiff::scene::axis::x>();
    const bool freeY = !skiff::scene::stateOf(fNode).fAutoSizeAxes.template has<skiff::scene::axis::y>();
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!inFlow(state)) {
        return;
      }
      const skia::SkRect &at = state.fBounds;
      // A pixel of slack: below it, rounding -- a row of fixed icons in a
      // window squeezed past what they hold came out 0.1 over, and was told.
      const float x = freeX ? std::max({0.0f, at.fRight - box.fRight - 1.0f, box.fLeft - at.fLeft - 1.0f}) : 0.0f;
      const float y = freeY ? std::max({0.0f, at.fBottom - box.fBottom - 1.0f, box.fTop - at.fTop - 1.0f}) : 0.0f;
      if (x <= 0.0f && y <= 0.0f) {
        state.fOverflowTold = false;
        return;
      }
      if (state.fOverflowTold) {
        return;
      }
      state.fOverflowTold = true;
      skiff::scene::tellOverflow(skiff::scene::Overflow{
          std::string(skiff::scene::typeOf(child).name), std::string(skiff::scene::typeOf(fNode).name), x, y,
          skiff::scene::viewsLaidOut().empty() ? std::string() : std::string(skiff::scene::viewsLaidOut().back())});
    });
  }

  // Whether a node gives way along an axis: it says it does, or it takes
  // its size from what it holds and something it holds gives way.
  template <class Axis, class Tree> static bool gives_way(Tree &node) {
    const skiff::scene::State &state = skiff::scene::stateOf(node);
    if (state.fShrinkAxes.template has<Axis>()) {
      return true;
    }
    if (!state.fAutoSizeAxes.template has<Axis>()) {
      return false;
    }
    bool any = false;
    skiff::scene::eachChild(node, [&](auto &child) {
      if (!any && inFlow(skiff::scene::stateOf(child)) && gives_way<Axis>(child)) {
        any = true;
      }
    });
    return any;
  }

  // CSS's flex-shrink, along a row: where what it holds is wider than it,
  // those that give way do, each by its share of the excess in proportion
  // to its width, down to what they can -- a few passes, as one that stops
  // at its least leaves the rest to the others.
  // A child told how far to give way: laid out again where that changed. Its
  // layout was kept by its box alone, and the box is the same -- a row of
  // tabs too wide for its panel was told to give way and laid out as it was,
  // and stood out past its edge.
  static void shrink_to(skiff::scene::State &state, float to) {
    if (state.fShrunkTo == to)
      return;
    state.fShrunkTo = to;
    state.fLayoutValid = false;
  }

  void shrink_to_fit(const skia::SkRect &box) {
    for (int pass = 0; pass < 3; ++pass) {
      float used = 0.0f;
      int count = 0;
      float giving = 0.0f;
      skiff::scene::eachChild(fNode, [&](auto &child) {
        const skiff::scene::State &state = skiff::scene::stateOf(child);
        if (!inFlow(state)) {
          return;
        }
        ++count;
        used += state.fBounds.width() + state.fMargin.totalX();
        if (gives_way<skiff::scene::axis::x>(child)) {
          giving += state.fBounds.width();
        }
      });
      used += fOptions.spacingX * static_cast<float>(std::max(0, count - 1));
      const float excess = used - box.width();
      if (excess <= 0.5f || giving <= 0.0f) {
        return;
      }
      bool gave = false;
      skiff::scene::eachChild(fNode, [&](auto &child) {
        skiff::scene::State &state = skiff::scene::stateOf(child);
        if (!inFlow(state) || !gives_way<skiff::scene::axis::x>(child)) {
          return;
        }
        const float width = state.fBounds.width();
        const float to = std::max(0.0f, width - excess * width / giving);
        shrink_to(state, std::max(to, 0.01f));
        skiff::scene::layout(child, boxFor(state, box));
        gave = gave || state.fBounds.width() < width - 0.25f;
      });
      if (!gave) {
        return;
      }
    }
  }

  // Whether a child grows along the flow: found by grow().
  bool fAnyGrows = false;
  // Children that grow take an equal share of what the rest leave along the
  // axis, written before anything is placed.
  template <class Axis> void grow(const skia::SkRect &box, float spacing) {
    // None grows -- most flows, a list of rows among them: nothing to hand
    // out, and nothing laid out for it. Each child was laid out here first,
    // to see what was taken, and then again where it was placed -- the rows
    // of a list at every width it was dragged through, those out of view too.
    bool any = false;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      const skiff::scene::State &state = skiff::scene::stateOf(child);
      any = any || (inFlow(state) && state.fGrowAxes.template has<Axis>());
    });
    fAnyGrows = any;
    if (!any) {
      return;
    }
    int growers = 0;
    int visible = 0;
    float taken = 0.0f;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!inFlow(state)) {
        return;
      }
      ++visible;
      if (state.fGrowAxes.template has<Axis>()) {
        ++growers;
        return;
      }
      skiff::scene::layout(child, boxFor(state, box));
      taken += Axis::length(state.fBounds) + Axis::margins(state.fMargin);
    });
    if (growers == 0) {
      return;
    }
    const float gaps = spacing * static_cast<float>(std::max(0, visible - 1));
    const float room = Axis::length(box);
    const float share =
        std::max(0.0f, (room - taken - gaps) / static_cast<float>(growers));
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      // Its margins are in its share: the room it takes, margins and all,
      // is the share, so what comes after it still fits.
      if (inFlow(state) && state.fGrowAxes.template has<Axis>()) {
        const float margins = Axis::margins(state.fMargin);
        state.arrangeAxisSize(Axis::kHorizontal, std::max(0.0f, share - margins));
      }
    });
  }

};
} // namespace detail

template <class... Children> class Flow : public skiff::scene::Node {
public:
  explicit Flow(FlowOptions options, Children... children)
      : fChildren(std::move(children)...), fOptions(options) {}

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }

  [[nodiscard]] const FlowOptions &options() const noexcept {
    return fOptions;
  }
  [[nodiscard]] skiff::scene::Look look() const { return skiff::scene::look::flow{fOptions}; }
  void setOptions(FlowOptions options) {
    fOptions = options;
    this->invalidateLayout();
  }
  void setSpacing(float x, float y) {
    if (x == fOptions.spacingX && y == fOptions.spacingY) {
      return;
    }
    fOptions.spacingX = x;
    fOptions.spacingY = y;
    this->invalidateLayout();
  }
  void setWrap(bool wrap) {
    if (wrap != fOptions.wrap) {
      fOptions.wrap = wrap;
      this->invalidateLayout();
    }
  }
  void setCrossAlign(skiff::scene::Align at) {
    if (!(at == fOptions.crossAlign)) {
      fOptions.crossAlign = at;
      this->invalidateLayout();
    }
  }
  void setJustify(Justify how) {
    fOptions.justify = how;
    this->invalidateLayout();
  }

  // Its layoutChildren is a flow's (the compiled layout lays it out by its
  // rules, not by this).
  using FlowLaidOut = void;
  // Outside a release build, laid out by the one flow every stack is (see
  // Stack::layoutChildren), through its table: a flow of a row type is not a
  // layout algorithm made again for that type.
  void layoutChildren() {
    if constexpr (skiff::scene::kErasedWalks) {
      skiff::scene::AnyNodeRef seen = skiff::scene::AnyNodeRef::of(*this);
      detail::Flowing<skiff::scene::AnyNodeRef>{seen, fOptions}.layout();
    } else {
      detail::Flowing<Flow>{*this, fOptions}.layout();
    }
  }

  std::tuple<Children...> fChildren;

private:
  FlowOptions fOptions;
};

// A node laid out as a flow of its own children, in the order its
// forEachChild names them, with the options it keeps: a screen declares
// what it holds, in what order, and how each sizes itself (fill, autoSize,
// grow, margins); where each goes is the layout's, never placed by hand.
// Vertical and not wrapping to begin with.
class Stack : public skiff::scene::Node {
public:
  FlowOptions fStack{.direction = direction::vertical{}, .wrap = false};
  [[nodiscard]] skiff::scene::Look look() const { return skiff::scene::look::flow{fStack}; }

  void setGap(float gap) {
    fStack.spacingX = gap;
    fStack.spacingY = gap;
    this->invalidateLayout();
  }
  void setHorizontal() {
    fStack.direction = direction::horizontal{};
    this->invalidateLayout();
  }
  // Laid out by the one flow for every stack outside a release build -- the
  // stack seen through its table, as the walks see it -- not a flow made
  // again for each type of node that is a stack; in a release build, its own.
  void layoutChildren(this auto &self) {
    if constexpr (skiff::scene::kErasedWalks) {
      skiff::scene::AnyNodeRef seen = skiff::scene::AnyNodeRef::of(self);
      detail::Flowing<skiff::scene::AnyNodeRef>{seen, self.fStack}.layout();
    } else {
      detail::Flowing<std::remove_reference_t<decltype(self)>>{self, self.fStack}.layout();
    }
  }
};

// Rows as a function of items, declared: the rows become one for each
// item, in the items' order. A row whose item it still shows -- found by
// its key, and said to by `shows` -- is kept as it is, with its layout,
// hover and selection; the others are made from their items, and rows no
// item has any more are dropped. A list updated this way does the work of
// what changed, not of all of it, and nothing in it is cleared by hand.
//
//   reconcile(rows, chats, [](auto& c) { return c.id; },
//             [](auto& row) { return row.id; },
//             [](auto& c) { return ChatRow(c); },
//             [](auto& row, auto& c) { return row.shown == view(c); });
//
// Keys may repeat (a message not yet given an id): each row is taken once.
// What reconcile tells of a row it makes again where it was, before it goes:
// its place and the row as it was. Nothing, unless asked.
struct NoRemade {
  template <class Row> void operator()(std::size_t, const Row &) const {}
};
template <class Row, std::ranges::input_range Items, class KeyOf, class RowKey,
          class Make, class Shows, class Remade = NoRemade>
bool reconcile(std::vector<Row> &rows, Items &&items, KeyOf keyOf,
               RowKey rowKey, Make make, Shows shows, Remade remade = {}) {
  using Key = std::remove_cvref_t<std::invoke_result_t<RowKey, const Row &>>;
  // The same rows in the same order -- the usual change, one or two of them
  // shown otherwise (someone's presence): those made again where they are,
  // the rest not moved. Every row was moved into a new list, each move a
  // node changing places, and the whole list walked again.
  if constexpr (std::ranges::forward_range<Items> && std::ranges::sized_range<Items>) {
    if (std::ranges::size(items) == rows.size()) {
      bool same = true;
      std::size_t i = 0;
      for (auto &&item : items) {
        if (!(std::invoke(rowKey, std::as_const(rows[i])) == std::invoke(keyOf, item))) {
          same = false;
          break;
        }
        ++i;
      }
      if (same) {
        if constexpr (std::is_move_assignable_v<Row>) {
          bool changed = false;
          i = 0;
          for (auto &&item : items) {
            if (!std::invoke(shows, std::as_const(rows[i]), item)) {
              std::invoke(remade, i, std::as_const(rows[i]));
              rows[i] = std::invoke(make, item);
              changed = true;
            }
            ++i;
          }
          return changed;
        } else {
          // Captured callables make composed rows move-constructible but
          // not assignable. Keep an unchanged list exactly where it is;
          // changed rows use the construction-only path below.
          if (std::ranges::equal(rows, items, [&](const Row& row, auto&& item) {
                return std::invoke(shows, row, item);
              }))
            return false;
        }
      }
    }
  }
  // The rows there are still the first items, in order, by their keys --
  // a chat's new message added at the end, or a row shown otherwise where
  // rows cannot be assigned (composed ones, holding captured callables):
  // those rows stay where they are, a changed one made again in its place,
  // the new ones made after them. Moving every row into a new list cost as
  // much as making them all again (a 10,000-message chat: 0.2 s to add one).
  if constexpr (std::ranges::forward_range<Items> && std::ranges::sized_range<Items>) {
    const bool prefix = std::ranges::size(items) >= rows.size() &&
                        std::ranges::equal(rows, items | std::views::take(rows.size()), [&](const Row &row, const auto &item) {
                          return std::invoke(rowKey, row) == std::invoke(keyOf, item);
                        });
    if (prefix) {
      bool changed = std::ranges::size(items) != rows.size();
      const std::size_t kept = rows.size();
      std::size_t i = 0;
      for (auto &&item : items) {
        if (i >= kept) {
          rows.push_back(std::invoke(make, item));
        } else if (!std::invoke(shows, std::as_const(rows[i]), item)) {
          std::invoke(remade, i, std::as_const(rows[i]));
          std::destroy_at(&rows[i]);
          std::construct_at(&rows[i], std::invoke(make, item));
          changed = true;
        }
        ++i;
      }
      return changed;
    }
  }
  std::multimap<Key, std::size_t> old;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    old.emplace(std::invoke(rowKey, std::as_const(rows[i])), i);
  }
  std::vector<bool> taken(rows.size(), false);
  std::vector<Row> next;
  bool changed = false;
  std::size_t at = 0;
  for (auto &&item : items) {
    const auto [first, last] = old.equal_range(std::invoke(keyOf, item));
    bool kept = false;
    for (auto it = first; it != last; ++it) {
      const std::size_t i = it->second;
      if (!taken[i] && std::invoke(shows, std::as_const(rows[i]), item)) {
        taken[i] = true;
        changed |= i != at;
        next.push_back(std::move(rows[i]));
        kept = true;
        break;
      }
    }
    if (!kept) {
      next.push_back(std::invoke(make, item));
      changed = true;
    }
    ++at;
  }
  changed |= next.size() != rows.size();
  rows = std::move(next);
  return changed;
}

// A part as a function of a value, declared: shown a view, it makes its
// content from it -- and only when the view differs from the one it was
// made from. Nothing in the content is set after it is made: a changed view
// is a new content. Its own Spec places it; the content fills it.
//
//   Memo<HeaderView, Header> header;
//   header.show(view_of(chat), [&](const HeaderView& v) { return Header(v); });
template <class View, class Content> class Memo : public skiff::scene::Node {
public:
  using BindingChildren = std::tuple<Content>;
  // Made again, from the view, where the view is not the one it shows.
  template <class Make> bool show(const View &view, Make &&make) {
    if (fView && *fView == view) {
      return false;
    }
    fContent.reset();
    fContent.emplace(Made<Make>{make, view});
    fView = view;
    this->invalidateLayout();
    return true;
  }
  [[nodiscard]] Content *content() noexcept { return fContent ? &*fContent : nullptr; }
  [[nodiscard]] const View *view() const noexcept { return fView ? &*fView : nullptr; }
  void forEachChild(auto &&f) { f(fContent); }

private:
  // What the content is made from: made in its place, never moved.
  template <class Make> struct Made {
    Make &make;
    const View &view;
    operator Content() const { return std::invoke(make, view); }
  };
  std::optional<View> fView;
  std::optional<Content> fContent;
};

// A Memo told how its content is made: shown a view by what reads the
// model for it (bind::Derived calls read), with nothing passed but the view.
template <class View, class Content, class Make> class MemoOf : public Memo<View, Content> {
public:
  // Made with nothing only where its Make is made so (as MemoRows).
  MemoOf()
    requires std::default_initializable<Make>
      : fMake{} {}
  explicit MemoOf(Make make) : fMake(std::move(make)) {}
  bool read(const View &view) { return this->show(view, fMake); }
  Make fMake;
};

// One row of MemoRows: its key, the view it was made from, and the row
// made from its item -- as its child, filling it across.
template <class Key, class View, class Row> class KeptRow : public skiff::scene::Node {
public:
  using BindingChildren = std::tuple<Row>;
  template <class Item, class Make>
  KeptRow(const Item &item, Make &make,
          skiff::scene::Spec spec = {.fillX = true, .autoSize = skiff::scene::axes::kY})
      : fKey(std::get<0>(item)), fView(std::get<1>(item)), fRow(std::invoke(make, item)) {
    fState.apply(spec);
  }
  void forEachChild(auto &&f) { f(fRow); }
  Key fKey;
  View fView;
  Row fRow;
};

// Rows as a function of a list of views, by their keys -- a chat list
// read from the model: each read, a row whose key is still listed with
// the view it was made from is kept as it is, the others are made by Make
// from their items, and the rows no longer listed go. An item is a tuple:
// its key, its view, and whatever else making its row needs (the part it
// shows, while the list is read). Fed by what reads the model
// (bind::Derived calls read with the list).
template <class Key, class View, class Row, class Make, class Container = Stack>
class MemoRows : public Container {
public:
  using Kept = KeptRow<Key, View, Row>;
  using BindingChildren = std::tuple<Kept>;
  // Made with nothing only where what makes its rows is made so: a lambda
  // that captures is not, and the member's own initializer, read as
  // libstdc++ asks whether this is (a tuple of them), was an error there
  // rather than an answer.
  MemoRows()
    requires std::default_initializable<Make>
      : fMake{} {}
  // A vertical list fills across by default. Horizontal items and wrapped
  // tabs can instead keep their natural size through their wrapper.
  explicit MemoRows(Make make, Container container = {},
                    skiff::scene::Spec rowSpec = {.fillX = true, .autoSize = skiff::scene::axes::kY})
      : Container(std::move(container)), fMake(std::move(make)), fRowSpec(std::move(rowSpec)) {}
  // Items: a key, its view, and the rest, in the order they are listed.
  template <std::ranges::input_range Items> bool read(Items &&items) {
    namespace scene = skiff::scene;
    // A row made again where it was: what it was, for the one in its place.
    struct Was {
      std::size_t place;
      scene::NodeId id;
      skia::SkRect bounds;
      float x, y;
      bool inFlow;
    };
    std::vector<Was> remade;
    const std::size_t before = fRows.size();
    const bool changed = reconcile(
        fRows, std::forward<Items>(items),
        [](const auto &item) -> const Key & { return std::get<0>(item); },
        [](const Kept &row) -> const Key & { return row.fKey; },
        [this](const auto &item) { return Kept(item, fMake, fRowSpec); },
        [](const Kept &row, const auto &item) { return row.fView == std::get<1>(item); },
        [&](std::size_t place, const Kept &row) {
          const scene::State &was = row.fState;
          remade.push_back({place, was.fId, was.fBounds, was.fX, was.fY, was.fInFlowAtLayout});
        });
    if (!changed)
      return false;
    if (fRows.size() != before || remade.empty()) {
      this->invalidateLayout();
      return true;
    }
    // Only rows made again where they were -- a message edited, a reaction
    // come: each new one put where its row was, as its row was placed and
    // laid out, so that the list lays out only it and moves those below it
    // by what it grew; told to the work tables where it is, its share of the
    // list's signature in place of the old row's, and marked. The old row's
    // going said the list's children changed, and they did, where it knows:
    // not reshaped, and not laid out whole. Every row was laid out, placed
    // and walked again for one of them.
    scene::State &self = this->fState;
    for (const Was &was : remade) {
      scene::State &now = fRows[was.place].fState;
      now.fBounds = was.bounds;
      now.fX = was.x;
      now.fY = was.y;
      now.fInFlowAtLayout = was.inFlow;
      self.fChildSignature += scene::work::childShare(static_cast<std::uint32_t>(was.place), now.fId);
      self.fChildSignature -= scene::work::childShare(static_cast<std::uint32_t>(was.place), was.id);
      scene::work::record(now.fId, self.fId, static_cast<std::uint32_t>(was.place));
      scene::work::mark(now.fId);
    }
    scene::work::reshaped().erase(self.fId);
    return true;
  }
  template <class Self, class F> void forEachChild(this Self &self, F &&f) { f(self.fRows); }
  [[nodiscard]] std::size_t size() const noexcept { return fRows.size(); }
  std::vector<Kept> fRows;
  Make fMake;
  skiff::scene::Spec fRowSpec{.fillX = true, .autoSize = skiff::scene::axes::kY};
};

} // namespace skiff::nodes
