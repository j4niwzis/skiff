// SPDX-License-Identifier: AGPL-3.0-only
//! skiff-blitz: a Blitz document, driven through a C API -- what skiff.web's
//! dom calls are, one Blitz mutation each; then resolved (Stylo's styles,
//! Taffy's layout, incrementally), painted into a buffer of pixels (Vello on
//! the processor), and asked which node is at a point. The document lives
//! here, behind an opaque pointer the C++ side holds; node ids are Blitz's.

use anyrender::{PaintScene as _, render_to_buffer};
use anyrender_vello_cpu::VelloCpuImageRenderer;
use blitz_dom::node::{ImageData, RasterImageData, SpecialElementData};
use blitz_dom::{DocumentConfig, LocalName, NodeId, QualName, ScrollBehavior, ns, util::Color};
use std::sync::Arc;
use atomic_refcell::AtomicRefCell;
use blitz_dom::{Document, EventDriver, EventHandler};
use blitz_traits::events::{
    BlitzImeEvent, BlitzKeyEvent, BlitzPointerEvent, BlitzPointerId, DomEvent, DomEventData, EventState, KeyState, MouseEventButton,
    MouseEventButtons, PointerCoords, PointerDetails, UiEvent,
};
use keyboard_types::{Code, Key, Location, Modifiers, NamedKey};
use blitz_traits::shell::{ClipboardError, ShellProvider};
use cursor_icon::CursorIcon;
use std::sync::Mutex;

// What the document asks of its host -- text put on the clipboard, text
// asked of it, the pointer's shape, where an input method's window goes --
// kept for the host to take.
#[derive(Default)]
struct Asked {
    copied: Option<String>,
    paste: bool,
    cursor: Option<CursorIcon>,
    ime: Option<(f32, f32, f32, f32)>,
}
#[derive(Default)]
struct Shell(Mutex<Asked>);
impl ShellProvider for Shell {
    fn set_cursor(&self, icon: Option<CursorIcon>) {
        self.0.lock().unwrap().cursor = icon;
    }
    fn set_ime_cursor_area(&self, x: f32, y: f32, width: f32, height: f32) {
        self.0.lock().unwrap().ime = Some((x, y, width, height));
    }
    fn request_clipboard_text(&self) -> Result<(), ClipboardError> {
        self.0.lock().unwrap().paste = true;
        Ok(())
    }
    fn set_clipboard_text(&self, text: String) -> Result<(), ClipboardError> {
        self.0.lock().unwrap().copied = Some(text);
        Ok(())
    }
}
use blitz_html::HtmlDocument;
use blitz_paint::paint_scene;
use blitz_traits::shell::{ColorScheme, Viewport};
use peniko::Fill;
use peniko::kurbo::Rect;

/// The document, and the viewport it is laid out in; what its fields were
/// edited to, not yet taken; the buttons held.
pub struct Page {
    doc: HtmlDocument,
    width: u32,
    height: u32,
    scale: f32,
    inputs: std::collections::VecDeque<(NodeId, String)>,
    taken: String,
    buttons: MouseEventButtons,
    shell: Arc<Shell>,
}

