export module skiff.nodes:image;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// How a picture fills a box of other proportions: covering it, cut at the
// middle of the side that is too long; or contained in it, whole.
namespace fit {
struct cover {};
struct contain {};
} // namespace fit
using Fit = std::variant<fit::cover, fit::contain>;

// Where a picture comes from: asked for each frame it is drawn, so a
// picture that comes later -- fetched, decoded -- is drawn once it is there,
// and one let go by a cache is not held here.
using ImageSource = std::function<const skia::Sp<skia::SkImage> *()>;

// A picture in a box, in the box's corner radius; nothing where it has not
// come -- the box's background shows, as a placeholder. Its proportions,
// for a box that follows them, are what it says.
class Image : public skiff::scene::Node {
public:
  explicit Image(ImageSource source, Fit how = fit::cover{})
      : fSource(std::move(source)), fFit(how) {}

  // Another picture: drawn from where it comes from now.
  void setSource(ImageSource source) {
    fSource = std::move(source);
    this->markDamaged();
  }

  [[nodiscard]] const skia::Sp<skia::SkImage> *image() const {
    const skia::Sp<skia::SkImage> *found = fSource ? fSource() : nullptr;
    return found && *found ? found : nullptr;
  }
  // Width over height of the picture, where it has come.
  [[nodiscard]] std::optional<float> ratio() const {
    const auto *found = this->image();
    if (!found || (*found)->height() == 0)
      return std::nullopt;
    return static_cast<float>((*found)->width()) / static_cast<float>((*found)->height());
  }

  // The picture coming, or going: drawn again, and laid out again for a
  // box that follows its proportions.
  void update(double) {
    const bool has = this->image() != nullptr;
    if (has != fHad) {
      fHad = has;
      this->invalidateLayout();
    }
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    const skia::Sp<skia::SkImage> *found = this->image();
    if (!found)
      return;
    const skia::SkRect &box = fState.fBounds;
    const float iw = static_cast<float>((*found)->width());
    const float ih = static_cast<float>((*found)->height());
    if (iw <= 0.0f || ih <= 0.0f || box.isEmpty())
      return;
    const int saved = canvas->save();
    const float radius = fState.fCornerRadius;
    canvas->clipRRect(skia::SkRRect::MakeRectXY(box, radius, radius), true);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    const skia::SkSamplingOptions sampling(skia::SkFilterMode::kLinear);
    std::visit(
        [&](auto how) { drawFitted(canvas, *found, box, iw, ih, how, sampling, paint); },
        fFit);
    canvas->restoreToCount(saved);
  }

private:
  static void drawFitted(skia::SkCanvas *canvas, const skia::Sp<skia::SkImage> &image, const skia::SkRect &box,
                         float iw, float ih, fit::cover, const skia::SkSamplingOptions &sampling,
                         const skia::SkPaint &paint) {
    const float scale = std::max(box.width() / iw, box.height() / ih);
    const float sw = box.width() / scale, sh = box.height() / scale;
    const skia::SkRect from = skia::SkRect::MakeXYWH((iw - sw) * 0.5f, (ih - sh) * 0.5f, sw, sh);
    canvas->drawImageRect(image, from, box, sampling, &paint, skia::SkCanvas::kFast_SrcRectConstraint);
  }
  static void drawFitted(skia::SkCanvas *canvas, const skia::Sp<skia::SkImage> &image, const skia::SkRect &box,
                         float iw, float ih, fit::contain, const skia::SkSamplingOptions &sampling,
                         const skia::SkPaint &paint) {
    const float scale = std::min(box.width() / iw, box.height() / ih);
    const float w = iw * scale, h = ih * scale;
    const skia::SkRect to =
        skia::SkRect::MakeXYWH(box.centerX() - w * 0.5f, box.centerY() - h * 0.5f, w, h);
    canvas->drawImageRect(image, to, sampling, &paint);
  }

  ImageSource fSource;
  Fit fFit;
  bool fHad = false;
};

} // namespace skiff::nodes
