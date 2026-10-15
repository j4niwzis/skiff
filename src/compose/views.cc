// Viewport state that follows a model key and a bound width description.
export module skiff.compose:views;

import std;
import skia;
import skiff.scene;
import skiff.model;
import skiff.bind;
import :wiring;

export namespace skiff::compose {

// A view described by one model part and resolved against another model:
// a filtered list's keys and snapshots, with its source rows in the main
// model. Both arrive through binding; construction needs neither model.
template <class M, class Told, class Compute, class N> struct WithModel : N {
  Compute fCompute;
  const M* fModel = nullptr;
  std::optional<Told> fTold;
  model::Revision fRevision = 0;
  bool fDirty = true, fRead = false;
  bind::Refreshes<1> fRefreshes = bind::refreshes(bind::refreshFrom<WithModel, M>());
  WithModel(Compute compute, N node) : N(std::move(node)), fCompute(std::move(compute)) {}
  void read(const Told& told) {
    fTold = told;
    fDirty = true;
    render();
  }
  void refresh(const M& model) {
    if (fModel != &model) fDirty = true;
    fModel = &model;
    render();
  }
  void render() {
    if (!fModel || !fTold) return;
    if (fRead && !fDirty && fRevision == fModel->revision()) return;
    N::read(std::invoke(fCompute, *fModel, *fTold));
    fRevision = fModel->revision();
    fRead = true;
    fDirty = false;
  }
};
template <class M, class Told, class Compute, class N> auto with_model(Compute compute, N node) {
  return WithModel<M, Told, Compute, N>(std::move(compute), std::move(node));
}

// The plate around a focusable field follows that field's focus. The
// field keeps its own geometry and input region inside the frame.
template <class N> struct FocusFrame : Box<N> {
  FocusFrame(Look look, N field) : Box<N>(std::move(look), std::move(field)) {}
  [[nodiscard]] bool wantsTick() const {
    return std::get<0>(this->fParts).focused() != this->selected();
  }
  void update(double) {
    const bool focused = std::get<0>(this->fParts).focused();
    if (focused != this->selected()) this->apply({.selected = focused});
  }
};
template <class N> auto focus_frame(Look look, N field) {
  return FocusFrame<N>(std::move(look), std::move(field));
}

// Observe a press before its target handles it, for example to dismiss a
// popover when the press is outside it. The answer can edit the local model
// while the target still handles its normal press.
// Answering does not take the press from its target.
template <class E, class Ask, class N> struct OnCapture : N {
  using Answer = E;
  using Out = Union<OutOf<N>, model::Types<E>>;
  Ask fAsk;
  OnCapture(Ask ask, N node) : N(std::move(node)), fAsk(std::move(ask)) {}
  [[nodiscard]] bool acceptsInput() const { return true; }
  using N::onPointer;
  std::optional<E> onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply) {
    N::onPointer(scene::phase::capture{}, press, reply);
    return std::invoke(fAsk, std::as_const(static_cast<N&>(*this)), press, reply);
  }
  // Empty background has no child target; the observer is the target.
  std::optional<E> onPointer(scene::phase::target, const scene::pointer::down& press, scene::PointerReply& reply) {
    N::onPointer(scene::phase::target{}, press, reply);
    return std::invoke(fAsk, std::as_const(static_cast<N&>(*this)), press, reply);
  }
};
template <class E, class Ask, class N>
  requires requires(N& node, const scene::pointer::down& press, scene::PointerReply& reply) {
    { node.onPointer(scene::phase::capture{}, press, reply) } -> std::same_as<void>;
    { node.onPointer(scene::phase::target{}, press, reply) } -> std::same_as<void>;
  }
auto onCapture(Ask ask, N node) {
  return OnCapture<E, Ask, N>(std::move(ask), std::move(node));
}

