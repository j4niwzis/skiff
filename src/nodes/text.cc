export module skiff.nodes:text;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// A line of text, or a paragraph when wrapped. Sizes itself to what it
// draws, so a flow can lay it out without anyone measuring by hand.
class Text : public skiff::scene::Node {
public:
  Text(std::string text, float size, skia::SkColor colour, bool bold = false)
      : fText(std::move(text)), fSize(size), fColour(colour), fBold(bold) {}

  void setText(std::string text) {
    if (text == fText) {
      return;
    }
    fText = std::move(text);
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  void setColour(skia::SkColor colour) {
    if (colour == fColour) {
      return;
    }
    fColour = colour;
    this->markDamaged();
  }
  void setFontSize(float size) {
    if (size == fSize) {
      return;
    }
    fSize = size;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  void setBold(bool bold) {
    if (bold == fBold) {
      return;
    }
    fBold = bold;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }
  [[nodiscard]] float fontSize() const noexcept { return fSize; }
  [[nodiscard]] skia::SkColor colour() const noexcept { return fColour; }
  [[nodiscard]] bool bold() const noexcept { return fBold; }

  // Cut to this width instead of sizing to the text.
  void setMaxWidth(float width) {
    if (width == fState.fMaxWidth) {
      return;
    }
    fState.fMaxWidth = width;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  // Broken across lines at spaces instead of running past the width.
  void setWrapped(bool wrapped) {
    if (wrapped == fWrapped) {
      return;
    }
    fWrapped = wrapped;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  // Ends in an ellipsis when it does not fit, rather than stopping
  // mid-glyph. Ignored when wrapped.
  void setElided(bool elided) {
    if (elided == fElided) {
      return;
    }
    fElided = elided;
    this->markDamaged();
  }

  static void setFont(skia::SkFont *font) {
    skiff::paint::defaultFont() = font;
  }

  void applyNodeStyle(const skiff::scene::Style &style, bool active) {
    if (!active && !fNodeStyleActive) {
      return;
    }
    if (active && !fNodeStyleActive) {
      fBaseSize = fSize;
      fBaseColour = fColour;
      fBaseBold = fBold;
    }
    const float size = active ? style.fontSize.value_or(fBaseSize) : fBaseSize;
    const skia::SkColor colour =
        active ? style.colour.value_or(fBaseColour) : fBaseColour;
    const bool bold = active ? style.fontBold.value_or(fBaseBold) : fBaseBold;
    if (size != fSize || bold != fBold) {
      fSize = size;
      fBold = bold;
      fMeasuredSize = -1.0f;
      this->invalidateLayout();
    }
    if (colour != fColour) {
      fColour = colour;
      this->markDamaged();
    }
    fNodeStyleActive = active;
  }

  void measure(const skia::SkRect &parent) {
    skiff::scene::State &state = fState;
    if (fMeasuredSize == fSize && !fWrapped) {
      return; // measured at this size, and the text has not changed
    }
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(nullptr, *font);
    if (fWrapped) {
      const float room = this->roomFor(parent);
      fLines = p.wrap(fText, room, fSize, fBold);
      state.fHeight =
          static_cast<float>(std::max<std::size_t>(1, fLines.size())) *
          fSize * 1.25f;
      // Its width is the room it wraps in -- unless that width is its
      // parent's to give, as a share of the parent's width, or its glyphs'.
      // One given no width at all keeps taking the parent's: the width
      // written here is not one it was given.
      if (!state.fGrowAxes.has<skiff::scene::axis::x>() &&
          !state.fRelativeSizeAxes.has<skiff::scene::axis::x>() &&
          (fWrapsToParent || state.fWidth <= 0.0f)) {
        fWrapsToParent = true;
        state.fWidth = room;
      }
      fMeasuredSize = fSize;
      return;
    }
    const float measured = p.measure(fText, fSize, fBold);
    // Sized by its flow or parent, it clips to the width it was given rather
    // than replacing that width with the glyphs'.
    if (!state.fGrowAxes.has<skiff::scene::axis::x>() && !state.fRelativeSizeAxes.has<skiff::scene::axis::x>()) {
      state.fWidth = state.fMaxWidth > 0.0f
                         ? std::min(state.fMaxWidth, measured)
                         : measured;
    }
    state.fHeight = fSize * 1.25f;
    fMeasuredSize = fSize;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    const skiff::scene::State &state = fState;
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fText.empty()) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    const int saved = canvas->save();
    const skia::SkRect &bounds = state.fBounds;
    if (fWrapped) {
      float y = bounds.fTop + fSize;
      for (const std::string &line : fLines) {
        p.text(line, bounds.fLeft, y, fSize, fColour, alpha, fBold);
        y += fSize * 1.25f;
      }
      canvas->restoreToCount(saved);
      return;
    }
    if (state.fMaxWidth > 0.0f || state.fGrowAxes.has<skiff::scene::axis::x>() ||
        state.fRelativeSizeAxes.has<skiff::scene::axis::x>()) {
      canvas->clipRect(bounds, true);
    }
    // The baseline sits at the top plus the ascent share of the line box.
    if (fElided) {
      p.textElided(fText, bounds.fLeft, bounds.fTop + fSize, bounds.width(),
                   fSize, fColour, alpha, fBold);
    } else {
      p.text(fText, bounds.fLeft, bounds.fTop + fSize, fSize, fColour, alpha,
             fBold);
    }
    canvas->restoreToCount(saved);
  }

private:
  // The width a wrapped line has to fit into, resolved as layout would.
  [[nodiscard]] float roomFor(const skia::SkRect &parent) const {
    const skiff::scene::State &state = fState;
    if (state.fMaxWidth > 0.0f) {
      return state.fMaxWidth;
    }
    if (state.fRelativeSizeAxes.has<skiff::scene::axis::x>()) {
      return parent.width() * state.fWidth - state.fMargin.totalX();
    }
    return state.fWidth > 0.0f && !fWrapsToParent
               ? state.fWidth
               : parent.width() - state.fMargin.totalX();
  }

  std::string fText;
  float fSize;
  skia::SkColor fColour;
  bool fBold;
  bool fWrapped = false;
  // Wrapped with no width of its own: it wraps to its parent's, whatever
  // that is at the time.
  bool fWrapsToParent = false;
  bool fElided = false;
  std::vector<std::string> fLines;
  float fMeasuredSize = -1.0f;
  float fBaseSize = 0.0f;
  skia::SkColor fBaseColour = 0;
  bool fBaseBold = false;
  bool fNodeStyleActive = false;
};

} // namespace skiff::nodes
