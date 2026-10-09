module;

// Shaped text, where the build asks for it: HarfBuzz turns a run of
// characters into positioned glyphs -- joining, conjuncts, ligatures, emoji
// sequences -- and alef says where runs of one direction are (UAX #9),
// where a line may end (UAX #14) and where a character ends (UAX #29).
#ifdef SKIFF_TEXT_SHAPING
#include <hb.h>
#endif

export module skiff.paint;

import std;
import splice;
import skia;
#ifdef SKIFF_TEXT_SHAPING
import alef.bidi;
import alef.grapheme;
import alef.line;
#endif

// Text and paint: the font stack that puts fallbacks behind a primary face,
// and a thin wrapper over the canvas so callers do not repeat paint setup.
// Nothing in here knows what it is drawing.
export namespace skiff::paint {

// Easing curves used by the framework's transforms.
[[nodiscard]] inline float outQuint(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const float u = 1.0f - t;
  return 1.0f - u * u * u * u * u;
}

[[nodiscard]] inline float outElasticHalf(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  constexpr float p = 0.5f;
  return std::pow(2.0f, -10.0f * t) *
             std::sin((t - p / 4.0f) * (2.0f * std::numbers::pi_v<float>) / p) +
         1.0f;
}

// Frame-rate independent approach toward a target (tau in milliseconds).
//
// This used to set a global flag whenever anything moved, which the frame
// loop read once a frame to decide whether to keep drawing. It worked, and it
// was a side channel: every eased value in the client wrote to one bool, and
// nothing could be asked who had moved. Every screen now settles in a pass of
// its own and marks what changed, and marked damage is what asks for the next
// frame -- so the flag has nothing left to say.
[[nodiscard]] inline float approach(float current, float target, float tauMs,
                                    double dtMs) {
  const float a = 1.0f - std::exp(-static_cast<float>(dtMs) / tauMs);
  const float next = current + (target - current) * a;
  // Exponential easing never arrives. Left alone, a value that has visually
  // settled keeps changing in the fifth decimal for ever, and anything
  // comparing it against its previous value -- which is how this client
  // decides what to repaint -- concludes that it is still moving. Below a
  // thousandth of a unit, which is under a pixel and under 1/255 of an
  // alpha, it is there.
  return std::abs(target - next) < 0.001f ? target : next;
}

// `approach()` has a finite, exact end: once it is visually close enough it
// returns the target itself. Frame scheduling must use that same contract,
// rather than a second tolerance which can declare the animation finished a
// frame before approach() has actually arrived.
[[nodiscard]] inline bool settled(float current, float target) noexcept {
  return current == target;
}

// How much moves: nothing, only small movements (a knob sliding, a section
// unfolding), or everything (a panel crossing the window too). Set once for
// the whole program, as a user setting, through motionLevel().
namespace motion {
struct none {};
struct reduced {};
struct full {};
} // namespace motion
using Motion = spl::variant<motion::none, motion::reduced, motion::full>;

// What kind of movement an animation is: a subtle one stays where it is (a
// knob, a fold), a sweeping one crosses the window.
namespace movement {
struct subtle {};
struct sweeping {};
} // namespace movement
using Movement = spl::variant<movement::subtle, movement::sweeping>;

inline Motion &motionLevel() {
  static Motion level = motion::full{};
  return level;
}

// Whether a movement of this kind moves at this level.
constexpr bool moves(motion::none, movement::subtle) { return false; }
constexpr bool moves(motion::none, movement::sweeping) { return false; }
constexpr bool moves(motion::reduced, movement::subtle) { return true; }
constexpr bool moves(motion::reduced, movement::sweeping) { return false; }
constexpr bool moves(motion::full, movement::subtle) { return true; }
constexpr bool moves(motion::full, movement::sweeping) { return true; }
[[nodiscard]] inline bool moves(const Movement &kind) {
  return spl::visit([](auto level, auto of) { return moves(level, of); },
                    motionLevel(), kind);
}

// A value easing toward its target, one frame at a time, by approach(): what
// a node keeps for anything it animates. Its step() is called from the
// node's update(nowMs), and its moving() is the node's settling(). Where
// motionLevel() does not move its kind of movement, it is at its target the
// moment it is given one.
class Eased {
public:
  explicit Eased(float value = 0.0f, float tauMs = 70.0f,
                 Movement kind = movement::subtle{}) noexcept
      : fValue(value), fTarget(value), fTauMs(tauMs), fKind(kind) {}

  [[nodiscard]] float value() const noexcept { return fValue; }
  [[nodiscard]] float target() const noexcept { return fTarget; }
  [[nodiscard]] bool moving() const noexcept {
    return !settled(fValue, fTarget);
  }

  // Where to go from here: at once, where this does not move.
  void setTarget(float target) {
    if (!this->moving()) {
      fLastMs = 0.0; // a value at rest starts its next move from now
    }
    fTarget = target;
    if (!moves(fKind)) {
      fValue = target;
    }
  }
  // There, without moving.
  void jump(float value) noexcept {
    fValue = value;
    fTarget = value;
    fLastMs = 0.0;
  }
  void setTau(float tauMs) noexcept { fTauMs = tauMs; }

  // One frame's step, at `nowMs`: whether the value changed.
  bool step(double nowMs) {
    const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
    fLastMs = nowMs;
    const float before = fValue;
    fValue = moves(fKind) ? approach(fValue, fTarget, fTauMs, dt) : fTarget;
    return fValue != before;
  }

private:
  float fValue;
  float fTarget;
  float fTauMs;
  Movement fKind;
  double fLastMs = 0.0;
};

// A value moving to its target over a fixed time, eased out -- quick, then
// settling -- so a move takes as long whichever way it goes, which
// approach()'s never-ending tail does not. A new target starts a new move
// from wherever the value is. Like Eased, it says what kind of movement it
// is, and where motionLevel() does not move that kind it is at its target at
// once.
class Tween {
public:
  explicit Tween(float value = 0.0f, float durationMs = 220.0f,
                 Movement kind = movement::subtle{}) noexcept
      : fValue(value), fFrom(value), fTarget(value), fDurationMs(durationMs),
        fKind(kind) {}

  [[nodiscard]] float value() const noexcept { return fValue; }
  [[nodiscard]] float target() const noexcept { return fTarget; }
  [[nodiscard]] bool moving() const noexcept { return fValue != fTarget; }

