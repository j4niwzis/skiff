export module skiff.nodes:flow;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// How a flow lays its children out: along which axis, how far apart, and
// what it does with room left over.
struct FlowOptions {
  enum class Direction : std::uint8_t { kHorizontal, kVertical };
  // How the room left along the axis is handed out.
  enum class Justify : std::uint8_t {
    kStart,
    kMiddle,
    kEnd,
    kSpaceBetween,
    kSpaceAround
  };

  Direction direction = Direction::kVertical;
  float spacingX = 0.0f;
  float spacingY = 0.0f;
  // A horizontal flow breaks into rows at its edge.
  bool wrap = true;
  // Rows centred in the container, as the cards are.
  bool centreRows = false;
  // Where children sit across the axis; a child can say otherwise with
  // alignSelf.
  skiff::scene::Align crossAlign = skiff::scene::Align::kStart;
  Justify justify = Justify::kStart;
};

// FillFlowContainer: children laid end to end, wrapping when they run out of
// room, which is how lazer builds every list and row of filters.
template <class... Children> class Flow : public skiff::scene::Node {
public:
  using Direction = FlowOptions::Direction;
  using Justify = FlowOptions::Justify;
  using Align = skiff::scene::Align;

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
  void setCrossAlign(Align align) {
    if (align != fOptions.crossAlign) {
      fOptions.crossAlign = align;
      this->invalidateLayout();
    }
  }
  void setJustify(Justify justify) {
    if (justify != fOptions.justify) {
      fOptions.justify = justify;
      this->invalidateLayout();
    }
  }

  void layoutChildren() {
    namespace scene = skiff::scene;
    const skia::SkRect box = fState.contentBox();
    const bool horizontal = fOptions.direction == Direction::kHorizontal;
    this->grow(box, horizontal);

    // What every visible child is at its own size. Clean children keep
    // their cached result: position does not change a measured size.
    std::vector<scene::State *> shown;
    scene::eachChild(*this, [&](auto &child) {
      scene::State &state = scene::stateOf(child);
      if (state.fVisible) {
        scene::layout(child, box);
        shown.push_back(&state);
      }
    });

    // Where each goes, worked out from what they measured.
    std::vector<std::pair<float, float>> places(shown.size());
    if (!horizontal) {
      float used = 0.0f;
      for (const scene::State *state : shown) {
        used += state->fBounds.height() + state->fMargin.totalY();
      }
      const int count = static_cast<int>(shown.size());
      const Spread spread = this->spread(
          box.height(),
          used + fOptions.spacingY * static_cast<float>(std::max(0, count - 1)),
          count);
      float y = spread.fStart;
      for (std::size_t i = 0; i < shown.size(); ++i) {
        const scene::State &state = *shown[i];
        places[i] = {this->crossOffset(state, box.width(),
                                       state.fBounds.width() +
                                           state.fMargin.totalX()),
                     y};
        y += state.fBounds.height() + state.fMargin.totalY() +
             fOptions.spacingY + spread.fBetween;
      }
    } else {
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
        const Spread spread =
            fOptions.centreRows
                ? Spread{(box.width() - rowWidth) * 0.5f, 0.0f}
                : this->spread(box.width(), rowWidth,
                               static_cast<int>(end - rowStart));
        float x = spread.fStart;
        for (std::size_t i = rowStart; i < end; ++i) {
          const scene::State &state = *shown[i];
          places[i] = {x, y + this->crossOffset(state, rowHeight,
                                                state.fBounds.height() +
                                                    state.fMargin.totalY())};
          x += state.fBounds.width() + state.fMargin.totalX() +
               fOptions.spacingX + spread.fBetween;
        }
        y += rowHeight + fOptions.spacingY;
        rowStart = end;
        rowWidth = 0.0f;
      };
      for (std::size_t i = 0; i < shown.size(); ++i) {
        const float width =
            shown[i]->fBounds.width() + shown[i]->fMargin.totalX();
        // Half a pixel of slack: four quarters add up to the width, and
        // whether that comes out a hair over depends on the arithmetic.
        if (fOptions.wrap && i > rowStart &&
            rowWidth + fOptions.spacingX + width > box.width() + 0.5f) {
          flush(i);
        }
        rowWidth += i == rowStart ? width : fOptions.spacingX + width;
      }
      flush(shown.size());
    }

    // And placed: a child that did not move keeps its layout.
    std::size_t at = 0;
    scene::eachChild(*this, [&](auto &child) {
      scene::State &state = scene::stateOf(child);
      if (!state.fVisible) {
        return;
      }
      state.arrange(places[at].first, places[at].second);
      scene::layout(child, box);
      ++at;
    });
  }

  std::tuple<Children...> fChildren;

