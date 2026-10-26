// skiff.web:memory -- the document as a tree of elements kept in memory,
// read back as HTML: what a test mounts a page into (dom_fake.cc), and what
// an engine embedded beside it keeps to write the page out (dom_blitz.cc).
export module skiff.web:memory;

import std;
import :dom;

export namespace skiff::web::memory {
using dom::Handle;
using dom::kNone;
using dom::Colour;
struct Element {
  std::string tag;
  std::string text;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<std::pair<std::string, std::string>> styles;
  std::string value;
  bool checked = false;
  bool alive = true;
  std::vector<Handle> children;
  std::vector<std::string> listened;
  std::string drawn;  // a canvas: a word per drawing call
};
std::vector<Element> &elements() {
  static std::vector<Element> all = {Element{}, Element{.tag = "body"}};
  return all;
}
Element &at(Handle h) { return elements().at(h); }
template <class Pairs> void set(Pairs &pairs, std::string_view name, std::string_view value) {
  for (auto &[n, v] : pairs)
    if (n == name) {
      v = std::string(value);
      return;
    }
  pairs.emplace_back(std::string(name), std::string(value));
}
std::string escaped(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '&': out += "&amp;"; break;
    case '"': out += "&quot;"; break;
    default: out += c;
    }
  }
  return out;
}
void dumpInto(std::string &out, Handle h) {
  const Element &e = at(h);
  if (!e.alive) return;
  out += '<' + e.tag;
  for (const auto &[n, v] : e.attrs) out += ' ' + n + "=\"" + escaped(v) + '"';
  if (!e.styles.empty()) {
    out += " style=\"";
    for (const auto &[p, v] : e.styles) out += p + ':' + v + ';';
    out += '"';
  }
  if (!e.value.empty()) out += " value=\"" + escaped(e.value) + '"';
  if (e.checked) out += " checked";
  out += '>';
  out += escaped(e.text);
  for (Handle c : e.children) dumpInto(out, c);
  out += "</" + e.tag + '>';
}

Handle root() { return 1; }
Handle create(std::string_view tag) {
  elements().push_back(Element{.tag = std::string(tag)});
  return static_cast<Handle>(elements().size() - 1);
}
void destroy(Handle h) {
  if (h == kNone || h >= elements().size()) return;
  at(h).alive = false;
  for (Element &e : elements()) std::erase(e.children, h);
}
void append(Handle parent, Handle child) {
  if (parent == kNone || child == kNone) return;
  for (Element &e : elements()) std::erase(e.children, child);
  at(parent).children.push_back(child);
}
Handle child(Handle parent, std::size_t index) {
  if (parent == kNone || parent >= elements().size()) return kNone;
  const auto &all = at(parent).children;
  return index < all.size() ? all[index] : kNone;
}
std::size_t children(Handle parent) { return parent == kNone || parent >= elements().size() ? 0 : at(parent).children.size(); }
Handle marked(std::string_view attribute, std::string_view value) {
  const auto &all = elements();
  const auto found = std::ranges::find_last_if(all, [&](const Element &e) {
    return e.alive && std::ranges::contains(e.attrs, std::pair<std::string, std::string>(std::string(attribute), std::string(value)));
  });
  return found.empty() ? kNone : static_cast<Handle>(found.begin() - all.begin());
}
void clear(Handle h) {
  if (h == kNone || h >= elements().size()) return;
  at(h).children.clear();
  at(h).text.clear();
}
void text(Handle h, std::string_view t) { at(h).text = std::string(t); }
void attr(Handle h, std::string_view n, std::string_view v) { set(at(h).attrs, n, v); }
void dropAttr(Handle h, std::string_view n) {
  std::erase_if(at(h).attrs, [&](const auto &p) { return p.first == n; });
}
// An empty value takes the property away, as setProperty does in a browser.
void style(Handle h, std::string_view p, std::string_view v) {
  if (v.empty()) std::erase_if(at(h).styles, [&](const auto &pair) { return pair.first == p; });
  else set(at(h).styles, p, v);
}
void value(Handle h, std::string_view v) { at(h).value = std::string(v); }
void checked(Handle h, bool on) { at(h).checked = on; }
void listen(Handle h, std::string_view event) { at(h).listened.emplace_back(event); }

void drew(Handle h, std::string_view word) {
  std::string &d = at(h).drawn;
  if (!d.empty()) d += ' ';
  d += word;
}
void canvasSize(Handle h, int width, int height) {
  set(at(h).attrs, "width", std::to_string(width));
  set(at(h).attrs, "height", std::to_string(height));
  drew(h, "size");
}
void canvasClear(Handle h, Colour) { drew(h, "clear"); }
void canvasBegin(Handle h) { drew(h, "begin"); }
void canvasMoveTo(Handle h, float, float) { drew(h, "moveto"); }
void canvasLineTo(Handle h, float, float) { drew(h, "lineto"); }
void canvasClose(Handle h) { drew(h, "close"); }
void canvasFill(Handle h, Colour) { drew(h, "fill"); }
void canvasStroke(Handle h, float, Colour) { drew(h, "stroke"); }
void canvasCircle(Handle h, float, float, float, Colour) { drew(h, "circle"); }
void canvasText(Handle h, float, float, float, Colour, std::string_view) { drew(h, "text"); }
std::string drawn(Handle h) { return at(h).drawn; }
std::string objectUrl(std::span<const unsigned char> bytes, std::string_view mime) {
  static int made = 0;
  return "blob:fake/" + std::to_string(++made) + "#" + std::string(mime) + "/" + std::to_string(bytes.size());
}
void scrollTo(Handle h, float offset) { set(at(h).attrs, "data-skiff-scroll", offset < 0.0f ? "end" : std::to_string(static_cast<int>(offset))); }

void picture(Handle h, const std::vector<std::uint8_t> &, int, int, std::string_view key) { set(at(h).attrs, "data-skiff-picture", key); }
std::string dump(Handle h) {
  std::string out;
  dumpInto(out, h);
  return out;
}
void fire(Handle target, std::string_view event, std::string_view v) {
  Element &e = at(target);
  // As a browser would have, before telling anyone: the control shows what
  // was done to it.
  if (event == "input" || event == "change") {
    if (e.tag == "input" && std::ranges::any_of(e.attrs, [](const auto &p) { return p.first == "type" && p.second == "checkbox"; }))
      e.checked = !e.checked;
    else
      e.value = std::string(v);
  }
}
void reset() {
  elements() = {Element{}, Element{.tag = "body"}};
}

} // namespace skiff::web::memory
