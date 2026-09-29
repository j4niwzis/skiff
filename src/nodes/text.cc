export module skiff.nodes.text;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// Links in the text: a range of it, and where it goes. Drawn in their own
// colour, underlined, where they stand; a press on one that does not
// become a selection opens it through skiff::scene::openLink.
struct TextLink {
  std::size_t first = 0;
  std::size_t last = 0;
  std::string target;
  // A pill, as a mention is drawn: a rounded plate behind it and, at its
  // start, a picture the program paints (its text leaves room for it).
  bool pill = false;
  // A picture in the line, in place of its range: a custom emoji. The
  // range is a placeholder the program put in the text for the room it
  // takes (an em space); the picture is the program's, for the target,
  // as the text's Pictures say.
  bool picture = false;
};

// Stretches of the text drawn otherwise: strong, emphasised (slanted),
// struck through, as code (on a tinted plate), as a quote (in the quote's
// colour, a bar at its line's start) -- what a message's HTML says of it.
struct TextStyled {
  std::size_t first = 0;
  std::size_t last = 0;
  bool strong = false;
  bool emphasis = false;
  bool struck = false;
  bool code = false;
  bool quote = false;
  // Marked: a stretch pointed at -- the part of a message a reply quoted
  // -- on a plate of the quote's colour, apart from what is selected.
  bool marked = false;
};

// What a text draws of the program's, by what it stands for: a pill's
// picture, and a picture in the line (a custom emoji). A type with static
// members, given as the text's template parameter: nothing is set while
// the program runs. These draw none.
struct NoPictures {
  static std::optional<skiff::scene::PillPicture> pill(std::string_view) { return std::nullopt; }
  static const skia::Sp<skia::SkImage> *picture(std::string_view) { return nullptr; }
};

// Which text's selection is shown, of all of them: the last pressed.
inline std::uint64_t &textSelectionOwner() {
  static std::uint64_t owner = 0;
  return owner;
}