  void setTarget(float target) {
    if (target == fTarget) {
      return;
    }
    fFrom = fValue;
    fTarget = target;
    fStartMs = -1.0;
    if (!moves(fKind)) {
      fValue = target;
    }
  }
  void jump(float value) noexcept {
    fValue = value;
    fFrom = value;
    fTarget = value;
    fStartMs = -1.0;
  }
  void setDuration(float durationMs) noexcept { fDurationMs = durationMs; }

  // One frame's step, at `nowMs`: whether the value changed. The first frame
  // of a move counts as one frame in.
  bool step(double nowMs) {
    if (!this->moving()) {
      return false;
    }
    if (!moves(fKind) || fDurationMs <= 0.0f) {
      fValue = fTarget;
      return true;
    }
    if (fStartMs < 0.0) {
      fStartMs = nowMs - 16.0;
    }
    // Missing frames skip samples, not time. Moving the start after a slow
    // frame stretched every animation on a machine drawing below 20 FPS.
    const float t = std::clamp(
        static_cast<float>((nowMs - fStartMs) / fDurationMs), 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    const float before = fValue;
    fValue = t >= 1.0f ? fTarget : fFrom + (fTarget - fFrom) * eased;
    return fValue != before;
  }

private:
  float fValue;
  float fFrom;
  float fTarget;
  float fDurationMs;
  Movement fKind;
  double fStartMs = -1.0;
};

// ---- Text with fallback ---------------------------------------------------
//
// Skia draws a string with exactly one typeface: a codepoint the typeface
// does not have becomes a box. Beatmap metadata is full of Japanese, Korean
// and the odd bit of everything else, so text is split into runs by which of
// the loaded faces can render it, and each run is drawn with that face.
//
// The stack is filled once at startup from the fonts shipped beside the
// binary; nothing is taken from the system. Lookups happen on the render
// thread only, which is what lets the coverage cache go unlocked.
// What text measures and shapes to, kept by its text, its size and its face.
// Two generations: when the newer is full the older goes, and what is found
// in the older moves to the newer -- the lines in use stay, rather than all
// of them going at once, and every line on the screen being shaped again in
// one frame. Each entry keeps its text, so that two texts whose keys meet
// are told apart rather than drawn as each other.
template <class Value> class TextCache {
public:
  [[nodiscard]] Value *find(std::uint64_t key, std::string_view text) {
    if (auto it = fNew.find(key); it != fNew.end()) {
      return it->second.text == text ? &it->second.value : nullptr;
    }
    if (auto it = fOld.find(key); it != fOld.end() && it->second.text == text) {
      Entry moved = std::move(it->second);
      fOld.erase(it);
      return &this->insert(key, std::move(moved)).value;
    }
    return nullptr;
  }
  Value &put(std::uint64_t key, std::string_view text, Value value) {
    return this->insert(key, Entry{std::string(text), std::move(value)}).value;
  }
  void clear() {
    fNew.clear();
    fOld.clear();
  }

private:
  struct Entry {
    std::string text;
    Value value;
  };
  static constexpr std::size_t kGeneration = 8192;
  Entry &insert(std::uint64_t key, Entry entry) {
    if (fNew.size() >= kGeneration) {
      fOld = std::exchange(fNew, {});
    }
    return fNew.insert_or_assign(key, std::move(entry)).first->second;
  }
  std::unordered_map<std::uint64_t, Entry> fNew;
  std::unordered_map<std::uint64_t, Entry> fOld;
};

class FontStack {
public:
  void setPrimary(skia::Sp<skia::SkTypeface> face) {
    fPrimary = std::move(face);
    // A weight instance of the same file, so bold text is a different face
    // rather than the same one dilated at rasterisation time. Faking bold
    // costs more per glyph than drawing one, and on a software rasteriser
    // that is the difference between a text-heavy screen at 150 frames a
    // second and at 60.
    fPrimaryBold.reset();
    if (fPrimary) {
      const auto weighed = [&](float weight) {
        skia::SkFontArguments::VariationPosition::Coordinate coordinate{kWeightAxis, weight};
        skia::SkFontArguments::VariationPosition position{&coordinate, 1};
        skia::SkFontArguments arguments;
        arguments.setVariationDesignPosition(position);
        return fPrimary->makeClone(arguments);
      };
      fPrimaryBold = weighed(600.0f);
      // Regular at 400 too: a variable face's own default can be heavier,
      // and text then looks almost bold -- heavier than the bold names.
      if (auto regular = weighed(400.0f))
        fPrimary = std::move(regular);
    }
    this->invalidateCaches();
  }

  // A family whose weights are files of their own, as most static faces
  // are: its regular, and the face its bold text is drawn with.
  void setPrimary(skia::Sp<skia::SkTypeface> regular, skia::Sp<skia::SkTypeface> bold) {
    fPrimary = std::move(regular);
    fPrimaryBold = std::move(bold);
    this->invalidateCaches();
  }

  // Picks the face for the weight instead of asking the rasteriser to
  // thicken one, falling back to that only when there is no bold instance.
  void applyWeight(skia::SkFont &font, bool bold) const {
    if (bold && fPrimaryBold) {
      font.setTypeface(fPrimaryBold);
      font.setEmbolden(false);
      return;
    }
    if (fPrimary) {
      font.setTypeface(fPrimary);
    }
    font.setEmbolden(bold);
  }
  // The face code is drawn in -- each character as wide as the next -- for
  // a text that asks for it; none set, code is drawn in the primary face.
  void setMonospace(skia::Sp<skia::SkTypeface> face) {
    fMonospace = std::move(face);
    this->invalidateCaches();
  }
  [[nodiscard]] const skia::Sp<skia::SkTypeface> &monospace() const noexcept { return fMonospace; }
  // The face for code, where there is one: set on the font for its run.
  [[nodiscard]] bool applyMonospace(skia::SkFont &font, bool bold) const {
    if (!fMonospace) {
      return false;
    }
    font.setTypeface(fMonospace);
    font.setEmbolden(bold);
    return true;
  }
  // Where faces for characters none of the fallbacks has are looked for.
  void setFontManager(skia::Sp<skia::SkFontMgr> manager) {
    fManager = std::move(manager);
    this->invalidateCaches();
  }
  void addFallback(skia::Sp<skia::SkTypeface> face) {
    if (face) {
      fFallbacks.push_back(std::move(face));
      this->invalidateCaches();
    }
  }
  void invalidateCaches() {
    fCoverage.clear();
    fAsciiCovered.clear();
    fWidths.clear();
#ifdef SKIFF_TEXT_SHAPING
    fShaped.clear();
    fWords.clear();
    fHarfBuzz.clear();
#endif
  }
  [[nodiscard]] const skia::Sp<skia::SkTypeface> &primary() const noexcept {
    return fPrimary;
  }
  [[nodiscard]] std::size_t fallbackCount() const noexcept {
    return fFallbacks.size();
  }

