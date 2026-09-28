export module skiff.nodes:cached;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// Where cache surfaces come from, and what to do with one once drawn into:
// Ganesh submits, Graphite does nothing. Not a context -- which backend a
// program has is its business. The provider is anything with make(width,
// height) and done(surface); the program keeps it for as long as it is set
// and calls dropSurfaces() before it goes.
struct CacheSurfaces {
  skia::Sp<skia::SkSurface> (*fMake)(void *provider, int width,
                                     int height) = nullptr;
  void (*fDone)(void *provider, skia::SkSurface *surface) = nullptr;
  void *fProvider = nullptr;
};

// One cache: a surface, the picture taken of it, and whether it is current.
// Registered while it exists, so the program can drop every surface at once
// when their context goes away.
class CacheSlot {
public:
  CacheSlot() { live().insert(this); }
  CacheSlot(CacheSlot &&other) noexcept
      : fSurface(std::move(other.fSurface)), fImage(std::move(other.fImage)),
        fWidth(other.fWidth), fHeight(other.fHeight), fValid(other.fValid),
        fOrigin(other.fOrigin) {
    live().insert(this);
  }
  CacheSlot &operator=(CacheSlot &&other) noexcept {
    fSurface = std::move(other.fSurface);
    fImage = std::move(other.fImage);
    fWidth = other.fWidth;
    fHeight = other.fHeight;
    fValid = other.fValid;
    fOrigin = other.fOrigin;
    return *this;
  }
  CacheSlot(const CacheSlot &) = delete;
  CacheSlot &operator=(const CacheSlot &) = delete;
  ~CacheSlot() { live().erase(this); }

  void drop() {
    fSurface.reset();
    fImage.reset();
    fValid = false;
  }

  static CacheSurfaces &surfaces() {
    static CacheSurfaces kept;
    return kept;
  }
  static std::set<CacheSlot *> &live() {
    static std::set<CacheSlot *> slots;
    return slots;
  }

  skia::Sp<skia::SkSurface> fSurface;
  skia::Sp<skia::SkImage> fImage;
  int fWidth = 0;
  int fHeight = 0;
  bool fValid = false;
  float fOrigin = 0.0f;
};

template <class Provider>
  requires requires(Provider &provider, int size, skia::SkSurface *surface) {
    { provider.make(size, size) } -> std::convertible_to<skia::Sp<skia::SkSurface>>;
    provider.done(surface);
  }
void setCacheSurfaces(Provider &provider) {
  CacheSlot::surfaces() = {
      +[](void *kept, int width, int height) -> skia::Sp<skia::SkSurface> {
        return static_cast<Provider *>(kept)->make(width, height);
      },
      +[](void *kept, skia::SkSurface *surface) {
        static_cast<Provider *>(kept)->done(surface);
      },
      &provider};
}
// Every cache dropped: a surface belongs to the context that made it, and a
// program tears that down before it exits.
inline void dropCacheSurfaces() {
  CacheSlot::surfaces() = {};
  for (CacheSlot *slot : CacheSlot::live()) {
    slot->drop();
  }
}

// A subtree drawn once into a texture and shown from it until something
// inside changes. While the subtree is animating, caching would cost more
// than it saves, so it draws straight through.
template <class... Children> class CachedContainer : public skiff::scene::Node {
public:
  explicit CachedContainer(Children... children)
      : fChildren(std::move(children)...) {}

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }

  void invalidateCache() {
    fCache.fValid = false;
    fCache.fImage.reset();
    this->markDamaged();
  }

  // Anything that moves inside invalidates what was captured.
  void layoutChildren() {
    skiff::scene::layoutChildrenInContentBox(*this);
    fCache.fValid = false;
  }

  void draw(skia::SkCanvas *canvas, float inheritedAlpha) {
    namespace scene = skiff::scene;
    scene::State &state = fState;
    if (!state.fVisible || state.fAlpha <= 0.001f) {
      return;
    }
    const CacheSurfaces &surfaces = CacheSlot::surfaces();
    // Device pixels, not units: drawn at unit size into a scaled canvas, the
    // texture would be resampled every frame.
    const skia::SkMatrix matrix = canvas->getTotalMatrix();
    const float scaleX = std::abs(matrix.getScaleX());
    const float scaleY = std::abs(matrix.getScaleY());
    const bool plainScale = matrix.getSkewX() == 0.0f &&
                            matrix.getSkewY() == 0.0f && scaleX > 0.0f &&
                            scaleY > 0.0f;
    const float sx = plainScale ? scaleX : 1.0f;
    const float sy = plainScale ? scaleY : 1.0f;
    const skia::SkRect &bounds = state.fBounds;
    const int width = static_cast<int>(std::ceil(bounds.width() * sx));
    const int height = static_cast<int>(std::ceil(bounds.height() * sy));
    if (!surfaces.fMake || width <= 0 || height <= 0 ||
        scene::walk::animating(*this)) {
      scene::drawDefault(*this, canvas, inheritedAlpha);
      return;
    }
    if (!fCache.fSurface || fCache.fWidth != width ||
        fCache.fHeight != height) {
      fCache.fSurface = surfaces.fMake(surfaces.fProvider, width, height);
      fCache.fImage.reset();
      fCache.fWidth = width;
      fCache.fHeight = height;
      fCache.fValid = false;
    }
    if (!fCache.fSurface) {
      scene::drawDefault(*this, canvas, inheritedAlpha);
      return;
    }
    if (!fCache.fValid || fCache.fOrigin != bounds.fLeft + bounds.fTop) {
      auto *cacheCanvas = fCache.fSurface->getCanvas();
      cacheCanvas->clear(skia::colorSetARGB(0, 0, 0, 0));
      const int saved = cacheCanvas->save();
      cacheCanvas->scale(sx, sy);
      cacheCanvas->translate(-bounds.fLeft, -bounds.fTop);
      // Drawn at full alpha, without this node's own alpha: that is applied
      // when the picture is drawn.
      const float alpha = state.fAlpha;
      state.fAlpha = 1.0f;
      scene::drawDefault(*this, cacheCanvas, 1.0f);
      state.fAlpha = alpha;
      cacheCanvas->restoreToCount(saved);
      if (surfaces.fDone) {
        surfaces.fDone(surfaces.fProvider, fCache.fSurface.get());
      }
      fCache.fValid = true;
      fCache.fOrigin = bounds.fLeft + bounds.fTop;
      // Taken once per repaint: on Graphite a snapshot is a copy of the
      // whole texture.
      fCache.fImage = fCache.fSurface->makeImageSnapshot();
    }
    skia::SkPaint paint;
    paint.setAlphaf(inheritedAlpha * state.fAlpha);
    if (fCache.fImage) {
      if (sx == 1.0f && sy == 1.0f) {
        canvas->drawImage(fCache.fImage.get(), bounds.fLeft, bounds.fTop,
                          skia::SkSamplingOptions(), &paint);
      } else {
        canvas->drawImageRect(fCache.fImage.get(),
                              skia::SkRect::MakeXYWH(bounds.fLeft, bounds.fTop,
                                                     bounds.width(),
                                                     bounds.height()),
                              skia::SkSamplingOptions(), &paint);
      }
    }
    state.fDrawnBounds = bounds;
  }

  std::tuple<Children...> fChildren;

private:
  CacheSlot fCache;
};

} // namespace skiff::nodes