// A line of text, or a paragraph when wrapped. Sizes itself to what it
// draws, so a flow can lay it out without anyone measuring by hand.
template <class Pictures = NoPictures> class BasicText : public skiff::scene::Node {
public:
  using Link = TextLink;
  using Styled = TextStyled;

  BasicText(std::string text, float size, skia::SkColor colour, bool bold = false)
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

  // Selectable, as a message's text is: a drag across it selects, a double
  // press selects a word, Ctrl+A all of it, and Ctrl+C copies what is
  // selected through skiff::scene::clipboard(). The selection shows while
  // the text has the focus a press gives it.
  void setSelectable(bool selectable) {
    fSelectable = selectable;
    if (!selectable) {
      fAnchor = fCaret = 0;
    }
    this->markDamaged();
  }
  void setSelectionColour(skia::SkColor colour) {
    fSelectionColour = colour;
    this->markDamaged();
  }
  [[nodiscard]] bool selectable() const noexcept { return fSelectable; }

  void setStyles(std::vector<Styled> styles, skia::SkColor quote_colour) {
    fStyles = std::move(styles);
    fQuoteColour = quote_colour;
    fWrappedRoom = -1.0f;  // a quote's lines wrap narrower: wrapped again
    this->markDamaged();
  }
  [[nodiscard]] const std::vector<Styled> &styles() const noexcept { return fStyles; }
  // How far down from its top the line an offset is on starts, as drawn.
  [[nodiscard]] float lineTopOf(std::size_t offset) const {
    float y = 0.0f;
    for (const auto &[start, line] : this->shownLines()) {
      if (offset < start + line.size() + 1) {
        return y;
      }
      y += fSize * 1.25f;
    }
    return y;
  }
  [[nodiscard]] Styled styleAt(std::size_t offset) const {
    Styled out;
    for (const Styled &one : fStyles) {
      if (offset >= one.first && offset < one.last) {
        out.strong = out.strong || one.strong;
        out.emphasis = out.emphasis || one.emphasis;
        out.struck = out.struck || one.struck;
        out.code = out.code || one.code;
        out.quote = out.quote || one.quote;
        out.marked = out.marked || one.marked;
      }
    }
    return out;
  }
  void setLinks(std::vector<Link> links, skia::SkColor colour) {
    fLinks = std::move(links);
    fLinkColour = colour;
    this->markDamaged();
  }
  [[nodiscard]] const std::vector<Link> &links() const noexcept { return fLinks; }
  // The link at an offset, if one is there.
  [[nodiscard]] const Link *linkAt(std::size_t offset) const {
    for (const Link &one : fLinks) {
      if (offset >= one.first && offset < one.last) {
        return &one;
      }
    }
    return nullptr;
  }
  // How wide its last line is, as drawn: what room it leaves at its end --
  // a message's time sits there when it fits.
  [[nodiscard]] float lastLineWidth() const {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return 0.0f;
    }
    const skiff::paint::Painter p(nullptr, *font);
    if (!fWrapped || fLines.empty()) {
      return p.measure(fText, fSize, fBold);
    }
    return p.measure(fLines.back(), fSize, fBold);
  }
  [[nodiscard]] bool hasSelection() const noexcept { return fAnchor != fCaret; }
  [[nodiscard]] std::string selected() const {
    return fText.substr(std::min(fAnchor, fCaret), std::max(fAnchor, fCaret) - std::min(fAnchor, fCaret));
  }
  [[nodiscard]] bool acceptsInput() const { return fSelectable; }
  [[nodiscard]] bool showsFocus() const { return false; }

  using Node::onPointer;
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::down &at,
                 skiff::scene::PointerReply &reply) {
    if (!fSelectable || at.button != 1) {
      return;
    }
    const std::size_t at_offset = this->offsetAt(at.x, at.y);
    // A second press where the first was, soon: the word there.
    const auto now = std::chrono::steady_clock::now();
    if (now - fLastPress < std::chrono::milliseconds(400) && at_offset == fLastOffset) {
      this->selectWordAt(at_offset);
      fLastPress = {};
      this->markDamaged();
      return;
    }
    fLastPress = now;
    fLastOffset = at_offset;
    fAnchor = fCaret = at_offset;
    fPressX = at.x;
    fPressY = at.y;
    // The selection shown is this one's from now: one text's at a time.
    textSelectionOwner() = fState.fId;
    // Not taken yet: a scrolled list around this may take a press that
    // moves at once as a scroll. A move that reaches this selects, and the
    // pointer is taken then.
    fPressed = true;
    this->markDamaged();
  }
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::move &at,
                 skiff::scene::PointerReply &reply) {
    // A selection is begun by a press held still a moment first, where the
    // pointer moves up or down at once -- that is a scroll, which what holds
    // this takes. Across, it is a selection at once, as a mouse drags over
    // words: made to wait, a drag over a message's text selected nothing.
    if (fPressed && !fDragging) {
      const bool across = std::abs(at.x - fPressX) >= std::abs(at.y - fPressY);
      if (!across && std::chrono::steady_clock::now() - fLastPress < std::chrono::milliseconds(250)) {
        return;
      }
      fDragging = true;
      reply.capturePointer();
    }
    if (!fDragging) {
      return;
    }
    fCaret = this->offsetAt(at.x, at.y);
    this->takePillsWhole();
    this->markDamaged();
    reply.handle();
  }
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::up &,
                 skiff::scene::PointerReply &reply) {
    // Pressed and let go without selecting: a link there is opened.
    if (fPressed && !fDragging) {
      if (const Link *link = this->linkAt(fLastOffset)) {
        fPressed = false;
        skiff::scene::openLink(link->target);
        reply.handle();
        return;
      }
    }
    fPressed = false;
    if (fDragging) {
      fDragging = false;
      reply.releasePointer();
    }
  }
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::cancel &,
                 skiff::scene::PointerReply &reply) {
    fPressed = false;
    if (fDragging) {
      fDragging = false;
      reply.releasePointer();
    }
  }

  using Node::onKey;
  void onKey(skiff::scene::phase::target, const skiff::scene::key::down &press, skiff::scene::Reply &reply) {
    namespace keys = skiff::scene::keys;
    if (!fSelectable || !press.modifiers.has<skiff::scene::modifier::control>()) {
      return;
    }
    if (press.key == keys::kC && this->hasSelection()) {
      skiff::scene::setClipboardText(this->selected());
      reply.handle();
    } else if (press.key == keys::kA) {
      fAnchor = 0;
      fCaret = fText.size();
      this->markDamaged();
      reply.handle();
    }
  }
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
  // Wrapped, as wide as its widest line rather than all the room it wraps
  // in: a message's bubble is as wide as what it says.
  void setShrinksToLines(bool shrinks) {
    if (shrinks == fShrinks) {
      return;
    }
    fShrinks = shrinks;
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
    // Cut where it runs out of room, it gives way in a row that has too
    // little: it is shown with its ellipsis rather than past the row's end.
    if (elided) {
      fState.fShrinkAxes = skiff::scene::axes::kX;
    } else {
      fState.fShrinkAxes = skiff::scene::axes::kNone;
    }
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
      // Wrapped at this width already, the text and size as they were: its
      // lines stand. Measuring them again is what a long list cannot pay
      // for at every frame.
      if (fMeasuredSize == fSize && room == fWrappedRoom) {
        return;
      }
      fWrappedRoom = room;
      // Where some of it is quoted, every line wraps as narrow as a quoted
      // one, which stands in by kQuoteIndent past its bar.
      const float indent = std::ranges::any_of(fStyles, &Styled::quote) ? kQuoteIndent : 0.0f;
      fLines = p.wrap(fText, room - indent, fSize, fBold);
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
        if (fShrinks) {
          float widest = 0.0f;
          for (const std::string &line : fLines) {
            widest = std::max(widest, p.measure(line, fSize, fBold));
          }
          state.fWidth = std::min(room, std::ceil(widest + indent) + 1.0f);
        }
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
    this->drawSelection(canvas, p, alpha);
    if (fWrapped) {
      float y = bounds.fTop + fSize;
      if (fLinks.empty() && fStyles.empty()) {
        for (const std::string &line : fLines) {
          p.text(line, bounds.fLeft, y, fSize, fColour, alpha, fBold);
          y += fSize * 1.25f;
        }
      } else {
        for (const auto &[start, line] : this->shownLines()) {
          this->drawWithLinks(canvas, p, start, line, bounds.fLeft, y, alpha);
          y += fSize * 1.25f;
        }
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
  // Where each shown line starts in the text: the wrapped lines are its
  // pieces, in order.
  [[nodiscard]] std::vector<std::pair<std::size_t, std::string_view>> shownLines() const {
    std::vector<std::pair<std::size_t, std::string_view>> out;
    if (!fWrapped) {
      out.emplace_back(0, fText);
      return out;
    }
    std::size_t from = 0;
    for (const std::string &line : fLines) {
      const std::size_t at = fText.find(line, from);
      const std::size_t start = at == std::string::npos ? from : at;
      out.emplace_back(start, std::string_view(fText).substr(start, line.size()));
      from = start + line.size();
    }
    return out;
  }
  // The offset in the text nearest a point: its line, then the character
  // boundary nearest across it.
  [[nodiscard]] std::size_t offsetAt(float x, float y) const {
    skia::SkFont *font = skiff::paint::defaultFont();
    const auto lines = this->shownLines();
    if (font == nullptr || lines.empty()) {
      return 0;
    }
    const skiff::paint::Painter p(nullptr, *font);
    const skia::SkRect &bounds = fState.fBounds;
    const float lineHeight = fSize * 1.25f;
    const auto index = static_cast<std::size_t>(
        std::clamp((y - bounds.fTop) / lineHeight, 0.0f, static_cast<float>(lines.size() - 1)));
    const auto [start, line] = lines[index];
    const float into = x - bounds.fLeft - this->indentOf(start);
    std::size_t best = 0;
    float bestDistance = std::abs(into);
    for (std::size_t i = 1; i <= line.size(); ++i) {
      if (i < line.size() && (static_cast<unsigned char>(line[i]) & 0xC0) == 0x80) {
        continue;  // inside a character
      }
      const float distance = std::abs(p.measure(std::string(line.substr(0, i)), fSize, fBold) - into);
      if (distance < bestDistance) {
        bestDistance = distance;
        best = i;
      }
    }
    return start + best;
  }
  void selectWordAt(std::size_t at) {
    const auto wordy = [](unsigned char c) { return c >= 0x80 || std::isalnum(c) || c == '_'; };
    std::size_t from = std::min(at, fText.size());
    std::size_t to = from;
    while (from > 0 && wordy(static_cast<unsigned char>(fText[from - 1]))) {
      --from;
    }
    while (to < fText.size() && wordy(static_cast<unsigned char>(fText[to]))) {
      ++to;
    }
    fAnchor = from;
    fCaret = to;
    this->takePillsWhole();
  }
  // A pill -- a mention, a room -- is selected whole or not at all: an end
  // of the selection inside one goes to the pill's end that takes it in.
  void takePillsWhole() {
    const bool forward = fCaret >= fAnchor;
    for (const Link &one : fLinks) {
      if (!one.pill) {
        continue;
      }
      const auto inside = [&](std::size_t at) { return at > one.first && at < one.last; };
      if (inside(fAnchor)) {
        fAnchor = forward ? one.first : one.last;
      }
      if (inside(fCaret)) {
        fCaret = forward ? one.last : one.first;
      }
    }
  }
  // Behind the selected part of each line, a plate in the selection's colour.
  void drawSelection(skia::SkCanvas *canvas, const skiff::paint::Painter &p, float alpha) const {
    // The last text pressed shows its selection -- whether or not it has the
    // keyboard's focus: a selectable text does not take it on a press, and
    // required, no selection was ever drawn.
    if (!fSelectable || fAnchor == fCaret || textSelectionOwner() != fState.fId) {
      return;
    }
    const std::size_t low = std::min(fAnchor, fCaret), high = std::max(fAnchor, fCaret);
    const skia::SkRect &bounds = fState.fBounds;
    const float lineHeight = fSize * 1.25f;
    float top = bounds.fTop;
    skia::SkPaint plate;
    plate.setColor(fSelectionColour);
    plate.setAlphaf(plate.getAlphaf() * alpha);
    for (const auto &[start, line] : this->shownLines()) {
      const std::size_t end = start + line.size();
      if (high > start && low < end) {
        const std::size_t a = std::max(low, start) - start, b = std::min(high, end) - start;
        const float left = p.measure(std::string(line.substr(0, a)), fSize, fBold);
        const float right = p.measure(std::string(line.substr(0, b)), fSize, fBold);
        const float from = bounds.fLeft + this->indentOf(start);
        canvas->drawRect(skia::SkRect::MakeLTRB(from + left, top, from + right, top + lineHeight), plate);
      }
      top += lineHeight;
    }
  }

  // A line in pieces: plain in the text's colour, links in theirs and
  // underlined.
  // A pill's picture, as an avatar: the picture where there is one, over
  // the gradient with the initials in white where there is not.
  static void drawPillPicture(skia::SkCanvas *canvas, const skiff::paint::Painter &p, const skia::SkRect &disc,
                              const skiff::scene::PillPicture &look, float alpha) {
    const int saved = canvas->save();
    canvas->clipRRect(skia::SkRRect::MakeOval(disc), true);
    if (look.picture && *look.picture) {
      skia::SkPaint paint;
      paint.setAlphaf(alpha);
      canvas->drawImageRect(*look.picture, disc, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    } else {
      skiff::paint::verticalGradient(canvas, disc, look.top, look.bottom, alpha);
      const float size = disc.width() * 0.4f;
      const float width = p.measure(look.initials, size, true);
      p.textIn(disc, look.initials, size, skia::colorSetARGB(255, 255, 255, 255), alpha, true,
               (disc.width() - width) * 0.5f);
    }
    canvas->restoreToCount(saved);
  }
  void drawWithLinks(skia::SkCanvas *canvas, const skiff::paint::Painter &p, std::size_t start,
                     std::string_view line, float x, float y, float alpha) const {
    const std::size_t end = start + line.size();
    std::vector<std::size_t> cuts{start, end};
    for (const Link &one : fLinks) {
      if (one.last > start && one.first < end) {
        cuts.push_back(std::max(one.first, start));
        cuts.push_back(std::min(one.last, end));
      }
    }
    for (const Styled &one : fStyles) {
      if (one.last > start && one.first < end) {
        cuts.push_back(std::max(one.first, start));
        cuts.push_back(std::min(one.last, end));
      }
    }
    // A quoted line: a bar in the quote's colour at its start.
    if (this->styleAt(start).quote) {
      skia::SkPaint bar;
      bar.setAntiAlias(true);
      bar.setColor(fQuoteColour);
      bar.setAlphaf(bar.getAlphaf() * alpha);
      canvas->drawRoundRect(skia::SkRect::MakeXYWH(x, y - fSize, 2.5f, fSize * 1.25f), 1.25f, 1.25f, bar);
    }
    std::ranges::sort(cuts);
    cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());
    float at = x + this->indentOf(start);
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
      const std::string piece(line.substr(cuts[i] - start, cuts[i + 1] - cuts[i]));
      const Link *link = this->linkAt(cuts[i]);
      const bool linked = link != nullptr;
      const Styled style = this->styleAt(cuts[i]);
      const bool bold = fBold || style.strong;
      const skia::SkColor colour = linked ? fLinkColour : style.quote ? fQuoteColour : fColour;
      const float width = p.measure(piece, fSize, bold);
      // Marked: on a plate of the quote's colour.
      if (style.marked) {
        skia::SkPaint plate;
        plate.setAntiAlias(true);
        plate.setColor(fQuoteColour);
        plate.setAlphaf(0.28f * alpha);
        canvas->drawRoundRect(skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, fSize * 1.25f), 3.0f, 3.0f,
                              plate);
      }
      // Code: on a plate of the text's colour, faint.
      if (style.code) {
        skia::SkPaint plate;
        plate.setAntiAlias(true);
        plate.setColor(fColour);
        plate.setAlphaf(0.10f * alpha);
        canvas->drawRoundRect(skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, fSize * 1.25f), 3.0f, 3.0f,
                              plate);
      }
      // Struck: a line through its middle.
      if (style.struck) {
        skia::SkPaint line;
        line.setColor(colour);
        line.setAlphaf(line.getAlphaf() * alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(at, y - fSize * 0.32f, width, 1.0f), line);
      }
      if (link && link->picture) {
        // The picture, square, a little over the text's size, standing on
        // its baseline; nothing where the program has none (yet).
        if (cuts[i] == link->first)
          if (const skia::Sp<skia::SkImage> *picture = Pictures::picture(link->target);
              picture && *picture) {
            const float side = fSize * 1.2f;
            skia::SkPaint paint;
            paint.setAlphaf(alpha);
            canvas->drawImageRect(*picture,
                                  skia::SkRect::MakeXYWH(at + (width - side) * 0.5f, y - fSize * 0.98f, side, side),
                                  skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
          }
        at += width;
        continue;
      }
      if (link && link->pill) {
        // The plate, the picture at its start where the pill begins, the
        // text over it -- not underlined.
        const float height = fSize * 1.25f;
        const skia::SkRect plate = skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, height);
        skia::SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(colour);
        fill.setAlphaf(0.18f * alpha);
        canvas->drawRRect(skia::SkRRect::MakeRectXY(plate, height * 0.5f, height * 0.5f), fill);
        if (cuts[i] == link->first)
          if (const auto look = Pictures::pill(link->target)) {
            const float side = height - 4.0f;
            drawPillPicture(canvas, p, skia::SkRect::MakeXYWH(at + 1.0f, plate.fTop + 2.0f, side, side), *look, alpha);
          }
        p.text(piece, at, y, fSize, colour, alpha, fBold);
        at += width;
        continue;
      }
      if (style.emphasis && skiff::paint::defaultFont())
        skiff::paint::defaultFont()->setSkewX(-0.2f);
      p.text(piece, at, y, fSize, colour, alpha, bold);
      if (style.emphasis && skiff::paint::defaultFont())
        skiff::paint::defaultFont()->setSkewX(0.0f);
      if (linked) {
        skia::SkPaint under;
        under.setColor(colour);
        under.setAlphaf(under.getAlphaf() * alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(at, y + 2.0f, width, 1.0f), under);
      }
      at += width;
    }
  }

  // How far a line's text stands in: a quoted one's, past its bar.
  static constexpr float kQuoteIndent = 10.0f;
  [[nodiscard]] float indentOf(std::size_t start) const {
    return this->styleAt(start).quote ? kQuoteIndent : 0.0f;
  }
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
  float fWrappedRoom = -1.0f;
  // Wrapped with no width of its own: it wraps to its parent's, whatever
  // that is at the time.
  bool fWrapsToParent = false;
  bool fElided = false;
  std::vector<Styled> fStyles;
  skia::SkColor fQuoteColour = 0;
  std::vector<std::string> fLines;
  float fMeasuredSize = -1.0f;
  float fBaseSize = 0.0f;
  skia::SkColor fBaseColour = 0;
  bool fBaseBold = false;
  bool fNodeStyleActive = false;
  bool fSelectable = false;
  float fPressX = 0.0f, fPressY = 0.0f;
  bool fShrinks = false;
  bool fDragging = false;
  bool fPressed = false;
  std::vector<Link> fLinks;
  skia::SkColor fLinkColour = skia::colorSetARGB(255, 82, 160, 230);
  std::size_t fAnchor = 0;
  std::size_t fCaret = 0;
  std::chrono::steady_clock::time_point fLastPress{};
  std::size_t fLastOffset = 0;
  skia::SkColor fSelectionColour = skia::colorSetARGB(110, 64, 167, 227);
};

using Text = BasicText<>;

} // namespace skiff::nodes
