// layout_bench.engines -- what each kind computes in each pass (the rules of
// layout_bench.tree, as code), and three ways of running the schedule over
// a tree: every node in every pass (what a first layout does, and a
// relayout that remembers nothing); again only where something changed,
// found through two dirty bits per node and pass -- the node's own and
// "something under it" -- walking down the spine to it (what browsers do
// today, and skiff's fLayoutValid / fSubtreeDirty); and again only where
// something changed, kept in a queue in the tree's order that never visits
// a node it does not recompute (Kirisame, Wang and Panchekha's Spineless
// Traversal, PLDI 2025).
//
// A Debug build reaches every kind's code through a table, as skiff's
// erased walks do; a release build visits the variant, concrete.
export module layout_bench.engines;

import std;
export import layout_bench.tree;

export namespace layout_bench {

using enum attribute;
inline constexpr bool kErased = LAYOUT_BENCH_ERASED != 0;
inline constexpr std::size_t kPasses = kSchedule.size();
enum class pass_id : std::uint8_t { pref, width, height, place };
[[nodiscard]] constexpr std::size_t index(pass_id one) { return std::to_underlying(one); }

template <class... F> struct overloaded : F... { using F::operator()...; };

// A kind's alternative, reached as the build has it: the variant visited
// (release), or a table of one function for each alternative (Debug).
template <class Visit>
decltype(auto) on_kind(const kind_t& kind, Visit&& visit) {
  if constexpr (kErased) {
    using result = decltype(std::visit(visit, kind));
    using entry = result (*)(const kind_t&, Visit&);
    static constexpr auto table = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::array<entry, sizeof...(I)>{+[](const kind_t& k, Visit& v) -> result { return v(std::get<I>(k)); }...};
    }(std::make_index_sequence<std::variant_size_v<kind_t>>{});
    return table[kind.index()](kind, visit);
  } else {
    return std::visit(std::forward<Visit>(visit), kind);
  }
}

// ---- the rules, as code ----------------------------------------------------------
[[nodiscard]] inline auto values_of(const tree& t, attribute what, node_id n) {
  return t.children[n] | std::views::transform([&t, what](node_id c) { return t.at(what, c); });
}
[[nodiscard]] inline float gaps(float gap, std::size_t count) { return count > 1 ? gap * static_cast<float>(count - 1) : 0.0f; }
// Text wrapped at a width: its lines, as many as its words need.
[[nodiscard]] inline float wrapped_height(const text& k, float width) {
  struct line_so_far {
    float used = 0.0f;
    int lines = 0;
  };
  const auto end = std::ranges::fold_left(k.words, line_so_far{}, [&](line_so_far now, float word) {
    if (now.lines == 0) return line_so_far{word, 1};
    const float next = now.used + k.space + word;
    return next <= width ? line_so_far{next, now.lines} : line_so_far{word, now.lines + 1};
  });
  return static_cast<float>(std::max(end.lines, 1)) * k.line;
}
[[nodiscard]] inline float pref_of(const tree& t, node_id n) {
  return on_kind(t.kinds[n], overloaded{
      [&](const column& k) { return std::ranges::max(values_of(t, pref, n), {}, std::identity{}) + 2.0f * k.pad; },
      [&](const row& k) { return std::ranges::fold_left(values_of(t, pref, n), 0.0f, std::plus{}) + gaps(k.gap, t.children[n].size()) + 2.0f * k.pad; },
      [&](const text& k) { return std::ranges::fold_left(k.words, 0.0f, std::plus{}) + gaps(k.space, k.words.size()); },
      [&](const fixed& k) { return k.w; }});
}
[[nodiscard]] inline float height_of(const tree& t, node_id n) {
  return on_kind(t.kinds[n], overloaded{
      [&](const column& k) { return std::ranges::fold_left(values_of(t, height, n), 0.0f, std::plus{}) + gaps(k.gap, t.children[n].size()) + 2.0f * k.pad; },
      [&](const row& k) { return std::ranges::fold_left(values_of(t, height, n), 0.0f, [](float a, float b) { return std::max(a, b); }) + 2.0f * k.pad; },
      [&](const text& k) { return wrapped_height(k, t.at(width, n)); },
      [&](const fixed& k) { return k.h; }});
}
// What a container gives its children: their widths. `set(what, child,
// value)` keeps it and says whether it changed.
template <class Set>
void give_widths(tree& t, node_id n, Set& set) {
  on_kind(t.kinds[n], overloaded{
      [&](const column& k) {
        const float inner = std::max(0.0f, t.at(width, n) - 2.0f * k.pad);
        for (node_id c : t.children[n]) set(width, c, inner);
      },
      [&](const row& k) {
        const auto& kids = t.children[n];
        const float inner = std::max(0.0f, t.at(width, n) - 2.0f * k.pad);
        const float natural = std::ranges::fold_left(values_of(t, pref, n), 0.0f, std::plus{}) + gaps(k.gap, kids.size());
        for (const auto [i, c] : std::views::enumerate(kids)) {
          const float own = t.at(pref, c);
          set(width, c, static_cast<std::size_t>(i) == k.grows ? std::max(0.0f, inner - (natural - own)) : std::min(own, inner));
        }
      },
      [](const text&) {}, [](const fixed&) {}});
}
// And their places, from the sizes they came to.
template <class Set>
void give_places(tree& t, node_id n, Set& set) {
  on_kind(t.kinds[n], overloaded{
      [&](const column& k) {
        float at = k.pad;
        for (node_id c : t.children[n]) {
          set(x, c, k.pad);
          set(y, c, at);
          at += t.at(height, c) + k.gap;
        }
      },
      [&](const row& k) {
        float at = k.pad;
        for (node_id c : t.children[n]) {
          set(x, c, at);
          set(y, c, k.pad);
          at += t.at(width, c) + k.gap;
        }
      },
      [](const text&) {}, [](const fixed&) {}});
}

