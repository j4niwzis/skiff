export module skiff.nodes.box;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// A filled rectangle, optionally rounded, with whatever it holds laid out in
// it. The framework's Box.
template <class... Children> class Box : public skiff::scene::Node {
public:
  explicit Box(skia::SkColor colour, Children... children)
      : fChildren(std::move(children)...), fColour(colour) {}

  void setColour(skia::SkColor colour) {
    if (colour == fColour) {
      return;
    }
    fColour = colour;
    this->markDamaged();
  }
  [[nodiscard]] skia::SkColor colour() const noexcept { return fColour; }

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }

  void applyNodeStyle(const skiff::scene::Style &style, bool active) {
    if (!active && !fNodeStyleActive) {
      return;
    }
    if (active && !fNodeStyleActive) {
      fBaseColour = fColour;
    }
    const skia::SkColor target =
        active ? style.backgroundColour.value_or(fBaseColour) : fBaseColour;
    if (target != fColour) {
      fColour = target;
      this->markDamaged();
    }
    fNodeStyleActive = active;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    namespace detail = skiff::scene::detail;
    const skiff::scene::State &state = fState;
    const skia::SkRRect shape = skia::SkRRect::MakeRectXY(state.fBounds, state.fCornerRadius, state.fCornerRadius);
    // A panel's colour -- a dialog's sheet, a drawer's -- painted as the
    // panels' look says, as a fill is: at its opacity, frosted or edged; the
    // same colour inside a panel not painted again.
    skia::SkColor colour = fColour;
    const detail::PanelLook &look = detail::panelLook();
    bool panel = false;
    if (look.active && std::ranges::contains(look.panels, colour)) {
      if (detail::insidePanel()) {
        return;
      }
      panel = true;
      detail::insidePanel() = true;
      colour = detail::atOpacity(colour, look.opacity);
    } else if (look.active && std::ranges::contains(look.tints, colour)) {
      colour = detail::atOpacity(colour, look.opacity);
    }
    if (panel && look.frosted && detail::backdrop().image && !detail::backdrop().device.isEmpty()) {
      skia::SkMatrix inverse;
      if (canvas->getTotalMatrix().invert(&inverse)) {
        const int saved = canvas->save();
        canvas->clipRRect(shape, true);
        skia::SkPaint frost;
        frost.setAlphaf(alpha);
        canvas->drawImageRect(detail::backdrop().image, inverse.mapRect(detail::backdrop().device),
                              skia::SkSamplingOptions(detail::backdropSampling()), &frost);
        canvas->restoreToCount(saved);
      }
    }
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(colour);
    // The colour's own alpha, and the node's on top of it.
    paint.setAlphaf(alpha * static_cast<float>((colour >> 24) & 0xffu) / 255.0f);
    canvas->drawRRect(shape, paint);
    if (panel && look.edge) {
      skia::SkPaint edge;
      edge.setAntiAlias(true);
      edge.setStyle(skia::kStrokeStyle);
      edge.setStrokeWidth(1.0f);
      edge.setColor(detail::atOpacity(0xFFFFFFFFu, 0.22f));
      edge.setAlphaf(edge.getAlphaf() * alpha);
      canvas->drawRRect(shape, edge);
    }
  }

  std::tuple<Children...> fChildren;

private:
  skia::SkColor fColour;
  skia::SkColor fBaseColour = 0;
  bool fNodeStyleActive = false;
};

} // namespace skiff::nodes
