// skiff.web:dom, without a browser: the tree of elements kept in memory
// (skiff.web:memory), read back as HTML. What a test mounts a page into, and
// sends events into.
module skiff.web;

import std;
import :memory;

namespace skiff::web::dom {
Handle root() { return memory::root(); }
Handle create(std::string_view tag) { return memory::create(tag); }
void destroy(Handle h) { memory::destroy(h); }
void append(Handle parent, Handle child) { memory::append(parent, child); }
Handle child(Handle parent, std::size_t index) { return memory::child(parent, index); }
std::size_t children(Handle parent) { return memory::children(parent); }
Handle marked(std::string_view attribute, std::string_view value) { return memory::marked(attribute, value); }
void clear(Handle h) { memory::clear(h); }
void text(Handle h, std::string_view t) { memory::text(h, t); }
void attr(Handle h, std::string_view n, std::string_view v) { memory::attr(h, n, v); }
void dropAttr(Handle h, std::string_view n) { memory::dropAttr(h, n); }
void style(Handle h, std::string_view p, std::string_view v) { memory::style(h, p, v); }
void value(Handle h, std::string_view v) { memory::value(h, v); }
void checked(Handle h, bool on) { memory::checked(h, on); }
void listen(Handle h, std::string_view event) { memory::listen(h, event); }
void canvasSize(Handle h, int width, int height) { memory::canvasSize(h, width, height); }
void canvasClear(Handle h, Colour c) { memory::canvasClear(h, c); }
void canvasBegin(Handle h) { memory::canvasBegin(h); }
void canvasMoveTo(Handle h, float x, float y) { memory::canvasMoveTo(h, x, y); }
void canvasLineTo(Handle h, float x, float y) { memory::canvasLineTo(h, x, y); }
void canvasClose(Handle h) { memory::canvasClose(h); }
void canvasFill(Handle h, Colour c) { memory::canvasFill(h, c); }
void canvasStroke(Handle h, float width, Colour c) { memory::canvasStroke(h, width, c); }
void canvasCircle(Handle h, float cx, float cy, float r, Colour c) { memory::canvasCircle(h, cx, cy, r, c); }
void canvasText(Handle h, float x, float y, float size, Colour c, std::string_view t) { memory::canvasText(h, x, y, size, c, t); }
std::string drawn(Handle h) { return memory::drawn(h); }
std::string objectUrl(std::span<const unsigned char> bytes, std::string_view mime) { return memory::objectUrl(bytes, mime); }
void scrollTo(Handle h, float offset) { memory::scrollTo(h, offset); }
std::string dump(Handle h) { return memory::dump(h); }
void fire(Handle target, std::string_view event, std::string_view v) { memory::fire(target, event, v); }
void reset() { memory::reset(); }
} // namespace skiff::web::dom
