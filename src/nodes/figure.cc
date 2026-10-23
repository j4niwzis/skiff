export module skiff.nodes.figure;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// Shapes as nodes, drawn by Skia: a polygon, a polyline, a line, a circle,
// each with the points, the fill and the stroke it has now, in the
// coordinates of the figure it is in. The page has the same (skiff.web's),
// as SVG elements the browser moves; here a change damages the node and it
// is drawn again. Bound to a model as any node, listed as many as the model
// has (skiff.compose's group).
struct Shape : skiff::scene::Node {
  scene::Colour fFill = 0xffffffffu, fStroke = 0;
  float fStrokeWidth = 0.0f, fOpacity = 1.0f;
  std::string fClass;
  void setFill(scene::Colour fill) { change(fFill, fill); }
  void setStroke(scene::Colour stroke, float width) {
    change(fStroke, stroke);
    change(fStrokeWidth, width);
  }
  void setOpacity(float opacity) { change(fOpacity, opacity); }
  void setClass(std::string name) { fClass = std::move(name); }
  // A style of the page's own (an offset-path, an animation): nothing here.
  void style(std::string, std::string) {}
  // A shape takes no pointer: a press on it is the figure's.
  [[nodiscard]] bool acceptsInput() const { return false; }

protected:
  template <class V> void change(V &field, V value) {
    if (field != value) {
      field = std::move(value);
      this->markDamaged();
    }
  }
  [[nodiscard]] bool fills() const noexcept { return ((fFill >> 24) & 0xffu) != 0; }
  [[nodiscard]] bool strokes() const noexcept { return ((fStroke >> 24) & 0xffu) != 0 && fStrokeWidth > 0.0f; }
  // The points as a path in the painter's own: closed, for a polygon.
  template <class P> [[nodiscard]] static typename P::Path pathOf(std::span<const scene::Point> points, bool close) {
    typename P::Path path;
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (i == 0) path.moveTo(points[i].fX, points[i].fY);
      else path.lineTo(points[i].fX, points[i].fY);
    }
    if (close) path.close();
    return path;
  }
  // Drawn in the figure's own coordinates, at the shape's opacity; the
  // program's paint over it.
  template <class Hooks, class P, class Draw> void drawShape(Hooks &hooks, const P &p, float alpha, Draw &&draw) {
    const scene::State &state = fState;
    if (fOpacity > 0.0f) {
      p.save();
      p.translate(state.fBounds.left(), state.fBounds.top());
      draw(alpha * fOpacity);
      p.restore();
    }
    skiff::scene::detail::paintOver(hooks, state, p, alpha);
  }
};
class Polygon : public Shape {
public:
  explicit Polygon(std::vector<scene::Point> points = {}, scene::Colour fill = 0xffffffffu) : fPoints(std::move(points)) { fFill = fill; }
  void setPoints(std::vector<scene::Point> points) { change(fPoints, std::move(points)); }
  [[nodiscard]] const std::vector<scene::Point> &points() const noexcept { return fPoints; }
  template <class Hooks, class P> void drawSelf(Hooks &hooks, P &p, float alpha) {
    drawShape(hooks, p, alpha, [&](float shown) {
      const auto path = pathOf<P>(fPoints, true);
      if (fills()) p.fill(path, fFill, shown);
      if (strokes()) p.stroke(path, fStrokeWidth, fStroke, shown, P::Cap::square, false);
    });
  }

private:
  std::vector<scene::Point> fPoints;
};
class Polyline : public Shape {
public:
  explicit Polyline(std::vector<scene::Point> points = {}, scene::Colour stroke = 0xffffffffu, float width = 1.0f) : fPoints(std::move(points)) {
    fFill = 0;
    fStroke = stroke;
    fStrokeWidth = width;
  }
  void setPoints(std::vector<scene::Point> points) { change(fPoints, std::move(points)); }
  [[nodiscard]] const std::vector<scene::Point> &points() const noexcept { return fPoints; }
  template <class Hooks, class P> void drawSelf(Hooks &hooks, P &p, float alpha) {
    drawShape(hooks, p, alpha, [&](float shown) {
      if (fPoints.size() < 2 || !strokes()) return;
      p.stroke(pathOf<P>(fPoints, false), fStrokeWidth, fStroke, shown, P::Cap::round, true);
    });
  }

private:
  std::vector<scene::Point> fPoints;
};
class Line : public Shape {
public:
  Line(scene::Point a = {}, scene::Point b = {}, scene::Colour stroke = 0xffffffffu, float width = 1.0f) : fA(a), fB(b) {
    fFill = 0;
    fStroke = stroke;
    fStrokeWidth = width;
  }
  void setEnds(scene::Point a, scene::Point b) {
    if (a.fX != fA.fX || a.fY != fA.fY || b.fX != fB.fX || b.fY != fB.fY) {
      fA = a;
      fB = b;
      this->markDamaged();
    }
  }
  template <class Hooks, class P> void drawSelf(Hooks &hooks, P &p, float alpha) {
    drawShape(hooks, p, alpha, [&](float shown) {
      if (strokes()) p.line(fA.fX, fA.fY, fB.fX, fB.fY, fStrokeWidth, fStroke, shown, P::Cap::square);
    });
  }

private:
  scene::Point fA, fB;
};
class Circle : public Shape {
public:
  Circle(scene::Point centre = {}, float radius = 1.0f, scene::Colour fill = 0xffffffffu) : fCentre(centre), fRadius(radius) { fFill = fill; }
  void setCentre(scene::Point centre) {
    if (centre.fX != fCentre.fX || centre.fY != fCentre.fY) {
      fCentre = centre;
      this->markDamaged();
    }
  }
  void setRadius(float radius) { change(fRadius, radius); }
  [[nodiscard]] scene::Point centre() const noexcept { return fCentre; }
  template <class Hooks, class P> void drawSelf(Hooks &hooks, P &p, float alpha) {
    drawShape(hooks, p, alpha, [&](float shown) {
      if (fills()) p.circle(fCentre.fX, fCentre.fY, fRadius, fFill, shown);
      if (strokes()) p.strokeCircle(fCentre.fX, fCentre.fY, fRadius, fStrokeWidth, fStroke, shown);
    });
  }

private:
  scene::Point fCentre;
  float fRadius;
};

// The figure: its parts laid out in its box, each over the others, in the
// size it says; a press on it comes as pressed(Press) to the figure that
// has it, then as its press (onPress), as a Canvas has it.
template <class... Parts> class Figure : public skiff::scene::Node {
public:
  struct Press {
    scene::Point at;
    int button = 0;
  };
  std::tuple<Parts...> fParts;
  Figure(float width, float height, Parts... parts) : fParts(std::move(parts)...), fWidth(width), fHeight(height) {
    this->apply(scene::Spec{.width = width, .height = height});
  }
  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fParts);
  }
  void setSize(float width, float height) {
    if (width == fWidth && height == fHeight) return;
    fWidth = width;
    fHeight = height;
    this->apply(scene::Spec{.width = width, .height = height});
    this->markDamaged();
  }
  void setClass(std::string name) { fClass = std::move(name); }
  [[nodiscard]] bool acceptsInput() const { return true; }
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
  float fWidth, fHeight;
  std::string fClass = "skiff-figure";
  bool fArmed = false;
  int fButton = 0;
};
template <class... Parts> [[nodiscard]] Figure<Parts...> figure(float width, float height, Parts... parts) {
  return Figure<Parts...>(width, height, std::move(parts)...);
}

} // namespace skiff::nodes
