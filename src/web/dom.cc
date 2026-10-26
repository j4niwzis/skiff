// skiff.web:dom -- the page, as the web backend reaches it: elements by a
// handle, made, placed, written to and listened on. One implementation per
// build: the browser's document under Emscripten (dom_em.cc), a tree kept in
// memory everywhere else (dom_fake.cc), which a test reads back as HTML and
// sends events into by hand. Nothing here is a node: the nodes (skiff.web)
// say what elements they are, and this is how they say it.
export module skiff.web:dom;

import std;

export namespace skiff::web::dom {

using Handle = std::uint32_t;
inline constexpr Handle kNone = 0;

// Where a page is mounted: the document's body, or the element with id
// "skiff" where the page has one.
[[nodiscard]] Handle root();
// An element by its tag; SVG's (svg, g, polygon, line, circle, path...) in SVG's namespace.
[[nodiscard]] Handle create(std::string_view tag);
void destroy(Handle element);
void append(Handle parent, Handle child);
// An element the document has already -- a page written beforehand -- at a
// place among its parent's elements (none past them), how many there are,
// and an element emptied of what it holds: what a page taken over reads.
[[nodiscard]] Handle child(Handle parent, std::size_t index);
[[nodiscard]] std::size_t children(Handle parent);
// The last element of the document with an attribute of that value: one the
// server wrote, found by the browser's part of the program (none without a
// browser but those made here).
[[nodiscard]] Handle marked(std::string_view attribute, std::string_view value);
void clear(Handle element);
void text(Handle element, std::string_view text);
void attr(Handle element, std::string_view name, std::string_view value);
void dropAttr(Handle element, std::string_view name);
void style(Handle element, std::string_view property, std::string_view value);
// A control's value and a checkbox's state: set as the model says, not as
// the pointer left them.
void value(Handle element, std::string_view value);
void checked(Handle element, bool on);
// Events listened on, by name (click, input, change). They reach the page
// through skiff_web_event(handle, event, value), which the program defines
// for the page it has -- extern "C", for the document's listeners to call --
// forwarding to Page::event: no pointer kept anywhere, the page's type known
// where the event arrives. A test calls Page::event itself (web::fire).
// A pointer event on a canvas or an svg (click, contextmenu) carries where
// and which: its value is "x,y,button", in the element's CSS pixels.
void listen(Handle element, std::string_view event);

// A canvas element drawn on, in CSS pixels (the device's ratio is the
// browser's business): a path begun, extended, closed, filled or stroked;
// a circle; a line of text centred on a point. Colours are 0xAARRGGBB.
using Colour = std::uint32_t;
void canvasSize(Handle canvas, int width, int height);
void canvasClear(Handle canvas, Colour colour);
void canvasBegin(Handle canvas);
void canvasMoveTo(Handle canvas, float x, float y);
void canvasLineTo(Handle canvas, float x, float y);
void canvasClose(Handle canvas);
void canvasFill(Handle canvas, Colour colour);
void canvasStroke(Handle canvas, float width, Colour colour);
void canvasCircle(Handle canvas, float cx, float cy, float r, Colour colour);
void canvasText(Handle canvas, float x, float y, float size, Colour colour, std::string_view text);
// What was drawn on a canvas, as the in-memory document keeps it: a word per
// call ("size clear begin moveto lineto close fill ..."). Nothing in a browser.
[[nodiscard]] std::string drawn(Handle canvas);
// A picture's bytes as a URL the page can show: an object URL (a fake one
// without a browser).
[[nodiscard]] std::string objectUrl(std::span<const unsigned char> bytes, std::string_view mime);
// An <img>'s picture, as pixels (RGBA, not premultiplied, row after row),
// and the key it is known by: given where the document takes pixels (an
// engine's, Blitz's); kept as the key alone elsewhere.
void picture(Handle element, const std::vector<std::uint8_t> &rgba, int width, int height, std::string_view key);
// Scrolled to an offset along its way, or -- below zero -- to its end.
void scrollTo(Handle element, float offset);

// What the page under `element` is, as HTML -- the fake's for a test; the
// browser's innerHTML under Emscripten.
[[nodiscard]] std::string dump(Handle element);
// An event made by hand: what the document would have done to the element
// before telling anyone -- a checkbox toggled, a value typed. Under
// Emscripten, dispatched to the element, so its listener runs.
void fire(Handle target, std::string_view event, std::string_view value);
// Everything forgotten: for a test to start a page from nothing.
void reset();

} // namespace skiff::web::dom
