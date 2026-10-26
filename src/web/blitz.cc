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
// How far an element is scrolled down by the engine, in CSS pixels.
[[nodiscard]] float scrolled(dom::Handle element);

// The pointer and the keyboard, as the engine takes them for its own fields
// (focus, caret, selection, editing): the pointer moved, pressed or let go
// at a point (CSS pixels); text typed, as the system commits it; a key that
// edits.
enum class pointer { moved, pressed, released };
void point(pointer what, float x, float y, int button);
void commitText(std::string_view text);
enum class editing_key { backspace = 1, erase, enter, left, right, up, down, home, end, tab };
struct key_modifiers {
  bool shift = false, control = false, alt = false, meta = false;
};
void key(editing_key which, key_modifiers held);
// The fields the engine edited since, oldest first: each element and its
// value now.
[[nodiscard]] std::vector<std::pair<dom::Handle, std::string>> takeEdits();

} // namespace skiff::web::blitz
