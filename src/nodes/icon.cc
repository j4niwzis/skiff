export module skiff.nodes.icon;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// An icon as data: what is drawn, in points from the middle of its box --
// lines, circles, arcs, rectangles and paths -- each stroked at a width with
// round ends, or filled; the whole turned where it says. What a symbolic
// icon is, declared: nothing here draws but the Icon node.
namespace path_step {
using skiff::scene::path_step::move;
using skiff::scene::path_step::line;
using skiff::scene::path_step::cubic;
using skiff::scene::path_step::close;
} // namespace path_step
using skiff::scene::PathStep;
namespace mark {
using skiff::scene::mark::line;
using skiff::scene::mark::circle;
using skiff::scene::mark::arc;
using skiff::scene::mark::rect;
using skiff::scene::mark::path;
} // namespace mark
using skiff::scene::MarkShape;
using skiff::scene::Mark;
using skiff::scene::IconShape;

// An icon: its shape, in its colour, in the middle of its box.
class Icon : public skiff::scene::Node {
public:
  explicit Icon(IconShape shape, skia::SkColor colour = skia::colorSetARGB(255, 255, 255, 255))
      : fShape(std::move(shape)), fColour(colour) {}

  void setShape(IconShape shape) {
    fShape = std::move(shape);
    this->markDamaged();
  }
  void setColour(skia::SkColor colour) {
    if (colour == fColour)
      return;
    fColour = colour;
    this->markDamaged();
  }
  [[nodiscard]] skia::SkColor colour() const noexcept { return fColour; }

  template <class P> void drawSelf(P &p, float alpha) {
    const skia::SkRect &box = fState.fBounds;
    p.save();
    p.translate(box.centerX(), box.centerY());
    if (fShape.rotation != 0.0f)
      p.rotate(fShape.rotation);
    for (const Mark &one : fShape.marks) {
      spl::visit([&](const auto &shape) { drawMark(p, shape, one.colour.value_or(fColour), one.filled, one.width, alpha); },
                 one.shape);
    }
    p.restore();
  }

private:
  // Each mark by its own overload -- not named draw, which is the node's
  // hook. Filled, or stroked `width` wide with round ends.
  template <class P>
  static void drawMark(const P &p, const mark::line &one, skia::SkColor colour, bool, float width, float alpha) {
    p.line(one.x1, one.y1, one.x2, one.y2, width, colour, alpha);
  }
  template <class P>
  static void drawMark(const P &p, const mark::circle &one, skia::SkColor colour, bool filled, float width, float alpha) {
    if (filled)
      p.circle(one.x, one.y, one.radius, colour, alpha);
    else
      p.strokeCircle(one.x, one.y, one.radius, width, colour, alpha);
  }
  template <class P>
  static void drawMark(const P &p, const mark::arc &one, skia::SkColor colour, bool filled, float width, float alpha) {
    const skia::SkRect oval = skia::SkRect::MakeLTRB(one.left, one.top, one.right, one.bottom);
    if (filled)
      p.fillArc(oval, one.start, one.sweep, colour, alpha);
    else
      p.arc(oval, one.start, one.sweep, width, colour, alpha);
  }
  template <class P>
  static void drawMark(const P &p, const mark::rect &one, skia::SkColor colour, bool filled, float width, float alpha) {
    const skia::SkRect box = skia::SkRect::MakeLTRB(one.left, one.top, one.right, one.bottom);
    if (filled)
      p.fillRounded(box, one.radius, colour, alpha);
    else
      p.strokeRounded(box, one.radius, colour, width, alpha);
  }
  template <class P>
  static void drawMark(const P &p, const mark::path &one, skia::SkColor colour, bool filled, float width, float alpha) {
    typename P::Path built;
    for (const PathStep &step : one.steps)
      spl::visit(spl::overloaded{[&](const path_step::move &at) { built.moveTo(at.x, at.y); },
                                 [&](const path_step::line &at) { built.lineTo(at.x, at.y); },
                                 [&](const path_step::cubic &at) { built.cubicTo(at.x1, at.y1, at.x2, at.y2, at.x, at.y); },
                                 [&](const path_step::close &) { built.close(); }},
                 step);
    if (filled)
      p.fill(built, colour, alpha);
    else
      p.stroke(built, width, colour, alpha);
  }

  IconShape fShape;
  skia::SkColor fColour;
};

} // namespace skiff::nodes
