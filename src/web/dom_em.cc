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
  Module.skiffDom = { els: [null, document.getElementById('skiff') || document.body], ctx: {} };
  // A colour as CSS, from 0xAARRGGBB; a canvas's context, kept once made.
  Module.skiffDom.rgba = function(c) {
    c = c >>> 0;
    return 'rgba(' + ((c >> 16) & 255) + ',' + ((c >> 8) & 255) + ',' + (c & 255) + ',' + (((c >>> 24) & 255) / 255) + ')';
  };
  Module.skiffDom.context = function(h) {
    const D = Module.skiffDom;
    return D.ctx[h] || (D.ctx[h] = D.els[h].getContext('2d'));
  };
});
EM_JS(int, skiffDomCreate, (const char *tag, int len), {
  const name = UTF8ToString(tag, len);
  // SVG's elements are made in SVG's namespace, or they would not draw.
  const svg = {svg: 1, g: 1, polygon: 1, polyline: 1, line: 1, circle: 1, path: 1, rect: 1, text: 1, defs: 1};
  const el = svg[name] ? document.createElementNS('http://www.w3.org/2000/svg', name) : document.createElement(name);
  Module.skiffDom.els.push(el);
  return Module.skiffDom.els.length - 1;
});
EM_JS(int, skiffDomChild, (int parent, int index), {
  const p = Module.skiffDom.els[parent];
  const el = p ? p.children[index] : null;
  if (!el) return 0;
  Module.skiffDom.els.push(el);
  return Module.skiffDom.els.length - 1;
});
EM_JS(int, skiffDomChildren, (int parent), {
  const p = Module.skiffDom.els[parent];
  return p ? p.children.length : 0;
});
EM_JS(void, skiffDomClear, (int h), {
  const el = Module.skiffDom.els[h];
  if (el) el.replaceChildren();
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
    if ((t.tagName === 'CANVAS' || t.tagName === 'svg') && typeof ev.clientX === 'number') {
      // Where in the element, whatever child of it was hit.
      const r = t.getBoundingClientRect();
      value = (ev.clientX - r.left) + ',' + (ev.clientY - r.top) + ',' + ev.button;
      if (name === 'contextmenu') ev.preventDefault();
    } else if (name === 'scroll') value = String(t.scrollTop || t.scrollLeft || 0);
    else if (name === 'keydown' || name === 'keyup') value = ev.key + '|' + (ev.shiftKey ? 1 : 0) + ((ev.ctrlKey || ev.metaKey) ? 1 : 0);
    else if (t.tagName === 'SELECT') value = String(t.selectedIndex);
    else if ('value' in t) value = String(t.value);
    Module.ccall('skiff_web_event', null, ['number', 'string', 'string'], [h, name, value]);
  });
});
EM_JS(void, skiffDomCanvasSize, (int h, int w, int hh), {
  const el = Module.skiffDom.els[h];
  const r = window.devicePixelRatio || 1;
  el.width = Math.max(1, Math.round(w * r));
  el.height = Math.max(1, Math.round(hh * r));
  el.style.width = w + 'px';
  el.style.height = hh + 'px';
  const ctx = el.getContext('2d');
  ctx.setTransform(r, 0, 0, r, 0, 0);
  Module.skiffDom.ctx[h] = ctx;
});
EM_JS(void, skiffDomCanvasClear, (int h, unsigned c), {
  const el = Module.skiffDom.els[h];
  const ctx = Module.skiffDom.context(h);
  ctx.fillStyle = Module.skiffDom.rgba(c);
  ctx.fillRect(0, 0, el.width, el.height);
});
EM_JS(void, skiffDomCanvasBegin, (int h), { Module.skiffDom.context(h).beginPath(); });
EM_JS(void, skiffDomCanvasMoveTo, (int h, float x, float y), { Module.skiffDom.context(h).moveTo(x, y); });
EM_JS(void, skiffDomCanvasLineTo, (int h, float x, float y), { Module.skiffDom.context(h).lineTo(x, y); });
EM_JS(void, skiffDomCanvasClose, (int h), { Module.skiffDom.context(h).closePath(); });
EM_JS(void, skiffDomCanvasFill, (int h, unsigned c), {
  const ctx = Module.skiffDom.context(h);
  ctx.fillStyle = Module.skiffDom.rgba(c);
  ctx.fill();
});
EM_JS(void, skiffDomCanvasStroke, (int h, float w, unsigned c), {
  const ctx = Module.skiffDom.context(h);
  ctx.lineWidth = w;
  ctx.lineCap = 'square';
  ctx.lineJoin = 'miter';
  ctx.strokeStyle = Module.skiffDom.rgba(c);
  ctx.stroke();
});
EM_JS(void, skiffDomCanvasCircle, (int h, float x, float y, float r, unsigned c), {
  const ctx = Module.skiffDom.context(h);
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.fillStyle = Module.skiffDom.rgba(c);
  ctx.fill();
});
EM_JS(void, skiffDomCanvasText, (int h, float x, float y, float size, unsigned c, const char *s, int len), {
  const ctx = Module.skiffDom.context(h);
  ctx.font = 'bold ' + size + 'px system-ui, sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillStyle = Module.skiffDom.rgba(c);
  ctx.fillText(UTF8ToString(s, len), x, y);
});
EM_JS(char *, skiffDomObjectUrl, (const unsigned char *p, int len, const char *mime, int ml), {
  const blob = new Blob([HEAPU8.slice(p, p + len)], { type: UTF8ToString(mime, ml) });
  const s = URL.createObjectURL(blob);
  const n = lengthBytesUTF8(s) + 1;
  const out = _malloc(n);
  stringToUTF8(s, out, n);
  return out;
});
EM_JS(void, skiffDomScrollTo, (int h, float offset), {
  const el = Module.skiffDom.els[h];
  const horizontal = getComputedStyle(el).flexDirection === 'row';
  const at = offset < 0 ? (horizontal ? el.scrollWidth : el.scrollHeight) : offset;
  if (horizontal) el.scrollLeft = at; else el.scrollTop = at;
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
  // The page's root, whether or not anything has been made in it yet: what a
  // page written beforehand put there is cleared too.
  const root = Module.skiffDom ? Module.skiffDom.els[1] : document.getElementById('skiff');
  if (root) root.replaceChildren();
  Module.skiffDom = null;
  skiffDomInit();
  if (root) Module.skiffDom.els[1] = root;
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
Handle child(Handle parent, std::size_t index) {
  skiffDomInit();
  return static_cast<Handle>(skiffDomChild(static_cast<int>(parent), static_cast<int>(index)));
}
std::size_t children(Handle parent) {
  skiffDomInit();
  return static_cast<std::size_t>(skiffDomChildren(static_cast<int>(parent)));
}
void clear(Handle h) { skiffDomClear(static_cast<int>(h)); }
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
void canvasSize(Handle h, int width, int height) { skiffDomCanvasSize(static_cast<int>(h), width, height); }
void canvasClear(Handle h, Colour c) { skiffDomCanvasClear(static_cast<int>(h), c); }
void canvasBegin(Handle h) { skiffDomCanvasBegin(static_cast<int>(h)); }
void canvasMoveTo(Handle h, float x, float y) { skiffDomCanvasMoveTo(static_cast<int>(h), x, y); }
void canvasLineTo(Handle h, float x, float y) { skiffDomCanvasLineTo(static_cast<int>(h), x, y); }
void canvasClose(Handle h) { skiffDomCanvasClose(static_cast<int>(h)); }
void canvasFill(Handle h, Colour c) { skiffDomCanvasFill(static_cast<int>(h), c); }
void canvasStroke(Handle h, float width, Colour c) { skiffDomCanvasStroke(static_cast<int>(h), width, c); }
void canvasCircle(Handle h, float cx, float cy, float r, Colour c) { skiffDomCanvasCircle(static_cast<int>(h), cx, cy, r, c); }
void canvasText(Handle h, float x, float y, float size, Colour c, std::string_view text) {
  skiffDomCanvasText(static_cast<int>(h), x, y, size, c, text.data(), static_cast<int>(text.size()));
}
std::string drawn(Handle) { return {}; }
std::string objectUrl(std::span<const unsigned char> bytes, std::string_view mime) {
  char *p = skiffDomObjectUrl(bytes.data(), static_cast<int>(bytes.size()), mime.data(), static_cast<int>(mime.size()));
  std::string out(p);
  std::free(p);
  return out;
}
void scrollTo(Handle h, float offset) { skiffDomScrollTo(static_cast<int>(h), offset); }
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
