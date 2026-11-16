// The benchmark: a chat of 100, 1,000 and 10,000 messages laid out, then
// edited as a messenger is -- a word typed into a message, a tick widened,
// the window resized, a message added -- and laid out again after every
// edit by each engine: all of it again, the two dirty bits, the queue. Each
// says how long an edit took on average and how many nodes it visited; and
// each result is checked against a layout from nothing.
import std;
import layout_bench.edits;

using namespace layout_bench;

namespace {

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
    // Each pass of it alone, a layout done before (so every value is
    // there): where a first layout's time goes.
    {
      chat c = make_chat(messages, 640.0f, 42);
      full{}.run(c.nodes);
      const auto time_pass = [&]<pass_id Pass>(std::string_view name) {
        full engine;
        const auto start = std::chrono::steady_clock::now();
        for (int round = 0; round < 10; ++round) flat_pass<Pass>(engine, c.nodes);
        const std::chrono::duration<double, std::micro> took = std::chrono::steady_clock::now() - start;
        std::println("{:<14} {:>8} {:>8}  {:<12} {:>12.1f} {:>14}  {}", name, messages, nodes, "pass", took.count() / 10.0, nodes, "-");
      };
      time_pass.template operator()<pass_id::pref>("  prefs up");
      time_pass.template operator()<pass_id::width>("  widths down");
      time_pass.template operator()<pass_id::height>("  heights up");
      time_pass.template operator()<pass_id::place>("  places down");
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
