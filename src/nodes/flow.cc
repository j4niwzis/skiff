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

  void layoutChildren() {
    std::visit([this](const auto &along) { this->lay(along); },
               fOptions.direction);
  }

  std::tuple<Children...> fChildren;

private:
  // The children that show, laid out at their own size in the box; what the
  // placing is worked out from.
  std::vector<skiff::scene::State *> measured(const skia::SkRect &box) {
    std::vector<skiff::scene::State *> shown;
    skiff::scene::eachChild(*this, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (state.fVisible) {
        skiff::scene::layout(child, box);
        shown.push_back(&state);
      }
    });
    return shown;
  }

  // And placed: a child that did not move keeps its layout.
  void place(const skia::SkRect &box,
             const std::vector<std::pair<float, float>> &places) {
    std::size_t at = 0;
    skiff::scene::eachChild(*this, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!state.fVisible) {
        return;
      }
      state.arrange(places[at].first, places[at].second);
      skiff::scene::layout(child, box);
      ++at;
    });
  }

  [[nodiscard]] float crossOffset(const skiff::scene::State &child,
                                  float line, float own) const {
    return (line - own) * child.fAlignSelf.value_or(fOptions.crossAlign).at;
  }

  void lay(direction::vertical) {
    const skia::SkRect box = fState.contentBox();
    this->grow<skiff::scene::axis::y>(box, fOptions.spacingY);
    const auto shown = this->measured(box);
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
      places[i] = {this->crossOffset(state, box.width(),
                                     state.fBounds.width() +
                                         state.fMargin.totalX()),
                   y};
      y += state.fBounds.height() + state.fMargin.totalY() +
           fOptions.spacingY + gaps.fBetween;
    }
    this->place(box, places);
  }

  void lay(direction::horizontal) {
    const skia::SkRect box = fState.contentBox();
    if (!fOptions.wrap) {
      this->grow<skiff::scene::axis::x>(box, fOptions.spacingX);
    }
    const auto shown = this->measured(box);
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
        places[i] = {x, y + this->crossOffset(state, rowHeight,
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
    this->place(box, places);
  }

  // Children that grow take an equal share of what the rest leave along the
  // axis, written before anything is placed.
  template <class Axis> void grow(const skia::SkRect &box, float spacing) {
    constexpr bool horizontal = std::same_as<Axis, skiff::scene::axis::x>;
    int growers = 0;
    int visible = 0;
    float taken = 0.0f;
    skiff::scene::eachChild(*this, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (!state.fVisible) {
        return;
      }
      ++visible;
      if (state.fGrowAxes.template has<Axis>()) {
        ++growers;
        return;
      }
      skiff::scene::layout(child, box);
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
    skiff::scene::eachChild(*this, [&](auto &child) {
      skiff::scene::State &state = skiff::scene::stateOf(child);
      if (state.fVisible && state.fGrowAxes.template has<Axis>()) {
        state.arrangeAxisSize(horizontal, share);
      }
    });
  }

  FlowOptions fOptions;
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
