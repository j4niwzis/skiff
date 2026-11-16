// skiff.layout -- layout said as an attribute grammar (Meyerovich et al.,
// "Parallel Schedule Synthesis for Attribute Grammars", PPoPP'13): what each
// kind of node computes, from what of itself, its parent and its children;
// the fixed schedule of tree passes that computes it all; and the check,
// while compiling, that the schedule computes every attribute after what it
// reads. The scene's layout is generated from these (skiff.scene): a kind
// whose rules read what its pass has not computed yet does not compile, and
// says which rule.
//
// One function over the rules as data, not a template for each kind: the
// constant evaluator's work for a kind is a few milliseconds (spans, not
// views -- it walks a view's iterators through every adaptor).
export module skiff.layout;

import std;

export namespace skiff::layout {

// What layout computes for every node: its natural width (with room
// enough: a text on one line, a row's children side by side), the least
// width it can take (a wrapped text's longest word, what a row that shrinks
// gives way to), the width it is given, its height at that width, and where
// it is in its parent's content box.
enum class attribute : std::uint8_t { pref, least, width, height, x, y };
inline constexpr std::size_t kAttributes = 6;

// Whose attribute a rule reads or writes: the node's own, its parent's, or
// its children's (all of them, as a container reads them).
enum class place : std::uint8_t { self, parent, children };
struct reads {
  attribute what;
  place where;
};
// One rule of a kind: an attribute of its own (synthesized) or of its
// children (inherited: what a container gives them), from what it reads.
struct rule {
  attribute target;
  place of = place::self;
  std::array<reads, 4> from{};
  std::size_t count = 0;
};
[[nodiscard]] consteval rule computes(attribute target, place of, std::initializer_list<reads> from) {
  rule out{target, of};
  std::ranges::copy(from, out.from.begin());
  out.count = from.size();
  return out;
}

// The passes: postorder (children before their parent: what flows up) or
// preorder (a parent before its children: what flows down), and the
// attributes each computes.
enum class order : std::uint8_t { post, pre };
struct pass {
  order walk;
  std::array<bool, kAttributes> computes{};
};
[[nodiscard]] consteval pass walk(order walk, std::initializer_list<attribute> computed) {
  pass out{walk};
  for (attribute one : computed) out.computes[std::to_underlying(one)] = true;
  return out;
}
// Box layout's: natural and least widths up; widths down (a row hands out
// what its fixed children leave to those that grow, cuts those that shrink);
// heights up (text wrapping at the width it was given, a wrapped flow
// breaking its lines); places down (stacked, aligned across, anchored).
inline constexpr std::array kSchedule{walk(order::post, {attribute::pref, attribute::least}),
                                      walk(order::pre, {attribute::width}),
                                      walk(order::post, {attribute::height}),
                                      walk(order::pre, {attribute::x, attribute::y})};
inline constexpr std::size_t kPasses = kSchedule.size();

// What the check says of a kind: right, or the first rule that is not, and
// why.
struct verdict {
  bool ok = true;
  std::string_view why;
  std::size_t rule = 0;
  friend constexpr bool operator==(const verdict&, const verdict&) = default;
};

// A kind's rules checked against a schedule: each in a pass that may write
// it (up a postorder pass, down a preorder one), reading only what it may --
// anything an earlier pass computed; in the same pass, only what that pass's
// order has computed already: in a postorder pass the node's children's, in
// a preorder one its own (given it by its parent) or its parent's; never a
// sibling's of the same pass, siblings going side by side -- and every
// attribute there is computed: its own synthesized ones, and, where it has
// children, theirs. An opaque kind (a node laid out whole by its own hooks,
// the paper's nested region) is checked only at its boundary: it computes
// its own pref and height.
[[nodiscard]] consteval verdict check_rules(std::span<const rule> rules, bool holds, std::span<const pass> schedule) {
  const auto pass_of = [&](attribute what) -> std::optional<std::size_t> {
    for (std::size_t at = 0; at < schedule.size(); ++at)
      if (schedule[at].computes[std::to_underlying(what)]) return at;
    return std::nullopt;
  };
  const auto may_read = [&](const rule& one, const reads& read) {
    const auto at = pass_of(one.target);
    const auto then = pass_of(read.what);
    if (!at || !then || *then > *at) return false;
    if (*then < *at) return true;
    switch (schedule[*at].walk) {
      case order::post: return one.of == place::self && (read.where == place::children || read.where == place::self);
      case order::pre:
        return (one.of == place::children && read.where == place::self) ||
               (one.of == place::self && (read.where == place::parent || read.where == place::self));
    }
    return false;
  };
  for (std::size_t index = 0; index < rules.size(); ++index) {
    const rule& one = rules[index];
    const auto at = pass_of(one.target);
    if (!at)
      return {false, "no pass computes the attribute", index};
    const bool upward = one.of == place::self && schedule[*at].walk == order::post;
    const bool downward = schedule[*at].walk == order::pre;
    if (!upward && !downward)
      return {false, "a postorder pass cannot give children an attribute", index};
    for (std::size_t r = 0; r < one.count; ++r) {
      if (one.from[r].what == one.target && one.from[r].where == one.of)
        return {false, "a rule reads what it computes", index};
      if (!may_read(one, one.from[r]))
        return {false, "a rule reads what its pass has not computed yet", index};
    }
  }
  const auto has = [&](attribute what, place of) {
    for (const rule& one : rules)
      if (one.target == what && one.of == of) return true;
    return false;
  };
  if (!has(attribute::pref, place::self) || !has(attribute::least, place::self) || !has(attribute::height, place::self))
    return {false, "a kind leaves its own pref, least or height uncomputed", 0};
  if (holds && !(has(attribute::width, place::children) && has(attribute::x, place::children) && has(attribute::y, place::children)))
    return {false, "a container leaves its children's width or place uncomputed", 0};
  return {};
}
template <class Kind, auto& Schedule = kSchedule>
[[nodiscard]] consteval verdict check() {
  return check_rules(Kind::rules, Kind::holds, Schedule);
}

// The kind a node type says it is laid out as, where its type fixes it (a
// column type, a row type): the compiled passes then take it from the type.
enum class kind_of : std::uint8_t { column, row, edge };

// How a node is sized along an axis, where its type fixes it: its own size,
// a share of its parent's, what it holds, or (along a row) a share of what
// its fixed siblings leave. A type that says it (`static constexpr sizing
// xSizing, ySizing`) has the compiled passes work out that axis by that case
// alone, chosen while compiling -- not by asking its spec at every layout.
enum class sizing : std::uint8_t { own, share, content, grown };
// A node type's sizing along both axes, as a type.
template <sizing X, sizing Y> struct sized {
  static constexpr sizing x = X;
  static constexpr sizing y = Y;
};

// ---- the kinds of skiff's nodes, and their rules ---------------------------------------
using enum attribute;
using enum place;

// A column (a vertical flow): as wide as its widest child (padding added),
// its children given its content width -- or their natural one, clamped to
// it, where they size themselves -- as tall as they are stacked, gaps
// between; each placed under the one before, aligned across.
struct column {
  static constexpr bool holds = true;
  static constexpr std::array rules{
      computes(pref, self, {{pref, children}}),
      computes(least, self, {{least, children}}),
      computes(width, children, {{width, self}, {pref, children}}),
      computes(height, self, {{height, children}}),
      computes(x, children, {{width, children}}),
      computes(y, children, {{height, children}}),
  };
};
// A row (a horizontal flow, not wrapped): its children side by side; those
// of a fixed width given it, those that grow what the rest leave, those
// that shrink cut in proportion down to their least (CSS flex); as tall as
// its tallest; placed one after another, aligned across.
struct row {
  static constexpr bool holds = true;
  static constexpr std::array rules{
      computes(pref, self, {{pref, children}}),
      computes(least, self, {{least, children}}),
      computes(width, children, {{width, self}, {pref, children}, {least, children}}),
      computes(height, self, {{height, children}}),
      computes(x, children, {{width, children}}),
      computes(y, children, {{height, children}}),
  };
};
// A wrapped row: lines broken where the children's widths reach its own --
// a function of widths, known after the second pass -- its height the lines'
// (the third), each child placed on its line (the fourth). Those that grow
// share their line.
struct wrapped_row {
  static constexpr bool holds = true;
  static constexpr std::array rules{
      computes(pref, self, {{pref, children}}),
      computes(least, self, {{least, children}}),
      computes(width, children, {{width, self}, {pref, children}, {least, children}}),
      computes(height, self, {{height, children}, {width, children}, {width, self}}),
      computes(x, children, {{width, children}, {width, self}}),
      computes(y, children, {{height, children}, {width, children}, {width, self}}),
  };
};
// Text: on one line its natural width, its longest word its least; its
// lines, and so its height, at the width it is given -- the third pass
// reading the second.
struct text {
  static constexpr bool holds = false;
  static constexpr std::array rules{
      computes(pref, self, {}),
      computes(least, self, {}),
      computes(height, self, {{width, self}}),
  };
};
// A box of its own size -- an icon, an image, a spacer: as wide and tall as
// it says.
struct fixed_box {
  static constexpr bool holds = false;
  static constexpr std::array rules{
      computes(pref, self, {}),
      computes(least, self, {}),
      computes(height, self, {}),
  };
};
// A box sized as a share of its parent: its width given (a share of the
// parent's), its height a share of the parent's height -- given down, so
// read as what its parent's box is in the fourth pass's terms: the parent's
// width (second pass) for a relative width; a relative height is the
// parent's given height, which a parent sized by its children does not have
// until its own third pass: such a child is placed by its parent's box
// (anchored) instead.
struct relative_box {
  static constexpr bool holds = false;
  static constexpr std::array rules{
      computes(pref, self, {}),
      computes(least, self, {}),
      computes(height, self, {{width, self}}),
  };
};
// A scroll view: its contents as wide as it, their height free (laid out in
// a box of any height), its own height given (not its contents'); the
// contents placed at its top, moved by the offset when drawn.
struct scroll {
  static constexpr bool holds = true;
  static constexpr std::array rules{
      computes(pref, self, {{pref, children}}),
      computes(least, self, {}),
      computes(width, children, {{width, self}}),
      computes(height, self, {{width, self}}),
      computes(x, children, {}),
      computes(y, children, {}),
  };
};
// A node laid out whole by its own hooks (measure, layoutChildren) -- a
// widget's, a program's: a nested region, laid out as one when the passes
// reach it; at its boundary it gives its natural and least width and its
// height at the width it is given.
struct opaque {
  static constexpr bool holds = false;
  static constexpr std::array rules{
      computes(pref, self, {}),
      computes(least, self, {}),
      computes(height, self, {{width, self}}),
  };
};

// Every kind of skiff's checked while skiff compiles.
static_assert(check<column>().ok && check<row>().ok && check<wrapped_row>().ok && check<text>().ok && check<fixed_box>().ok &&
              check<relative_box>().ok && check<scroll>().ok && check<opaque>().ok);

}  // namespace skiff::layout
