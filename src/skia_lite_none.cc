// skiff's stand-in for Skia, where there is no browser: what a canvas draws
// goes nowhere, and a text is as wide as its face's characters say.
module skia;

import std;

namespace skia::lite {
void draw(Target, const SkMatrix &, const std::vector<float> &, const SkPaint &) {}
void clip(Target, const SkMatrix &, const std::vector<float> &) {}
void save(Target) {}
void restore(Target) {}
void layer(Target, float) {}
void text(Target, const SkMatrix &, std::string_view, float, float, float, bool, const SkPaint &) {}
void clear(Target, SkColor) {}
Target attach(std::string_view, int, int, float) { return 0; }
std::optional<float> measure(std::string_view, float, bool) { return std::nullopt; }
std::optional<std::pair<float, float>> metrics(float, bool) { return std::nullopt; }
} // namespace skia::lite
