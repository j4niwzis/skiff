// layout_bench.edits -- the edits a messenger makes to its chat, each made
// to the tree and told the engine as the rules read it: a word typed into
// a message, a tick widened, the window resized, a message added; and the
// check of a result against a layout from nothing.
export module layout_bench.edits;

import std;
export import layout_bench.engines;

export namespace layout_bench {

struct type_word {};
struct widen_tick {};
struct resize {};
struct new_message {};
using edit_t = spl::variant<type_word, widen_tick, resize, new_message>;
constexpr std::string_view name_of(type_word) { return "type a word"; }
constexpr std::string_view name_of(widen_tick) { return "widen a tick"; }
constexpr std::string_view name_of(resize) { return "resize"; }
constexpr std::string_view name_of(new_message) { return "add a message"; }

// An edit made to the chat, and what it changes told the engine: the
// inputs it changed, as the rules read them.
template <class Engine>
void apply(type_word, chat& c, Engine& engine, std::size_t step) {
  const node_id body = c.bodies[(step * 7919) % c.bodies.size()];
  spl::get<text>(c.nodes.kinds[body]).say(31.0f);
  engine.mark(pass_id::pref, body, c.nodes);
  engine.mark(pass_id::height, body, c.nodes);
}
template <class Engine>
void apply(widen_tick, chat& c, Engine& engine, std::size_t step) {
  const node_id tick = c.ticks[(step * 7919) % c.ticks.size()];
  auto& box = spl::get<fixed>(c.nodes.kinds[tick]);
  box.w = box.w == 12.0f ? 16.0f : 12.0f;
  engine.mark(pass_id::pref, tick, c.nodes);
}
template <class Engine>
void apply(resize, chat& c, Engine& engine, std::size_t step) {
  c.nodes.window = step % 2 == 0 ? 640.0f : 900.0f;
  c.nodes.at(width, 0) = c.nodes.window;
  engine.mark(pass_id::width, 0, c.nodes);
}
template <class Engine>
void apply(new_message, chat& c, Engine& engine, std::size_t) {
  const auto before = static_cast<node_id>(c.nodes.size());
  c.add(1);
  engine.grow(c.nodes);
  engine.added_child(c.nodes, c.list);
  for (node_id n = before; n < c.nodes.size(); ++n) {
    engine.mark_whole(pass_id::pref, n, c.nodes);
    engine.mark_whole(pass_id::height, n, c.nodes);
    if (!c.nodes.children[n].empty()) {
      engine.mark(pass_id::width, n, c.nodes);
      engine.mark_place(n, 0, c.nodes);
    }
  }
  engine.mark(pass_id::width, c.list, c.nodes);
  engine.mark_place(c.list, c.nodes.places[before], c.nodes);
}

// The same tree laid out from nothing.
[[nodiscard]] inline bool same_as_fresh(const chat& c) {
  tree fresh = c.nodes;
  for (auto& each : fresh.values) std::ranges::fill(each, 0.0f);
  full{}.run(fresh);
  // Sums kept by delta round differently from sums made again: equal to
  // within a hundredth of a pixel.
  return std::ranges::equal(fresh.values | std::views::join, c.nodes.values | std::views::join,
                            [](float a, float b) { return std::abs(a - b) <= 0.01f + 1e-6f * std::abs(b); });
}


}  // namespace layout_bench
