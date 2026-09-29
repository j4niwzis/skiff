export module skiff.nodes:flow;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// Along which axis a flow lays its children.
namespace direction {
struct vertical {};
struct horizontal {};
} // namespace direction
using Direction = std::variant<direction::vertical, direction::horizontal>;

// How the room a line leaves is handed out.
namespace justify {
struct start {};
struct middle {};
struct end {};
struct space_between {};
struct space_around {};
} // namespace justify
using Justify = std::variant<justify::start, justify::middle, justify::end,
                               justify::space_between, justify::space_around>;

struct FlowOptions {
  Direction direction = direction::vertical{};
  float spacingX = 0.0f;
  float spacingY = 0.0f;
  // A horizontal flow breaks into rows at its edge.
  bool wrap = true;
  // Rows centred in the container, as the cards are.
  bool centreRows = false;
  // Where children sit across the axis; a child can say otherwise with
  // alignSelf.
  skiff::scene::Align crossAlign = skiff::scene::align::kStart;
  Justify justify = justify::start{};
};

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
  return std::visit(
      skiff::scene::overloaded{
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

// FillFlowContainer: children laid end to end, wrapping when they run out of
// room, which is how lazer builds every list and row of filters.
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
  void workOutAutoRoom(const skia::SkRect &box) {
    float fixed = 0.0f;
    int shown = 0;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      const skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!state.fVisible) {
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
    std::visit([this](const auto &along) { lay(along); }, fOptions.direction);
  }

  // The children that show, laid out at their own size in the box; what the
  // placing is worked out from.
  std::vector<skiff::scene::State *> measured(const skia::SkRect &box) {
    std::vector<skiff::scene::State *> shown;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (state.fVisible) {
        skiff::scene::layout(child, boxFor(state, box));
        shown.push_back(&state);
      }
    });
    return shown;
  }

  // And placed: a child that did not move keeps its layout.
  void place(const skia::SkRect &box,
             const std::vector<std::pair<float, float>> &places) {
    std::size_t at = 0;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!state.fVisible) {
        return;
      }
      state.arrange(places[at].first, places[at].second);
      skiff::scene::layout(child, boxFor(state, box));
      ++at;
    });
  }

  [[nodiscard]] float crossOffset(const skiff::scene::State &child,
                                  float line, float own) const {
    return (line - own) * child.fAlignSelf.value_or(fOptions.crossAlign).at;
  }

  void lay(direction::vertical) {
    const skia::SkRect box = fNode.fState.contentBox();
    grow<skiff::scene::axis::y>(box, fOptions.spacingY);
    const auto shown = measured(box);
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
    float y = gaps.fStart;
    for (std::size_t i = 0; i < shown.size(); ++i) {
      const skiff::scene::State &state = *shown[i];
      places[i] = {crossOffset(state, box.width(),
                                     state.fBounds.width() +
                                         state.fMargin.totalX()),
                   y};
      y += state.fBounds.height() + state.fMargin.totalY() +
           fOptions.spacingY + gaps.fBetween;
    }
    place(box, places);
  }

  void lay(direction::horizontal) {
    const skia::SkRect box = fNode.fState.contentBox();
    if (!fOptions.wrap) {
      this->workOutAutoRoom(box);
      grow<skiff::scene::axis::x>(box, fOptions.spacingX);
    }
    const auto shown = measured(box);
    std::vector<std::pair<float, float>> places(shown.size());
    // Rows broken at the edge, then placed.
    std::size_t rowStart = 0;
    float rowWidth = 0.0f;
    float y = 0.0f;
    const auto flush = [&](std::size_t end) {
      if (end == rowStart) {
        return;
      }
      float rowHeight = 0.0f;
      for (std::size_t i = rowStart; i < end; ++i) {
        rowHeight = std::max(rowHeight, shown[i]->fBounds.height() +
                                            shown[i]->fMargin.totalY());
      }
      const Spread gaps =
          fOptions.centreRows
              ? Spread{(box.width() - rowWidth) * 0.5f, 0.0f}
              : spread(fOptions.justify, box.width(), rowWidth,
                       static_cast<int>(end - rowStart));
      float x = gaps.fStart;
      for (std::size_t i = rowStart; i < end; ++i) {
        const skiff::scene::State &state = *shown[i];
        places[i] = {x, y + crossOffset(state, rowHeight,
                                              state.fBounds.height() +
                                                  state.fMargin.totalY())};
        x += state.fBounds.width() + state.fMargin.totalX() +
             fOptions.spacingX + gaps.fBetween;
      }
      y += rowHeight + fOptions.spacingY;
      rowStart = end;
      rowWidth = 0.0f;
    };
    for (std::size_t i = 0; i < shown.size(); ++i) {
      const float width =
          shown[i]->fBounds.width() + shown[i]->fMargin.totalX();
      // Half a pixel of slack: four quarters add up to the width.
      if (fOptions.wrap && i > rowStart &&
          rowWidth + fOptions.spacingX + width > box.width() + 0.5f) {
        flush(i);
      }
      rowWidth += i == rowStart ? width : fOptions.spacingX + width;
    }
    flush(shown.size());
    place(box, places);
  }

  // Children that grow take an equal share of what the rest leave along the
  // axis, written before anything is placed.
  template <class Axis> void grow(const skia::SkRect &box, float spacing) {
    constexpr bool horizontal = std::same_as<Axis, skiff::scene::axis::x>;
    int growers = 0;
    int visible = 0;
    float taken = 0.0f;
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!state.fVisible) {
        return;
      }
      ++visible;
      if (state.fGrowAxes.template has<Axis>()) {
        ++growers;
        return;
      }
      skiff::scene::layout(child, boxFor(state, box));
      taken += horizontal ? state.fBounds.width() + state.fMargin.totalX()
                          : state.fBounds.height() + state.fMargin.totalY();
    });
    if (growers == 0) {
      return;
    }
    const float gaps = spacing * static_cast<float>(std::max(0, visible - 1));
    const float room = horizontal ? box.width() : box.height();
    const float share =
        std::max(0.0f, (room - taken - gaps) / static_cast<float>(growers));
    skiff::scene::eachChild(fNode, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      // Its margins are in its share: the room it takes, margins and all,
      // is the share, so what comes after it still fits.
      if (state.fVisible && state.fGrowAxes.template has<Axis>()) {
        const float margins = horizontal ? state.fMargin.totalX() : state.fMargin.totalY();
        state.arrangeAxisSize(horizontal, std::max(0.0f, share - margins));
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
#if defined(__cpp_structured_bindings) && __cpp_structured_bindings >= 202411L
    // C++26's binding packs, where the build has them.
    auto &[... each] = fChildren;
    (f(each), ...);
#else
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
#endif
  }

  [[nodiscard]] const FlowOptions &options() const noexcept {
    return fOptions;
  }
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

  void layoutChildren() { detail::Flowing<Flow>{*this, fOptions}.layout(); }

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

  void setGap(float gap) {
    fStack.spacingX = gap;
    fStack.spacingY = gap;
    this->invalidateLayout();
  }
  void setHorizontal() {
    fStack.direction = direction::horizontal{};
    this->invalidateLayout();
  }
  void layoutChildren(this auto &self) {
    detail::Flowing<std::remove_reference_t<decltype(self)>>{self, self.fStack}.layout();
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
template <class Row, std::ranges::input_range Items, class KeyOf, class RowKey,
          class Make, class Shows>
bool reconcile(std::vector<Row> &rows, Items &&items, KeyOf keyOf,
               RowKey rowKey, Make make, Shows shows) {
  using Key = std::remove_cvref_t<std::invoke_result_t<RowKey, const Row &>>;
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

// A vertical flow and a horizontal one, as they are usually written.
template <class... Children>
[[nodiscard]] Flow<Children...> column(float spacing, Children... children) {
  return Flow<Children...>(
      {.direction = direction::vertical{}, .spacingY = spacing},
      std::move(children)...);
}
template <class... Children>
[[nodiscard]] Flow<Children...> row(float spacing, Children... children) {
  return Flow<Children...>({.direction = direction::horizontal{},
                            .spacingX = spacing,
                            .wrap = false},
                           std::move(children)...);
}

} // namespace skiff::nodes
