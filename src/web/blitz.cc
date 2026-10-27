// skiff.web:blitz -- the page in Blitz (skiff-blitz, Rust: Stylo's styles,
// Taffy's layout, Vello's painting; no JavaScript), as the host that shows it
// reaches it: the viewport, styles and layout brought up to date, the page
// painted into pixels, what is at a point, a wheel at a point. Its document
// is what skiff.web's dom writes to (dom_blitz.cc), a call a mutation.
export module skiff.web:blitz;

import std;
import :dom;

export namespace skiff::web::blitz {

// The viewport, in physical pixels, how many of them a CSS pixel is, and
// how far the page is zoomed (the interface's scale).
void viewport(std::uint32_t width, std::uint32_t height, float scale, float zoom);
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
enum class editing_key { backspace = 1, erase, enter, left, right, up, down, home, end, tab, select_all, copy, cut, paste, undo, redo };
enum class font_face { regular, semibold, emoji };
void fontData(const unsigned char *bytes, std::size_t size, font_face face);
struct key_modifiers {
  bool shift = false, control = false, alt = false, meta = false;
};
void key(editing_key which, key_modifiers held);
// Where the engine put an element, in the viewport's CSS pixels.
struct box {
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};
[[nodiscard]] std::optional<box> boxOf(dom::Handle element);
// The element with the engine's focus (a field typed into), or none.
[[nodiscard]] dom::Handle focused();
// What the engine asks of its host: text it copied (to be put on the
// clipboard), a paste (answered with the clipboard's text), where an input
// method's window goes, the pointer's shape.
[[nodiscard]] std::optional<std::string> takeCopied();
[[nodiscard]] bool takePasteRequest();
void paste(std::string_view text);
[[nodiscard]] std::optional<box> takeImeArea();
enum class cursor { arrow, hand, text, resize_across, resize_down, not_allowed, wait };
[[nodiscard]] cursor cursorShape();
// The fields the engine edited since, oldest first: each element and its
// value now.
[[nodiscard]] std::vector<std::pair<dom::Handle, std::string>> takeEdits();

} // namespace skiff::web::blitz
