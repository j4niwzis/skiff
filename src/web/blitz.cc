// skiff.web:blitz -- the page in Blitz (skiff-blitz, Rust: Stylo's styles,
// Taffy's layout, Vello's painting; no JavaScript), as the host that shows it
// reaches it: the viewport, styles and layout brought up to date, the page
// painted into pixels, what is at a point, a wheel at a point. Its document
// is what skiff.web's dom writes to (dom_blitz.cc), a call a mutation.
export module skiff.web:blitz;

import std;
import :dom;

export namespace skiff::web::blitz {

// The viewport, in physical pixels, and how many of them a CSS pixel is.
void viewport(std::uint32_t width, std::uint32_t height, float scale);
// The page's own rules, beside each element's style.
void addCss(std::string_view css);
// Styles and layout brought up to date with what was changed since.
void resolve(double nowMs);
// Painted into `out`: as many rows of `stride` bytes as the viewport is
// high, RGBA.
void paint(std::uint8_t *out, std::size_t stride);
// The element the dom made that is at a point (CSS pixels), or holds what is
// there: none where it is none of theirs.
[[nodiscard]] dom::Handle at(float x, float y);
// A wheel at a point: whether anything scrolled.
bool scroll(float x, float y, double dx, double dy);

} // namespace skiff::web::blitz
