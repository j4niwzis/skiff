// skiff's stand-in for Skia in a browser (Emscripten): what a canvas made
// over a page's <canvas> draws is drawn by the browser's 2D context, and a
// text is as wide as the browser makes it -- the scene laid out and drawn
// with no Skia at all. Each draw is a shape (the numbers of SkPath's kinds),
// the matrix it is drawn through and its paint, handed over at once.
module;
#include <emscripten.h>
module skia;

import std;

// clang-format off
EM_JS_DEPS(skia_lite_codec, "malloc");
EM_JS(void, skiaLiteInit, (), {
  if (Module.skiaLite) return;
  const L = Module.skiaLite = { ctx: [null], els: [null], scale: [1] };
  L.font = function(size, bold) {
    const family = typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily;
    return (bold ? "600 " : "") + size + 'px ' + (family || 'sans-serif');
  };
  L.rgba = function(r, g, b, a) {
    return 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' + Math.round(b * 255) + ',' + a + ')';
  };
  L.path = function(ctx, s) {
    ctx.beginPath();
    let i = 0;
    while (i < s.length) {
      const k = s[i++];
      if (k === 1) { ctx.moveTo(s[i], s[i + 1]); i += 2; }
      else if (k === 2) { ctx.lineTo(s[i], s[i + 1]); i += 2; }
      else if (k === 3) { ctx.quadraticCurveTo(s[i], s[i + 1], s[i + 2], s[i + 3]); i += 4; }
      else if (k === 4) { ctx.bezierCurveTo(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]); i += 6; }
      else if (k === 5) { ctx.closePath(); }
      else if (k === 6) { ctx.rect(s[i], s[i + 1], s[i + 2], s[i + 3]); i += 4; }
      else if (k === 7) {
        ctx.roundRect(s[i], s[i + 1], s[i + 2], s[i + 3],
                      [{x: s[i + 4], y: s[i + 5]}, {x: s[i + 6], y: s[i + 7]}, {x: s[i + 8], y: s[i + 9]}, {x: s[i + 10], y: s[i + 11]}]);
        i += 12;
      }
      else if (k === 8) {
        ctx.moveTo(s[i] + Math.max(0, s[i + 2]), s[i + 1]);
        ctx.ellipse(s[i], s[i + 1], Math.max(0, s[i + 2]), Math.max(0, s[i + 3]), 0, 0, Math.PI * 2);
        i += 4;
      }
      else if (k === 9) {
        const a = s[i + 4] * Math.PI / 180, b = (s[i + 4] + s[i + 5]) * Math.PI / 180;
        ctx.ellipse(s[i], s[i + 1], Math.max(0, s[i + 2]), Math.max(0, s[i + 3]), 0, a, b, s[i + 5] < 0);
        i += 6;
      }
      else break;
    }
  };
  // A paint: style, colour (r g b a), stroke width, cap, join, then a linear
  // gradient where there is one -- from, to, how many colours, each colour.
  L.apply = function(ctx, p) {
    const colour = L.rgba(p[1], p[2], p[3], p[4]);
    ctx.lineWidth = p[5] > 0 ? p[5] : 1;
    ctx.lineCap = ['butt', 'round', 'square'][p[6] | 0] || 'butt';
    ctx.lineJoin = ['miter', 'round', 'bevel'][p[7] | 0] || 'miter';
    if (p[8] > 0) {
      const g = ctx.createLinearGradient(p[9], p[10], p[11], p[12]);
      const n = p[13] | 0;
      for (let j = 0; j < n; ++j) {
        const at = 14 + j * 4;
        g.addColorStop(n > 1 ? j / (n - 1) : 0, L.rgba(p[at], p[at + 1], p[at + 2], p[at + 3]));
      }
      ctx.fillStyle = g;
      ctx.strokeStyle = g;
      ctx.globalAlpha *= p[4];
    } else {
      ctx.fillStyle = colour;
      ctx.strokeStyle = colour;
    }
  };
  L.place = function(t, m) {
    const ctx = L.ctx[t], sc = L.scale[t];
    ctx.setTransform(sc * m[0], sc * m[1], sc * m[2], sc * m[3], sc * m[4], sc * m[5]);
    return ctx;
  };
  L.measure = function() {
    return L.mctx || (L.mctx = (typeof OffscreenCanvas === 'undefined' ? document.createElement('canvas'):new OffscreenCanvas(1,1)).getContext('2d'));
  };
});
EM_JS(int, skiaLiteAttach, (const char *id, int len, int w, int h, float scale), {
  skiaLiteInit();
  const L = Module.skiaLite;
  const el = document.getElementById(UTF8ToString(id, len));
  if (!el || !el.getContext) return 0;
  el.width = Math.max(1, Math.round(w * scale));
  el.height = Math.max(1, Math.round(h * scale));
  el.style.width = w + 'px';
  el.style.height = h + 'px';
  let t = L.els.indexOf(el);
  if (t <= 0) {
    L.els.push(el);
    L.ctx.push(el.getContext('2d'));
    L.scale.push(scale);
    t = L.els.length - 1;
  }
  L.scale[t] = scale;
  return t;
});
EM_JS(void, skiaLiteDraw, (int t, const float *m, const float *shape, int n, const float *paint, int pn), {
  const L = Module.skiaLite;
  if (!L || !L.ctx[t]) return;
  const ctx = L.ctx[t];
  ctx.save();
  L.place(t, HEAPF32.subarray(m >> 2, (m >> 2) + 6));
  L.path(ctx, HEAPF32.subarray(shape >> 2, (shape >> 2) + n));
  const p = HEAPF32.subarray(paint >> 2, (paint >> 2) + pn);
  L.apply(ctx, p);
  if (p[0] !== 1) ctx.fill();
  if (p[0] !== 0) ctx.stroke();
  ctx.restore();
});
EM_JS(void, skiaLiteClip, (int t, const float *m, const float *shape, int n), {
  const L = Module.skiaLite;
  if (!L || !L.ctx[t]) return;
  const ctx = L.place(t, HEAPF32.subarray(m >> 2, (m >> 2) + 6));
  L.path(ctx, HEAPF32.subarray(shape >> 2, (shape >> 2) + n));
  ctx.clip();
});
EM_JS(void, skiaLiteSave, (int t), {
  const L = Module.skiaLite;
  if (L && L.ctx[t]) L.ctx[t].save();
});
EM_JS(void, skiaLiteRestore, (int t), {
  const L = Module.skiaLite;
  if (L && L.ctx[t]) L.ctx[t].restore();
});
EM_JS(void, skiaLiteLayer, (int t, float alpha), {
  const L = Module.skiaLite;
  if (!L || !L.ctx[t]) return;
  L.ctx[t].save();
  L.ctx[t].globalAlpha *= alpha;
});
EM_JS(void, skiaLiteText, (int t, const float *m, const char *s, int len, float x, float y, float size, int bold, const float *paint, int pn), {
  const L = Module.skiaLite;
  if (!L || !L.ctx[t]) return;
  const ctx = L.ctx[t];
  ctx.save();
  L.place(t, HEAPF32.subarray(m >> 2, (m >> 2) + 6));
  L.apply(ctx, HEAPF32.subarray(paint >> 2, (paint >> 2) + pn));
  ctx.font = L.font(size, bold);
  ctx.textBaseline = 'alphabetic';
  ctx.fillText(UTF8ToString(s, len), x, y);
  ctx.restore();
});
EM_JS(void, skiaLiteClear, (int t, int argb), {
  const L = Module.skiaLite;
  if (!L || !L.ctx[t]) return;
  const ctx = L.ctx[t], c = argb >>> 0;
  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  if (((c >>> 24) & 255) === 0) {
    ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
  } else {
    ctx.fillStyle = 'rgba(' + ((c >> 16) & 255) + ',' + ((c >> 8) & 255) + ',' + (c & 255) + ',' + (((c >>> 24) & 255) / 255) + ')';
    ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);
  }
  ctx.restore();
});
EM_JS(double, skiaLiteMeasure, (const char *s, int len, float size, int bold), {
  skiaLiteInit();
  const L = Module.skiaLite;
  const c = L.measure();
  c.font = L.font(size, bold);
  return c.measureText(UTF8ToString(s, len)).width;
});
EM_JS(void, skiaLiteMetrics, (float size, int bold, float *out), {
  skiaLiteInit();
  const L = Module.skiaLite;
  const c = L.measure();
  c.font = L.font(size, bold);
  const m = c.measureText('Hg');
  HEAPF32[out >> 2] = -(m.fontBoundingBoxAscent || size * 0.8);
  HEAPF32[(out >> 2) + 1] = m.fontBoundingBoxDescent || size * 0.2;
});
// Decoding can run on a pthread. OffscreenCanvas and createImageBitmap do
// not require the DOM, and the caller's bytes are copied before yielding.
EM_ASYNC_JS(unsigned char *, skiaLiteDecode, (const void *data, size_t size, int most, int *dimensions), {
  let bitmap;
  try {
    if (!size || size > 67108864) return 0;
    bitmap = await createImageBitmap(new Blob([HEAPU8.slice(data,data+size)]));
    const ratio = most > 0 ? Math.min(1,most/Math.max(bitmap.width,bitmap.height)):1;
    const width = Math.max(1,Math.round(bitmap.width*ratio)), height = Math.max(1,Math.round(bitmap.height*ratio));
    if (width*height > 67108864) return 0;
    const canvas = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(width,height):document.createElement('canvas');
    canvas.width = width;canvas.height = height;
    const context = canvas.getContext('2d',{willReadFrequently:true});
    context.drawImage(bitmap,0,0,width,height);
    const pixels = context.getImageData(0,0,width,height).data;
    const result = _malloc(pixels.length);if (!result) return 0;
    HEAPU8.set(pixels,result);
    HEAP32[dimensions >> 2] = width;HEAP32[(dimensions >> 2)+1] = height;
    return result;
  } catch (error) {return 0;}
  finally {bitmap?.close();}
});
EM_ASYNC_JS(char *, skiaLiteEncode, (const unsigned char *rgba, int width, int height, int jpeg, size_t *length), {
  try {
    if (!width || !height || width*height > 67108864) return 0;
    const canvas = typeof OffscreenCanvas !== 'undefined' ? new OffscreenCanvas(width,height):document.createElement('canvas');
    canvas.width = width;canvas.height = height;
    const context = canvas.getContext('2d');
    context.putImageData(new ImageData(new Uint8ClampedArray(HEAPU8.slice(rgba,rgba+width*height*4)),width,height),0,0);
    const type = jpeg ? 'image/jpeg':'image/png';
    const blob = canvas.convertToBlob ? await canvas.convertToBlob({type,quality:0.9}):
      await new Promise(resolve => canvas.toBlob(resolve,type,0.9));
    const bytes = new Uint8Array(await blob.arrayBuffer()), result = _malloc(bytes.length);
    if (!result) return 0;
    HEAPU8.set(bytes,result);HEAPU32[length >> 2] = bytes.length;
    return result;
  } catch (error) {return 0;}
});
// clang-format on

