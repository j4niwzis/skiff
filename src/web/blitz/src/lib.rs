// SPDX-License-Identifier: AGPL-3.0-only
//! skiff-blitz: a Blitz document, driven through a C API -- what skiff.web's
//! dom calls are, one Blitz mutation each; then resolved (Stylo's styles,
//! Taffy's layout, incrementally), painted into a buffer of pixels (Vello on
//! the processor), and asked which node is at a point. The document lives
//! here, behind an opaque pointer the C++ side holds; node ids are Blitz's.

use anyrender::{PaintScene as _, render_to_buffer};
use anyrender_vello_cpu::VelloCpuImageRenderer;
use blitz_dom::{DocumentConfig, LocalName, NodeId, QualName, ns, util::Color};
use blitz_html::HtmlDocument;
use blitz_paint::paint_scene;
use blitz_traits::shell::{ColorScheme, Viewport};
use peniko::Fill;
use peniko::kurbo::Rect;

/// The document, and the viewport it is laid out in.
pub struct Page {
    doc: HtmlDocument,
    width: u32,
    height: u32,
    scale: f32,
}

// Text from the C side: a pointer and a length, UTF-8 (skiff's strings).
// Where it is not UTF-8, what can be read of it.
fn text<'a>(ptr: *const u8, len: usize) -> std::borrow::Cow<'a, str> {
    if ptr.is_null() || len == 0 {
        return std::borrow::Cow::Borrowed("");
    }
    // SAFETY: the C side passes a pointer to `len` bytes it keeps alive for
    // the call.
    let bytes = unsafe { std::slice::from_raw_parts(ptr, len) };
    String::from_utf8_lossy(bytes)
}
fn html_name(name: &str) -> QualName {
    QualName::new(None, ns!(html), LocalName::from(name))
}
// Node ids across the C API: Blitz's own, as their raw u64 (0, the null id,
// is none).
fn id(raw: u64) -> NodeId {
    NodeId::from_u64(raw)
}
fn page<'a>(page: *mut Page) -> &'a mut Page {
    // SAFETY: the pointer is one skiff_blitz_new made and not yet freed.
    unsafe { &mut *page }
}

/// A new, empty page: a body in a document, laid out at `width` by `height`
/// physical pixels at `scale`.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_new(width: u32, height: u32, scale: f32) -> *mut Page {
    let doc = HtmlDocument::from_html(
        "<!DOCTYPE html><html><head></head><body></body></html>",
        DocumentConfig {
            viewport: Some(Viewport::new(width, height, scale, ColorScheme::Light)),
            ..Default::default()
        },
    );
    Box::into_raw(Box::new(Page { doc, width, height, scale }))
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_free(p: *mut Page) {
    if !p.is_null() {
        // SAFETY: made by skiff_blitz_new, freed once.
        drop(unsafe { Box::from_raw(p) });
    }
}
/// The body, where a page is mounted.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_body(p: *mut Page) -> u64 {
    let p = page(p);
    p.doc.query_selector("body").ok().flatten().map_or(0, NodeId::as_u64)
}
/// The page's own style sheet: the rules what is written selects on.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_add_css(p: *mut Page, css: *const u8, len: usize) {
    let p = page(p);
    let head = p.doc.query_selector("head").ok().flatten();
    let mut m = p.doc.mutate();
    let style = m.create_element(html_name("style"), vec![]);
    let content = m.create_text_node(&text(css, len));
    m.append_children(style, &[content]);
    if let Some(head) = head {
        m.append_children(head, &[style]);
    }
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_create_element(p: *mut Page, tag: *const u8, len: usize) -> u64 {
    page(p).doc.mutate().create_element(html_name(&text(tag, len)), vec![]).as_u64()
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_create_text(p: *mut Page, value: *const u8, len: usize) -> u64 {
    page(p).doc.mutate().create_text_node(&text(value, len)).as_u64()
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_set_text(p: *mut Page, node: u64, value: *const u8, len: usize) {
    page(p).doc.mutate().set_node_text(id(node), &text(value, len));
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_append(p: *mut Page, parent: u64, child: u64) {
    page(p).doc.mutate().append_children(id(parent), &[id(child)]);
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_remove(p: *mut Page, node: u64) {
    let _ = page(p).doc.mutate().remove_and_drop_node(id(node));
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_set_attribute(p: *mut Page, node: u64, name: *const u8, name_len: usize, value: *const u8, value_len: usize) {
    page(p).doc.mutate().set_attribute(id(node), html_name(&text(name, name_len)), &text(value, value_len));
}
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_clear_attribute(p: *mut Page, node: u64, name: *const u8, name_len: usize) {
    page(p).doc.mutate().clear_attribute(id(node), html_name(&text(name, name_len)));
}
/// A style property set on the element, or -- an empty value -- taken away.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_set_style(p: *mut Page, node: u64, name: *const u8, name_len: usize, value: *const u8, value_len: usize) {
    let value = text(value, value_len);
    let mut m = page(p).doc.mutate();
    if value.is_empty() {
        m.remove_style_property(id(node), &text(name, name_len));
    } else {
        m.set_style_property(id(node), &text(name, name_len), &value);
    }
}
/// The node a node is in, 0 where none.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_parent(p: *mut Page, node: u64) -> u64 {
    page(p).doc.get_node(id(node)).and_then(|n| n.parent).map_or(0, NodeId::as_u64)
}
/// The viewport: its physical size and scale.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_set_viewport(p: *mut Page, width: u32, height: u32, scale: f32) {
    let p = page(p);
    p.width = width;
    p.height = height;
    p.scale = scale;
    p.doc.set_viewport(Viewport::new(width, height, scale, ColorScheme::Light));
}
/// Styles and layout brought up to date with what changed.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_resolve(p: *mut Page, now_ms: f64) {
    page(p).doc.resolve(now_ms / 1000.0);
}
/// Painted into `out`: `height` rows of `stride` bytes, RGBA, the page's
/// physical size.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_paint(p: *mut Page, out: *mut u8, stride: usize) {
    let p = page(p);
    let (width, height, scale) = (p.width, p.height, p.scale as f64);
    let buffer = render_to_buffer::<VelloCpuImageRenderer, _>(
        |scene| {
            scene.fill(Fill::NonZero, Default::default(), Color::WHITE, Default::default(), &Rect::new(0.0, 0.0, width as f64, height as f64));
            paint_scene(scene, &mut p.doc, scale, width, height, 0, 0);
        },
        width,
        height,
    );
    let row = width as usize * 4;
    for y in 0..height as usize {
        // SAFETY: `out` holds `height` rows of `stride` bytes, each at least
        // `row` long.
        let to = unsafe { std::slice::from_raw_parts_mut(out.add(y * stride), row) };
        to.copy_from_slice(&buffer[y * row..(y + 1) * row]);
    }
}
/// The node at a point, in the page's CSS pixels; 0 where none.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_hit(p: *mut Page, x: f32, y: f32) -> u64 {
    page(p).doc.hit(x, y).map_or(0, |hit| hit.node_id.as_u64())
}
/// A wheel at a point, in CSS pixels: what is under it scrolled, or the
/// nearest box above it that can still scroll, else the page -- as a
/// browser scrolls. Whether anything moved.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_scroll_at(p: *mut Page, x: f32, y: f32, dx: f64, dy: f64) -> bool {
    let p = page(p);
    match p.doc.hit(x, y) {
        Some(hit) => p.doc.scroll_node_by_has_changed(hit.node_id, dx, dy, |_| {}),
        None => p.doc.scroll_viewport_by_has_changed(dx, dy),
    }
}
