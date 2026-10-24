// skiff.web:dom, without a browser: a tree of elements kept in memory, read
// back as HTML. What a test mounts a page into, and sends events into.
module skiff.web;

import std;

namespace skiff::web::dom {
namespace {
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
// A document: its elements by handle, and those changed since asked.
struct Kept {
  std::vector<Element> elements = {Element{}, Element{.tag = "body"}};
  std::vector<Handle> changed;
};
std::map<Document, Kept> &documents() {
  static std::map<Document, Kept> all = [] {
    std::map<Document, Kept> made;
    made.emplace(kFirstDocument, Kept{});
    return made;
  }();
  return all;
}
thread_local Document current = kFirstDocument;
Kept &kept() { return documents().at(current); }
std::vector<Element> &elements() { return kept().elements; }
// Its own state changed: said once, until it is taken.
void changed(Handle h) {
  if (h == kNone) return;
  auto &all = kept().changed;
  if (!std::ranges::contains(all, h)) all.push_back(h);
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
} // namespace

Handle root() { return 1; }
Handle create(std::string_view tag) {
  elements().push_back(Element{.tag = std::string(tag)});
  return static_cast<Handle>(elements().size() - 1);
}
void destroy(Handle h) {
  if (h == kNone || h >= elements().size()) return;
  at(h).alive = false;
  for (Handle parent = 0; parent < elements().size(); ++parent)
    if (std::erase(at(parent).children, h) > 0) changed(parent);
}
void append(Handle parent, Handle child) {
  if (parent == kNone || child == kNone) return;
  for (Handle was = 0; was < elements().size(); ++was)
    if (std::erase(at(was).children, child) > 0) changed(was);
  at(parent).children.push_back(child);
  changed(parent);
}
Handle child(Handle parent, std::size_t index) {
  if (parent == kNone || parent >= elements().size()) return kNone;
  const auto &all = at(parent).children;
  return index < all.size() ? all[index] : kNone;
}
std::size_t children(Handle parent) { return parent == kNone || parent >= elements().size() ? 0 : at(parent).children.size(); }
void clear(Handle h) {
  if (h == kNone || h >= elements().size()) return;
  at(h).children.clear();
  at(h).text.clear();
  changed(h);
}
void text(Handle h, std::string_view t) {
  at(h).text = std::string(t);
  changed(h);
}
void attr(Handle h, std::string_view n, std::string_view v) {
  set(at(h).attrs, n, v);
  changed(h);
}
void dropAttr(Handle h, std::string_view n) {
  std::erase_if(at(h).attrs, [&](const auto &p) { return p.first == n; });
  changed(h);
}
// An empty value takes the property away, as setProperty does in a browser.
void style(Handle h, std::string_view p, std::string_view v) {
  if (v.empty()) std::erase_if(at(h).styles, [&](const auto &pair) { return pair.first == p; });
  else set(at(h).styles, p, v);
  changed(h);
}
void value(Handle h, std::string_view v) {
  at(h).value = std::string(v);
  changed(h);
}
void checked(Handle h, bool on) {
  at(h).checked = on;
  changed(h);
}
void listen(Handle h, std::string_view event) { at(h).listened.emplace_back(event); }

namespace {
void drew(Handle h, std::string_view word) {
  changed(h);
  std::string &d = at(h).drawn;
  if (!d.empty()) d += ' ';
  d += word;
}
} // namespace
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
void scrollTo(Handle h, float offset) {
  set(at(h).attrs, "data-skiff-scroll", offset < 0.0f ? "end" : std::to_string(static_cast<int>(offset)));
  changed(h);
}

std::string dump(Handle h) {
  std::string out;
  dumpInto(out, h);
  return out;
}
void fire(Handle target, std::string_view event, std::string_view v) {
  Element &e = at(target);
  // As a browser would have, before telling anyone: the control shows what
  // was done to it.
  changed(target);
  if (event == "input" || event == "change") {
    if (e.tag == "input" && std::ranges::any_of(e.attrs, [](const auto &p) { return p.first == "type" && p.second == "checkbox"; }))
      e.checked = !e.checked;
    else
      e.value = std::string(v);
  }
}
void reset() { kept() = Kept{}; }

Document makeDocument() {
  const Document made = documents().rbegin()->first + 1;
  documents().emplace(made, Kept{});
  return made;
}
void use(Document document) {
  documents().try_emplace(document);
  current = document;
}
void dropDocument(Document document) {
  if (document == kFirstDocument) return;
  documents().erase(document);
  if (current == document) current = kFirstDocument;
}
std::vector<Handle> takeChanged() { return std::exchange(kept().changed, {}); }
std::string tagOf(Handle h) { return h < elements().size() ? at(h).tag : std::string(); }
std::vector<std::pair<std::string, std::string>> attrsOf(Handle h) {
  return h < elements().size() ? at(h).attrs : std::vector<std::pair<std::string, std::string>>{};
}
std::vector<std::string> listenedOn(Handle h) { return h < elements().size() ? at(h).listened : std::vector<std::string>{}; }
std::string textOf(Handle h) { return h < elements().size() ? at(h).text : std::string(); }
std::vector<std::pair<std::string, std::string>> stylesOf(Handle h) {
  return h < elements().size() ? at(h).styles : std::vector<std::pair<std::string, std::string>>{};
}
std::string valueOf(Handle h) { return h < elements().size() ? at(h).value : std::string(); }
bool checkedOf(Handle h) { return h < elements().size() && at(h).checked; }
Handle parentOf(Handle h) {
  for (Handle parent = 0; parent < elements().size(); ++parent)
    if (std::ranges::contains(at(parent).children, h)) return parent;
  return kNone;
}

} // namespace skiff::web::dom