  [[nodiscard]] float measure(const skia::SkFont &font,
                              std::string_view text) const {
    if (text.empty()) {
      return 0.0f;
    }
    // Measuring is the hot part of drawing a menu: the same labels are
    // measured every frame, at the same sizes, by every screen. The answer
    // only depends on the text, the size, the weight and the face.
    const std::uint64_t key = cacheKey(font, text);
    if (const float *known = fWidths.find(key, text)) {
      return *known;
    }
#ifdef SKIFF_TEXT_SHAPING
    const float width = this->shaped(font, text).width;
#else
    float width = 0.0f;
    this->forEachRun(font, text,
                     [&](const skia::SkFont &runFont, std::string_view run) {
                       width += runFont.measureText(
                           run.data(), run.size(), skia::SkTextEncoding::kUTF8);
                     });
#endif
    fWidths.put(key, text, width);
    return width;
  }

  void draw(skia::SkCanvas *canvas, const skia::SkFont &font,
            std::string_view text, float x, float y,
            const skia::SkPaint &paint) const {
#ifdef SKIFF_TEXT_SHAPING
    // Shaped: each run's glyphs where HarfBuzz put them, in one blob.
    const ShapedLine &line = this->shaped(font, text);
    if (line.runs.empty()) {
      return;
    }
    if (!line.blob) {
      skia::SkTextBlobBuilder builder;
      for (const ShapedRun &run : line.runs) {
        const auto &buffer = builder.allocRunPosH(run.font, static_cast<int>(run.glyphs.size()), 0.0f);
        std::ranges::copy(run.glyphs, buffer.glyphs);
        std::ranges::copy(run.xs, buffer.pos);
      }
      line.blob = builder.make();
    }
    if (line.blob) {
      canvas->drawTextBlob(line.blob, x, y, paint);
    }
    return;
#endif
    // Nothing to split when every byte is plain ASCII and the primary face
    // covers it, which is most of the text this client draws.
    if (isAscii(text) && this->asciiCovered(font.getTypeface())) {
      canvas->drawSimpleText(text.data(), text.size(),
                             skia::SkTextEncoding::kUTF8, x, y, font, paint);
      return;
    }
    this->forEachRun(font, text,
                     [&](const skia::SkFont &runFont, std::string_view run) {
                       canvas->drawSimpleText(run.data(), run.size(),
                                              skia::SkTextEncoding::kUTF8, x, y,
                                              runFont, paint);
                       x += runFont.measureText(run.data(), run.size(),
                                                skia::SkTextEncoding::kUTF8);
                     });
  }

#ifdef SKIFF_TEXT_SHAPING
  // A run of glyphs of one face, each where it starts from the line's left.
  struct ShapedRun {
    skia::SkFont font;
    std::vector<skia::SkGlyphID> glyphs;
    std::vector<float> xs;
  };
  // A line shaped: its runs left to right, and how wide it is.
  struct ShapedLine {
    std::vector<ShapedRun> runs;
    float width = 0.0f;
    // Its glyphs as one blob, placed from 0 on a baseline at 0: made at its
    // first drawing, and drawn where it goes after -- not built again, runs
    // copied and allocated, at every drawing of every line.
    mutable skia::Sp<skia::SkTextBlob> blob;
  };

  // The line as glyphs: split into runs of one direction by UAX #9, each
  // into runs of one face at character boundaries, each shaped by HarfBuzz
  // with the whole line as its context. Kept by the text, the size and the
  // face, as widths are: the same labels are drawn every frame.
  [[nodiscard]] const ShapedLine &shaped(const skia::SkFont &font, std::string_view text) const {
    const std::uint64_t key = cacheKey(font, text);
    if (const ShapedLine *known = fShaped.find(key, text)) {
      return *known;
    }
    ShapedLine line;
    const skia::SkTypeface *base = font.getTypeface();
    float x = 0.0f;
    const alef::bidi_paragraph paragraph(text);
    for (const alef::bidi_run run : paragraph.runs(0, text.size())) {
      const std::string_view piece = text.substr(run.first, run.last - run.first);
      // Where the face changes, at characters' edges only: a cluster's face
      // is its first code point's.
      struct Part {
        std::size_t first, last;
        int face;
      };
      std::vector<Part> parts;
      for (auto cluster : piece | alef::graphemes) {
        const std::string_view one(cluster.begin(), cluster.end());
        std::size_t at = 0;
        const int face = this->faceFor(decodeUtf8(one, at), base);
        const std::size_t first = run.first + static_cast<std::size_t>(one.data() - piece.data());
        if (!parts.empty() && parts.back().face == face && parts.back().last == first) {
          parts.back().last = first + one.size();
        } else {
          parts.push_back({first, first + one.size(), face});
        }
      }
      // Right to left, the last of them is drawn first.
      if (run.right_to_left()) {
        std::ranges::reverse(parts);
      }
      for (const Part &part : parts) {
        ShapedRun out;
        out.font = this->fontFor(font, part.face);
        const skia::SkTypeface *face = out.font.getTypeface();
        const HarfBuzzFont *hb = face ? this->harfBuzz(*face) : nullptr;
        if (hb == nullptr) {
          continue;
        }
        // Left to right: word by word, each word -- with the space after it
        // -- shaped once for its face and kept, at every size alike. A new
        // message is mostly words seen before: shaped whole, each of its
        // lines was HarfBuzz's work again.
        if (!run.right_to_left()) {
          const float scale = out.font.getSize() / static_cast<float>(hb->unitsPerEm);
          std::size_t at = part.first;
          while (at < part.last) {
            const std::size_t space = text.find(' ', at);
            const std::size_t end = space == std::string_view::npos || space >= part.last ? part.last : space + 1;
            const ShapedWord &word = this->shapedWord(*hb, face, text.substr(at, end - at));
            for (std::size_t i = 0; i < word.glyphs.size(); ++i) {
              out.glyphs.push_back(word.glyphs[i]);
              out.xs.push_back(x + word.xs[i] * scale);
            }
            x += word.advance * scale;
            at = end;
          }
          if (!out.glyphs.empty()) {
            line.runs.push_back(std::move(out));
          }
          continue;
        }
        hb_buffer_t *buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), static_cast<unsigned>(part.first),
                           static_cast<int>(part.last - part.first));
        hb_buffer_set_direction(buffer, run.right_to_left() ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(hb->font.get(), buffer, nullptr, 0);
        unsigned count = 0;
        const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
        const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);
        const float scale = out.font.getSize() / static_cast<float>(hb->unitsPerEm);
        out.glyphs.reserve(count);
        out.xs.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
          out.glyphs.push_back(static_cast<skia::SkGlyphID>(infos[i].codepoint));
          out.xs.push_back(x + static_cast<float>(positions[i].x_offset) * scale);
          x += static_cast<float>(positions[i].x_advance) * scale;
        }
        hb_buffer_destroy(buffer);
        if (!out.glyphs.empty()) {
          line.runs.push_back(std::move(out));
        }
      }
    }
    line.width = x;
    return fShaped.put(key, text, std::move(line));
  }

