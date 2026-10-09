export module skiff.nodes.text;

import std;
import skia;
import skiff.paint;
import skiff.scene;

export namespace skiff::nodes {

// Quotes inside quotes, coloured as Telegram colours its peers (tdesktop's
// historyPeerNNameFg, in the order of Telegram's colour indices: red,
// orange, violet, green, sea, blue, pink): the first level in the quote's
// own colour, each deeper one in the next of these -- one the same as the
// level's own passed over.
inline constexpr std::array<skia::SkColor, 7> kQuoteColours{
    0xFFC03D33u, 0xFFCE671Bu, 0xFF8544D6u, 0xFF4FAD2Du, 0xFF2996ADu, 0xFF168ACDu, 0xFFCD4073u,
};
[[nodiscard]] inline skia::SkColor quoteLevelColour(skia::SkColor own, int depth) {
  if (depth <= 1) {
    return own;
  }
  std::size_t index = 0;
  for (int level = 2;; ++index) {
    const skia::SkColor next = kQuoteColours[index % kQuoteColours.size()];
    if ((next & 0x00FFFFFFu) == (own & 0x00FFFFFFu)) {
      continue;
    }
    if (level == depth) {
      return next;
    }
    ++level;
  }
}

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
  std::string plain;  // custom emoji label, copied in place of its placeholder
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
  bool underline = false;
  // A spoiler: hidden under a plate of dots until the text is pressed,
  // which shows all of its spoilers.
  bool spoiler = false;
  // How deep in quotes, as styleAt() sums it: a quote inside a quote is 2.
  int depth = 0;
  // Marked: a stretch pointed at -- the part of a message a reply quoted
  // -- on a plate of the quote's colour, apart from what is selected.
  bool marked = false;
  // A block of code (HTML's <pre>), and the language it says it is in: what
  // holds the text may draw it as a block of its own.
  bool block = false;
  std::string language;
};

// What a text draws of the program's, by what it stands for: a pill's
// picture, and a picture in the line (a custom emoji). A type with static
// members, given as the text's template parameter: nothing is set while
// the program runs. These draw none.
struct NoPictures {
  static std::optional<skiff::scene::PillPicture> pill(std::string_view) { return std::nullopt; }
  static const skia::Sp<skia::SkImage> *picture(std::string_view) { return nullptr; }
};

