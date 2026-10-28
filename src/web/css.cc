// CSS declarations stay typed until a document or an HTML writer needs text.
export module skiff.web:css;

import std;

export namespace skiff::web::css {

template <std::size_t N> struct spelling {
  char chars[N];
  constexpr spelling(const char (&text)[N]) { std::ranges::copy(text, chars); }
  constexpr operator std::string_view() const { return {chars, N - 1}; }
};
template <spelling Text> struct keyword {
  static constexpr std::string_view text = Text;
  friend bool operator==(keyword, keyword) = default;
};
using relative = keyword<"relative">;
using absolute = keyword<"absolute">;
using flex_box = keyword<"flex">;
using grid = keyword<"grid">;
using block = keyword<"block">;
using webkit_box = keyword<"-webkit-box">;
using none = keyword<"none">;
using auto_ = keyword<"auto">;
using hidden = keyword<"hidden">;
using visible = keyword<"visible">;
using row = keyword<"row">;
using column = keyword<"column">;
using wrap = keyword<"wrap">;
using nowrap = keyword<"nowrap">;
using flex_start = keyword<"flex-start">;
using flex_end = keyword<"flex-end">;
using start = keyword<"start">;
using end = keyword<"end">;
using center = keyword<"center">;
using stretch = keyword<"stretch">;
using space_between = keyword<"space-between">;
using space_around = keyword<"space-around">;
using border_box = keyword<"border-box">;
using text = keyword<"text">;
using pointer = keyword<"pointer">;
using ew_resize = keyword<"ew-resize">;
using ns_resize = keyword<"ns-resize">;
using pre = keyword<"pre">;
using pre_wrap = keyword<"pre-wrap">;
using break_word = keyword<"break-word">;
using ellipsis = keyword<"ellipsis">;
using contain = keyword<"contain">;
using cover = keyword<"cover">;
using fill = keyword<"fill">;
using transparent = keyword<"transparent">;
using inherit = keyword<"inherit">;
using monospace = keyword<"monospace">;
using ui_monospace = keyword<"ui-monospace, monospace">;
using italic = keyword<"italic">;
using bold = keyword<"bold">;
using vertical = keyword<"vertical">;
using middle = keyword<"middle">;
using max_content = keyword<"max-content">;
using min_content = keyword<"min-content">;

struct zero { friend bool operator==(zero, zero) = default; };
struct pixels { float value; friend bool operator==(pixels, pixels) = default; };
struct percent { float value; friend bool operator==(percent, percent) = default; };
struct vmax { float value; friend bool operator==(vmax, vmax) = default; };
// The affine calculation used for a relative size or anchor minus its margins.
struct calculated {
  percent share;
  pixels subtract;
  friend bool operator==(calculated, calculated) = default;
};
using Length = std::variant<zero, pixels, percent, vmax, calculated>;
using Size = std::variant<zero, pixels, percent, vmax, calculated, auto_, max_content, min_content>;
using Display = std::variant<none, flex_box, grid, block, webkit_box>;
using Alignment = std::variant<flex_start, flex_end, center, stretch, start, end>;
using Justification = std::variant<flex_start, flex_end, center, space_between, space_around>;
using Fit = std::variant<contain, cover, fill>;

// Packed colours use skiff's 0xAARRGGBB convention. An explicit alpha also
// represents colours, such as translucent code plates, without quantisation.
struct argb { std::uint32_t value; friend bool operator==(argb, argb) = default; };
struct rgba {
  unsigned red, green, blue;
  float alpha;
  friend bool operator==(rgba, rgba) = default;
};
using Colour = std::variant<argb, rgba, transparent>;
struct sides {
  std::variant<Length, std::array<Length, 2>, std::array<Length, 4>> values;
  sides(Length all) : values(std::move(all)) {}
  sides(Length vertical, Length horizontal) : values(std::array{std::move(vertical), std::move(horizontal)}) {}
  sides(Length top, Length right, Length bottom, Length left)
      : values(std::array{std::move(top), std::move(right), std::move(bottom), std::move(left)}) {}
  friend bool operator==(const sides &, const sides &) = default;
};
struct line {
  pixels width;
  Colour colour;
  friend bool operator==(const line &, const line &) = default;
};
struct shadow {
  Length x, y, blur;
  Colour colour;
  std::optional<Length> spread;
  bool inset = false;
  friend bool operator==(const shadow &, const shadow &) = default;
};
struct linear_gradient {
  Colour top, bottom;
  friend bool operator==(const linear_gradient &, const linear_gradient &) = default;
};
struct colour_stop {
  Colour colour;
  std::vector<Length> positions;
  friend bool operator==(const colour_stop &, const colour_stop &) = default;
};
using to_right = keyword<"to right">;
using to_bottom = keyword<"to bottom">;
struct directed_gradient {
  std::variant<to_right, to_bottom> direction;
  std::vector<colour_stop> stops;
  friend bool operator==(const directed_gradient &, const directed_gradient &) = default;
};
struct blur {
  pixels radius;
  friend bool operator==(blur, blur) = default;
};
struct translate {
  Length x, y;
  friend bool operator==(const translate &, const translate &) = default;
};
struct translate_x {
  Length x;
  friend bool operator==(const translate_x &, const translate_x &) = default;
};
struct scale { float value; friend bool operator==(scale, scale) = default; };
using Transform = std::variant<translate, translate_x, scale>;
struct transforms {
  std::vector<Transform> values;
  friend bool operator==(const transforms &, const transforms &) = default;
};
struct flex_values {
  float grow, shrink;
  Size basis;
  friend bool operator==(const flex_values &, const flex_values &) = default;
};
struct decoration {
  bool struck = false, underline = false;
  friend bool operator==(decoration, decoration) = default;
};
struct cell {
  int row, column;
  friend bool operator==(cell, cell) = default;
};
struct gaps {
  Length row, column;
  friend bool operator==(const gaps &, const gaps &) = default;
};

template <spelling Name, class Value> struct property {
  static constexpr std::string_view name = Name;
  Value value;
  friend bool operator==(const property &, const property &) = default;
};
using position = property<"position", std::variant<relative, absolute>>;
using display = property<"display", Display>;
using width = property<"width", Size>;
using height = property<"height", Size>;
using min_width = property<"min-width", Size>;
using min_height = property<"min-height", Size>;
using max_width = property<"max-width", Size>;
using max_height = property<"max-height", Size>;
using left = property<"left", Length>;
using top = property<"top", Length>;
using margin = property<"margin", sides>;
using padding = property<"padding", sides>;
using padding_left = property<"padding-left", Length>;
using padding_right = property<"padding-right", Length>;
using box_sizing = property<"box-sizing", border_box>;
using border_radius = property<"border-radius", sides>;
using background_color = property<"background-color", Colour>;
using background = property<"background", Colour>;
using color = property<"color", Colour>;
using background_image = property<"background-image", std::variant<linear_gradient, directed_gradient>>;
using border = property<"border", std::variant<none, line>>;
using border_left = property<"border-left", std::variant<none, line>>;
using box_shadow = property<"box-shadow", std::variant<none, shadow>>;
using opacity = property<"opacity", float>;
using z_index = property<"z-index", int>;
using order = property<"order", int>;
using overflow = property<"overflow", std::variant<hidden, auto_, visible>>;
using overflow_x = property<"overflow-x", std::variant<hidden, auto_, visible>>;
using overflow_y = property<"overflow-y", std::variant<hidden, auto_, visible>>;
using overflow_wrap = property<"overflow-wrap", break_word>;
using transform = property<"transform", transforms>;
using flex = property<"flex", flex_values>;
using flex_shrink = property<"flex-shrink", float>;
using flex_direction = property<"flex-direction", std::variant<row, column>>;
using flex_wrap = property<"flex-wrap", std::variant<wrap, nowrap>>;
using align_self = property<"align-self", Alignment>;
using align_items = property<"align-items", Alignment>;
using justify_self = property<"justify-self", std::variant<start, end, center, stretch>>;
using justify_content = property<"justify-content", Justification>;
using grid_area = property<"grid-area", cell>;
using gap = property<"gap", std::variant<Length, gaps>>;
using column_gap = property<"column-gap", Length>;
using row_gap = property<"row-gap", Length>;
using cursor = property<"cursor", std::variant<text, pointer, ew_resize, ns_resize>>;
using font_size = property<"font-size", pixels>;
using font_weight = property<"font-weight", std::variant<int, bold>>;
using font_style = property<"font-style", italic>;
using font_family = property<"font-family", std::variant<inherit, monospace, ui_monospace>>;
using line_height = property<"line-height", float>;
using white_space = property<"white-space", std::variant<nowrap, pre, pre_wrap>>;
using text_decoration = property<"text-decoration", decoration>;
using text_overflow = property<"text-overflow", ellipsis>;
using user_select = property<"user-select", text>;
using object_fit = property<"object-fit", Fit>;
using vertical_align = property<"vertical-align", middle>;
using pointer_events = property<"pointer-events", std::variant<none, auto_>>;
using outline = property<"outline", none>;
using resize = property<"resize", none>;
using box_orient = property<"-webkit-box-orient", vertical>;
using line_clamp = property<"-webkit-line-clamp", int>;
using hover_background = property<"--skiff-hover-bg", Colour>;
using focus_background = property<"--skiff-focus-bg", Colour>;
using tail_background = property<"--skiff-bg", Colour>;
using tail_width = property<"--skiff-tail-w", pixels>;
using tail_height = property<"--skiff-tail-h", pixels>;
using range_track = property<"--skiff-track", directed_gradient>;
using progress = property<"--skiff-done", percent>;
using filter = property<"filter", std::variant<none, blur>>;

// An explicitly opaque extension for callers supplying CSS not modelled here.
// Its text is never parsed or used to decide layout in skiff.
struct opaque {
  std::string name, value;
  friend bool operator==(const opaque &, const opaque &) = default;
};
using Declaration = std::variant<position, display, width, height, min_width, min_height, max_width, max_height,
    left, top, margin, padding, padding_left, padding_right, box_sizing, border_radius, background_color,
    background, color, background_image, border, border_left, box_shadow, opacity, z_index, order,
    overflow, overflow_x, overflow_y, overflow_wrap, transform, flex, flex_shrink, flex_direction, flex_wrap,
    align_self, align_items, justify_self, justify_content, grid_area, gap, column_gap, row_gap, cursor,
    font_size, font_weight, font_style, font_family, line_height, white_space, text_decoration, text_overflow,
    user_select, object_fit, vertical_align, pointer_events, outline, resize, box_orient, line_clamp,
    hover_background, focus_background, tail_background, tail_width, tail_height, range_track, progress, filter, opaque>;
using Declarations = std::vector<Declaration>;

namespace detail {
[[nodiscard]] inline std::string number(float value, int precision = 3) {
  char buffer[64];
  const auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, precision);
  if (error != std::errc{}) throw std::invalid_argument("CSS number cannot be formatted");
  std::string result(buffer, end);
  while (!result.empty() && result.back() == '0') result.pop_back();
  if (!result.empty() && result.back() == '.') result.pop_back();
  return result.empty() ? "0" : result;
}
// The generic overload only reads a property's value; each value type has
// its own writer below. New alternatives cannot silently become raw text.
template <spelling Text> std::string write(keyword<Text>) { return std::string(keyword<Text>::text); }
inline std::string write(zero) { return "0"; }
inline std::string write(pixels one) { return number(one.value, 2) + "px"; }
inline std::string write(percent one) { return number(one.value) + "%"; }
inline std::string write(vmax one) { return number(one.value) + "vmax"; }
inline std::string write(calculated one) { return "calc(" + write(one.share) + " - " + write(one.subtract) + ")"; }
inline std::string write(float one) { return number(one); }
inline std::string write(int one) { return std::to_string(one); }
inline std::string write(rgba one) {
  return "rgba(" + std::to_string(one.red) + "," + std::to_string(one.green) + "," + std::to_string(one.blue) + "," + number(one.alpha) + ")";
}
inline std::string write(argb one) {
  return write(rgba{(one.value >> 16) & 255u, (one.value >> 8) & 255u, one.value & 255u, static_cast<float>(one.value >> 24) / 255.0f});
}
// Forward declarations allow variant and sequence writers to recurse into
// compound values without depending on their declaration order.
std::string write(const sides &);
std::string write(const line &);
std::string write(const shadow &);
std::string write(const linear_gradient &);
std::string write(const directed_gradient &);
std::string write(const colour_stop &);
std::string write(blur);
std::string write(const translate_x &);
std::string write(const translate &);
std::string write(scale);
std::string write(const transforms &);
std::string write(const flex_values &);
std::string write(decoration);
std::string write(cell);
std::string write(const gaps &);
template <class... T> std::string write(const std::variant<T...> &one) {
  return std::visit([](const auto &value) { return write(value); }, one);
}
template <std::ranges::input_range R> std::string joined(R &&values, std::string_view separator) {
  return values | std::views::transform([](const auto &one) { return write(one); }) |
         std::views::join_with(separator) | std::ranges::to<std::string>();
}
template <class T, std::size_t N> std::string write(const std::array<T, N> &values) { return joined(values, " "); }
inline std::string write(const sides &one) {
  return std::visit([](const auto &values) { return write(values); }, one.values);
}
inline std::string write(const line &one) { return write(one.width) + " solid " + write(one.colour); }
inline std::string write(const shadow &one) {
  return (one.inset ? "inset " : "") + write(one.x) + " " + write(one.y) + " " + write(one.blur)
      + (one.spread ? " " + write(*one.spread) : "") + " " + write(one.colour);
}
inline std::string write(const linear_gradient &one) { return "linear-gradient(" + write(one.top) + "," + write(one.bottom) + ")"; }
inline std::string write(const translate &one) { return "translate(" + write(one.x) + "," + write(one.y) + ")"; }
inline std::string write(scale one) { return "scale(" + number(one.value) + ")"; }
inline std::string write(const translate_x &one) { return "translateX(" + write(one.x) + ")"; }
inline std::string write(blur one) { return "blur(" + write(one.radius) + ")"; }
inline std::string write(const colour_stop &one) {
  return write(one.colour) + (one.positions.empty() ? "" : " " + joined(one.positions, " "));
}
inline std::string write(const directed_gradient &one) {
  return "linear-gradient(" + write(one.direction) + "," + joined(one.stops, ",") + ")";
}
inline std::string write(const transforms &one) { return joined(one.values, " "); }
inline std::string write(const flex_values &one) { return number(one.grow) + " " + number(one.shrink) + " " + write(one.basis); }
inline std::string write(decoration one) {
  return std::string(one.struck ? "line-through" : "") + (one.struck && one.underline ? " " : "") + (one.underline ? "underline" : "");
}
inline std::string write(cell one) { return std::to_string(one.row) + "/" + std::to_string(one.column); }
inline std::string write(const gaps &one) { return write(one.row) + " " + write(one.column); }

template <spelling Name, class Value> std::string_view nameOf(const property<Name, Value> &) { return property<Name, Value>::name; }
inline std::string_view nameOf(const opaque &one) { return one.name; }
template <spelling Name, class Value> std::string valueOf(const property<Name, Value> &one) { return write(one.value); }
inline std::string valueOf(const opaque &one) { return one.value; }
} // namespace detail