// ---- what a change makes dirty, as the rules read ---------------------------------
// A node's pref changed: its parent's pref, and the widths its parent gives
// (a row's read its children's prefs). A child's width changed: the widths
// it gives, its own height (text wraps at it), and the places its parent
// gives (a row's read its children's widths). A height changed: its
// parent's height, and the places its parent gives.
template <class Engine>
void pref_changed(Engine& engine, const tree& t, node_id n) {
  if (const node_id p = t.parents[n]; p != kNone) {
    engine.mark_up(pass_id::pref, p);
    engine.mark(pass_id::width, p, t);
  }
}
template <class Engine>
void width_changed(Engine& engine, const tree& t, node_id parent, node_id child) {
  engine.mark_down(pass_id::width, parent, child);
  engine.mark(pass_id::height, child, t);
  engine.mark(pass_id::place, parent, t);
}
template <class Engine>
void height_changed(Engine& engine, const tree& t, node_id n) {
  if (const node_id p = t.parents[n]; p != kNone) {
    engine.mark_up(pass_id::height, p);
    engine.mark(pass_id::place, p, t);
  }
}
// One node's visit in a pass, the changes it makes told the engine.
template <class Engine>
void visit(Engine& engine, tree& t, pass_id pass, node_id n) {
  ++engine.visits;
  switch (pass) {
    case pass_id::pref: {
      const float now = pref_of(t, n);
      if (std::exchange(t.at(pref, n), now) != now) pref_changed(engine, t, n);
      break;
    }
    case pass_id::height: {
      const float now = height_of(t, n);
      if (std::exchange(t.at(height, n), now) != now) height_changed(engine, t, n);
      break;
    }
    case pass_id::width: {
      auto set = [&](attribute what, node_id c, float value) {
        if (std::exchange(t.at(what, c), value) != value) width_changed(engine, t, n, c);
      };
      give_widths(t, n, set);
      break;
    }
    case pass_id::place: {
      auto set = [&](attribute what, node_id c, float value) { t.at(what, c) = value; };
      give_places(t, n, set);
      break;
    }
  }
}
[[nodiscard]] constexpr bool upward(pass_id pass) { return kSchedule[index(pass)].walk == order::post; }

