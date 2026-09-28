export module skiff.nodes:sprite;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// An image, cropped to fill its box rather than squashed into it.
class Sprite : public skiff::scene::Node {
public:
  explicit Sprite(skia::Sp<skia::SkImage> image = {})
      : fImage(std::move(image)) {}

  void setImage(skia::Sp<skia::SkImage> image) {
    if (image.get() == fImage.get()) {
      return;
    }
    fImage = std::move(image);
    this->markDamaged();
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    skiff::paint::imageFilled(canvas, fImage.get(), fState.fBounds, alpha);
  }

private:
  skia::Sp<skia::SkImage> fImage;
};

} // namespace skiff::nodes