private:
  // A word shaped alone, in its face's units: glyphs, where each starts,
  // and how far it goes.
  struct ShapedWord {
    std::vector<skia::SkGlyphID> glyphs;
    std::vector<float> xs;
    float advance = 0.0f;
  };
  struct WordHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view word) const noexcept { return std::hash<std::string_view>{}(word); }
  };
  using WordsOfFace = std::unordered_map<std::string, ShapedWord, WordHash, std::equal_to<>>;
  struct HarfBuzzFont;
  [[nodiscard]] const ShapedWord &shapedWord(const HarfBuzzFont &hb, const skia::SkTypeface *face,
                                             std::string_view word) const {
    WordsOfFace &words = fWords[face];
    if (const auto found = words.find(word); found != words.end()) {
      return found->second;
    }
    if (words.size() > 50000) {
      words.clear();
    }
    ShapedWord made;
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, word.data(), static_cast<int>(word.size()), 0, static_cast<int>(word.size()));
    hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(hb.font.get(), buffer, nullptr, 0);
    unsigned count = 0;
    const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);
    made.glyphs.reserve(count);
    made.xs.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      made.glyphs.push_back(static_cast<skia::SkGlyphID>(infos[i].codepoint));
      made.xs.push_back(made.advance + static_cast<float>(positions[i].x_offset));
      made.advance += static_cast<float>(positions[i].x_advance);
    }
    hb_buffer_destroy(buffer);
    return words.emplace(std::string(word), std::move(made)).first->second;
  }
  mutable std::unordered_map<const skia::SkTypeface *, WordsOfFace> fWords;
  // A face as HarfBuzz reads it: its file's bytes, at its own units, with
  // the variation the typeface is an instance of -- the weight of a clone.
  struct HarfBuzzFont {
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font{nullptr, &hb_font_destroy};
    unsigned unitsPerEm = 1000;
  };
  [[nodiscard]] const HarfBuzzFont *harfBuzz(const skia::SkTypeface &face) const {
    if (const auto it = fHarfBuzz.find(&face); it != fHarfBuzz.end()) {
      return &it->second;
    }
    int index = 0;
    const auto stream = face.openStream(&index);
    if (!stream) {
      return nullptr;
    }
    std::vector<char> bytes(stream->getLength());
    bytes.resize(stream->read(bytes.data(), bytes.size()));
    hb_blob_t *blob = hb_blob_create(bytes.data(), static_cast<unsigned>(bytes.size()), HB_MEMORY_MODE_DUPLICATE,
                                     nullptr, nullptr);
    hb_face_t *hbFace = hb_face_create(blob, static_cast<unsigned>(index));
    hb_blob_destroy(blob);
    HarfBuzzFont made;
    made.unitsPerEm = std::max(1u, hb_face_get_upem(hbFace));
    made.font.reset(hb_font_create(hbFace));
    hb_face_destroy(hbFace);
    hb_font_set_scale(made.font.get(), static_cast<int>(made.unitsPerEm), static_cast<int>(made.unitsPerEm));
    // Skia takes a span: an empty one asks how many axes there are.
    if (const int axes = face.getVariationDesignPosition({}); axes > 0) {
      std::vector<skia::SkFontArguments::VariationPosition::Coordinate> coordinates(static_cast<std::size_t>(axes));
      face.getVariationDesignPosition(coordinates);
      std::vector<hb_variation_t> variations;
      for (const auto &one : coordinates) {
        variations.push_back({one.axis, one.value});
      }
      hb_font_set_variations(made.font.get(), variations.data(), static_cast<unsigned>(variations.size()));
    }
    return &fHarfBuzz.emplace(&face, std::move(made)).first->second;
  }
  mutable TextCache<ShapedLine> fShaped;
  mutable std::unordered_map<const skia::SkTypeface *, HarfBuzzFont> fHarfBuzz;