[[nodiscard]] inline std::string_view name(const Declaration &one) {
  return std::visit([](const auto &value) { return detail::nameOf(value); }, one);
}
[[nodiscard]] inline std::string value(const Declaration &one) {
  return std::visit([](const auto &value) { return detail::valueOf(value); }, one);
}
[[nodiscard]] inline const Declaration *find(const Declarations &rules, std::string_view key) {
  const auto at = std::ranges::find(rules, key, [](const auto &one) { return name(one); });
  return at == rules.end() ? nullptr : &*at;
}
template <class Property> [[nodiscard]] const Declaration *find(const Declarations &rules) { return find(rules, Property::name); }
inline bool erase(Declarations &rules, std::string_view key) {
  return std::erase_if(rules, [&](const auto &one) { return name(one) == key; }) != 0;
}
template <class Property> bool erase(Declarations &rules) { return erase(rules, Property::name); }
inline bool set(Declarations &rules, Declaration one) {
  const auto at = std::ranges::find(rules, name(one), [](const auto &rule) { return name(rule); });
  if (at == rules.end()) rules.push_back(std::move(one));
  else {
    if (*at == one) return false;
    *at = std::move(one);
  }
  return true;
}
// Compatibility for public APIs receiving arbitrary CSS. Empty text removes
// a declaration; typed callers remove it explicitly with erase<Property>.
inline bool set(Declarations &rules, std::string_view key, std::string_view text) {
  if (text.empty()) return erase(rules, key);
  return set(rules, opaque{std::string(key), std::string(text)});
}
} // namespace skiff::web::css
