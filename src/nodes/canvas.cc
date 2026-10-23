export module skiff.nodes.canvas;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// The painter a Canvas node draws with: the words of the page's
// CanvasPainter (skiff.web), over the scene's painter -- Skia's, or any
// other. Coordinates are the node's own, from its top left.
template <class P> class CanvasPainterOf {
public:
  // Nothing stays: the scene paints the node whole each time it is damaged.
  static constexpr bool kRetains = false;
  CanvasPainterOf(const P &painter, float width, float height, float alpha)
      : fPainter(painter), fWidth(width), fHeight(height), fAlpha(alpha) {}
  void clear(scene::Colour colour) { fPainter.fillRect(skia::SkRect::MakeWH(fWidth, fHeight), colour, fAlpha); }
  void fillPolygon(std::span<const scene::Point> points, scene::Colour colour) {
    fPainter.fill(path(points), colour, fAlpha);
  }
  void strokePolygon(std::span<const scene::Point> points, float width, scene::Colour colour) {
    fPainter.stroke(path(points), width, colour, fAlpha, P::Cap::square, false);
  }
  void line(scene::Point a, scene::Point b, float width, scene::Colour colour) {
    fPainter.line(a.fX, a.fY, b.fX, b.fY, width, colour, fAlpha, P::Cap::square);
  }
  void circle(scene::Point centre, float radius, scene::Colour colour) {
    fPainter.circle(centre.fX, centre.fY, radius, colour, fAlpha);
  }
  // Centred on `at`, bold; nothing where the program set no font.
  void text(scene::Point at, std::string_view text, float size, scene::Colour colour) {
    if (!skiff::paint::defaultFont()) return;
    const std::string s(text);
    const float width = fPainter.measure(s, size, true);
    fPainter.text(s, at.fX - width / 2.0f, at.fY + size * 0.35f, size, colour, fAlpha, true);
  }

private:
  [[nodiscard]] static typename P::Path path(std::span<const scene::Point> points) {
    typename P::Path built;
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (i == 0) built.moveTo(points[i].fX, points[i].fY);
      else built.lineTo(points[i].fX, points[i].fY);
    }
    built.close();
    return built;
  }
  const P &fPainter;
  float fWidth, fHeight, fAlpha;
};
// On Skia, as the scene draws it.
using SkiaPainter = CanvasPainterOf<skiff::paint::Painter>;

// A node that draws itself through a painter -- its own paint(painter); `draw`
// is the scene's hook on every node -- in
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

  template <class Hooks, class P> void drawSelf(this auto &self, Hooks &hooks, P &p, float alpha) {
    const scene::State &state = self.fState;
    const skia::SkRect &bounds = state.fBounds;
    p.save();
    p.translate(bounds.left(), bounds.top());
    p.clipRect(skia::SkRect::MakeWH(bounds.width(), bounds.height()));
    CanvasPainterOf<P> painter(p, bounds.width(), bounds.height(), alpha);
    self.paint(painter);
    p.restore();
    skiff::scene::detail::paintOver(hooks, state, p, alpha);
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
