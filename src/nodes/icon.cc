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

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    const skia::SkRect &box = fState.fBounds;
    const int saved = canvas->save();
    canvas->translate(box.centerX(), box.centerY());
    if (fShape.rotation != 0.0f)
      canvas->rotate(fShape.rotation);
    for (const Mark &one : fShape.marks) {
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(one.colour.value_or(fColour));
      paint.setAlphaf(paint.getAlphaf() * alpha);
      if (!one.filled) {
        paint.setStyle(skia::kStrokeStyle);
        paint.setStrokeWidth(one.width);
        paint.setStrokeCap(skia::kRoundCap);
      }
      spl::visit([&](const auto &shape) { drawMark(canvas, shape, paint); }, one.shape);
    }
    canvas->restoreToCount(saved);
  }

private:
  // Each mark by its own overload -- not named draw, which is the node's hook.
  static void drawMark(skia::SkCanvas *canvas, const mark::line &one, const skia::SkPaint &paint) {
    canvas->drawLine(one.x1, one.y1, one.x2, one.y2, paint);
  }
  static void drawMark(skia::SkCanvas *canvas, const mark::circle &one, const skia::SkPaint &paint) {
    canvas->drawCircle(one.x, one.y, one.radius, paint);
  }
  static void drawMark(skia::SkCanvas *canvas, const mark::arc &one, const skia::SkPaint &paint) {
    canvas->drawArc(skia::SkRect::MakeLTRB(one.left, one.top, one.right, one.bottom), one.start, one.sweep, false,
                    paint);
  }
  static void drawMark(skia::SkCanvas *canvas, const mark::rect &one, const skia::SkPaint &paint) {
    canvas->drawRoundRect(skia::SkRect::MakeLTRB(one.left, one.top, one.right, one.bottom), one.radius, one.radius,
                          paint);
  }
  static void drawMark(skia::SkCanvas *canvas, const mark::path &one, const skia::SkPaint &paint) {
    skia::SkPathBuilder built;
    for (const PathStep &step : one.steps)
      spl::visit(spl::overloaded{[&](const path_step::move &at) { built.moveTo(at.x, at.y); },
                            [&](const path_step::line &at) { built.lineTo(at.x, at.y); },
                            [&](const path_step::cubic &at) { built.cubicTo(at.x1, at.y1, at.x2, at.y2, at.x, at.y); },
                            [&](const path_step::close &) { built.close(); }},
                 step);
    canvas->drawPath(built.detach(), paint);
  }

  IconShape fShape;
  skia::SkColor fColour;
};

} // namespace skiff::nodes
