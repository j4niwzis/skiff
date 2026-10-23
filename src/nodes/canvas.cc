export module skiff.nodes.canvas;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// The painter a Canvas node draws with: the words of the page's
// CanvasPainter (skiff.web), on a Skia canvas. Coordinates are the node's
// own, from its top left.
class SkiaPainter {
public:
  // Nothing stays: the scene paints the node whole each time it is damaged.
  static constexpr bool kRetains = false;
  SkiaPainter(skia::SkCanvas *canvas, float width, float height, float alpha)
      : fCanvas(canvas), fWidth(width), fHeight(height), fAlpha(alpha) {}
  void clear(scene::Colour colour) {
    fCanvas->drawRect(skia::SkRect::MakeWH(fWidth, fHeight), fill(colour));
  }
  void fillPolygon(std::span<const scene::Point> points, scene::Colour colour) {
    fCanvas->drawPath(path(points), fill(colour));
  }
  void strokePolygon(std::span<const scene::Point> points, float width, scene::Colour colour) {
    fCanvas->drawPath(path(points), stroke(width, colour));
  }
  void line(scene::Point a, scene::Point b, float width, scene::Colour colour) {
    fCanvas->drawLine(a.fX, a.fY, b.fX, b.fY, stroke(width, colour));
  }
  void circle(scene::Point centre, float radius, scene::Colour colour) {
    fCanvas->drawCircle(centre.fX, centre.fY, radius, fill(colour));
  }
  void text(scene::Point at, std::string_view text, float size, scene::Colour colour) {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (!font) return;
    const skiff::paint::Painter p(fCanvas, *font);
    const std::string s(text);
    const float width = p.measure(s, size, true);
    p.text(s, at.fX - width / 2.0f, at.fY + size * 0.35f, size, colour, fAlpha, true);
  }

private:
  [[nodiscard]] skia::SkPaint fill(scene::Colour colour) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(colour);
    p.setAlphaf(fAlpha * static_cast<float>((colour >> 24) & 0xffu) / 255.0f);
    return p;
  }
  [[nodiscard]] skia::SkPaint stroke(float width, scene::Colour colour) const {
    skia::SkPaint p = fill(colour);
    p.setStyle(skia::kStrokeStyle);
    p.setStrokeWidth(width);
    p.setStrokeCap(skia::SkPaint::kSquare_Cap);
    return p;
  }
  [[nodiscard]] static skia::SkPath path(std::span<const scene::Point> points) {
    skia::SkPathBuilder builder;
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (i == 0) builder.moveTo(points[i].fX, points[i].fY);
      else builder.lineTo(points[i].fX, points[i].fY);
    }
    builder.close();
    return builder.detach();
  }
  skia::SkCanvas *fCanvas;
  float fWidth, fHeight, fAlpha;
};

// A node that draws itself through a painter -- its own draw(painter) -- in
// the size it says (setSize), and takes a click: pressed(Press) on the node
// that is one, where and which button, then its press (onPress) as any
// control's. The page has the same (skiff.web's Canvas): a program draws
// and is clicked once, for either.
class Canvas : public skiff::scene::Node {
public:
  struct Press {
    scene::Point at;
    int button = 0;
  };
  void setSize(int width, int height) {
    if (width == fWidth && height == fHeight) return;
    fWidth = width;
    fHeight = height;
    this->apply(scene::Spec{.width = static_cast<float>(width), .height = static_cast<float>(height)});
    this->markDamaged();
  }
  [[nodiscard]] int width() const noexcept { return fWidth; }
  [[nodiscard]] int height() const noexcept { return fHeight; }
  void redraw() { this->markDamaged(); }
  [[nodiscard]] bool acceptsInput() const { return true; }

  template <class Hooks> void drawSelf(this auto &self, Hooks &hooks, skia::SkCanvas *canvas, float alpha) {
    const scene::State &state = self.fState;
    const skia::SkRect &bounds = state.fBounds;
    canvas->save();
    canvas->translate(bounds.left(), bounds.top());
    canvas->clipRect(skia::SkRect::MakeWH(bounds.width(), bounds.height()));
    SkiaPainter painter(canvas, bounds.width(), bounds.height(), alpha);
    self.draw(painter);
    canvas->restore();
    hooks.over(state, canvas, alpha);
  }

  // A press and its release inside: a click, by the button pressed.
  using Node::onPointer;
  void onPointer(scene::phase::target, const scene::pointer::down &press, scene::PointerReply &reply) {
    fArmed = true;
    fButton = press.button;
    reply.handle();
  }
  void onPointer(this auto &self, scene::phase::target, const scene::pointer::up &release, scene::PointerReply &reply) {
    if (!std::exchange(self.fArmed, false) || !self.fState.fBounds.contains(release.x, release.y)) return;
    if constexpr (requires(Press p) { self.pressed(p); }) {
      self.pressed(Press{{release.x - self.fState.fBounds.left(), release.y - self.fState.fBounds.top()}, self.fButton});
      scene::pressLater(self.fState);
    }
    reply.handle();
  }
  void onPointer(scene::phase::target, const scene::pointer::cancel &, scene::PointerReply &) { fArmed = false; }

private:
  int fWidth = 1, fHeight = 1;
  bool fArmed = false;
  int fButton = 0;
};

} // namespace skiff::nodes
