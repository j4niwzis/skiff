// skiff.web:dom in Blitz: each call one mutation of a Blitz document
// (skiff-blitz's C API), beside the tree kept in memory (skiff.web:memory)
// that the page is read back from as HTML. A handle is the memory's; Blitz's
// node for it is kept by it.
module;
#include <cstdint>
#include <cstddef>
extern "C" {
// skiff-blitz (src/web/blitz): a page, opaque here.
struct skiff_blitz_page;
skiff_blitz_page *skiff_blitz_new(std::uint32_t width, std::uint32_t height, float scale);
std::uint64_t skiff_blitz_body(skiff_blitz_page *page);
void skiff_blitz_add_css(skiff_blitz_page *page, const char *css, std::size_t len);
void skiff_blitz_add_font(skiff_blitz_page *page, const unsigned char *bytes, std::size_t len, std::uint32_t face);
std::uint64_t skiff_blitz_create_element(skiff_blitz_page *page, const char *tag, std::size_t len);
std::uint64_t skiff_blitz_create_text(skiff_blitz_page *page, const char *text, std::size_t len);
void skiff_blitz_set_text(skiff_blitz_page *page, std::uint64_t node, const char *text, std::size_t len);
void skiff_blitz_append(skiff_blitz_page *page, std::uint64_t parent, std::uint64_t child);
void skiff_blitz_remove(skiff_blitz_page *page, std::uint64_t node);
void skiff_blitz_set_attribute(skiff_blitz_page *page, std::uint64_t node, const char *name, std::size_t name_len, const char *value,
                               std::size_t value_len);
void skiff_blitz_clear_attribute(skiff_blitz_page *page, std::uint64_t node, const char *name, std::size_t name_len);
void skiff_blitz_set_value(skiff_blitz_page *page, std::uint64_t node, const char *value, std::size_t len);
void skiff_blitz_set_style(skiff_blitz_page *page, std::uint64_t node, const char *name, std::size_t name_len, const char *value,
                           std::size_t value_len);
std::uint64_t skiff_blitz_parent(skiff_blitz_page *page, std::uint64_t node);
void skiff_blitz_set_viewport(skiff_blitz_page *page, std::uint32_t width, std::uint32_t height, float scale, float zoom);
void skiff_blitz_resolve(skiff_blitz_page *page, double now_ms);
void skiff_blitz_paint(skiff_blitz_page *page, std::uint8_t *out, std::size_t stride);
std::uint64_t skiff_blitz_hit(skiff_blitz_page *page, float x, float y);
bool skiff_blitz_scroll_at(skiff_blitz_page *page, float x, float y, double dx, double dy);
double skiff_blitz_scroll_offset(skiff_blitz_page *page, std::uint64_t node);
void skiff_blitz_scroll_to(skiff_blitz_page *page, std::uint64_t node, double y);
void skiff_blitz_set_picture(skiff_blitz_page *page, std::uint64_t node, const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height,
                             const char *key, std::size_t key_len);
void skiff_blitz_pointer(skiff_blitz_page *page, std::uint32_t what, float x, float y, std::uint32_t button);
void skiff_blitz_commit_text(skiff_blitz_page *page, const char *text, std::size_t len);
void skiff_blitz_key(skiff_blitz_page *page, std::uint32_t which, std::uint32_t mods);
bool skiff_blitz_next_input(skiff_blitz_page *page, std::uint64_t *node, const char **value, std::size_t *len);
std::uint64_t skiff_blitz_focused(skiff_blitz_page *page);
bool skiff_blitz_rect(skiff_blitz_page *page, std::uint64_t node, float *x, float *y, float *width, float *height);
bool skiff_blitz_take_copied(skiff_blitz_page *page, const char **value, std::size_t *len);
bool skiff_blitz_take_paste_request(skiff_blitz_page *page);
void skiff_blitz_paste(skiff_blitz_page *page, const char *text, std::size_t len);
std::uint32_t skiff_blitz_cursor(skiff_blitz_page *page);
bool skiff_blitz_take_ime_area(skiff_blitz_page *page, float *x, float *y, float *width, float *height);
}
module skiff.web;

import std;
import :memory;
import :blitz;