#else
private:
#endif
  static constexpr std::size_t kMaxCachedWidths = 8192;

  [[nodiscard]] static bool isAscii(std::string_view text) {
    for (const char c : text) {
      if (static_cast<unsigned char>(c) >= 0x80) {
        return false;
      }
    }
    return true;
  }

  // Whether a face can draw the printable ASCII range, asked once per face.
  [[nodiscard]] bool asciiCovered(const skia::SkTypeface *face) const {
    if (face == nullptr) {
      return false;
    }
    const auto it = fAsciiCovered.find(face);
    if (it != fAsciiCovered.end()) {
      return it->second;
    }
    bool covered = true;
    for (std::int32_t cp = 0x20; cp < 0x7f; ++cp) {
      if (face->unicharToGlyph(cp) == 0) {
        covered = false;
        break;
      }
    }
    fAsciiCovered.emplace(face, covered);
    return covered;
  }

  [[nodiscard]] static std::uint64_t cacheKey(const skia::SkFont &font,
                                              std::string_view text) {
    std::uint64_t hash = std::hash<std::string_view>{}(text);
    hash ^=
        std::hash<const void *>{}(font.getTypeface()) * 0x9e3779b97f4a7c15ull;
    hash ^= static_cast<std::uint64_t>(font.getSize() * 64.0f) << 17;
    hash ^= static_cast<std::uint64_t>(font.isEmbolden()) << 61;
    // Linear metrics measure wider than hinted ones by a fraction of a pixel
    // per glyph, so a width cached under one is wrong under the other.
    hash ^= static_cast<std::uint64_t>(font.isLinearMetrics()) << 60;
    // A slanted face (italic, made by skewing) shapes the same glyphs but
    // draws them otherwise: its runs kept apart from the upright ones.
    hash ^= static_cast<std::uint64_t>(font.getSkewX() != 0.0f) << 59;
    return hash;
  }

  // -1 is the font the caller handed in; anything else indexes fFallbacks.
  [[nodiscard]] int faceFor(std::int32_t codepoint,
                            const skia::SkTypeface *base) const {
    if (base != nullptr && base->unicharToGlyph(codepoint) != 0) {
      return -1;
    }
    const auto cached = fCoverage.find(codepoint);
    if (cached != fCoverage.end()) {
      return cached->second;
    }
    int found = -1;
    for (std::size_t i = 0; i < fFallbacks.size(); ++i) {
      if (fFallbacks[i]->unicharToGlyph(codepoint) != 0) {
        found = static_cast<int>(i);
        break;
      }
    }
    // None of the faces had it: the system's fonts are asked for one that
    // has, which then stays among the fallbacks -- a box only where no font
    // on the machine has the character.
    if (found < 0 && fManager) {
      if (auto face = fManager->matchFamilyStyleCharacter(nullptr, skia::SkFontStyle(), nullptr, 0, codepoint);
          face && face->unicharToGlyph(codepoint) != 0) {
        fFallbacks.push_back(std::move(face));
        found = static_cast<int>(fFallbacks.size()) - 1;
      }
    }
    // A manager that cannot say which font has a character -- the one that
    // reads a directory of fonts says nothing, whatever is in it -- has
    // every one of its fonts asked instead, the first that has it taken.
    // Each character is asked about once: the answer is kept below.
    if (found < 0 && fManager) {
      if (!fEveryFaceListed) {
        fEveryFaceListed = true;
        for (int family = 0; family < fManager->countFamilies(); ++family) {
          const auto styles = fManager->createStyleSet(family);
          for (int style = 0; styles && style < styles->count(); ++style) {
            if (auto face = styles->createTypeface(style)) {
              fEveryFace.push_back(std::move(face));
            }
          }
        }
      }
      for (const auto &face : fEveryFace) {
        if (face->unicharToGlyph(codepoint) != 0) {
          fFallbacks.push_back(face);
          found = static_cast<int>(fFallbacks.size()) - 1;
          break;
        }
      }
    }
    fCoverage.emplace(codepoint, found);
    return found;
  }

  // Splits the text where the face has to change and hands each piece over.
  template <typename Fn>
  void forEachRun(const skia::SkFont &font, std::string_view text,
                  Fn &&fn) const {
    if (text.empty()) {
      return;
    }
    const skia::SkTypeface *base = font.getTypeface();
    std::size_t runStart = 0;
    int runFace = 0;
    bool haveRun = false;
    std::size_t i = 0;
    while (i < text.size()) {
      const std::size_t start = i;
      const std::int32_t cp = decodeUtf8(text, i);
      const int face = this->faceFor(cp, base);
      if (!haveRun) {
        runStart = start;
        runFace = face;
        haveRun = true;
        continue;
      }
      if (face != runFace) {
        fn(this->fontFor(font, runFace),
           text.substr(runStart, start - runStart));
        runStart = start;
        runFace = face;
      }
    }
    if (haveRun) {
      fn(this->fontFor(font, runFace), text.substr(runStart));
    }
  }

  [[nodiscard]] skia::SkFont fontFor(const skia::SkFont &font, int face) const {
    if (face < 0 || face >= static_cast<int>(fFallbacks.size())) {
      return font;
    }
    skia::SkFont out = font;
    out.setTypeface(fFallbacks[static_cast<std::size_t>(face)]);
    return out;
  }

  // Returns the codepoint at `i` and advances past it. Malformed input is
  // consumed a byte at a time so this always terminates.
  [[nodiscard]] static std::int32_t decodeUtf8(std::string_view text,
                                               std::size_t &i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    int extra = 0;
    std::int32_t cp = byte;
    if (byte >= 0xf0) {
      extra = 3;
      cp = byte & 0x07;
    } else if (byte >= 0xe0) {
      extra = 2;
      cp = byte & 0x0f;
    } else if (byte >= 0xc0) {
      extra = 1;
      cp = byte & 0x1f;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {
      ++i;
      return byte;
    }
    for (int n = 0; n < extra; ++n) {
      const auto cont =
          static_cast<unsigned char>(text[i + 1 + static_cast<std::size_t>(n)]);
      if ((cont & 0xc0) != 0x80) {
        ++i;
        return byte;
      }
      cp = (cp << 6) | (cont & 0x3f);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
  }

  // 'wght', the OpenType weight axis.
  static constexpr std::uint32_t kWeightAxis =
      (static_cast<std::uint32_t>('w') << 24) |
      (static_cast<std::uint32_t>('g') << 16) |
      (static_cast<std::uint32_t>('h') << 8) | static_cast<std::uint32_t>('t');

  skia::Sp<skia::SkTypeface> fPrimary;
  skia::Sp<skia::SkTypeface> fPrimaryBold;
  skia::Sp<skia::SkTypeface> fMonospace;
  // Mutable: a face found on demand while drawing is added to them.
  mutable std::vector<skia::Sp<skia::SkTypeface>> fFallbacks;
  mutable std::unordered_map<std::int32_t, int> fCoverage;
  skia::Sp<skia::SkFontMgr> fManager;
  // Every font the manager has, listed the first time one is needed.
  mutable bool fEveryFaceListed = false;
  mutable std::vector<skia::Sp<skia::SkTypeface>> fEveryFace;
  mutable std::unordered_map<const skia::SkTypeface *, bool> fAsciiCovered;
  mutable TextCache<float> fWidths;
};

inline FontStack &fonts() {
  // One per thread rather than one per process. It carries caches -- measured
  // widths, which typeface covers which codepoint -- and it mutates the SkFont
  // it is handed, so two threads drawing text through one of these would be
  // writing to the same caches at the same time. Only the render thread draws
  // today, so this costs nothing today; it is what lets a second thread draw
  // at all, which is what rendering a video export off the render thread
  // needs. Typefaces underneath are refcounted and shared, so the second
  // stack is a set of caches rather than a second copy of the fonts.
  static thread_local FontStack stack;
  return stack;
}

// One font for everything drawn through here, handed over by the app at
// startup. It lived in nodes::Text, which was fine until anything other than
// a Text node wanted to measure a string.
inline skia::SkFont *&defaultFont() {
  static skia::SkFont *font = nullptr;
  return font;
}

// The alpha a colour already carries, times the one the caller asked for.
// SkPaint::setAlphaf replaces rather than multiplies, so passing both without
// combining them turns a translucent colour opaque.
[[nodiscard]] inline float combinedAlpha(skia::SkColor color, float alpha) {
  return static_cast<float>((color >> 24) & 0xffu) / 255.0f * alpha;
}

// Colour4.Lighten: each channel scaled towards white, alpha left alone. What
// a hovered tab or row is drawn in.
[[nodiscard]] inline skia::SkColor lighten(skia::SkColor colour, float amount) {
  const auto channel = [amount](std::uint32_t v) {
    return static_cast<std::uint8_t>(
        std::min(255.0f, static_cast<float>(v) * (1.0f + amount)));
  };
  return skia::colorSetARGB(
      (colour >> 24) & 0xffu, channel((colour >> 16) & 0xffu),
      channel((colour >> 8) & 0xffu), channel(colour & 0xffu));
}

// FillMode.Fill: the image is cropped to the destination's aspect ratio
// rather than squashed into it, which is what a cover or a thumbnail wants
// and what drawImageRect will not do on its own.
inline void imageFilled(skia::SkCanvas *canvas, const skia::SkImage *image,
                        const skia::SkRect &dst, float alpha = 1.0f) {
  if (canvas == nullptr || image == nullptr) {
    return;
  }
  const float iw = static_cast<float>(image->width());
  const float ih = static_cast<float>(image->height());
  if (iw <= 0.0f || ih <= 0.0f) {
    return;
  }
  const float scale = std::max(dst.width() / iw, dst.height() / ih);
  const float srcW = dst.width() / scale;
  const float srcH = dst.height() / scale;
  const skia::SkRect src = skia::SkRect::MakeXYWH(
      (iw - srcW) * 0.5f, (ih - srcH) * 0.5f, srcW, srcH);
  skia::SkPaint p;
  p.setAlphaf(alpha);
  canvas->drawImageRect(
      image, src, dst, skia::SkSamplingOptions(skia::SkFilterMode::kLinear),
      alpha < 1.0f ? &p : nullptr, skia::SkCanvas::kStrict_SrcRectConstraint);
}

// Text whose size is animating -- a logo on the beat, a judgement popping --
// has to be drawn without grid fitting and without rounded advances. Every
// outline snaps to the pixel grid at its own threshold as the size passes
// through it, and every advance rounds to a whole pixel at its own, so the
// letters stop moving together: one jumps while its neighbours stay, which
// reads as a single twitching letter rather than as text being resampled.
//
// Held for the duration of the draw and put back afterwards, because static
// text wants exactly the opposite -- grid fitting is what makes a label at
// 11 points legible on a screen with no pixels to spare.
class SmoothScaling {
public:
  explicit SmoothScaling(skia::SkFont &font)
      : fFont(&font), fSubpixel(font.isSubpixel()),
        fLinearMetrics(font.isLinearMetrics()), fHinting(font.getHinting()) {
    font.setSubpixel(true);
    font.setLinearMetrics(true);
    font.setHinting(skia::kNoHinting);
  }
  ~SmoothScaling() {
    fFont->setSubpixel(fSubpixel);
    fFont->setLinearMetrics(fLinearMetrics);
    fFont->setHinting(fHinting);
  }
  SmoothScaling(const SmoothScaling &) = delete;
  SmoothScaling &operator=(const SmoothScaling &) = delete;

private:
  skia::SkFont *fFont;
  bool fSubpixel;
  bool fLinearMetrics;
  skia::SkFontHinting fHinting;
};

// A two-stop gradient between two points, in one draw.
//
// Skia m148 spells this SkGradient plus a free function: the colours and the
// tiling are a SkGradient::Colors, how to interpolate between them is a
// SkGradient::Interpolation, and SkShaders::LinearGradient turns the pair and
// two points into a shader. The header is SkGradient.h -- SkGradientShader.h
// and SkGradientShader::MakeLinear are both gone.
inline void linearGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                           skia::SkPoint from, skia::SkPoint to,
                           skia::SkColor start, skia::SkColor end,
                           float alpha = 1.0f) {
  if (canvas == nullptr || rect.isEmpty()) {
    return;
  }
  const skia::SkPoint points[2] = {from, to};
  const skia::SkColor4f colours[2] = {skia::SkColor4f::FromColor(start),
                                      skia::SkColor4f::FromColor(end)};
  const skia::SkGradient gradient(
      skia::SkGradient::Colors(colours, skia::SkTileMode::kClamp),
      skia::SkGradient::Interpolation{});
  skia::SkPaint p;
  p.setAntiAlias(true);
  p.setAlphaf(alpha);
  p.setShader(skia::LinearGradient(points, gradient));
  canvas->drawRect(rect, p);
}

