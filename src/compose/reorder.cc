// A held item can move along a bar or between a horizontal and vertical bar.
// The application supplies the two lanes and the item factory, not pointer
// handlers or callbacks that change its children.
export module skiff.compose:reorder;

import std;
import splice;
import skia;
import skiff.scene;
import skiff.paint;
import skiff.model;
import :boxes;
import :wiring;

export namespace skiff::compose {

enum class ReorderBar { horizontal, vertical };
struct ItemsDragging {
  bool on = false;
};
template <class Key> struct ItemsPlaced {
  ReorderBar from, to;
  Key item;
  std::vector<Key> order;
};

// Rows are MemoRows: their keys and immutable views make a fresh ghost
// possible without copying nodes. Drop can include a header around the view.
template <class Viewport>
  requires requires(const Viewport& view, const skia::SkRect& box) { view.toView(box); }
skia::SkRect reorder_shown_at(const Viewport& view, const skia::SkRect& box) { return view.toView(box); }
template <class Viewport>
  requires requires(const Viewport& view) { view.offset(); }
skia::SkRect reorder_shown_at(const Viewport& view, const skia::SkRect& box) { return box.makeOffset(-view.offset(), 0.0f); }
template <class Viewport, class Rows> struct ReorderLane {
  Viewport& view;
  Rows& rows;
  skia::SkRect drop;
  float spacing = 0.0f;
  [[nodiscard]] skia::SkRect shown_at(const skia::SkRect& box) const {
    return reorder_shown_at(view, box);
  }
};

template <class Key, class View, class Make, class Surface, class Lanes>
struct ReorderBars : Box<Surface, std::optional<std::invoke_result_t<Make&, const Key&, const View&, bool>>> {
  using Row = std::invoke_result_t<Make&, const Key&, const View&, bool>;
  using Base = Box<Surface, std::optional<Row>>;
  // A successful placement also ends dragging. A cancellation or release
  // outside the lanes answers ItemsDragging{false}. The program owns lane
  // visibility; transient marks on rows and target lanes stay here.
  // Order previews during a drag can be added as another answer later.
  using Answer = spl::variant<ItemsDragging, ItemsPlaced<Key>>;
  using Out = Union<OutOf<Surface>, model::Types<Answer>>;
  struct Drag {
    Key key;
    View view;
    ReorderBar from;
    float x, y;
    std::chrono::steady_clock::time_point pressed;
    bool moving = false, scrolling = false;
    float last_x = 0.0f;
    float width = 0.0f, height = 0.0f;
  };
  Make fMake;
  Lanes fLanes;
  std::optional<Drag> fDrag;
  std::optional<Key> fLanding;
  paint::Tween fFly{0.0f, 180.0f};
  skia::SkPoint fFrom{}, fTo{};
  skia::SkColor fHighlight = 0;
  float fGhostWidth = 0.0f, fGhostHeight = 0.0f;

