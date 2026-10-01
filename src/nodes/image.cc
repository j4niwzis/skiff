export module skiff.nodes.image;

import std;
import splice;
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
using Fit = splice::variant<fit::cover, fit::contain>;

// Where a picture comes from: a value called for each frame it is drawn,
// so a picture that comes later -- fetched, decoded -- is drawn once it is
// there, and one let go by a cache is not held here. Its type is the
// Image's parameter: a key and the cache it is looked up in, say.
template <class Source>
concept ImageSource = std::copy_constructible<Source> && requires(const Source &source) {
  { source() } -> std::convertible_to<const skia::Sp<skia::SkImage> *>;
};

// A picture in a box, in the box's corner radius; nothing where it has not
// come -- the box's background shows, as a placeholder. Its proportions,
// for a box that follows them, are what it says.
template <ImageSource Source> class Image : public skiff::scene::Node {
public:
  explicit Image(Source source, Fit how = fit::cover{})
      : fSource(std::move(source)), fFit(how) {}

  // Another picture: drawn from where it comes from now.
  void setSource(Source source) {
    fSource = std::move(source);
    fWaiting = false;
    this->markDamaged();
  }

  // Its box the same whatever the picture -- a custom emoji in a grid of
  // fixed cells: the picture coming repaints it, and lays nothing out
  // again. Every one coming into a list -- and each let go by its cache and
  // come back -- laid the list out again in the middle of a scroll.
  void keepBox(bool kept = true) { fBoxKept = kept; }

  [[nodiscard]] const skia::Sp<skia::SkImage> *image() const {
    const skia::Sp<skia::SkImage> *found = fSource();
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
  // Ticked until its picture has come -- or, where its source wakes those
  // waiting on it (waiters()), not ticked at all: woken when it comes. A
  // person without a picture was otherwise asked for it at every frame,
  // for as long as they were shown.
  [[nodiscard]] bool wantsTick() const { return !fHad && !fWaiting; }
  void update(double) {
    const bool has = this->image() != nullptr;
    if (has != fHad) {
      fHad = has;
      if (fBoxKept) {
        this->markDamaged();
      } else {
        this->invalidateLayout();
      }
    }
    fWaiting = !has && this->waitOn(fSource);
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    const skia::Sp<skia::SkImage> *found = this->image();
    if (!found) {
      // Let go by its cache: waited for again, till it is back.
      if (fHad) {
        fHad = false;
        fWaiting = this->waitOn(fSource);
        if (!fWaiting) {
          skiff::scene::work::mark(fState.fId);
        }
      }
      return;
    }
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
    // Drawn smaller than it is: scaled once to the pixels it covers, and
    // that copy put down as it is at every draw -- not filtered down again
    // each time it is repainted, which was a scroll's frame's most costly
    // part where avatars went by.
    if (const skia::Sp<skia::SkImage> *scaled = this->scaledFor(canvas, *found, box, iw, ih)) {
      canvas->drawImageRect(*scaled, box, skia::SkSamplingOptions(skia::SkFilterMode::kNearest), &paint);
      canvas->restoreToCount(saved);
      return;
    }
    const skia::SkSamplingOptions sampling(skia::SkFilterMode::kLinear);
    splice::visit(
        [&](auto how) { drawFitted(canvas, *found, box, iw, ih, how, sampling, paint); },
        fFit);
    canvas->restoreToCount(saved);
  }

private:
  // The picture as the box shows it, at the device's pixels, where that is
  // smaller than the picture: made again for another picture or size.
  const skia::Sp<skia::SkImage> *scaledFor(skia::SkCanvas *canvas, const skia::Sp<skia::SkImage> &image,
                                           const skia::SkRect &box, float iw, float ih) {
    const skia::SkMatrix matrix = canvas->getTotalMatrix();
    if (matrix.getSkewX() != 0.0f || matrix.getSkewY() != 0.0f) {
      return nullptr;
    }
    const float sx = matrix.getScaleX(), sy = matrix.getScaleY();
    const int width = static_cast<int>(std::lround(box.width() * sx));
    const int height = static_cast<int>(std::lround(box.height() * sy));
    // Only smaller, and not huge: a picture shown at its size or larger is
    // drawn as it is.
    if (width <= 0 || height <= 0 || (static_cast<float>(width) >= iw && static_cast<float>(height) >= ih) ||
        static_cast<std::int64_t>(width) * height > (1 << 21)) {
      return nullptr;
    }
    if (!fScaled || fScaledOf != image->uniqueID() || fScaledWidth != width || fScaledHeight != height) {
      // Scaled already for another node -- the same avatar in another row,
      // a row made again: that copy, not another made.
      const ScaledKey key{image->uniqueID(), width, height, fFit.index()};
      if (const auto known = scaledCache().find(key); known != scaledCache().end()) {
        fScaled = known->second;
        fScaledOf = image->uniqueID();
        fScaledWidth = width;
        fScaledHeight = height;
        return &fScaled;
      }
      fScaled = nullptr;
      skia::Sp<skia::SkSurface> surface = skia::Raster(skia::SkImageInfo::MakeN32Premul(width, height));
      if (!surface) {
        return nullptr;
      }
      skia::SkCanvas *into = surface->getCanvas();
      into->scale(static_cast<float>(width) / box.width(), static_cast<float>(height) / box.height());
      into->translate(-box.fLeft, -box.fTop);
      skia::SkPaint plain;
      const skia::SkSamplingOptions smooth(skia::SkFilterMode::kLinear, skia::SkMipmapMode::kLinear);
      splice::visit([&](auto how) { drawFitted(into, image, box, iw, ih, how, smooth, plain); }, fFit);
      fScaled = surface->makeImageSnapshot();
      // Kept for the others, a few hundred at most: the oldest half let go.
      if (scaledCache().size() >= kScaledKept) {
        auto at = scaledCache().begin();
        for (std::size_t i = 0; i < kScaledKept / 2 && at != scaledCache().end(); ++i)
          at = scaledCache().erase(at);
      }
      scaledCache().insert_or_assign(key, fScaled);
      fScaledOf = image->uniqueID();
      fScaledWidth = width;
      fScaledHeight = height;
    }
    return fScaled ? &fScaled : nullptr;
  }

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

  // The scaled copies, by picture, size and fit: shared by every Image.
  struct ScaledKey {
    std::uint32_t image = 0;
    int width = 0, height = 0;
    std::size_t fit = 0;
    friend auto operator<=>(const ScaledKey &, const ScaledKey &) = default;
  };
  static constexpr std::size_t kScaledKept = 512;
  static std::map<ScaledKey, skia::Sp<skia::SkImage>> &scaledCache() {
    static std::map<ScaledKey, skia::Sp<skia::SkImage>> kept;
    return kept;
  }
  // Waiting on the source, where it says who waits: whether it does.
  template <class S>
    requires requires(const S &source) {
      { source.waiters() } -> std::same_as<skiff::scene::Waiters &>;
    }
  bool waitOn(const S &source) {
    source.waiters().wait(fState.fId);
    return true;
  }
  bool waitOn(const auto &) { return false; }  // asked at every frame instead

  Source fSource;
  Fit fFit;
  bool fHad = false;
  bool fBoxKept = false;
  bool fWaiting = false;
  // The picture scaled to what it covers, and what it was made for.
  skia::Sp<skia::SkImage> fScaled;
  std::uint32_t fScaledOf = 0;
  int fScaledWidth = 0, fScaledHeight = 0;
};

} // namespace skiff::nodes