// A pill's picture, as an avatar: the picture where there is one, over
// the gradient with the initials in white where there is not.
inline void drawPillPicture(skia::SkCanvas *canvas, const skiff::paint::Painter &p, const skia::SkRect &disc,
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
// A pill's plate, as a mention is drawn in a message and in a field: a
// rounded plate in the pill's colour behind `width` of text at `x`, on the
// baseline `y`, and its picture at its start where it has one. The text
// over it is the caller's.
inline void drawPill(skia::SkCanvas *canvas, const skiff::paint::Painter &p, float x, float y, float width,
                     float size, skia::SkColor colour, const std::optional<skiff::scene::PillPicture> &look,
                     float alpha) {
  const float height = size * 1.5f;
  const skia::SkRect plate = skia::SkRect::MakeXYWH(x - 1.0f, y - size, width + 2.0f, height);
  skia::SkPaint fill;
  fill.setAntiAlias(true);
  fill.setColor(colour);
  fill.setAlphaf(0.18f * alpha);
  canvas->drawRRect(skia::SkRRect::MakeRectXY(plate, height * 0.5f, height * 0.5f), fill);
  if (look) {
    const float side = height - 4.0f;
    drawPillPicture(canvas, p, skia::SkRect::MakeXYWH(x + 1.0f, plate.fTop + 2.0f, side, side), *look, alpha);
  }
}

// Which text's selection is shown, of all of them: the last pressed.
// A selectable text pressed with the right button asks a menu for itself:
// skiff::scene::textMenusAsked.
inline std::uint64_t &textSelectionOwner() {
  static std::uint64_t owner = 0;
  return owner;
}

// A line of text, or a paragraph when wrapped. Sizes itself to what it
// draws, so a flow can lay it out without anyone measuring by hand.
//
// As CSS's text, by default: one line where it fits its parent, wrapped at
// the parent's width -- as wide as its widest line -- where it does not;
// never run past the parent's edge. setWrapped(true) always wraps,
// setWrapped(false) never does, setElided(true) keeps one line cut with an
// ellipsis; a text sized by its row (grow, or a share of the width) keeps
// one line, clipped, as that width is its row's to give.
// A text's pictures, whatever they are: asked as they would be, through the
// two functions they have. Erasure, outside a release build only.
struct AnyPictures {
  std::optional<skiff::scene::PillPicture> (*fPill)(std::string_view) = &NoPictures::pill;
  const skia::Sp<skia::SkImage> *(*fPicture)(std::string_view) = &NoPictures::picture;
  bool (*fAnimated)(std::string_view) = nullptr;
  double (*fWakeAt)(std::string_view) = nullptr;
  template <class Pictures> [[nodiscard]] static AnyPictures of() {
    AnyPictures made{&Pictures::pill, &Pictures::picture};
    if constexpr (requires { Pictures::animated(std::string_view{}); })
      made.fAnimated = &Pictures::animated;
    if constexpr (requires { Pictures::wakeAt(std::string_view{}); })
      made.fWakeAt = &Pictures::wakeAt;
    return made;
  }
  [[nodiscard]] double wakeAt(std::string_view target) const {
    return fWakeAt ? fWakeAt(target) : std::numeric_limits<double>::infinity();
  }
  [[nodiscard]] bool animated(std::string_view target) const { return fAnimated && fAnimated(target); }
  [[nodiscard]] std::optional<skiff::scene::PillPicture> pill(std::string_view target) const { return fPill(target); }
  [[nodiscard]] const skia::Sp<skia::SkImage> *picture(std::string_view target) const { return fPicture(target); }
};

namespace internal {
template <class Pictures = NoPictures> class BasicText : public skiff::scene::Node {
public:
  using Link = TextLink;
  using Styled = TextStyled;

  BasicText(std::string text, float size, skia::SkColor colour, bool bold = false, bool selectable = false)
      : fText(std::move(text)), fSize(size), fColour(colour), fBold(bold), fSelectable(selectable) {}

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
  // Drawn in the monospace face, as code: each character as wide as the next.
  void setMonospace(bool monospace) {
    if (monospace == fMonospace) {
      return;
    }
    fMonospace = monospace;
    fMeasuredSize = -1.0f;
    fWrappedRoom = -1.0f;
    this->invalidateLayout();
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

  // Selectable, as a message's text is: a drag across it selects, a double
  // press selects a word, Ctrl+A all of it, and Ctrl+C copies what is
  // selected through skiff::scene::setClipboardText(). The selection shows while
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
    fSpoilersShown = false;
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
      y += this->lineHeight();
    }
    return y;
  }
  // Whether a point in it is on a quoted stretch: for the program to act on
  // a press there (a reply's quote, to what it quotes).
  [[nodiscard]] bool quotedAt(float x, float y) const { return this->styleAt(this->offsetAt(x, y)).quote; }
  // The quote a point is on -- the outermost around it, where quotes are
  // inside quotes -- as its bytes in the text: which of a text's quotes was
  // pressed, not only that one was.
  [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> quoteAt(float x, float y) const {
    const std::size_t offset = this->offsetAt(x, y);
    std::optional<std::pair<std::size_t, std::size_t>> out;
    for (const Styled &one : fStyles) {
      if (one.quote && offset >= one.first && offset < one.last &&
          (!out || one.last - one.first > out->second - out->first)) {
        out = std::pair{one.first, one.last};
      }
    }
    return out;
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
        out.underline = out.underline || one.underline;
        out.spoiler = out.spoiler || one.spoiler;
        out.depth += one.quote ? 1 : 0;  // quotes inside quotes overlap
        out.marked = out.marked || one.marked;
      }
    }
    return out;
  }
  void setLinks(std::vector<Link> links, skia::SkColor colour) {
    fLinks = std::move(links);
    fLinkColour = colour;
    fHoveredLink.reset();
    this->markDamaged();
  }
  // A link under the pointer lights up, as a browser's does: repainted as the
  // pointer comes onto the text and leaves it, where it has links.
  [[nodiscard]] bool hoverChangesAppearance() const { return !fLinks.empty(); }
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
    const skiff::paint::Painter p(nullptr, *font, fMonospace);
    if (!fWrapped || fLines.empty()) {
      return p.measure(fText, fSize, fBold);
    }
    return p.measure(fLines.back(), fSize, fBold);
  }
  [[nodiscard]] bool hasSelection() const noexcept { return fAnchor != fCaret; }
  [[nodiscard]] scene::ClipboardFragment copiedRange(std::size_t first, std::size_t last) const {
    std::vector<scene::ClipboardAtom> atoms;
    for (const auto& link : fLinks)
      if (link.picture && link.first >= first && link.last <= last)
        atoms.push_back({link.first - first, link.last - first, link.target,
                         link.plain.empty() ? std::string(":emoji:") : link.plain, true});
    return scene::clipboardFragment(fText.substr(first, last - first), std::move(atoms));
  }
  [[nodiscard]] std::string selected() const {
    return this->copiedRange(std::min(fAnchor, fCaret), std::max(fAnchor, fCaret)).text;
  }
  [[nodiscard]] bool acceptsInput() const { return fSelectable; }
  [[nodiscard]] bool showsFocus() const { return false; }

  using Node::onPointer;
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::down &at,
                 skiff::scene::PointerReply &reply) {
    if (!fSelectable) {
      return;
    }
    if (at.button == 3) {
      const std::size_t on = this->offsetAt(at.x, at.y);
      const Link *pressed_link = this->linkAt(on);
      std::optional<std::string> link;
      if (pressed_link && !pressed_link->target.empty()) {
        link = pressed_link->target;
      }
      const std::size_t low = std::min(fAnchor, fCaret), high = std::max(fAnchor, fCaret);
      const bool in_selection = textSelectionOwner() == fState.fId && low != high && on >= low && on <= high;
      // What its menu copies: the selection pressed in, else all of it --
      // all of it not selected for that, lit up blue at every right-click.
      // Not taken: what holds the text may have a menu of its own -- a
      // message's -- which the program puts first.
      auto copied = in_selection ? this->copiedRange(low, high) : this->copiedRange(0, fText.size());
      scene::clipboardCandidate() = copied;
      scene::textMenusAsked().push_back(scene::text_menu::of_text{std::move(copied.text), std::move(link)});
      return;
    }
    if (at.button != 1) {
      return;
    }
    const std::size_t at_offset = this->offsetAt(at.x, at.y);
    // A second press where the first was, soon: the word there.
    const auto now = std::chrono::steady_clock::now();
    if (now - fLastPress < std::chrono::milliseconds(400) && at_offset == fLastOffset) {
      this->selectWordAt(at_offset);
      this->publishSelection();
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
    skiff::scene::selectedText().clear();
    // Not taken yet: a scrolled list around this may take a press that
    // moves at once as a scroll. A move that reaches this selects, and the
    // pointer is taken then.
    fPressed = true;
    this->markDamaged();
  }
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::move &at,
                 skiff::scene::PointerReply &reply) {
    // Which link the pointer is on: repainted where that changes.
    if (!fLinks.empty()) {
      const Link *link = this->linkAt(this->offsetAt(at.x, at.y));
      const std::optional<std::size_t> hovered =
          link != nullptr && !link->pill && !link->picture
              ? std::optional<std::size_t>(static_cast<std::size_t>(link - fLinks.data()))
              : std::nullopt;
      if (hovered != fHoveredLink) {
        fHoveredLink = hovered;
        this->markDamaged();
      }
    }
    // A selection is begun by a press held still a moment first, where the
    // pointer moves up or down at once -- that is a scroll, which what holds
    // this takes. Across, it is a selection at once, as a mouse drags over
    // words: made to wait, a drag over a message's text selected nothing.
    // The release went elsewhere -- another scene, past the window: the
    // press is over, not a drag to follow the pointer on.
    if (!skiff::scene::pointerHeld()) {
      fPressed = false;
      if (fDragging) {
        fDragging = false;
        reply.releasePointer();
      }
      return;
    }
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
    fDragX = at.x;
    fDragY = at.y;
    fCaret = this->offsetAt(at.x, at.y);
    this->takePillsWhole();
    this->publishSelection();
    this->markDamaged();
    reply.handle();
  }
  // Dragged past a scrolling view's edge, the text moves under a pointer
  // held still: the selection follows it, as a move would.
  void update(double) {
    if constexpr (requires { fPictures.animated(std::string_view{}); }) {
      std::vector<std::uint32_t> ids;
      for (const auto& link : fLinks)
        if (link.picture) {
          const auto* picture = fPictures.picture(link.target);
          ids.push_back(picture && *picture ? (*picture)->uniqueID() : 0);
        }
      if (ids != fPictureIds) {
        fPictureIds = std::move(ids);
        this->markDamaged();
      }
    }
    if (!fDragging) {
      return;
    }
    const std::size_t now = this->offsetAt(fDragX, fDragY);
    if (now != fCaret) {
      fCaret = now;
      this->takePillsWhole();
      this->publishSelection();
      this->markDamaged();
    }
  }
  [[nodiscard]] double wakeAt() const {
    double next = std::numeric_limits<double>::infinity();
    if constexpr (requires { fPictures.wakeAt(std::string_view{}); })
      for (const auto& link : fLinks)
        if (link.picture)
          next = std::min(next, fPictures.wakeAt(link.target));
    return next;
  }
  [[nodiscard]] bool wantsTick() const {
    if (fDragging)
      return true;
    if constexpr (requires { fPictures.animated(std::string_view{}); })
      for (const auto& link : fLinks)
        if (link.picture) {
          const auto* picture = fPictures.picture(link.target);
          if (!picture || !*picture || fPictures.animated(link.target))
            return true;
        }
    return false;
  }
  void onPointer(skiff::scene::phase::target, const skiff::scene::pointer::up &release,
                 skiff::scene::PointerReply &reply) {
    // Pressed and let go without selecting: a link there is opened; else a
    // click, for what holds the text -- a quoted stretch in a message goes
    // to what it quotes. A selectable text took every press, and a click on
    // it reached nothing above.
    if (fPressed && !fDragging) {
      // A spoiler pressed: its text shown -- all of them, as Telegram's.
      if (!fSpoilersShown && this->styleAt(fLastOffset).spoiler) {
        fPressed = false;
        fSpoilersShown = true;
        this->markDamaged();
        reply.handle();
        return;
      }
      if (const Link *link = this->linkAt(fLastOffset)) {
        fPressed = false;
        if (!link->picture)
          skiff::scene::openLink(link->target);
        reply.handle();
        return;
      }
      if (fState.fBounds.contains(release.x, release.y)) {
        reply.fClickAbove = skia::SkPoint::Make(release.x, release.y);
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
      scene::clipboardCandidate() = this->copiedRange(std::min(fAnchor, fCaret), std::max(fAnchor, fCaret));
      skiff::scene::setClipboardText(this->selected());
      reply.handle();
    } else if (press.key == keys::kA) {
      fAnchor = 0;
      fCaret = fText.size();
      this->publishSelection();
      this->markDamaged();
      reply.handle();
    }
  }
  [[nodiscard]] float lineHeight() const {
    return fSize * (std::ranges::any_of(fLinks, [](const Link& link) { return link.pill; }) ? 1.5f : 1.25f);
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
  // Broken across lines at spaces instead of running past the width --
  // always, or (false) never; by default, where it does not fit.
  void setWrapped(bool wrapped) {
    fWrapChoice = wrapped;
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
    // Wrapped or not as it fits, where nothing chose: as CSS's text.
    const bool automatic = !fWrapChoice && !fElided && !state.fGrowAxes.has<skiff::scene::axis::x>() &&
                           !state.fRelativeSizeAxes.has<skiff::scene::axis::x>();
    if (fMeasuredSize == fSize && !fWrapped && !automatic) {
      // Measured at this size, and the text has not changed -- but cut at
      // the room it has now, where that is what it is cut at.
      if (fElided) {
        this->sizeOnOneLine(parent);
      }
      return;
    }
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(nullptr, *font, fMonospace);
    // Its width on one line: measured again only as the text or size change.
    if (fMeasuredSize != fSize) {
      fNatural = p.measure(fText, fSize, fBold);
    }
    if (automatic) {
      const float room = state.fMaxWidth > 0.0f ? state.fMaxWidth : parent.width() - state.fMargin.totalX();
      const bool wrap = room > 0.0f && fNatural > room + 0.5f;
      if (wrap != fWrapped) {
        fWrapped = wrap;
        // Wrapped: at its parent's width, not at the one-line width it had
        // (which would wrap it at its own length, one line past the edge).
        fWrapsToParent = wrap;
        fWrappedRoom = -1.0f;
        fMeasuredSize = -1.0f;
        if (wrap) {
          fShrinks = true;  // as wide as its widest line, not the parent
        }
      } else if (!wrap && fMeasuredSize == fSize) {
        return;
      }
    }
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
      // As narrow as the deepest quote's lines: a bar for each level.
      int deepest = 0;
      for (const Styled &one : fStyles) {
        if (one.quote) {
          deepest = std::max(deepest, this->styleAt(one.first).depth);
        }
      }
      const float indent = kQuoteIndent * static_cast<float>(deepest) + (deepest > 0 ? kQuoteRight : 0.0f);
      fLines = p.wrap(fText, room - indent, fSize, fBold);
      state.fHeight =
          static_cast<float>(std::max<std::size_t>(1, fLines.size())) *
          this->lineHeight();
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
    this->sizeOnOneLine(parent);
    state.fHeight = this->lineHeight();
    fMeasuredSize = fSize;
  }
  // On one line: as wide as its glyphs, within its largest width. Sized by
  // its flow or parent, it clips to the width it was given rather than
  // replacing that width with the glyphs'. Elided, it is cut at the room
  // its parent has too: a sender's name cut at the bubble's widest stood
  // out of a bubble in a chat narrower than that, by as much.
  void sizeOnOneLine(const skia::SkRect &parent) {
    skiff::scene::State &state = fState;
    if (state.fGrowAxes.has<skiff::scene::axis::x>() || state.fRelativeSizeAxes.has<skiff::scene::axis::x>()) {
      return;
    }
    float width = state.fMaxWidth > 0.0f ? std::min(state.fMaxWidth, fNatural) : fNatural;
    if (fElided && parent.width() > 0.0f) {
      width = std::min(width, std::max(0.0f, parent.width() - state.fMargin.totalX()));
    }
    state.fWidth = width;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) {
    const skiff::scene::State &state = fState;
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fText.empty()) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font, fMonospace);
    const int saved = canvas->save();
    const skia::SkRect &bounds = state.fBounds;
    if (fWrapped && !fStyles.empty()) {
      this->drawQuotes(canvas, p, alpha);
    }
    this->drawSelection(canvas, p, alpha);
    if (fWrapped) {
      float y = bounds.fTop + fSize;
      if (fLinks.empty() && fStyles.empty()) {
        for (const std::string &line : fLines) {
          p.text(line, bounds.fLeft, y, fSize, fColour, alpha, fBold);
          y += this->lineHeight();
        }
      } else {
        for (const auto &[start, line] : this->shownLines()) {
          this->drawWithLinks(canvas, p, start, line, bounds.fLeft, y, alpha);
          y += this->lineHeight();
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
    if (!fLinks.empty() || !fStyles.empty()) {
      const float baseline = bounds.fTop + fSize;
      const std::string shown = fElided ? p.elide(fText, bounds.width(), fSize, fBold) : fText;
      if (shown == fText) {
        this->drawWithLinks(canvas, p, 0, fText, bounds.fLeft, baseline, alpha);
      } else {
        constexpr std::string_view ellipsis = "\u2026";
        std::size_t end = shown.empty() ? 0 : shown.size() - ellipsis.size();
        // An elision must not replace part of a picture or pill with an
        // ellipsis that would then inherit that object's span.
        for (const Link& link : fLinks)
          if ((link.picture || link.pill) && link.first < end && end < link.last)
            end = link.first;
        const float x = this->drawWithLinks(canvas, p, 0, std::string_view(fText).substr(0, end),
                                            bounds.fLeft, baseline, alpha);
        if (!shown.empty()) p.text(std::string(ellipsis), x, baseline, fSize, fColour, alpha, fBold);
      }
    } else if (fElided) {
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
    const skiff::paint::Painter p(nullptr, *font, fMonospace);
    const skia::SkRect &bounds = fState.fBounds;
    const float lineHeight = this->lineHeight();
    const auto index = static_cast<std::size_t>(
        std::clamp((y - bounds.fTop) / lineHeight, 0.0f, static_cast<float>(lines.size() - 1)));
    const auto [start, line] = lines[index];
    const float into = x - bounds.fLeft - this->indentOf(start);
    // The character boundaries, and among them the one nearest: found by
    // halving, the width up to a boundary growing with it -- measuring up to
    // every boundary made a long line's every move of a selecting drag cost
    // its length squared.
    std::vector<std::size_t> cuts{0};
    for (std::size_t i = 1; i <= line.size(); ++i) {
      if (i == line.size() || (static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) {
        cuts.push_back(i);
      }
    }
    const auto widthTo = [&](std::size_t cut) {
      return cut == 0 ? 0.0f : p.measure(std::string(line.substr(0, cut)), fSize, fBold);
    };
    std::size_t low = 0, high = cuts.size() - 1;
    while (low < high) {
      const std::size_t middle = (low + high) / 2;
      if (widthTo(cuts[middle]) < into) {
        low = middle + 1;
      } else {
        high = middle;
      }
    }
    // The first boundary at or past the point, or the one before it: the
    // nearer.
    std::size_t best = cuts[low];
    if (low > 0 && std::abs(widthTo(cuts[low - 1]) - into) <= std::abs(widthTo(cuts[low]) - into)) {
      best = cuts[low - 1];
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
      if (!one.pill && !one.picture) {
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
  // What is selected, said to the scene: this is the text showing it.
  void publishSelection() const {
    if (textSelectionOwner() == fState.fId) {
      auto copied = this->copiedRange(std::min(fAnchor, fCaret), std::max(fAnchor, fCaret));
      skiff::scene::selectedText() = copied.text;
      skiff::scene::clipboardCandidate() = std::move(copied);
    }
  }
  void drawSelection(skia::SkCanvas *canvas, const skiff::paint::Painter &p, float alpha) const {
    // The last text pressed shows its selection -- whether or not it has the
    // keyboard's focus: a selectable text does not take it on a press, and
    // required, no selection was ever drawn.
    if (!fSelectable || fAnchor == fCaret || textSelectionOwner() != fState.fId) {
      return;
    }
    const std::size_t low = std::min(fAnchor, fCaret), high = std::max(fAnchor, fCaret);
    const skia::SkRect &bounds = fState.fBounds;
    const float lineHeight = this->lineHeight();
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
  float drawWithLinks(skia::SkCanvas *canvas, const skiff::paint::Painter &p, std::size_t start,
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
    std::ranges::sort(cuts);
    cuts.erase(std::ranges::unique(cuts).begin(), cuts.end());
    float at = x + this->indentOf(start);
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
      const std::string piece(line.substr(cuts[i] - start, cuts[i + 1] - cuts[i]));
      const Link *link = this->linkAt(cuts[i]);
      const bool linked = link != nullptr;
      const Styled style = this->styleAt(cuts[i]);
      const bool bold = fBold || style.strong;
      const skia::SkColor colour = linked ? fLinkColour : fColour;
      const float width = p.measure(piece, fSize, bold);
      // Marked: on a plate of the quote's colour.
      if (style.marked) {
        skia::SkPaint plate;
        plate.setAntiAlias(true);
        plate.setColor(fQuoteColour);
        plate.setAlphaf(0.28f * alpha);
        canvas->drawRoundRect(skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, this->lineHeight()), 3.0f, 3.0f,
                              plate);
      }
      // Code: on a plate of the text's colour, faint.
      if (style.code) {
        skia::SkPaint plate;
        plate.setAntiAlias(true);
        plate.setColor(fColour);
        plate.setAlphaf(0.10f * alpha);
        canvas->drawRoundRect(skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, this->lineHeight()), 3.0f, 3.0f,
                              plate);
      }
      // Struck: a line through its middle.
      if (style.struck) {
        skia::SkPaint line;
        line.setColor(colour);
        line.setAlphaf(line.getAlphaf() * alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(at, y - fSize * 0.32f, width, 1.0f), line);
      }
      // A spoiler not shown yet: a faint plate under a scatter of dots in
      // the text's colour, its text not drawn.
      if (style.spoiler && !fSpoilersShown) {
        skia::SkPaint plate;
        plate.setAntiAlias(true);
        plate.setColor(colour);
        plate.setAlphaf(0.12f * alpha);
        const skia::SkRect box = skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, this->lineHeight());
        canvas->drawRoundRect(box, 3.0f, 3.0f, plate);
        skia::SkPaint dot;
        dot.setAntiAlias(true);
        dot.setColor(colour);
        dot.setAlphaf(0.7f * alpha);
        for (int row = 0; row * 2.5f < box.height() - 1.0f; ++row) {
          for (int column = 0; column * 2.5f < box.width() - 1.0f; ++column) {
            // Scattered, the same at every frame: some places have none.
            if ((row * 7 + column * 13 + static_cast<int>(cuts[i])) % 5 < 2) {
              canvas->drawCircle(box.fLeft + 1.0f + column * 2.5f + (row % 2) * 1.2f, box.fTop + 1.5f + row * 2.5f,
                                 0.55f, dot);
            }
          }
        }
        at += width;
        continue;
      }
      // Underlined: a line under it, as a link's.
      if (style.underline && !linked) {
        skia::SkPaint under;
        under.setColor(colour);
        under.setAlphaf(under.getAlphaf() * alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(at, y + 2.0f, width, 1.0f), under);
      }
      if (link && link->picture) {
        // The picture, square, a little over the text's size, standing on
        // its baseline; nothing where the program has none (yet).
        if (cuts[i] == link->first)
          if (const skia::Sp<skia::SkImage> *picture = fPictures.picture(link->target);
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
        drawPill(canvas, p, at, y, width, fSize, colour,
                 cuts[i] == link->first ? fPictures.pill(link->target) : std::nullopt, alpha);
        p.text(piece, at, y, fSize, colour, alpha, fBold);
        at += width;
        continue;
      }
      // The link under the pointer: on a faint plate of its colour.
      if (linked && fState.fHovered && fHoveredLink && link == &fLinks[*fHoveredLink]) {
        skia::SkPaint plate;
        plate.setAntiAlias(true);
        plate.setColor(fLinkColour);
        plate.setAlphaf(0.16f * alpha);
        canvas->drawRoundRect(skia::SkRect::MakeXYWH(at - 1.0f, y - fSize, width + 2.0f, fSize * 1.3f), 3.0f, 3.0f,
                              plate);
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
    return at;
  }

  // How far a line's text stands in: a quoted one's, past its bar -- for
  // each level. And the room a quote keeps at its right, for its mark.
  static constexpr float kQuoteIndent = 12.0f;
  static constexpr float kQuoteRight = 16.0f;
  static constexpr float kQuoteRadius = 5.0f;
  // The quotes, as Telegram draws a blockquote: each run of lines in a
  // level of quote on a rounded plate of its own, faint in the level's
  // colour, from its indent to the text's right edge, a bar at its left and
  // a quote mark at its top right. A quote inside a quote lies on the outer
  // one's plate: its words on both tints. The words themselves in the
  // text's colour, readable on any bubble.
  void drawQuotes(skia::SkCanvas *canvas, const skiff::paint::Painter &p, float alpha) const {
    const skia::SkRect &bounds = fState.fBounds;
    const float lineHeight = this->lineHeight();
    std::vector<int> depths;
    int deepest = 0;
    for (const auto &[start, line] : this->shownLines()) {
      depths.push_back(this->styleAt(start).depth);
      deepest = std::max(deepest, depths.back());
    }
    for (int level = 0; level < deepest; ++level) {
      const skia::SkColor tint = quoteColourAt(fQuoteColour, level + 1);
      for (std::size_t i = 0; i < depths.size();) {
        if (depths[i] <= level) {
          ++i;
          continue;
        }
        std::size_t j = i;
        while (j < depths.size() && depths[j] > level) {
          ++j;
        }
        const float left = bounds.fLeft + static_cast<float>(level) * kQuoteIndent;
        const skia::SkRect plate = skia::SkRect::MakeLTRB(left, bounds.fTop + static_cast<float>(i) * lineHeight,
                                                          bounds.fRight, bounds.fTop + static_cast<float>(j) * lineHeight);
        const int saved = canvas->save();
        canvas->clipRRect(skia::SkRRect::MakeRectXY(plate, kQuoteRadius, kQuoteRadius), true);
        skia::SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(tint);
        fill.setAlphaf(0.12f * alpha);
        canvas->drawRect(plate, fill);
        fill.setColor(tint);
        fill.setAlphaf(fill.getAlphaf() * alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(left, plate.fTop, 3.0f, plate.height()), fill);
        canvas->restoreToCount(saved);
        p.text("\u201D", bounds.fRight - kQuoteRight + 4.0f, plate.fTop + fSize, fSize, tint, alpha);
        i = j;
      }
    }
  }
  [[nodiscard]] float indentOf(std::size_t start) const {
    return kQuoteIndent * static_cast<float>(this->styleAt(start).depth);
  }
  // A quote's colour at a depth: its own at the first, then Telegram's.
  [[nodiscard]] static skia::SkColor quoteColourAt(skia::SkColor colour, int depth) {
    return quoteLevelColour(colour, depth);
  }
  // The width a wrapped line has to fit into, resolved as layout would.
  [[nodiscard]] float roomFor(const skia::SkRect &parent) const {
    const skiff::scene::State &state = fState;
    // At most its own maximum, as CSS's max-width -- and never more than its
    // parent has.
    if (state.fMaxWidth > 0.0f) {
      const float parentRoom = parent.width() - state.fMargin.totalX();
      return parentRoom > 0.0f ? std::min(state.fMaxWidth, parentRoom) : state.fMaxWidth;
    }
    if (state.fRelativeSizeAxes.has<skiff::scene::axis::x>()) {
      return parent.width() * state.fWidth - state.fMargin.totalX();
    }
    return state.fWidth > 0.0f && !fWrapsToParent
               ? state.fWidth
               : parent.width() - state.fMargin.totalX();
  }

public:
  // Where its pictures come from: Pictures' own, or erased.
  void setPictures(Pictures pictures) { fPictures = std::move(pictures); }

private:
  [[no_unique_address]] Pictures fPictures{};
  std::string fText;
  float fSize;
  skia::SkColor fColour;
  bool fBold;
  bool fMonospace = false;
  bool fWrapped = false;
  // Wrapped or not as chosen; none chosen, as it fits.
  std::optional<bool> fWrapChoice;
  // Its width on one line, at fMeasuredSize.
  float fNatural = 0.0f;
  float fWrappedRoom = -1.0f;
  // Wrapped with no width of its own: it wraps to its parent's, whatever
  // that is at the time.
  bool fWrapsToParent = false;
  bool fElided = false;
  std::vector<Styled> fStyles;
  // Its spoilers shown: pressed once, until its styles are set anew.
  bool fSpoilersShown = false;
  skia::SkColor fQuoteColour = 0;
  std::vector<std::string> fLines;
  float fMeasuredSize = -1.0f;
  float fBaseSize = 0.0f;
  skia::SkColor fBaseColour = 0;
  bool fBaseBold = false;
  bool fNodeStyleActive = false;
  bool fSelectable = false;
  float fDragX = 0.0f, fDragY = 0.0f;  // where a selecting drag is now
  float fPressX = 0.0f, fPressY = 0.0f;
  bool fShrinks = false;
  bool fDragging = false;
  bool fPressed = false;
  std::vector<Link> fLinks;
  std::vector<std::uint32_t> fPictureIds;
  // The link the pointer was last on, of fLinks: lit while it is over this.
  std::optional<std::size_t> fHoveredLink;
  skia::SkColor fLinkColour = skia::colorSetARGB(255, 82, 160, 230);
  std::size_t fAnchor = 0;
  std::size_t fCaret = 0;
  std::chrono::steady_clock::time_point fLastPress{};
  std::size_t fLastOffset = 0;
  skia::SkColor fSelectionColour = skia::colorSetARGB(110, 64, 167, 227);
};
} // namespace internal

// The text over erased pictures, taking the pictures' own type: all of its
// code but this is internal::BasicText<AnyPictures>'s, made once.
template <class Pictures> class ErasedBasicText : public internal::BasicText<AnyPictures> {
  using Base = internal::BasicText<AnyPictures>;

public:
  // Its own handlers, as the wrapper's own: brought in here, so that they
  // are taken for it. Else Node's defaults -- deducing `this`, an exact
  // match for the wrapper -- beat the widget's own, which reach it through
  // the base, and the widget took no key, text or press.
  using Base::onPointer;
  using Base::onKey;
  using Base::onText;
  using Base::onSemantic;
  template <class... Args>
    requires std::constructible_from<Base, Args...>
  ErasedBasicText(Args &&...args) : Base(std::forward<Args>(args)...) {
    this->setPictures(AnyPictures::of<Pictures>());
  }
};

// A text: made for its pictures in a release build, over erased ones
// otherwise -- as the walks are.
template <class Pictures = NoPictures>
using BasicText = std::conditional_t<skiff::scene::kErasedWalks, ErasedBasicText<Pictures>, internal::BasicText<Pictures>>;

using Text = BasicText<>;

} // namespace skiff::nodes
