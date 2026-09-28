export module skiff.nodes:box;

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
    const skiff::scene::State &state = fState;
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fColour);
    paint.setAlphaf(alpha);
    if (state.fCornerRadius > 0.0f) {
      canvas->drawRRect(skia::SkRRect::MakeRectXY(state.fBounds,
                                                  state.fCornerRadius,
                                                  state.fCornerRadius),
                        paint);
    } else {
      canvas->drawRect(state.fBounds, paint);
    }
  }

  std::tuple<Children...> fChildren;

private:
  skia::SkColor fColour;
  skia::SkColor fBaseColour = 0;
  bool fNodeStyleActive = false;
};

} // namespace skiff::nodes
