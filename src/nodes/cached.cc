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
  // Where a cache surface comes from, and what to do with it once it has
  // been drawn into.
  //
  // Not a context: which kind of context this program has -- a Ganesh one,
  // or a Graphite recorder -- is the app's business, and a node that names
  // one of them is a node that only exists in a build with that backend.
  // What this needs is a surface of a size, and a way to say that it is
  // finished with, which Ganesh answers by submitting and Graphite answers
  // by doing nothing: a recorder has already taken the calls.
  struct Surfaces {
    std::function<skia::Sp<skia::SkSurface>(int width, int height)> fMake;
    std::function<void(skia::SkSurface *)> fDone;
  };

  static void setSurfaces(Surfaces surfaces) {
    surfacesSlot() = std::move(surfaces);
  }

  // Every cache, dropped.
  //
  // A cache surface belongs to whatever made it, and what made it is a
  // context that a program tears down before it exits. Holding one past that
  // is a surface referring to a device that is gone, which is not an error
  // anybody sees until the node is destroyed and something unrefs it. So the
  // program says when the surfaces stop being valid, and this is where every
  // node hears it.
  static void dropSurfaces() {
    surfacesSlot() = {};
    for (CachedContainer *node : live()) {
      node->fCache.reset();
      node->fCached.reset();
      node->fCacheValid = false;
    }
  }

  CachedContainer() { live().insert(this); }
  ~CachedContainer() override { live().erase(this); }

  void invalidateCache() {
    fCacheValid = false;
    fCached.reset();
    this->markDamaged();
  }

  void draw(skia::SkCanvas *canvas, float inheritedAlpha = 1.0f) override {
    if (!fVisible || fAlpha <= 0.001f) {
      return;
    }
    const Surfaces &surfaces = surfacesSlot();
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
    if (!surfaces.fMake || width <= 0 || height <= 0 ||
        this->animatingTree()) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCache || fCacheWidth != width || fCacheHeight != height) {
      fCache = surfaces.fMake(width, height);
      fCached.reset();
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
      if (surfaces.fDone) {
        surfaces.fDone(fCache.get());
      }
      fCacheValid = true;
      fCachedOrigin = fBounds.fLeft + fBounds.fTop;
      // Taken once per repaint rather than once per frame.
      //
      // What a snapshot costs depends on the backend, and on one of them it
      // is a copy of the whole texture: Graphite hands back an image of what
      // the surface holds now, so asking every frame is copying every cached
      // panel every frame -- which is a menu at seventeen frames a second
      // where the other backend does a hundred. The picture only changes
      // where this function has just redrawn it, so this is where it is
      // taken.
      fCached = fCache->makeImageSnapshot();
    }

    skia::SkPaint paint;
    paint.setAlphaf(inheritedAlpha * fAlpha);
    const skia::Sp<skia::SkImage> &image = fCached;
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
  static Surfaces &surfacesSlot() {
    static Surfaces surfaces;
    return surfaces;
  }

  // Which of these exist, so that the program can say when what they hold
  // stops being valid. A set of raw pointers, kept by the constructor and
  // the destructor of the only thing that puts itself in it.
  static std::set<CachedContainer *> &live() {
    static std::set<CachedContainer *> nodes;
    return nodes;
  }

  skia::Sp<skia::SkSurface> fCache;
  // What was in the cache when it was last drawn into, which is what every
  // frame after that draws.
  skia::Sp<skia::SkImage> fCached;
  int fCacheWidth = 0;
  int fCacheHeight = 0;
  bool fCacheValid = false;
  float fCachedOrigin = 0.0f;
};

} // namespace skiff::nodes