inline void verticalGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                             skia::SkColor top, skia::SkColor bottom,
                             float alpha = 1.0f) {
  linearGradient(canvas, rect, {rect.fLeft, rect.fTop},
                 {rect.fLeft, rect.fBottom}, top, bottom, alpha);
}

inline void horizontalGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                               skia::SkColor left, skia::SkColor right,
                               float alpha = 1.0f) {
  linearGradient(canvas, rect, {rect.fLeft, rect.fTop},
                 {rect.fRight, rect.fTop}, left, right, alpha);
}

// ---- Drawing helpers -----------------------------------------------------
//
// A thin wrapper around the canvas and the shared font, so screens do not
// repeat paint setup. Holds no state of its own.
// How a run of text is drawn: its weight and its slant.
struct TextFace {
  bool bold = false;
  bool italic = false;
  friend bool operator==(const TextFace &, const TextFace &) = default;
};
// The slant of an italic made from an upright face: about 12 degrees.
inline constexpr float kItalicSkew = -0.21f;

class Painter {
public:
  // Drawing in the monospace face, where `monospace` is asked and the stack
  // has one: code.
  Painter(skia::SkCanvas *canvas, skia::SkFont &font, bool monospace = false)
      : fCanvas(canvas), fFont(&font), fMonospace(monospace) {}

  [[nodiscard]] skia::SkCanvas *canvas() const noexcept { return fCanvas; }

  void fillRounded(const skia::SkRect &rect, float radius, skia::SkColor color,
                   float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fCanvas->drawRRect(skia::SkRRect::MakeRectXY(rect, radius, radius), p);
  }

