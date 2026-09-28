export module skiff.nodes:grid;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// One track of a grid: a fixed size, the size of what is in it, or a share
// of what the fixed and automatic ones leave (`1fr`).
struct Track {
  enum class Kind : std::uint8_t { kFixed, kAuto, kFraction };

  Kind fKind = Kind::kFraction;
  float fValue = 1.0f;

  [[nodiscard]] static Track fixed(float size) { return {Kind::kFixed, size}; }
  [[nodiscard]] static Track automatic() { return {Kind::kAuto, 0.0f}; }
  [[nodiscard]] static Track fraction(float share = 1.0f) {
    return {Kind::kFraction, share};
  }
};

// Rows and columns of given sizes, the children dealt into the cells in
// order. A cell does not move its child: the child is laid out against the
// cell as its parent, and anchors itself in it.
template <class... Children> class Grid : public skiff::scene::Node {
public:
  explicit Grid(Children... children) : fChildren(std::move(children)...) {}

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }

  void setRows(std::vector<Track> rows) {
    fRows = std::move(rows);
    this->invalidateLayout();
  }
  void setColumns(std::vector<Track> columns) {
    fColumns = std::move(columns);
    this->invalidateLayout();
  }
  void setGaps(float row, float column) {
    if (row == fRowGap && column == fColumnGap) {
      return;
    }
    fRowGap = row;
    fColumnGap = column;
    this->invalidateLayout();
  }

  // Where a cell ended up, for what has to be placed against one.
  [[nodiscard]] skia::SkRect cellBox(std::size_t row,
                                     std::size_t column) const {
    if (row >= fRowSizes.size() || column >= fColumnSizes.size()) {
      return skia::SkRect::MakeEmpty();
    }
    const skia::SkRect box = fState.contentBox();
    float x = box.fLeft;
    for (std::size_t c = 0; c < column; ++c) {
      x += fColumnSizes[c] + fColumnGap;
    }
    float y = box.fTop;
    for (std::size_t r = 0; r < row; ++r) {
      y += fRowSizes[r] + fRowGap;
    }
    return skia::SkRect::MakeXYWH(x, y, fColumnSizes[column], fRowSizes[row]);
  }

  void layoutChildren() {
    namespace scene = skiff::scene;
    const skia::SkRect box = fState.contentBox();
    std::vector<scene::State *> shown;
    scene::eachChild(*this, [&](auto &child) {
      scene::State &state = scene::stateOf(child);
      if (state.fVisible) {
        scene::layout(child, box); // its own size, which sizes auto tracks
        shown.push_back(&state);
      }
    });
    const std::size_t columns = std::max<std::size_t>(1, fColumns.size());
    const std::size_t rows =
        fRows.empty()
            ? std::max<std::size_t>(1, (shown.size() + columns - 1) / columns)
            : fRows.size();
    fColumnSizes =
        resolve(fColumns, columns, box.width(), fColumnGap, true, columns, shown);
    fRowSizes =
        resolve(fRows, rows, box.height(), fRowGap, false, columns, shown);

    std::size_t index = 0;
    scene::eachChild(*this, [&](auto &child) {
      if (!scene::stateOf(child).fVisible) {
        return;
      }
      const std::size_t row = index / columns;
      const std::size_t column = index % columns;
      ++index;
      if (row < rows) { // more children than cells: the rest stay unplaced
        scene::layout(child, this->cellBox(row, column));
      }
    });
  }

  std::tuple<Children...> fChildren;

private:
  [[nodiscard]] static float
  naturalSize(std::size_t track, bool horizontal, std::size_t columns,
              const std::vector<skiff::scene::State *> &shown) {
    float size = 0.0f;
    for (std::size_t index = 0; index < shown.size(); ++index) {
      const std::size_t at = horizontal ? index % columns : index / columns;
      if (at != track) {
        continue;
      }
      const skiff::scene::State &state = *shown[index];
      size = std::max(size, horizontal
                                ? state.fBounds.width() + state.fMargin.totalX()
                                : state.fBounds.height() +
                                      state.fMargin.totalY());
    }
    return size;
  }

  [[nodiscard]] static std::vector<float>
  resolve(const std::vector<Track> &tracks, std::size_t count, float room,
          float gap, bool horizontal, std::size_t columns,
          const std::vector<skiff::scene::State *> &shown) {
    std::vector<float> sizes(count, 0.0f);
    float taken = gap * static_cast<float>(count > 0 ? count - 1 : 0);
    float shares = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
      const Track track = i < tracks.size() ? tracks[i] : Track::fraction(1.0f);
      switch (track.fKind) {
      case Track::Kind::kFixed:
        sizes[i] = track.fValue;
        taken += sizes[i];
        break;
      case Track::Kind::kAuto:
        sizes[i] = naturalSize(i, horizontal, columns, shown);
        taken += sizes[i];
        break;
      case Track::Kind::kFraction:
        shares += track.fValue;
        break;
      }
    }
    if (shares > 0.0f) {
      const float left = std::max(0.0f, room - taken);
      for (std::size_t i = 0; i < count; ++i) {
        const Track track =
            i < tracks.size() ? tracks[i] : Track::fraction(1.0f);
        if (track.fKind == Track::Kind::kFraction) {
          sizes[i] = left * track.fValue / shares;
        }
      }
    }
    return sizes;
  }

  std::vector<Track> fRows;
  std::vector<Track> fColumns;
  float fRowGap = 0.0f;
  float fColumnGap = 0.0f;
  std::vector<float> fRowSizes;
  std::vector<float> fColumnSizes;
};

} // namespace skiff::nodes