private:
  struct Spread {
    float fStart = 0.0f;
    float fBetween = 0.0f;
  };

  [[nodiscard]] float crossOffset(const skiff::scene::State &child,
                                  float line, float own) const {
    switch (child.fAlignSelf.value_or(fOptions.crossAlign)) {
    case Align::kMiddle:
      return (line - own) * 0.5f;
    case Align::kEnd:
      return line - own;
    case Align::kStart:
      break;
    }
    return 0.0f;
  }

  [[nodiscard]] Spread spread(float room, float used, int count) const {
    const float slack = std::max(0.0f, room - used);
    const auto gaps = static_cast<float>(std::max(0, count - 1));
    switch (fOptions.justify) {
    case Justify::kMiddle:
      return {slack * 0.5f, 0.0f};
    case Justify::kEnd:
      return {slack, 0.0f};
    case Justify::kSpaceBetween:
      return {0.0f, gaps > 0.0f ? slack / gaps : 0.0f};
    case Justify::kSpaceAround:
      return {count > 0 ? slack / static_cast<float>(count) * 0.5f : 0.0f,
              count > 0 ? slack / static_cast<float>(count) : 0.0f};
    case Justify::kStart:
      break;
    }
    return {};
  }

  // Children that grow take an equal share of what the rest leave along the
  // axis, written before anything is placed. A wrapping row is left alone:
  // what is left over depends on the rows, which depend on these widths.
  void grow(const skia::SkRect &box, bool horizontal) {
    namespace scene = skiff::scene;
    if (horizontal && fOptions.wrap) {
      return;
    }
    const auto grows = [horizontal](const scene::State &state) {
      return horizontal ? scene::hasX(state.fGrowAxes)
                        : scene::hasY(state.fGrowAxes);
    };
    int growers = 0;
    int visible = 0;
    float taken = 0.0f;
    scene::eachChild(*this, [&](auto &child) {
      scene::State &state = scene::stateOf(child);
      if (!state.fVisible) {
        return;
      }
      ++visible;
      if (grows(state)) {
        ++growers;
        return;
      }
      scene::layout(child, box);
      taken += horizontal ? state.fBounds.width() + state.fMargin.totalX()
                          : state.fBounds.height() + state.fMargin.totalY();
    });
    if (growers == 0) {
      return;
    }
    const float gaps = (horizontal ? fOptions.spacingX : fOptions.spacingY) *
                       static_cast<float>(std::max(0, visible - 1));
    const float room = horizontal ? box.width() : box.height();
    const float share =
        std::max(0.0f, (room - taken - gaps) / static_cast<float>(growers));
    scene::eachChild(*this, [&](auto &child) {
      scene::State &state = scene::stateOf(child);
      if (state.fVisible && grows(state)) {
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
      {.direction = FlowOptions::Direction::kVertical, .spacingY = spacing},
      std::move(children)...);
}
template <class... Children>
[[nodiscard]] Flow<Children...> row(float spacing, Children... children) {
  return Flow<Children...>({.direction = FlowOptions::Direction::kHorizontal,
                            .spacingX = spacing,
                            .wrap = false},
                           std::move(children)...);
}

} // namespace skiff::nodes