namespace skia::lite {
Sp<SkImage> decode(const void *data, std::size_t size, int most) {
  if (!data || size == 0) return nullptr;
  std::array<int, 2> dimensions{};
  auto *pixels = skiaLiteDecode(data, size, most, dimensions.data());
  if (!pixels) return nullptr;
  const auto result = imageFromRGBA(dimensions[0], dimensions[1], pixels);
  std::free(pixels);
  return result;
}
std::string encode(const SkImage &image, bool jpeg) {
  if (image.width() <= 0 || image.height() <= 0 || static_cast<std::int64_t>(image.width()) * image.height() > kMostPixels) return {};
  // Canvas reads a contiguous RGBA buffer while its asynchronous call runs.
  const auto rgba = image.pixels().colours | std::views::transform([](SkColor colour) {
    return std::array{static_cast<std::uint8_t>(colour >> 16), static_cast<std::uint8_t>(colour >> 8),
                      static_cast<std::uint8_t>(colour), static_cast<std::uint8_t>(colour >> 24)};
  }) | std::views::join | std::ranges::to<std::vector>();
  std::size_t length = 0;
  char *data = skiaLiteEncode(rgba.data(), image.width(), image.height(), jpeg ? 1 : 0, &length);
  if (!data) return {};
  std::string result(data, length);
  std::free(data);
  return result;
}

namespace {
std::array<float, 6> matrixOf(const SkMatrix &m) {
  return {m.getScaleX(), m.getSkewY(), m.getSkewX(), m.getScaleY(), m.getTranslateX(), m.getTranslateY()};
}
std::vector<float> paintOf(const SkPaint &p) {
  const SkColor4f c = p.getColor4f();
  const float style = p.getStyle() == SkPaint::kFill_Style ? 0.0f : p.getStyle() == SkPaint::kStroke_Style ? 1.0f : 2.0f;
  std::vector<float> out{style, c.fR, c.fG, c.fB, c.fA, p.getStrokeWidth(), static_cast<float>(p.getStrokeCap()),
                         static_cast<float>(p.getStrokeJoin()), 0.0f};
  if (const SkShader *shader = p.getShader(); shader && shader->linear && !shader->colours.empty()) {
    out[8] = 1.0f;
    out.insert(out.end(), {shader->from.fX, shader->from.fY, shader->to.fX, shader->to.fY, static_cast<float>(shader->colours.size())});
    for (const SkColor4f &one : shader->colours) out.insert(out.end(), {one.fR, one.fG, one.fB, one.fA});
  }
  return out;
}
} // namespace

void draw(Target t, const SkMatrix &m, const std::vector<float> &shape, const SkPaint &p) {
  if (t == 0 || shape.empty()) return;
  const auto matrix = matrixOf(m);
  const auto paint = paintOf(p);
  skiaLiteDraw(t, matrix.data(), shape.data(), static_cast<int>(shape.size()), paint.data(), static_cast<int>(paint.size()));
}
void clip(Target t, const SkMatrix &m, const std::vector<float> &shape) {
  if (t == 0 || shape.empty()) return;
  const auto matrix = matrixOf(m);
  skiaLiteClip(t, matrix.data(), shape.data(), static_cast<int>(shape.size()));
}
void save(Target t) { if (t != 0) skiaLiteSave(t); }
void restore(Target t) { if (t != 0) skiaLiteRestore(t); }
void layer(Target t, float alpha) { if (t != 0) skiaLiteLayer(t, alpha); }
void text(Target t, const SkMatrix &m, std::string_view text, float x, float y, float size, bool bold, const SkPaint &p) {
  if (t == 0 || text.empty()) return;
  const auto matrix = matrixOf(m);
  const auto paint = paintOf(p);
  skiaLiteText(t, matrix.data(), text.data(), static_cast<int>(text.size()), x, y, size, bold ? 1 : 0, paint.data(),
               static_cast<int>(paint.size()));
}
void clear(Target t, SkColor colour) { if (t != 0) skiaLiteClear(t, static_cast<int>(colour)); }
Target attach(std::string_view id, int width, int height, float scale) {
  return skiaLiteAttach(id.data(), static_cast<int>(id.size()), width, height, scale);
}
std::optional<float> measure(std::string_view text, float size, bool bold) {
  if (text.empty()) return 0.0f;
  return static_cast<float>(skiaLiteMeasure(text.data(), static_cast<int>(text.size()), size, bold ? 1 : 0));
}
std::optional<std::pair<float, float>> metrics(float size, bool bold) {
  std::array<float, 2> out{};
  skiaLiteMetrics(size, bold ? 1 : 0, out.data());
  return std::pair{out[0], out[1]};
}
} // namespace skia::lite
