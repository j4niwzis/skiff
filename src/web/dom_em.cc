// skiff.web:dom in the browser, through Emscripten: the elements are the
// document's, kept in a table on the JS side by the handle the C++ side holds.
// Events listened on come back through skiff_web_event(handle, event, value),
// which the program defines for its page (extern "C", EMSCRIPTEN_KEEPALIVE) and
// the JS listeners call through Module.ccall -- so the program links with
// -sEXPORTED_RUNTIME_METHODS=ccall and -sEXPORTED_FUNCTIONS=_main,_skiff_web_event.
module;
#include <emscripten.h>
#include <cstdint>
module skiff.web;

import std;

// clang-format off
EM_JS(void, skiffDomInit, (), {
  if (Module.skiffDom) return;
  Module.skiffDom = { els: [null, document.getElementById('skiff') || document.body] };
});
EM_JS(int, skiffDomCreate, (const char *tag, int len), {
  const el = document.createElement(UTF8ToString(tag, len));
  Module.skiffDom.els.push(el);
  return Module.skiffDom.els.length - 1;
});
EM_JS(void, skiffDomDestroy, (int h), {
  const el = Module.skiffDom.els[h];
  if (el && el.parentNode) el.parentNode.removeChild(el);
  Module.skiffDom.els[h] = null;
});
EM_JS(void, skiffDomAppend, (int parent, int child), {
  Module.skiffDom.els[parent].appendChild(Module.skiffDom.els[child]);
});
EM_JS(void, skiffDomText, (int h, const char *s, int len), {
  Module.skiffDom.els[h].textContent = UTF8ToString(s, len);
});
EM_JS(void, skiffDomAttr, (int h, const char *n, int nl, const char *v, int vl), {
  Module.skiffDom.els[h].setAttribute(UTF8ToString(n, nl), UTF8ToString(v, vl));
});
EM_JS(void, skiffDomDropAttr, (int h, const char *n, int nl), {
  Module.skiffDom.els[h].removeAttribute(UTF8ToString(n, nl));
});
EM_JS(void, skiffDomStyle, (int h, const char *p, int pl, const char *v, int vl), {
  Module.skiffDom.els[h].style.setProperty(UTF8ToString(p, pl), UTF8ToString(v, vl));
});
EM_JS(void, skiffDomValue, (int h, const char *v, int vl), {
  Module.skiffDom.els[h].value = UTF8ToString(v, vl);
});
EM_JS(void, skiffDomChecked, (int h, int on), {
  Module.skiffDom.els[h].checked = !!on;
});
EM_JS(void, skiffDomListen, (int h, const char *e, int el), {
  const name = UTF8ToString(e, el);
  const target = Module.skiffDom.els[h];
  target.addEventListener(name, function(ev) {
    const t = ev.currentTarget;
    let value = '';
    if (t.tagName === 'SELECT') value = String(t.selectedIndex);
    else if ('value' in t) value = String(t.value);
    Module.ccall('skiff_web_event', null, ['number', 'string', 'string'], [h, name, value]);
  });
});
EM_JS(char *, skiffDomDump, (int h), {
  const s = Module.skiffDom.els[h].outerHTML;
  const n = lengthBytesUTF8(s) + 1;
  const p = _malloc(n);
  stringToUTF8(s, p, n);
  return p;
});
EM_JS(void, skiffDomFire, (int h, const char *e, int el, const char *v, int vl), {
  const t = Module.skiffDom.els[h];
  const name = UTF8ToString(e, el);
  if ('value' in t && (name === 'input' || name === 'change')) t.value = UTF8ToString(v, vl);
  t.dispatchEvent(new Event(name, { bubbles: true }));
});
EM_JS(void, skiffDomReset, (), {
  const root = Module.skiffDom ? Module.skiffDom.els[1] : null;
  if (root) root.replaceChildren();
  Module.skiffDom = { els: [null, root || document.body] };
});
// clang-format on

namespace skiff::web::dom {
Handle root() {
  skiffDomInit();
  return 1;
}
Handle create(std::string_view tag) {
  skiffDomInit();
  return static_cast<Handle>(skiffDomCreate(tag.data(), static_cast<int>(tag.size())));
}
void destroy(Handle h) { skiffDomDestroy(static_cast<int>(h)); }
void append(Handle parent, Handle child) { skiffDomAppend(static_cast<int>(parent), static_cast<int>(child)); }
void text(Handle h, std::string_view t) { skiffDomText(static_cast<int>(h), t.data(), static_cast<int>(t.size())); }
void attr(Handle h, std::string_view n, std::string_view v) {
  skiffDomAttr(static_cast<int>(h), n.data(), static_cast<int>(n.size()), v.data(), static_cast<int>(v.size()));
}
void dropAttr(Handle h, std::string_view n) { skiffDomDropAttr(static_cast<int>(h), n.data(), static_cast<int>(n.size())); }
void style(Handle h, std::string_view p, std::string_view v) {
  skiffDomStyle(static_cast<int>(h), p.data(), static_cast<int>(p.size()), v.data(), static_cast<int>(v.size()));
}
void value(Handle h, std::string_view v) { skiffDomValue(static_cast<int>(h), v.data(), static_cast<int>(v.size())); }
void checked(Handle h, bool on) { skiffDomChecked(static_cast<int>(h), on ? 1 : 0); }
void listen(Handle h, std::string_view e) { skiffDomListen(static_cast<int>(h), e.data(), static_cast<int>(e.size())); }
std::string dump(Handle h) {
  char *p = skiffDomDump(static_cast<int>(h));
  std::string out(p);
  std::free(p);
  return out;
}
void fire(Handle h, std::string_view e, std::string_view v) {
  skiffDomFire(static_cast<int>(h), e.data(), static_cast<int>(e.size()), v.data(), static_cast<int>(v.size()));
}
void reset() { skiffDomReset(); }

} // namespace skiff::web::dom