  void strokeRounded(const skia::SkRect &rect, float radius,
                     skia::SkColor color, float width,
                     float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    p.setStyle(skia::kStrokeStyle);
    p.setStrokeWidth(width);
    fCanvas->drawRRect(skia::SkRRect::MakeRectXY(rect, radius, radius), p);
  }

  void fillRect(const skia::SkRect &rect, skia::SkColor color,
                float alpha = 1.0f) const {
    skia::SkPaint p;
    // Antialiased like everything else here. It was not, which only showed
    // once the listing's own rect() -- which was -- started coming through
    // this one: a bar at a fractional scroll offset picked up a hard edge.
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fCanvas->drawRect(rect, p);
  }

  void circle(float cx, float cy, float r, skia::SkColor color,
              float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fCanvas->drawCircle(cx, cy, r, p);
  }

  // bold sits after alpha rather than beside size, where it would read
  // better, because these had callers before they could embolden anything
  // and moving it would have turned every alpha silently into a weight.
  [[nodiscard]] float measure(const std::string &text, float size,
                              bool bold = false) const {
    fFont->setSize(size);
    this->face(bold);
    const float width = fonts().measure(*fFont, text);
    fonts().applyWeight(*fFont, false);
    return width;
  }

  void text(const std::string &str, float x, float y, float size,
            skia::SkColor color, float alpha = 1.0f, bool bold = false) const {
    fFont->setSize(size);
    this->face(bold);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fonts().draw(fCanvas, *fFont, str, x, y, p);
    fonts().applyWeight(*fFont, false);
  }

  // Text in a face of its own: bold, italic or both -- an editor's styled
  // runs. Italic is the face slanted, as Qt slants a face without one.
  [[nodiscard]] float measure(const std::string &text, float size, TextFace face) const {
    fFont->setSkewX(face.italic ? kItalicSkew : 0.0f);
    const float width = this->measure(text, size, face.bold);
    fFont->setSkewX(0.0f);
    return width;
  }
  void text(const std::string &str, float x, float y, float size, skia::SkColor color, float alpha,
            TextFace face) const {
    fFont->setSkewX(face.italic ? kItalicSkew : 0.0f);
    this->text(str, x, y, size, color, alpha, face.bold);
    fFont->setSkewX(0.0f);
  }

  void textClipped(const std::string &str, float x, float y, float maxW,
                   float size, skia::SkColor color, float alpha = 1.0f,
                   bool bold = false) const {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(x), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    this->text(str, x, y, size, color, alpha, bold);
    fCanvas->restore();
  }

  // Centred, but clipped to a width so a long title cannot run past a panel.
  void textCenteredClipped(const std::string &str, float cx, float y,
                           float maxW, float size, skia::SkColor color,
                           float alpha = 1.0f, bool bold = false) const {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(cx - maxW * 0.5f), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    this->textCentered(str, cx, y, size, color, alpha, bold);
    fCanvas->restore();
  }

  void textCentered(const std::string &str, float cx, float y, float size,
                    skia::SkColor color, float alpha = 1.0f,
                    bool bold = false) const {
    const float w = this->measure(str, size, bold);
    this->text(str, cx - w * 0.5f, y, size, color, alpha, bold);
  }

  void imageFilled(const skia::SkImage *image, const skia::SkRect &dst,
                   float alpha = 1.0f) const {
    skiff::paint::imageFilled(fCanvas, image, dst, alpha);
  }

  void verticalGradient(const skia::SkRect &rect, skia::SkColor top,
                        skia::SkColor bottom, float alpha = 1.0f) const {
    skiff::paint::verticalGradient(fCanvas, rect, top, bottom, alpha);
  }

  // The longest prefix of `str` that fits in `width`, with a single-character
  // ellipsis where something was dropped. Cut on UTF-8 boundaries, so a
  // multi-byte character is never halved.
  [[nodiscard]] std::string elide(const std::string &str, float width,
                                  float size, bool bold = false) const {
    if (width <= 0.0f) {
      return {};
    }
    if (this->measure(str, size, bold) <= width) {
      return str;
    }
    static constexpr std::string_view kEllipsis = "\u2026";
    const float room =
        width - this->measure(std::string(kEllipsis), size, bold);
    if (room <= 0.0f) {
      return std::string(kEllipsis);
    }
    // Binary search on the cut, snapped outwards to a character boundary.
    const auto boundary = [&str](std::size_t at) {
      while (at > 0 && (static_cast<unsigned char>(str[at]) & 0xc0u) == 0x80u) {
        --at;
      }
      return at;
    };
    std::size_t low = 0;
    std::size_t high = str.size();
    while (low < high) {
      const std::size_t mid = boundary(low + (high - low + 1) / 2);
      if (mid == low) {
        break;
      }
      if (this->measure(str.substr(0, mid), size, bold) <= room) {
        low = mid;
      } else {
        high = mid - 1;
      }
    }
    return str.substr(0, boundary(low)) + std::string(kEllipsis);
  }

