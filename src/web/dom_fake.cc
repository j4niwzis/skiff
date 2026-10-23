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
} // namespace

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
void text(Handle h, std::string_view t) { at(h).text = std::string(t); }
void attr(Handle h, std::string_view n, std::string_view v) { set(at(h).attrs, n, v); }
void dropAttr(Handle h, std::string_view n) {
  std::erase_if(at(h).attrs, [&](const auto &p) { return p.first == n; });
}
void style(Handle h, std::string_view p, std::string_view v) { set(at(h).styles, p, v); }
void value(Handle h, std::string_view v) { at(h).value = std::string(v); }
void checked(Handle h, bool on) { at(h).checked = on; }
void listen(Handle h, std::string_view event) { at(h).listened.emplace_back(event); }

namespace {
void drew(Handle h, std::string_view word) {
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

} // namespace skiff::web::dom
