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
[[nodiscard]] Handle create(std::string_view tag);
void destroy(Handle element);
void append(Handle parent, Handle child);
void text(Handle element, std::string_view text);
void attr(Handle element, std::string_view name, std::string_view value);
void dropAttr(Handle element, std::string_view name);
void style(Handle element, std::string_view property, std::string_view value);
// A control's value and a checkbox's state: set as the model says, not as
// the pointer left them.
void value(Handle element, std::string_view value);
void checked(Handle element, bool on);
// Events the page's sink is sent: by name (click, input, change), with the
// element they happened on and its value then.
void listen(Handle element, std::string_view event);
using Sink = void (*)(void *context, Handle target, std::string_view event, std::string_view value);
void sink(Sink sink, void *context);

// What the page under `element` is, as HTML -- the fake's for a test; the
// browser's innerHTML under Emscripten.
[[nodiscard]] std::string dump(Handle element);
// An event made by hand, delivered as the page's would be: a test's click,
// a value typed. Under Emscripten, dispatched to the element.
void fire(Handle target, std::string_view event, std::string_view value);
// Everything forgotten: for a test to start a page from nothing.
void reset();

} // namespace skiff::web::dom