  // The text broken into lines that each fit in `width`, split at spaces. A
  // word longer than the width gets a line of its own and overhangs, which is
  // what a browser does with an unbreakable string.
  // Lines of at most `width`: a newline starts one, words go on while they
  // fit, and a word wider than a line is broken where it has to be.
  [[nodiscard]] std::vector<std::string> wrap(const std::string &str,
                                              float width, float size,
                                              bool bold = false) const {
    std::vector<std::string> lines;
    if (str.empty()) {
      return lines;
    }
#ifdef SKIFF_TEXT_SHAPING
    // Lines end where UAX #14 lets them -- after a space, between two CJK
    // characters, after a hyphen -- and must where it says so. A piece wider
    // than a line alone is cut between characters.
    if (width > 0.0f) {
      const auto trimmed = [](std::string_view line) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\n' || line.back() == '\r')) {
          line.remove_suffix(1);
        }
        return std::string(line);
      };
      const std::string_view all(str);
      std::size_t lineStart = 0;
      std::size_t lineEnd = 0;  // where the line as it stands ends
      float lineWidth = 0.0f;   // its pieces' widths, spaces and all
      // Each piece measured once, on its own: a line's width is its pieces'.
      for (auto piece : all | alef::line_breaks) {
        const std::string_view text(piece.text.begin(), piece.text.end());
        const std::size_t first = static_cast<std::size_t>(text.data() - all.data());
        const std::size_t last = first + text.size();
        const float whole = this->measure(std::string(text), size, bold);
        const float bare = this->measure(trimmed(text), size, bold);
        if (lineEnd > lineStart && lineWidth + bare > width) {
          lines.push_back(trimmed(all.substr(lineStart, lineEnd - lineStart)));
          lineStart = first;
          lineWidth = 0.0f;
        }
        // Alone and still too wide: as much of it to a line as fits, cut
        // between characters.
        if (bare > width) {
          std::size_t from = first;
          float taken = 0.0f;
          for (auto cluster : text | alef::graphemes) {
            const std::string_view one(cluster.begin(), cluster.end());
            const float w = this->measure(std::string(one), size, bold);
            const std::size_t at = static_cast<std::size_t>(one.data() - all.data());
            if (at > from && taken + w > width) {
              lines.push_back(std::string(all.substr(from, at - from)));
              from = at;
              taken = 0.0f;
            }
            taken += w;
          }
          lineStart = from;
          lineWidth = taken;
        } else {
          lineWidth += whole;
        }
        lineEnd = last;
        if (piece.mandatory) {
          lines.push_back(trimmed(all.substr(lineStart, lineEnd - lineStart)));
          lineStart = lineEnd;
          lineWidth = 0.0f;
        }
      }
      if (lineEnd > lineStart || lines.empty()) {
        lines.push_back(trimmed(all.substr(lineStart, lineEnd - lineStart)));
      }
      return lines;
    }
#endif
    const auto next = [&](std::size_t at) {
      ++at;
      while (at < str.size() &&
             (static_cast<unsigned char>(str[at]) & 0xC0u) == 0x80u) {
        ++at;
      }
      return at;
    };
    std::size_t start = 0;
    while (true) {
      const std::size_t newline = str.find('\n', start);
      const std::string paragraph =
          str.substr(start, newline == std::string::npos ? std::string::npos
                                                         : newline - start);
      if (width <= 0.0f) {
        lines.push_back(paragraph);
      } else {
        std::string line;
        std::size_t at = 0;
        while (at <= paragraph.size()) {
          const std::size_t space = paragraph.find(' ', at);
          std::string word = paragraph.substr(
              at, space == std::string::npos ? std::string::npos : space - at);
          const std::string candidate = line.empty() ? word : line + " " + word;
          if (this->measure(candidate, size, bold) <= width) {
            line = candidate;
          } else {
            if (!line.empty()) {
              lines.push_back(line);
              line.clear();
            }
            // Too wide alone: as much of it to a line as fits.
            while (this->measure(word, size, bold) > width) {
              std::size_t cut = next(0);
              for (std::size_t end = next(cut);
                   cut < word.size() &&
                   this->measure(word.substr(0, end), size, bold) <= width;
                   end = next(end)) {
                cut = end;
                if (end >= word.size()) {
                  break;
                }
              }
              if (cut >= word.size()) {
                break;
              }
              lines.push_back(word.substr(0, cut));
              word = word.substr(cut);
            }
            line = word;
          }
          if (space == std::string::npos) {
            break;
          }
          at = space + 1;
        }
        lines.push_back(line);
      }
      if (newline == std::string::npos) {
        break;
      }
      start = newline + 1;
    }
    return lines;
  }

  // Text that says it was cut rather than stopping mid-glyph.
  void textElided(const std::string &str, float x, float y, float maxW,
                  float size, skia::SkColor color, float alpha = 1.0f,
                  bool bold = false) const {
    this->text(this->elide(str, maxW, size, bold), x, y, size, color, alpha,
               bold);
  }

  void textElidedIn(const skia::SkRect &box, const std::string &str, float size,
                    skia::SkColor color, float alpha = 1.0f, bool bold = false,
                    float inset = 0.0f) const {
    this->textElided(str, box.fLeft + inset, this->middleBaseline(box, size),
                     box.width() - inset * 2.0f, size, color, alpha, bold);
  }

  // The baseline that puts a line of this size in the middle of a box, from
  // the font's ascent and descent. The alternative is a constant added to the
  // middle, which has to be picked per size and per face by eye.
  [[nodiscard]] float middleBaseline(const skia::SkRect &box,
                                     float size) const {
    fFont->setSize(size);
    skia::SkFontMetrics metrics;
    fFont->getMetrics(&metrics);
    return box.centerY() - (metrics.fAscent + metrics.fDescent) * 0.5f;
  }

  // Text in a box: down the middle vertically, and clipped to the box so a
  // long string cannot run out of it. `inset` is taken off both ends.
  void textIn(const skia::SkRect &box, const std::string &str, float size,
              skia::SkColor color, float alpha = 1.0f, bool bold = false,
              float inset = 0.0f) const {
    this->textClipped(str, box.fLeft + inset, this->middleBaseline(box, size),
                      box.width() - inset * 2.0f, size, color, alpha, bold);
  }

  // The same, centred across the box as well.
  void textCentredIn(const skia::SkRect &box, const std::string &str,
                     float size, skia::SkColor color, float alpha = 1.0f,
                     bool bold = false, float inset = 0.0f) const {
    this->textCenteredClipped(
        str, box.centerX(), this->middleBaseline(box, size),
        box.width() - inset * 2.0f, size, color, alpha, bold);
  }

  // Text with a soft shadow, the way lazer draws judgements and HUD numbers.
  void textShadowed(const std::string &str, float cx, float y, float size,
                    skia::SkColor color, float alpha = 1.0f) const {
    const float w = this->measure(str, size);
    fFont->setSize(size);
    skia::SkPaint shadow;
    shadow.setAntiAlias(true);
    shadow.setColor(skia::colorSetARGB(255, 0, 0, 0));
    shadow.setAlphaf(alpha * 0.45f);
    fonts().draw(fCanvas, *fFont, str, cx - w * 0.5f + size * 0.045f,
                 y + size * 0.05f, shadow);
    this->text(str, cx - w * 0.5f, y, size, color, alpha);
  }

private:
  skia::SkCanvas *fCanvas;
  skia::SkFont *fFont;
  // Code: drawn in the stack's monospace face.
  bool fMonospace = false;
  // The face for a run: the monospace one for code, where there is one; the
  // weight's own face for the rest.
  void face(bool bold) const {
    if (fMonospace && fonts().applyMonospace(*fFont, bold)) {
      return;
    }
    fonts().applyWeight(*fFont, bold);
  }
};

} // namespace skiff::paint
