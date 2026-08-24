export module skiff.nodes:cached;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::nodes

export namespace skiff::nodes {

// A subtree rendered once into a texture and shown from it afterwards.
//
// Lists of cards, panels of settings and pages of metadata are expensive to
// draw and change rarely: hundreds of glyphs, rounded rectangles and images
// per frame that produce the same pixels every time. This draws them once
// into an offscreen surface and blits that until something inside changes.
// While the subtree is animating, caching would cost more than it saves, so
// it draws straight through.
class CachedContainer : public skiff::scene::TypedDrawable<CachedContainer> {
public:
  // The GPU context the cache surfaces are created on, handed over by the app
  // once the renderer exists.
  static void setContext(skia::GrDirectContext *context) {
    contextSlot() = context;
  }

  void invalidateCache() {
    fCacheValid = false;
    this->markDamaged();
  }

  void draw(skia::SkCanvas *canvas, float inheritedAlpha = 1.0f) override {
    if (!fVisible || fAlpha <= 0.001f) {
      return;
    }
    skia::GrDirectContext *context = contextSlot();
    // The cache has to hold device pixels, not units. Drawn at unit size into
    // a canvas that carries an interface scale, the texture is resampled on
    // every frame -- blurred, and off the fast blit path that is the entire
    // point of caching. The scale is read from the canvas rather than passed
    // in, so nothing above has to know this node exists.
    const skia::SkMatrix matrix = canvas->getTotalMatrix();
    const float scaleX = std::abs(matrix.getScaleX());
    const float scaleY = std::abs(matrix.getScaleY());
    const bool plainScale = matrix.getSkewX() == 0.0f &&
                            matrix.getSkewY() == 0.0f && scaleX > 0.0f &&
                            scaleY > 0.0f;
    const float sx = plainScale ? scaleX : 1.0f;
    const float sy = plainScale ? scaleY : 1.0f;
    const int width = static_cast<int>(std::ceil(fBounds.width() * sx));
    const int height = static_cast<int>(std::ceil(fBounds.height() * sy));
    if (context == nullptr || width <= 0 || height <= 0 ||
        this->animatingTree()) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCache || fCacheWidth != width || fCacheHeight != height) {
      fCache = skia::RenderTarget(
          context, skia::kNo,
          skia::SkImageInfo::Make(width, height, skia::kRGBA_8888_SkColorType,
                                  skia::kPremul_SkAlphaType));
      fCacheWidth = width;
      fCacheHeight = height;
      fCacheValid = false;
    }
    if (!fCache) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCacheValid || fCachedOrigin != fBounds.fLeft + fBounds.fTop) {
      auto *cacheCanvas = fCache->getCanvas();
      cacheCanvas->clear(skia::colorSetARGB(0, 0, 0, 0));
      const int saved = cacheCanvas->save();
      // The subtree is laid out in screen space; shift it into the cache and
      // draw it at the scale the cache is held at.
      cacheCanvas->scale(sx, sy);
      cacheCanvas->translate(-fBounds.fLeft, -fBounds.fTop);
      Drawable::draw(cacheCanvas, 1.0f);
      cacheCanvas->restoreToCount(saved);
      context->flushAndSubmit(fCache.get());
      fCacheValid = true;
      fCachedOrigin = fBounds.fLeft + fBounds.fTop;
    }

    skia::SkPaint paint;
    paint.setAlphaf(inheritedAlpha * fAlpha);
    auto image = fCache->makeImageSnapshot();
    if (image) {
      if (sx == 1.0f && sy == 1.0f) {
        // Nothing is scaled: the plain blit, as before, which is the cheapest
        // path Skia has.
        canvas->drawImage(image.get(), fBounds.fLeft, fBounds.fTop,
                          skia::SkSamplingOptions(), &paint);
      } else {
        // Back out at unit size: with the canvas scale applied that lands one
        // device pixel per cached pixel, which is the fast path again.
        canvas->drawImageRect(
            image.get(),
            skia::SkRect::MakeXYWH(fBounds.fLeft, fBounds.fTop,
                                   fBounds.width(), fBounds.height()),
            skia::SkSamplingOptions(), &paint);
      }
    }
    this->noteDrawn();
  }

protected:
  // Anything that moves inside invalidates what was captured.
  void layoutChildren() override {
    Drawable::layoutChildren();
    fCacheValid = false;
  }

private:
  static skia::GrDirectContext *&contextSlot() {
    static skia::GrDirectContext *context = nullptr;
    return context;
  }

  skia::Sp<skia::SkSurface> fCache;
  int fCacheWidth = 0;
  int fCacheHeight = 0;
  bool fCacheValid = false;
  float fCachedOrigin = 0.0f;
};

} // namespace skiff::nodes