// What the document's events leave for the host: each field's value as an
// edit leaves it (`input`), kept to be taken.
struct Inputs<'a>(&'a mut std::collections::VecDeque<(NodeId, String)>);
impl EventHandler for Inputs<'_> {
    fn handle_event(&mut self, chain: &[NodeId], event: &mut DomEvent, _doc: &mut dyn Document, _state: &mut EventState) {
        if let (DomEventData::Input(input), Some(target)) = (&event.data, chain.first()) {
            self.0.push_back((*target, input.value.clone()));
        }
    }
}
fn handle(p: &mut Page, event: UiEvent) {
    let mut driver = EventDriver::new(&mut p.doc, Inputs(&mut p.inputs));
    driver.handle_ui_event(event);
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
    let shell = Arc::new(Shell::default());
    let doc = HtmlDocument::from_html(
        "<!DOCTYPE html><html><head></head><body></body></html>",
        DocumentConfig {
            viewport: Some(Viewport::new(width, height, scale, ColorScheme::Light)),
            shell_provider: Some(Arc::clone(&shell) as _),
            ..Default::default()
        },
    );
    Box::into_raw(Box::new(Page {
        doc,
        width,
        height,
        scale,
        inputs: Default::default(),
        taken: String::new(),
        buttons: MouseEventButtons::None,
        shell,
    }))
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
pub extern "C" fn skiff_blitz_set_viewport(p: *mut Page, width: u32, height: u32, scale: f32, zoom: f32) {
    let p = page(p);
    p.width = width;
    p.height = height;
    p.scale = scale;
    let mut viewport = Viewport::new(width, height, scale, ColorScheme::Light);
    viewport.zoom = zoom;
    p.doc.set_viewport(viewport);
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
/// How far a box is scrolled down, in CSS pixels: the engine's own scroll.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_scroll_offset(p: *mut Page, node: u64) -> f64 {
    page(p).doc.get_node(id(node)).map_or(0.0, |n| n.scroll_offset().y)
}
/// A box scrolled down to an offset, in CSS pixels, at once.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_scroll_to(p: *mut Page, node: u64, y: f64) {
    page(p).doc.scroll_to(id(node), 0.0, y, ScrollBehavior::Instant);
}
/// An <img>'s picture: `width` by `height` pixels, RGBA, not premultiplied,
/// row after row -- the element laid out and painted again with it. `key`,
/// what it is known by, is put on the element (data-skiff-picture).
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_set_picture(p: *mut Page, node: u64, rgba: *const u8, width: u32, height: u32, key: *const u8, key_len: usize) {
    let p = page(p);
    let len = width as usize * height as usize * 4;
    let pixels = if rgba.is_null() || len == 0 {
        Vec::new()
    } else {
        // SAFETY: the C side passes `len` bytes, kept alive for the call.
        unsafe { std::slice::from_raw_parts(rgba, len) }.to_vec()
    };
    if let Some(n) = p.doc.get_node_mut(id(node)) {
        if let Some(element) = n.element_data_mut() {
            element.special_data = SpecialElementData::Image(Box::new(ImageData::Raster(RasterImageData::new(width, height, Arc::new(pixels)))));
        }
        n.clear_layout_cache();
    }
    // Said on it, which restyles and lays it out again with its picture.
    p.doc.mutate().set_attribute(id(node), html_name("data-skiff-picture"), &text(key, key_len));
}
/// The pointer, at a point in CSS pixels: moved (0), pressed (1) or let go
/// (2), with a button (0 the main, 1 the middle, 2 the other) -- a field
/// focused, its caret put, a selection dragged, as the engine does them.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_pointer(p: *mut Page, what: u32, x: f32, y: f32, button: u32) {
    let p = page(p);
    let button = match button {
        1 => MouseEventButton::Auxiliary,
        2 => MouseEventButton::Secondary,
        _ => MouseEventButton::Main,
    };
    let held = match button {
        MouseEventButton::Auxiliary => MouseEventButtons::Auxiliary,
        MouseEventButton::Secondary => MouseEventButtons::Secondary,
        _ => MouseEventButtons::Primary,
    };
    match what {
        1 => p.buttons |= held,
        2 => p.buttons.remove(held),
        _ => {}
    }
    let event = BlitzPointerEvent {
        id: BlitzPointerId::Mouse,
        is_primary: true,
        coords: PointerCoords { page_x: x, page_y: y, screen_x: x, screen_y: y, client_x: x, client_y: y },
        button,
        buttons: p.buttons,
        mods: Modifiers::empty(),
        details: PointerDetails::default(),
        element: Default::default(),
        active_pointers: Arc::new(AtomicRefCell::new(Vec::new())),
    };
    let event = match what {
        1 => UiEvent::PointerDown(event),
        2 => UiEvent::PointerUp(event),
        _ => UiEvent::PointerMove(event),
    };
    handle(p, event);
}
/// Text typed (UTF-8), as the system commits it -- a key's character, or
/// what an input method composed: put into the field with the focus.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_commit_text(p: *mut Page, value: *const u8, len: usize) {
    let p = page(p);
    let value = text(value, len).into_owned();
    handle(p, UiEvent::Ime(BlitzImeEvent::Commit(value)));
}
/// A key that edits rather than types, pressed: 1 Backspace, 2 Delete,
/// 3 Enter, 4 left, 5 right, 6 up, 7 down, 8 Home, 9 End, 10 Tab; `mods`
/// 1 Shift, 2 Control, 4 Alt, 8 Meta.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_key(p: *mut Page, which: u32, mods: u32) {
    let p = page(p);
    let named = match which {
        1 => NamedKey::Backspace,
        2 => NamedKey::Delete,
        3 => NamedKey::Enter,
        4 => NamedKey::ArrowLeft,
        5 => NamedKey::ArrowRight,
        6 => NamedKey::ArrowUp,
        7 => NamedKey::ArrowDown,
        8 => NamedKey::Home,
        9 => NamedKey::End,
        10 => NamedKey::Tab,
        _ => return,
    };
    let mut modifiers = Modifiers::empty();
    if mods & 1 != 0 {
        modifiers |= Modifiers::SHIFT;
    }
    if mods & 2 != 0 {
        modifiers |= Modifiers::CONTROL;
    }
    if mods & 4 != 0 {
        modifiers |= Modifiers::ALT;
    }
    if mods & 8 != 0 {
        modifiers |= Modifiers::META;
    }
    for state in [KeyState::Pressed, KeyState::Released] {
        let event = BlitzKeyEvent {
            key: Key::Named(named),
            code: Code::Unidentified,
            modifiers,
            location: Location::Standard,
            is_auto_repeating: false,
            is_composing: false,
            state,
            text: None,
        };
        handle(p, if state == KeyState::Pressed { UiEvent::KeyDown(event) } else { UiEvent::KeyUp(event) });
    }
}
/// The next field edited and its value now, oldest first: whether there was
/// one. The value stays the page's until the next call.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_next_input(p: *mut Page, node: *mut u64, value: *mut *const u8, len: *mut usize) -> bool {
    let p = page(p);
    let Some((id, text)) = p.inputs.pop_front() else {
        return false;
    };
    p.taken = text;
    // SAFETY: the C side passes three places to write to, alive for the call.
    unsafe {
        *node = id.as_u64();
        *value = p.taken.as_ptr();
        *len = p.taken.len();
    }
    true
}
/// The node with the focus, 0 where none: a field being typed into.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_focused(p: *mut Page) -> u64 {
    page(p).doc.get_focussed_node_id().map_or(0, NodeId::as_u64)
}
/// A node's box in the viewport, in CSS pixels: whether it has one.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_rect(p: *mut Page, node: u64, x: *mut f32, y: *mut f32, width: *mut f32, height: *mut f32) -> bool {
    let Some(rect) = page(p).doc.get_client_bounding_rect(id(node)) else {
        return false;
    };
    // SAFETY: the C side passes four places to write to, alive for the call.
    unsafe {
        *x = rect.x as f32;
        *y = rect.y as f32;
        *width = rect.width as f32;
        *height = rect.height as f32;
    }
    true
}
/// Text the document put on the clipboard (copied or cut in a field), since
/// last asked: whether there was some. It stays the page's until the next call.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_take_copied(p: *mut Page, value: *mut *const u8, len: *mut usize) -> bool {
    let p = page(p);
    let Some(text) = p.shell.0.lock().unwrap().copied.take() else {
        return false;
    };
    p.taken = text;
    // SAFETY: the C side passes two places to write to, alive for the call.
    unsafe {
        *value = p.taken.as_ptr();
        *len = p.taken.len();
    }
    true
}
/// Whether the document asked for the clipboard's text (a paste), since
/// last asked: the host answers with skiff_blitz_paste.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_take_paste_request(p: *mut Page) -> bool {
    std::mem::take(&mut page(p).shell.0.lock().unwrap().paste)
}
/// The clipboard's text, pasted where the document asked for it.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_paste(p: *mut Page, value: *const u8, len: usize) {
    let p = page(p);
    let value = text(value, len).into_owned();
    handle(p, UiEvent::Paste(value));
}
/// The pointer's shape the document asks for: 0 the arrow, 1 a hand, 2 text,
/// 3 resizing across, 4 resizing down, 5 not allowed, 6 a wait.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_cursor(p: *mut Page) -> u32 {
    match page(p).shell.0.lock().unwrap().cursor {
        Some(CursorIcon::Pointer) => 1,
        Some(CursorIcon::Text | CursorIcon::VerticalText) => 2,
        Some(CursorIcon::EwResize | CursorIcon::ColResize) => 3,
        Some(CursorIcon::NsResize | CursorIcon::RowResize) => 4,
        Some(CursorIcon::NotAllowed) => 5,
        Some(CursorIcon::Wait | CursorIcon::Progress) => 6,
        _ => 0,
    }
}
/// Where the document's caret is for an input method's window, in CSS
/// pixels, since last asked: whether it moved.
#[unsafe(no_mangle)]
pub extern "C" fn skiff_blitz_take_ime_area(p: *mut Page, x: *mut f32, y: *mut f32, width: *mut f32, height: *mut f32) -> bool {
    let Some((ax, ay, aw, ah)) = page(p).shell.0.lock().unwrap().ime.take() else {
        return false;
    };
    // SAFETY: the C side passes four places to write to, alive for the call.
    unsafe {
        *x = ax;
        *y = ay;
        *width = aw;
        *height = ah;
    }
    true
}
