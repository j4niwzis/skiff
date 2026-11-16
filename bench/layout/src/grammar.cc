// layout_bench.grammar -- layout said as an attribute grammar (Meyerovich et
// al., "Parallel Schedule Synthesis for Attribute Grammars", PPoPP'13): what
// each kind of node computes, from what of itself, its parent and its
// children; the schedule of tree passes that computes it all; and the check,
// while compiling, that the schedule computes every attribute after what it
// reads -- one kind at a time, so that its cost grows with the kinds there
// are, never with the trees they make.
export module layout_bench.grammar;

import std;

export namespace layout_bench {

// What layout computes for every node: its natural width (pref), the width
// it is given, its height, and where it is in its parent.
enum class attribute : std::uint8_t { pref, width, height, x, y };
inline constexpr std::size_t kAttributes = 5;

// Whose attribute a rule reads or writes: the node's own, its parent's, its
// children's (all of them, as a container reads them).
enum class place : std::uint8_t { self, parent, children };
struct reads {
  attribute what;
  place where;
};
// One rule of a kind: an attribute of its own (synthesized) or of its
// children (inherited, as a container gives them), from what it reads.
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

// The passes: a postorder pass (children before their parent: what flows
// up) or a preorder one (a parent before its children: what flows down),
// and the attributes each computes.
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
// Box layout's: natural widths up, widths down, heights up (text wrapping
// at the width it was given), places down.
inline constexpr std::array kSchedule{walk(order::post, {attribute::pref}), walk(order::pre, {attribute::width}),
                                      walk(order::post, {attribute::height}), walk(order::pre, {attribute::x, attribute::y})};

// What the check says of a kind: right, or the first rule that is not, and
// why.
struct verdict {
  bool ok = true;
  std::string_view why;
  std::size_t rule = 0;
  friend constexpr bool operator==(const verdict&, const verdict&) = default;
};

// The pass that computes an attribute, where one does.
template <auto& Schedule>
[[nodiscard]] consteval std::optional<std::size_t> pass_of(attribute what) {
  const auto found = std::ranges::find_if(Schedule, [&](const pass& one) { return one.computes[std::to_underlying(what)]; });
  return found == Schedule.end() ? std::nullopt : std::optional<std::size_t>(found - Schedule.begin());
}
// Whether a rule may read what it reads in the pass that runs it: anything
// an earlier pass computed; in the same pass, only what that pass's order
// has computed already -- in a postorder pass, the node's children's, in a
// preorder one, its own (given it by its parent) or its parent's. Never a
// sibling's of the same pass: siblings go side by side.
template <auto& Schedule>
[[nodiscard]] consteval bool may_read(const rule& one, const reads& read) {
  const auto at = pass_of<Schedule>(one.target);
  const auto then = pass_of<Schedule>(read.what);
  if (!at || !then || *then > *at)
    return false;
  if (*then < *at)
    return true;
  switch (Schedule[*at].walk) {
    case order::post: return one.of == place::self && read.where == place::children;
    case order::pre:
      return (one.of == place::children && read.where == place::self) || (one.of == place::self && read.where == place::parent);
  }
  return false;
}
// A kind's rules, checked against the schedule: each in a pass that may
// write it (up a postorder pass, down a preorder one), reading only what it
// may; and every attribute there is computed for the kind -- its own
// synthesized ones, and, where it has children, theirs.
template <class Kind, auto& Schedule = kSchedule>
[[nodiscard]] consteval verdict check() {
  constexpr auto& rules = Kind::rules;
  for (const auto [index, one] : std::views::enumerate(rules)) {
    const auto at = pass_of<Schedule>(one.target);
    if (!at)
      return {false, "no pass computes the attribute", static_cast<std::size_t>(index)};
    const bool upward = one.of == place::self && Schedule[*at].walk == order::post;
    const bool downward = Schedule[*at].walk == order::pre;
    if (!upward && !downward)
      return {false, "a postorder pass cannot give children an attribute", static_cast<std::size_t>(index)};
    if (!std::ranges::all_of(one.from | std::views::take(one.count), [&](const reads& read) { return may_read<Schedule>(one, read); }))
      return {false, "a rule reads what its pass has not computed yet", static_cast<std::size_t>(index)};
  }
  const auto has = [&](attribute what, place of) { return std::ranges::any_of(rules, [&](const rule& one) { return one.target == what && one.of == of; }); };
  if (!has(attribute::pref, place::self) || !has(attribute::height, place::self))
    return {false, "a kind leaves its own pref or height uncomputed", 0};
  if (Kind::holds && !(has(attribute::width, place::children) && has(attribute::x, place::children) && has(attribute::y, place::children)))
    return {false, "a container leaves its children's width or place uncomputed", 0};
  return {};
}

}  // namespace layout_bench
