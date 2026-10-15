// Viewport state that follows a model key and a bound width description.
export module skiff.compose:views;

import std;
import skia;
import skiff.scene;
import skiff.model;
import :wiring;

export namespace skiff::compose {

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