// Known-height rows retain the full scroll extent while only the viewport
// and its neighbouring screens have scene nodes. Direct jumps, wheel
// movement and restored offsets all select rows before their next layout.
struct VirtualRowsOptions {
  std::size_t step = 40;
  float minimum_view = 400.0f;
};
template <class Item, class Height, class N> struct VirtualRows : N {
  Height fHeight;
  VirtualRowsOptions fOptions;
  std::vector<Item> fItems;
  std::vector<float> fTops{0.0f};
  std::size_t fFrom = 0, fTo = 0;
  std::optional<float> fRequested;
  bool fHaveRows = false;
  VirtualRows(Height height, VirtualRowsOptions options, N node)
      : N(std::move(node)), fHeight(std::move(height)), fOptions(options) {
    fOptions.step = std::max(std::size_t{1}, fOptions.step);
    this->setHoldsInView(false);
  }
  void read(const std::vector<Item>& items) {
    fItems = items;
    fTops.assign(1, 0.0f);
    fTops.reserve(fItems.size() + 1);
    for (const auto& item : fItems)
      fTops.push_back(fTops.back() + std::max(0.0f, static_cast<float>(std::invoke(fHeight, item))));
    // Changes to the list preserve its offset, including an empty list
    // becoming populated while the viewport happened to be at its end.
    N::setCurrent(fRequested.value_or(this->current()));
    showRows(true);
  }
  void setCurrent(float offset) {
    fRequested = std::max(0.0f, offset);
    N::setCurrent(*fRequested);
    showRows(false);
  }
  void scrollToStart() { setCurrent(0.0f); }
  bool showRows(bool changed) {
    const float view = std::max(this->bounds().height(), fOptions.minimum_view);
    const float at = std::clamp(fRequested.value_or(this->current()), 0.0f,
                              std::max(0.0f, fTops.back() - this->bounds().height()));
    const auto indexAt = [&](float y) {
      const auto next = std::upper_bound(fTops.begin(), fTops.end(), std::max(0.0f, y));
      return std::min(fItems.size(), static_cast<std::size_t>(next - fTops.begin() - 1));
    };
    const auto step = fOptions.step;
    const auto from = indexAt(at - view) / step * step;
    const auto last = std::min(fItems.size(), indexAt(at + 2.0f * view) + 1);
    const auto to = std::min(fItems.size(), std::max(from + step, (last + step - 1) / step * step));
    if (!changed && fHaveRows && from == fFrom && to == fTo) return false;
    fHaveRows = true;
    fFrom = from;
    fTo = to;
    auto& rows = std::get<0>(this->fChildren);
    rows.apply({.padding = {fTops[from], 0.0f, fTops.back() - fTops[to], 0.0f}});
    rows.read(std::vector<Item>(fItems.begin() + from, fItems.begin() + to));
    this->invalidateLayout();
    return true;
  }
  void layoutChildren() {
    showRows(false);
    N::layoutChildren();
    fRequested.reset();
    // Layout may clamp the offset after a shorter list or viewport resize.
    if (showRows(false)) N::layoutChildren();
  }
  void update(double now) {
    N::update(now);
    showRows(false);
  }
};
template <class Item, class Height, class N>
auto virtual_rows(Height height, N node, VirtualRowsOptions options = {}) {
  return VirtualRows<Item, Height, N>(std::move(height), options, std::move(node));
}

// Each list has its own offset. setCurrent retains the requested offset
// until the new contents are laid out, even when the old list was shorter.
template <class Key, class N> struct RememberedScroll : N {
  std::optional<Key> fKey;
  std::vector<std::pair<Key, float>> fPositions;
  explicit RememberedScroll(N node) : N(std::move(node)) {}
  void read(const Key& key) {
    if (fKey && *fKey == key) return;
    if (fKey) {
      const auto old = std::ranges::find(fPositions, *fKey, &std::pair<Key, float>::first);
      if (old == fPositions.end()) fPositions.emplace_back(*fKey, this->current());
      else old->second = this->current();
    }
    fKey = key;
    const auto saved = std::ranges::find(fPositions, key, &std::pair<Key, float>::first);
    this->setCurrent(saved == fPositions.end() ? 0.0f : saved->second);
  }
};
template <class Key, class N> auto remembered_scroll(N node) {
  return RememberedScroll<Key, N>(std::move(node));
}

// Search results start at their top for each new result generation. A
// refresh of the same generation preserves the reader's offset.
template <class Key, class N> struct RestartedScroll : N {
  std::optional<Key> fKey;
  explicit RestartedScroll(N node) : N(std::move(node)) {}
  void read(const Key& key) {
    if (fKey && *fKey == key) return;
    fKey = key;
    this->scrollToStart();
  }
};
template <class Key, class N> auto restarted_scroll(N node) {
  return RestartedScroll<Key, N>(std::move(node));
}

// A column can fill a narrow window or retain its requested width within
// the limits of a wider one. This is geometry; the requested values are
// read from the model, not set on an application node by its holder.
struct WidthLimit {
  float wanted = 300.0f;
  float least = 240.0f;
  float share = 0.6f;
  bool whole = false;
  friend bool operator==(const WidthLimit&, const WidthLimit&) = default;
};
template <class N> struct ClampedWidth : N {
  WidthLimit fLimit;
  ClampedWidth(WidthLimit limit, N node) : N(std::move(node)), fLimit(limit) {
    this->apply({.fillX = false});
  }
  void read(const WidthLimit& limit) {
    if (limit == fLimit) return;
    fLimit = limit;
    this->invalidateLayout();
  }
  void measure(const skia::SkRect& parent) {
    this->fState.fWidth = fLimit.whole ? parent.width()
        : std::clamp(fLimit.wanted, std::min(fLimit.least, parent.width()),
                     std::max(fLimit.least, parent.width() * fLimit.share));
  }
};
template <class N> auto clamped_width(WidthLimit limit, N node) {
  return ClampedWidth<N>(limit, std::move(node));
}
} // namespace skiff::compose