namespace skiff::web {
namespace {
// The Blitz page, made as it is first written to; and each handle's node in
// it, and its text's, where it has text.
struct Kept {
  skiff_blitz_page *page = nullptr;
  std::vector<std::uint64_t> nodes;
  std::vector<std::uint64_t> texts;
  std::unordered_map<std::uint64_t, dom::Handle> handles;
};
Kept &kept() {
  static Kept all;
  return all;
}
skiff_blitz_page *page() {
  Kept &k = kept();
  if (k.page == nullptr) {
    k.page = skiff_blitz_new(800, 600, 1.0f);
    k.nodes = {0, skiff_blitz_body(k.page)};
    k.texts = {0, 0};
    k.handles.insert_or_assign(k.nodes[1], memory::root());
  }
  return k.page;
}
std::uint64_t nodeOf(dom::Handle h) {
  (void)page();
  const auto &nodes = kept().nodes;
  return h < nodes.size() ? nodes[h] : 0;
}
} // namespace

namespace dom {
Handle root() {
  (void)page();
  return memory::root();
}
Handle create(std::string_view tag) {
  const Handle h = memory::create(tag);
  const std::uint64_t node = skiff_blitz_create_element(page(), tag.data(), tag.size());
  Kept &k = kept();
  if (k.nodes.size() <= h) {
    k.nodes.resize(h + 1, 0);
    k.texts.resize(h + 1, 0);
  }
  k.nodes[h] = node;
  k.handles.insert_or_assign(node, h);
  return h;
}
void destroy(Handle h) {
  memory::destroy(h);
  if (const std::uint64_t node = nodeOf(h); node != 0) {
    skiff_blitz_remove(page(), node);
    kept().handles.erase(node);
    kept().nodes[h] = 0;
  }
}
void append(Handle parent, Handle child) {
  memory::append(parent, child);
  if (nodeOf(parent) != 0 && nodeOf(child) != 0) skiff_blitz_append(page(), nodeOf(parent), nodeOf(child));
}
Handle child(Handle parent, std::size_t index) { return memory::child(parent, index); }
std::size_t children(Handle parent) { return memory::children(parent); }
Handle marked(std::string_view attribute, std::string_view value) { return memory::marked(attribute, value); }
void clear(Handle h) {
  for (std::size_t i = memory::children(h); i > 0; --i) destroy(memory::child(h, i - 1));
  memory::clear(h);
  text(h, "");
}
// Its text: a text node of its own, made as it is first given one, before
// what it holds.
void text(Handle h, std::string_view t) {
  memory::text(h, t);
  Kept &k = kept();
  if (nodeOf(h) == 0) return;
  if (k.texts[h] == 0) {
    if (t.empty()) return;
    k.texts[h] = skiff_blitz_create_text(page(), t.data(), t.size());
    skiff_blitz_append(page(), k.nodes[h], k.texts[h]);
    return;
  }
  skiff_blitz_set_text(page(), k.texts[h], t.data(), t.size());
}
void attr(Handle h, std::string_view n, std::string_view v) {
  memory::attr(h, n, v);
  if (nodeOf(h) != 0) skiff_blitz_set_attribute(page(), nodeOf(h), n.data(), n.size(), v.data(), v.size());
}
void dropAttr(Handle h, std::string_view n) {
  memory::dropAttr(h, n);
  if (nodeOf(h) != 0) skiff_blitz_clear_attribute(page(), nodeOf(h), n.data(), n.size());
}
void style(Handle h, std::string_view p, std::string_view v) {
  memory::style(h, p, v);
  if (nodeOf(h) != 0) skiff_blitz_set_style(page(), nodeOf(h), p.data(), p.size(), v.data(), v.size());
}
void value(Handle h, std::string_view v) {
  memory::value(h, v);
  if (nodeOf(h) != 0) skiff_blitz_set_value(page(), nodeOf(h), v.data(), v.size());
}
void checked(Handle h, bool on) {
  memory::checked(h, on);
  if (on) attr(h, "checked", "true");
  else dropAttr(h, "checked");
}
// Presses come from Blitz's hit test (blitz::at), not from listeners.
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
// Scrolled as the program says (a jump, its end): the engine's own scroll set,
// past its end where `offset` is below zero (the engine stops it there).
void scrollTo(Handle h, float offset) {
  memory::scrollTo(h, offset);
  if (nodeOf(h) != 0) skiff_blitz_scroll_to(page(), nodeOf(h), offset < 0.0f ? 1.0e9 : static_cast<double>(offset));
}
void picture(Handle h, const std::vector<std::uint8_t> &rgba, int width, int height, std::string_view key) {
  memory::picture(h, rgba, width, height, key);
  if (nodeOf(h) != 0 && rgba.size() >= static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u)
    skiff_blitz_set_picture(page(), nodeOf(h), rgba.data(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), key.data(),
                            key.size());
}
std::string dump(Handle h) { return memory::dump(h); }
void fire(Handle target, std::string_view event, std::string_view v) { memory::fire(target, event, v); }
void reset() { memory::reset(); }
} // namespace dom

namespace blitz {
void fontData(const unsigned char *bytes, std::size_t size, font_face face) {
  skiff_blitz_add_font(page(), bytes, size, static_cast<std::uint32_t>(face));
}
void viewport(std::uint32_t width, std::uint32_t height, float scale, float zoom) { skiff_blitz_set_viewport(page(), width, height, scale, zoom); }
std::optional<std::string> takeCopied() {
  const char *value = nullptr;
  std::size_t len = 0;
  if (!skiff_blitz_take_copied(page(), &value, &len)) return std::nullopt;
  return std::string(value, len);
}
bool takePasteRequest() { return skiff_blitz_take_paste_request(page()); }
void paste(std::string_view text) { skiff_blitz_paste(page(), text.data(), text.size()); }
std::optional<box> takeImeArea() {
  box out;
  if (!skiff_blitz_take_ime_area(page(), &out.x, &out.y, &out.width, &out.height)) return std::nullopt;
  return out;
}
cursor cursorShape() {
  using enum cursor;
  switch (skiff_blitz_cursor(page())) {
    case 1: return hand;
    case 2: return text;
    case 3: return resize_across;
    case 4: return resize_down;
    case 5: return not_allowed;
    case 6: return wait;
    default: return arrow;
  }
}
void addCss(std::string_view css) { skiff_blitz_add_css(page(), css.data(), css.size()); }
void resolve(double nowMs) { skiff_blitz_resolve(page(), nowMs); }
void paint(std::uint8_t *out, std::size_t stride) { skiff_blitz_paint(page(), out, stride); }
dom::Handle at(float x, float y) {
  for (std::uint64_t node = skiff_blitz_hit(page(), x, y); node != 0; node = skiff_blitz_parent(page(), node))
    if (const auto found = kept().handles.find(node); found != kept().handles.end()) return found->second;
  return dom::kNone;
}
bool scroll(float x, float y, double dx, double dy) { return skiff_blitz_scroll_at(page(), x, y, dx, dy); }
void point(pointer what, float x, float y, int button) {
  const std::uint32_t kind = what == pointer::pressed ? 1u : what == pointer::released ? 2u : 0u;
  skiff_blitz_pointer(page(), kind, x, y, static_cast<std::uint32_t>(button));
}
void commitText(std::string_view text) { skiff_blitz_commit_text(page(), text.data(), text.size()); }
void key(editing_key which, key_modifiers held) {
  const std::uint32_t mods = (held.shift ? 1u : 0u) | (held.control ? 2u : 0u) | (held.alt ? 4u : 0u) | (held.meta ? 8u : 0u);
  skiff_blitz_key(page(), static_cast<std::uint32_t>(which), mods);
}
std::vector<std::pair<dom::Handle, std::string>> takeEdits() {
  std::vector<std::pair<dom::Handle, std::string>> out;
  std::uint64_t node = 0;
  const char *value = nullptr;
  std::size_t len = 0;
  while (skiff_blitz_next_input(page(), &node, &value, &len))
    if (const auto found = kept().handles.find(node); found != kept().handles.end()) out.emplace_back(found->second, std::string(value, len));
  return out;
}
std::optional<box> boxOf(dom::Handle element) {
  box out;
  if (nodeOf(element) == 0 || !skiff_blitz_rect(page(), nodeOf(element), &out.x, &out.y, &out.width, &out.height)) return std::nullopt;
  return out;
}
dom::Handle focused() {
  const auto found = kept().handles.find(skiff_blitz_focused(page()));
  return found == kept().handles.end() ? dom::kNone : found->second;
}
float scrolled(dom::Handle element) {
  return nodeOf(element) == 0 ? 0.0f : static_cast<float>(skiff_blitz_scroll_offset(page(), nodeOf(element)));
}
} // namespace blitz

} // namespace skiff::web