  ReorderBars(Surface surface, Make make, Lanes lanes, skia::SkColor highlight)
      : Base(vbox(0.0f, {.fillX = true, .fillY = true}), std::move(surface), std::optional<Row>{}),
        fMake(std::move(make)), fLanes(std::move(lanes)), fHighlight(highlight) {}
  Surface& surface() { return std::get<0>(this->fParts); }
  auto& ghost() { return std::get<1>(this->fParts); }
  template <class F> decltype(auto) lane(ReorderBar which, F&& use) {
    auto lanes = fLanes(surface());
    if (which == ReorderBar::horizontal) return std::invoke(use, std::get<0>(lanes));
    return std::invoke(use, std::get<1>(lanes));
  }
  template <class F> void both(F&& use) {
    lane(ReorderBar::horizontal, use);
    lane(ReorderBar::vertical, use);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }
  std::optional<ReorderBar> bar_at(float x, float y) {
    if (lane(ReorderBar::horizontal, [&](auto& line) { return line.view.visible() && line.drop.contains(x, y); }))
      return ReorderBar::horizontal;
    if (lane(ReorderBar::vertical, [&](auto& line) { return line.view.visible() && line.drop.contains(x, y); }))
      return ReorderBar::vertical;
    return std::nullopt;
  }
  void ghost_at(float x, float y) {
    if (!ghost()) return;
    const auto bounds = this->bounds();
    ghost()->apply({.shiftX = x - bounds.fLeft - fGhostWidth * 0.5f,
                    .shiftY = y - bounds.fTop - fGhostHeight * 0.5f});
  }
  void light(std::optional<ReorderBar> over) {
    lane(ReorderBar::horizontal, [&](auto& line) {
      line.view.apply({.background = over == ReorderBar::horizontal ? fHighlight : skia::SkColor{0}});
    });
    lane(ReorderBar::vertical, [&](auto& line) {
      line.view.apply({.background = over == ReorderBar::vertical ? fHighlight : skia::SkColor{0}});
    });
  }
  void show_key(const Key& key, bool shown, std::optional<ReorderBar> only = std::nullopt) {
    const auto change = [&](auto& line) {
      std::ranges::for_each(line.rows.fRows, [&](auto& row) {
        if (row.fKey == key) row.fRow.setVisible(shown);
      });
      line.rows.invalidateLayout();
    };
    if (only) lane(*only, change);
    else both(change);
    this->invalidateLayout();
  }
  void land() {
    if (fLanding) show_key(*fLanding, true);
    fLanding.reset();
    ghost().reset();
    this->invalidateLayout();
    this->markDamaged();
  }
  void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply) {
    if (press.button > 1) return;
    if (fLanding) land();
    fDrag.reset();
    const auto arm = [&](ReorderBar from) {
      lane(from, [&](auto& line) {
        if (!line.view.visible()) return;
        const auto row = std::ranges::find_if(line.rows.fRows, [&](const auto& one) {
          return one.fRow.visible() && line.shown_at(one.fRow.bounds()).contains(press.x, press.y);
        });
        if (row != line.rows.fRows.end()) {
          fDrag.emplace(row->fKey, row->fView, from, press.x, press.y, std::chrono::steady_clock::now());
          fDrag->width = row->fRow.bounds().width();
          fDrag->height = row->fRow.bounds().height();
        }
      });
    };
    arm(ReorderBar::horizontal);
    if (!fDrag) arm(ReorderBar::vertical);
    if (fDrag) reply.deferClick();
  }
  template <class Phase>
    requires (std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  std::optional<Answer> onPointer(Phase, const scene::pointer::move& at, scene::PointerReply& reply) {
    if (!fDrag) return std::nullopt;
    bool starting = false;
    auto& drag = *fDrag;
    const auto scroll = [&](float delta) {
      auto lanes = fLanes(surface());
      std::get<0>(lanes).view.scroll_by(delta);
    };
    if (drag.scrolling) {
      scroll(drag.last_x - at.x);
      drag.last_x = at.x;
      reply.handle();
      return std::nullopt;
    }
    if (!drag.moving) {
      const float dx = at.x - drag.x, dy = at.y - drag.y;
      if (std::abs(dx) < 6.0f && std::abs(dy) < 6.0f) return std::nullopt;
      const bool quick = std::chrono::steady_clock::now() - drag.pressed < std::chrono::milliseconds(250);
      if (quick && drag.from == ReorderBar::vertical && std::abs(dy) > std::abs(dx)) {
        fDrag.reset(); // the vertical scroll view owns this move
        return std::nullopt;
      }
      if (quick && drag.from == ReorderBar::horizontal && std::abs(dx) > std::abs(dy)) {
        drag.scrolling = true;
        drag.last_x = at.x;
        scroll(drag.x - at.x);
        reply.capturePointer();
        reply.handle();
        return std::nullopt;
      }
      drag.moving = true;
      starting = true;
      fGhostWidth = drag.width;
      fGhostHeight = drag.height;
      ghost().emplace(std::invoke(fMake, drag.key, drag.view, true));
      ghost()->apply({.place = scene::anchor::kTopLeft, .x = 0.0f, .y = 0.0f, .alpha = 0.9f});
      show_key(drag.key, false, drag.from);
      reply.capturePointer();
      reply.suppressHover();
    }
    ghost_at(at.x, at.y);
    light(bar_at(at.x, at.y));
    this->markDamaged();
    reply.handle();
    if (starting) return Answer{ItemsDragging{true}};
    return std::nullopt;
  }
  // The insertion is computed from visible row centers, excluding the
  // dragged key. A drop outside both lanes returns to its original lane.
  template <class Phase>
    requires (std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  std::optional<Answer> onPointer(Phase, const scene::pointer::up& at, scene::PointerReply& reply) {
    if (!fDrag) return std::nullopt;
    auto drag = std::exchange(fDrag, std::nullopt);
    if (!drag->moving && !drag->scrolling) return std::nullopt;
    reply.releasePointer();
    reply.handle();
    if (drag->scrolling) return std::nullopt;
    light(std::nullopt);
    const auto over = bar_at(at.x, at.y);
    const auto to = over.value_or(drag->from);
    auto order = lane(to, [&](auto& line) {
      return line.rows.fRows | std::views::filter([&](const auto& row) { return row.fKey != drag->key; }) |
          std::views::transform([](const auto& row) { return row.fKey; }) | std::ranges::to<std::vector>();
    });
    const auto index = lane(to, [&](auto& line) {
      return std::ranges::count_if(line.rows.fRows, [&](const auto& row) {
        const auto box = line.shown_at(row.fRow.bounds());
        return row.fKey != drag->key && (to == ReorderBar::horizontal ? box.centerX() < at.x : box.centerY() < at.y);
      });
    });
    order.insert(order.begin() + index, drag->key);
    fFrom = {at.x, at.y};
    fTo = lane(to, [&](auto& line) {
      auto others = line.rows.fRows | std::views::filter([&](const auto& row) {
        return row.fKey != drag->key && row.fRow.visible();
      }) | std::views::transform([&](const auto& row) { return line.shown_at(row.fRow.bounds()); }) |
          std::ranges::to<std::vector>();
      const bool horizontal = to == ReorderBar::horizontal;
      const float step = (horizontal ? fGhostWidth : fGhostHeight) + line.spacing;
      if (static_cast<std::size_t>(index) < others.size()) return skia::SkPoint{others[index].centerX(), others[index].centerY()};
      if (!others.empty()) return horizontal ? skia::SkPoint{others.back().centerX() + step, others.back().centerY()}
                                            : skia::SkPoint{others.back().centerX(), others.back().centerY() + step};
      const auto box = line.view.bounds();
      return horizontal ? skia::SkPoint{box.fLeft + step * 0.5f, box.centerY()}
                        : skia::SkPoint{box.centerX(), box.fTop + step * 0.5f};
    });
    fLanding = drag->key;
    fFly.jump(0.0f);
    fFly.setTarget(1.0f);
    scene::work::mark(this->fState.fId);
    if (over) return Answer{ItemsPlaced<Key>{drag->from, to, std::move(drag->key), std::move(order)}};
    return Answer{ItemsDragging{false}};
  }
  template <class Phase>
    requires (std::same_as<Phase, scene::phase::capture> || std::same_as<Phase, scene::phase::target>)
  std::optional<Answer> onPointer(Phase, const scene::pointer::cancel&, scene::PointerReply& reply) {
    if (!fDrag) return std::nullopt;
    const bool moving = fDrag->moving;
    if (moving) {
      light(std::nullopt);
      show_key(fDrag->key, true);
      ghost().reset();
    }
    fDrag.reset();
    reply.releasePointer();
    if (moving) return Answer{ItemsDragging{false}};
    return std::nullopt;
  }
  using Base::onPointer;
  [[nodiscard]] bool wantsTick() const { return fLanding.has_value(); }
  [[nodiscard]] bool settling() const { return fLanding.has_value(); }
  void update(double now) {
    if (!fLanding) return;
    show_key(*fLanding, false);
    fFly.step(now);
    const float t = fFly.value();
    ghost_at(fFrom.x() + (fTo.x() - fFrom.x()) * t, fFrom.y() + (fTo.y() - fFrom.y()) * t);
    if (!fFly.moving()) land();
  }
};
template <class Key, class View, class Make, class Surface, class Lanes>
auto reorder_bars(Surface surface, Make make, Lanes lanes, skia::SkColor highlight = 0) {
  return ReorderBars<Key, View, Make, Surface, Lanes>(std::move(surface), std::move(make), std::move(lanes), highlight);
}
} // namespace skiff::compose
