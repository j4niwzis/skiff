// The benchmark: a chat of 100, 1,000 and 10,000 messages laid out, then
// edited as a messenger is -- a word typed into a message, a tick widened,
// the window resized, a message added -- and laid out again after every
// edit by each engine: all of it again, the two dirty bits, the queue. Each
// says how long an edit took on average and how many nodes it visited; and
// each result is checked against a layout from nothing.
import std;
import layout_bench.engines;

using namespace layout_bench;

namespace {

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
  spl::get<text>(c.nodes.kinds[body]).words.push_back(31.0f);
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
[[nodiscard]] bool same_as_fresh(const chat& c) {
  tree fresh = c.nodes;
  for (auto& each : fresh.values) std::ranges::fill(each, 0.0f);
  full{}.run(fresh);
  // Sums kept by delta round differently from sums made again: equal to
  // within a hundredth of a pixel.
  return std::ranges::equal(fresh.values | std::views::join, c.nodes.values | std::views::join,
                            [](float a, float b) { return std::abs(a - b) <= 0.01f + 1e-6f * std::abs(b); });
}

struct result {
  double us_per_edit = 0.0;
  double visits_per_edit = 0.0;
  bool right = false;
};
template <class Engine>
result measure(edit_t edit, std::size_t messages, std::size_t edits) {
  chat c = make_chat(messages, 640.0f, 42);
  full{}.run(c.nodes);
  Engine engine;
  engine.grow(c.nodes);
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t step = 0; step < edits; ++step) {
    spl::visit([&](auto one) { apply(one, c, engine, step); }, edit);
    engine.run(c.nodes);
  }
  const std::chrono::duration<double, std::micro> took = std::chrono::steady_clock::now() - start;
  return {took.count() / static_cast<double>(edits), static_cast<double>(engine.visits) / static_cast<double>(edits), same_as_fresh(c)};
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::size_t> sizes = argc > 1 ? std::views::iota(1, argc) | std::views::transform([&](int i) { return static_cast<std::size_t>(std::stoul(argv[i])); }) | std::ranges::to<std::vector>()
                                                  : std::vector<std::size_t>{100, 1000, 10000};
  std::println("build: {}", kErased ? "Debug (erased kinds)" : "release (concrete kinds)");
  std::println("{:<14} {:>8} {:>8}  {:<12} {:>12} {:>14}  {}", "edit", "messages", "nodes", "engine", "us/edit", "visits/edit", "check");
  for (const std::size_t messages : sizes) {
    const std::size_t nodes = make_chat(messages, 640.0f, 42).nodes.size();
    // A first layout: every node, every pass.
    {
      chat c = make_chat(messages, 640.0f, 42);
      const auto start = std::chrono::steady_clock::now();
      full first;
      first.run(c.nodes);
      const std::chrono::duration<double, std::micro> took = std::chrono::steady_clock::now() - start;
      std::println("{:<14} {:>8} {:>8}  {:<12} {:>12.1f} {:>14.0f}  {}", "first layout", messages, nodes, "full", took.count(), static_cast<double>(first.visits), "-");
    }
    for (const edit_t edit : {edit_t{type_word{}}, edit_t{widen_tick{}}, edit_t{resize{}}, edit_t{new_message{}}}) {
      const std::size_t edits = 200;
      const auto row = [&](std::string_view engine, result r) {
        std::println("{:<14} {:>8} {:>8}  {:<12} {:>12.2f} {:>14.1f}  {}", spl::visit([](auto one) { return name_of(one); }, edit), messages, nodes,
                     engine, r.us_per_edit, r.visits_per_edit, r.right ? "ok" : "WRONG");
      };
      row("full", measure<full>(edit, messages, edits));
      row("double dirty", measure<double_dirty>(edit, messages, edits));
      row("spineless", measure<spineless>(edit, messages, edits));
    }
  }
}