// ---- every node, every pass --------------------------------------------------------
// The tree in preorder: a postorder pass is it read from the end, a
// preorder one from the start.
struct full {
  std::size_t visits = 0;
  void mark(pass_id, node_id, const tree&) {}
  void mark_up(pass_id, node_id) {}
  void mark_down(pass_id, node_id, node_id) {}
  void grow(std::size_t) {}
  void run(tree& t) {
    t.at(width, 0) = t.window;
    for (std::size_t p = 0; p < kPasses; ++p) {
      const auto pass = static_cast<pass_id>(p);
      const auto n = static_cast<node_id>(t.size());
      if (upward(pass))
        for (node_id i = n; i-- > 0;) visit(*this, t, pass, i);
      else
        for (node_id i = 0; i < n; ++i) visit(*this, t, pass, i);
    }
  }
};

// ---- two dirty bits: the node's, and "something under it" ---------------------------
struct double_dirty {
  std::size_t visits = 0;
  std::array<std::vector<std::uint8_t>, kPasses> dirty, below;
  void grow(std::size_t size) {
    for (auto& each : dirty) each.resize(size, 0);
    for (auto& each : below) each.resize(size, 0);
  }
  // Dirty, from outside the pass: the spine above it marked to the root,
  // or as far as it is marked already.
  void mark(pass_id pass, node_id n, const tree& t) {
    dirty[index(pass)][n] = 1;
    for (node_id p = t.parents[n]; p != kNone && !below[index(pass)][p]; p = t.parents[p]) below[index(pass)][p] = 1;
  }
  // From within the pass: the parent of what a postorder pass is in (it is
  // on the way back up already), the child of what a preorder pass visits.
  void mark_up(pass_id pass, node_id parent) { dirty[index(pass)][parent] = 1; }
  void mark_down(pass_id pass, node_id parent, node_id child) {
    dirty[index(pass)][child] = 1;
    below[index(pass)][parent] = 1;
  }
  void walk(tree& t, pass_id pass, node_id n) {
    auto& own = dirty[index(pass)];
    auto& under = below[index(pass)];
    if (upward(pass)) {
      if (std::exchange(under[n], 0))
        for (node_id c : t.children[n]) if (dirty[index(pass)][c] || below[index(pass)][c]) walk(t, pass, c);
      if (std::exchange(own[n], 0)) visit(*this, t, pass, n);
    } else {
      if (std::exchange(own[n], 0)) visit(*this, t, pass, n);
      if (std::exchange(under[n], 0))
        for (node_id c : t.children[n]) if (dirty[index(pass)][c] || below[index(pass)][c]) walk(t, pass, c);
    }
  }
  void run(tree& t) {
    for (std::size_t p = 0; p < kPasses; ++p)
      if (dirty[p][0] || below[p][0]) walk(t, static_cast<pass_id>(p), 0);
  }
};

// ---- a queue in the tree's order: no spine walked ----------------------------------
// A postorder pass takes the latest node in preorder first (all under a
// node come after it), a preorder one the earliest.
struct spineless {
  std::size_t visits = 0;
  std::array<std::vector<std::uint8_t>, kPasses> queued;
  std::array<std::vector<node_id>, kPasses> heaps;
  void grow(std::size_t size) {
    for (auto& each : queued) each.resize(size, 0);
  }
  void push(pass_id pass, node_id n) {
    if (std::exchange(queued[index(pass)][n], 1)) return;
    auto& heap = heaps[index(pass)];
    heap.push_back(n);
    if (upward(pass)) std::ranges::push_heap(heap, std::less{});
    else std::ranges::push_heap(heap, std::greater{});
  }
  void mark(pass_id pass, node_id n, const tree&) { push(pass, n); }
  void mark_up(pass_id pass, node_id parent) { push(pass, parent); }
  void mark_down(pass_id pass, node_id, node_id child) { push(pass, child); }
  void run(tree& t) {
    for (std::size_t p = 0; p < kPasses; ++p) {
      const auto pass = static_cast<pass_id>(p);
      auto& heap = heaps[p];
      while (!heap.empty()) {
        if (upward(pass)) std::ranges::pop_heap(heap, std::less{});
        else std::ranges::pop_heap(heap, std::greater{});
        const node_id n = heap.back();
        heap.pop_back();
        queued[p][n] = 0;
        visit(*this, t, pass, n);
      }
    }
  }
};

}  // namespace layout_bench
