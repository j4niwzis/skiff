// layout_bench.tree -- the trees laid out: four kinds of node (a column, a
// row, wrapping text, a box of a fixed size), each with its rules; a tree
// kept flat, in preorder, its attributes in arrays of their own; and a chat
// made of them, as a messenger's timeline has it.
export module layout_bench.tree;

import std;
export import layout_bench.grammar;

export namespace layout_bench {

using enum attribute;
using enum place;

// A column: its children one under another, each as wide as it is inside.
struct column {
  float gap = 0.0f, pad = 0.0f;
  static constexpr bool holds = true;
  static constexpr std::array rules{
      computes(pref, self, {{pref, children}}),
      computes(width, children, {{width, self}}),
      computes(height, self, {{height, children}}),
      computes(x, children, {}),
      computes(y, children, {{height, children}}),
  };
};
// A row: its children side by side, each as wide as it would be, but for
// the one that grows (if any), which takes what is left.
struct row {
  float gap = 0.0f, pad = 0.0f;
  std::size_t grows = std::numeric_limits<std::size_t>::max();
  static constexpr bool holds = true;
  static constexpr std::array rules{
      computes(pref, self, {{pref, children}}),
      computes(width, children, {{width, self}, {pref, children}}),
      computes(height, self, {{height, children}}),
      computes(x, children, {{width, children}}),
      computes(y, children, {}),
  };
};
// Text: its words' widths, wrapped where they reach the width it is given.
struct text {
  std::vector<float> words;
  float space = 4.0f, line = 18.0f;
  static constexpr bool holds = false;
  static constexpr std::array rules{
      computes(pref, self, {}),
      computes(height, self, {{width, self}}),
  };
};
// A box of its own size: an avatar, an icon.
struct fixed {
  float w = 0.0f, h = 0.0f;
  static constexpr bool holds = false;
  static constexpr std::array rules{
      computes(pref, self, {}),
      computes(height, self, {}),
  };
};
using kind_t = std::variant<column, row, text, fixed>;

// Every kind checked while compiling: the grammar holds for all of them.
static_assert(check<column>().ok && check<row>().ok && check<text>().ok && check<fixed>().ok);

using node_id = std::uint32_t;
inline constexpr node_id kNone = std::numeric_limits<node_id>::max();

// A tree, flat: its nodes in preorder (a parent before everything under it),
// so that a postorder pass is the nodes from the last to the first, a
// preorder one from the first to the last -- the arrays read in order.
struct tree {
  std::vector<kind_t> kinds;
  std::vector<node_id> parents;
  std::vector<std::vector<node_id>> children;
  std::array<std::vector<float>, kAttributes> values;
  float window = 0.0f;

  [[nodiscard]] std::size_t size() const { return kinds.size(); }
  [[nodiscard]] float& at(attribute what, node_id n) { return values[std::to_underlying(what)][n]; }
  [[nodiscard]] float at(attribute what, node_id n) const { return values[std::to_underlying(what)][n]; }
  node_id add(kind_t kind, node_id parent) {
    const auto id = static_cast<node_id>(kinds.size());
    kinds.push_back(std::move(kind));
    parents.push_back(parent);
    children.emplace_back();
    for (auto& each : values) each.push_back(0.0f);
    if (parent != kNone)
      children[parent].push_back(id);
    return id;
  }
};

// Words as a message has them: deterministic, by the seed.
[[nodiscard]] inline std::vector<float> words(std::mt19937& random, std::size_t least, std::size_t most) {
  std::uniform_int_distribution<std::size_t> count(least, most);
  std::uniform_real_distribution<float> width(14.0f, 70.0f);
  return std::views::iota(std::size_t{0}, count(random)) | std::views::transform([&](std::size_t) { return width(random); }) |
         std::ranges::to<std::vector>();
}
// One message, under the list: its avatar; its name, its text, and its time
// with the tick by it -- eight nodes, as a timeline row has about as many.
// Its text and its tick, which edits change.
struct message_ids {
  node_id body, tick;
};
inline message_ids add_message(tree& t, node_id list, std::mt19937& random) {
  const node_id bubble = t.add(row{8.0f, 4.0f, 1}, list);
  t.add(fixed{32.0f, 32.0f}, bubble);
  const node_id body = t.add(column{2.0f, 6.0f}, bubble);
  t.add(text{words(random, 1, 3)}, body);
  const node_id said = t.add(text{words(random, 3, 60)}, body);
  const node_id meta = t.add(row{4.0f, 0.0f}, body);
  t.add(text{words(random, 1, 1)}, meta);
  return {said, t.add(fixed{12.0f, 12.0f}, meta)};
}
// The list of a chat: the window, its header, and the messages -- the list
// last, so a message added keeps the nodes in preorder.
struct chat {
  tree nodes;
  node_id list = kNone;
  std::vector<node_id> bodies, ticks;
  std::mt19937 random;
  void add(std::size_t messages) {
    for (std::size_t i = 0; i < messages; ++i) {
      const auto [body, tick] = add_message(nodes, list, random);
      bodies.push_back(body);
      ticks.push_back(tick);
    }
  }
};
[[nodiscard]] inline chat make_chat(std::size_t messages, float window, std::uint32_t seed) {
  chat out;
  out.random.seed(seed);
  auto& random = out.random;
  tree& t = out.nodes;
  t.window = window;
  const node_id root = t.add(column{}, kNone);
  const node_id header = t.add(row{8.0f, 8.0f, 1}, root);
  t.add(fixed{24.0f, 24.0f}, header);
  t.add(text{words(random, 2, 4)}, header);
  out.list = t.add(column{4.0f, 8.0f}, root);
  out.add(messages);
  return out;
}

}  // namespace layout_bench
