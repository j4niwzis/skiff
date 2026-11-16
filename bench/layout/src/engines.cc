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
// Each kind's code reached through spl::visit: through tables in a Debug
// build, as skiff's erased walks are, concretely in a release one.
export module layout_bench.engines;

import std;
export import layout_bench.tree;

export namespace layout_bench {

using enum attribute;
inline constexpr bool kErased = LAYOUT_BENCH_ERASED != 0;
inline constexpr std::size_t kPasses = kSchedule.size();
enum class pass_id : std::uint8_t { pref, width, height, place };
[[nodiscard]] constexpr std::size_t index(pass_id one) { return std::to_underlying(one); }

// A kind's alternative visited: spl::variant reaches it through tables
// outside a release build (SPLICE_ERASED), as skiff's erased walks do, and
// concretely in one.
template <class Visit>
decltype(auto) on_kind(const kind_t& kind, Visit&& visit) {
  return spl::visit(std::forward<Visit>(visit), kind);
}

// ---- the rules, as code ----------------------------------------------------------
[[nodiscard]] inline auto values_of(const tree& t, attribute what, node_id n) {
  return t.children[n] | std::views::transform([&t, what](node_id c) { return t.at(what, c); });
}
[[nodiscard]] inline float gaps(float gap, std::size_t count) { return count > 1 ? gap * static_cast<float>(count - 1) : 0.0f; }
// Text wrapped at a width: its lines, as many as its words need.
[[nodiscard]] inline float wrapped_height(const text& k, float width) {
  // On one line where it fits: no words walked.
  if (k.total <= width) return k.line;
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
  return on_kind(t.kinds[n], spl::overloaded{
      [&](const column& k) { return std::ranges::max(values_of(t, pref, n), {}, std::identity{}) + 2.0f * k.pad; },
      [&](const row& k) { return std::ranges::fold_left(values_of(t, pref, n), 0.0f, std::plus{}) + gaps(k.gap, t.children[n].size()) + 2.0f * k.pad; },
      [&](const text& k) { return k.total; },
      [&](const fixed& k) { return k.w; }});
}
[[nodiscard]] inline float height_of(const tree& t, node_id n) {
  return on_kind(t.kinds[n], spl::overloaded{
      [&](const column& k) { return std::ranges::fold_left(values_of(t, height, n), 0.0f, std::plus{}) + gaps(k.gap, t.children[n].size()) + 2.0f * k.pad; },
      [&](const row& k) { return std::ranges::fold_left(values_of(t, height, n), 0.0f, [](float a, float b) { return std::max(a, b); }) + 2.0f * k.pad; },
      [&](const text& k) { return wrapped_height(k, t.at(width, n)); },
      [&](const fixed& k) { return k.h; }});
}
// What a container gives its children: their widths. `set(what, child,
// value)` keeps it and says whether it changed.
template <class Set>
void give_widths(tree& t, node_id n, Set& set) {
  on_kind(t.kinds[n], spl::overloaded{
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
// And their places, from the sizes they came to: from the child `from`
// on -- those before it are where they were.
template <class Set>
void give_places(tree& t, node_id n, Set& set, std::size_t from = 0) {
  const auto& kids = t.children[n];
  if (from >= kids.size()) return;
  on_kind(t.kinds[n], spl::overloaded{
      [&](const column& k) {
        float at = from == 0 ? k.pad : t.at(y, kids[from - 1]) + t.at(height, kids[from - 1]) + k.gap;
        for (node_id c : kids | std::views::drop(from)) {
          set(x, c, k.pad);
          set(y, c, at);
          at += t.at(height, c) + k.gap;
        }
      },
      [&](const row& k) {
        float at = from == 0 ? k.pad : t.at(x, kids[from - 1]) + t.at(width, kids[from - 1]) + k.gap;
        for (node_id c : kids | std::views::drop(from)) {
          set(x, c, at);
          set(y, c, k.pad);
          at += t.at(width, c) + k.gap;
        }
      },
      [](const text&) {}, [](const fixed&) {}});
}

// ---- what a change makes dirty, as the rules read ---------------------------------
// Which of its children's attributes a kind reads to give them another:
// read off its rules while compiling, so that a change marks only the
// passes of the parents whose rules read it -- a column's widths do not
// read its children's prefs, a row's places do not read their heights.
template <class Kind>
inline constexpr auto kGivesFrom = [] {
  std::array<std::array<bool, kAttributes>, kAttributes> out{};
  for (const rule& one : Kind::rules)
    if (one.of == place::children)
      for (const reads& read : one.from | std::views::take(one.count))
        if (read.where == place::children) out[std::to_underlying(one.target)][std::to_underlying(read.what)] = true;
  return out;
}();
[[nodiscard]] inline bool gives_from(const tree& t, node_id n, attribute target, attribute what) {
  return spl::visit([&]<class Kind>(const Kind&) { return kGivesFrom<Kind>[std::to_underlying(target)][std::to_underlying(what)]; }, t.kinds[n]);
}
// A node's pref changed: its parent's pref, and the widths its parent gives
// where they read it. A child's width changed: the widths it gives, its own
// height (text wraps at it), and the places its parent gives where they
// read it. A height changed: its parent's height, and its parent's places
// where they read it.
template <class Engine>
void pref_changed(Engine& engine, const tree& t, node_id n) {
  if (const node_id p = t.parents[n]; p != kNone) {
    engine.mark_up(pass_id::pref, p);
    if (gives_from(t, p, width, pref)) engine.mark(pass_id::width, p, t);
  }
}
template <class Engine>
void width_changed(Engine& engine, const tree& t, node_id parent, node_id child) {
  engine.mark_down(pass_id::width, parent, child);
  engine.mark(pass_id::height, child, t);
  if (gives_from(t, parent, x, width) || gives_from(t, parent, y, width)) engine.mark_place(parent, t.places[child], t);
}
template <class Engine>
void height_changed(Engine& engine, const tree& t, node_id n) {
  if (const node_id p = t.parents[n]; p != kNone) {
    engine.mark_up(pass_id::height, p);
    if (gives_from(t, p, x, height) || gives_from(t, p, y, height)) engine.mark_place(p, t.places[n], t);
  }
}
// ---- a container's aggregate kept by delta -----------------------------------------
// A column's height is the sum of its children's, its pref their max; a
// row's the other way round. Recomputed, each costs as many children as
// the container has -- a list of 10,000 messages, at every word typed. Kept
// by delta, a child's change is folded into its parent where it happens: a
// sum moved by the difference, a max raised, or recomputed only where the
// child that was the max went down.
inline void fold_max(tree& t, node_id p, attribute what, float pad, float before, float now) {
  const float inner = t.at(what, p) - 2.0f * pad;
  if (now >= inner)
    t.at(what, p) = now + 2.0f * pad;
  else if (before >= inner)
    t.at(what, p) = what == pref ? pref_of(t, p) : height_of(t, p);
}
inline void fold(tree& t, node_id p, attribute what, float before, float now) {
  on_kind(t.kinds[p], spl::overloaded{
      [&](const column& k) {
        if (what == height) t.at(what, p) += now - before;
        else fold_max(t, p, what, k.pad, before, now);
      },
      [&](const row& k) {
        if (what == pref) t.at(what, p) += now - before;
        else fold_max(t, p, what, k.pad, before, now);
      },
      [](const text&) {}, [](const fixed&) {}});
}
// A child added: the sum gains a gap (its value comes in by delta, from 0).
inline void child_added(tree& t, node_id p) {
  on_kind(t.kinds[p], spl::overloaded{
      [&](const column& k) { if (t.children[p].size() > 1) t.at(height, p) += k.gap; },
      [&](const row& k) { if (t.children[p].size() > 1) t.at(pref, p) += k.gap; },
      [](const text&) {}, [](const fixed&) {}});
}
[[nodiscard]] inline bool holds_children(const tree& t, node_id n) {
  return spl::visit([]<class Kind>(const Kind&) { return Kind::holds; }, t.kinds[n]);
}

// One node's visit in a pass, the changes it makes told the engine. Where
// the engine keeps aggregates by delta, a container's value is already
// what its children made it (unless it is to be computed whole: new, or
// its inputs changed), and what changed is told against what its parent
// last folded in.
template <class Engine>
void synthesize(Engine& engine, tree& t, pass_id pass, attribute what, node_id n) {
  const auto compute = [&] { return what == pref ? pref_of(t, n) : height_of(t, n); };
  if constexpr (Engine::kDelta) {
    const float now = holds_children(t, n) && !engine.take_whole(pass, n) ? t.at(what, n) : compute();
    t.at(what, n) = now;
    float& told = engine.told(what, n);
    if (told == now) return;
    const float before = std::exchange(told, now);
    if (const node_id p = t.parents[n]; p != kNone) fold(t, p, what, before, now);
  } else {
    const float now = compute();
    if (std::exchange(t.at(what, n), now) == now) return;
  }
  if (what == pref) pref_changed(engine, t, n);
  else height_changed(engine, t, n);
}
// The pass known while compiling: a pass over every node is one loop of
// that pass's code, not a switch at each node.
template <pass_id Pass, class Engine>
void visit(Engine& engine, tree& t, node_id n) {
  ++engine.visits;
  if constexpr (Pass == pass_id::pref) {
    synthesize(engine, t, Pass, pref, n);
  } else if constexpr (Pass == pass_id::height) {
    synthesize(engine, t, Pass, height, n);
  } else if constexpr (Pass == pass_id::width) {
    auto set = [&](attribute what, node_id c, float value) {
      if (std::exchange(t.at(what, c), value) != value) width_changed(engine, t, n, c);
    };
    give_widths(t, n, set);
  } else {
    auto set = [&](attribute what, node_id c, float value) { t.at(what, c) = value; };
    give_places(t, n, set, engine.take_place_from(n));
  }
}
template <class Engine>
void visit(Engine& engine, tree& t, pass_id pass, node_id n) {
  switch (pass) {
    case pass_id::pref: visit<pass_id::pref>(engine, t, n); break;
    case pass_id::width: visit<pass_id::width>(engine, t, n); break;
    case pass_id::height: visit<pass_id::height>(engine, t, n); break;
    case pass_id::place: visit<pass_id::place>(engine, t, n); break;
  }
}
// Every node in a pass, in the arrays' order: from the end for a
// postorder pass, from the start for a preorder one.
template <pass_id Pass, class Engine>
void flat_pass(Engine& engine, tree& t) {
  const auto n = static_cast<node_id>(t.size());
  if constexpr (kSchedule[index(Pass)].walk == order::post)
    for (node_id i = n; i-- > 0;) visit<Pass>(engine, t, i);
  else
    for (node_id i = 0; i < n; ++i) visit<Pass>(engine, t, i);
}
template <class Engine>
void flat_pass(Engine& engine, tree& t, pass_id pass) {
  switch (pass) {
    case pass_id::pref: flat_pass<pass_id::pref>(engine, t); break;
    case pass_id::width: flat_pass<pass_id::width>(engine, t); break;
    case pass_id::height: flat_pass<pass_id::height>(engine, t); break;
    case pass_id::place: flat_pass<pass_id::place>(engine, t); break;
  }
}
[[nodiscard]] constexpr bool upward(pass_id pass) { return kSchedule[index(pass)].walk == order::post; }

// ---- every node, every pass --------------------------------------------------------
// The tree in preorder: a postorder pass is it read from the end, a
// preorder one from the start.
struct full {
  static constexpr bool kDelta = false;
  std::size_t visits = 0;
  void mark_whole(pass_id, node_id, const tree&) {}
  void added_child(tree&, node_id) {}
  void mark_place(node_id, std::size_t, const tree&) {}
  [[nodiscard]] std::size_t take_place_from(node_id) const { return 0; }
  void mark(pass_id, node_id, const tree&) {}
  void mark_up(pass_id, node_id) {}
  void mark_down(pass_id, node_id, node_id) {}
  void grow(const tree&) {}
  void run(tree& t) {
    t.at(width, 0) = t.window;
    flat_pass<pass_id::pref>(*this, t);
    flat_pass<pass_id::width>(*this, t);
    flat_pass<pass_id::height>(*this, t);
    flat_pass<pass_id::place>(*this, t);
  }
};

// What both incremental engines keep besides their marks: each node's pref
// and height as its parent last folded them in, and which containers are
// to be computed whole.
struct delta_state {
  static constexpr bool kDelta = true;
  std::array<std::vector<float>, 2> folded;
  std::array<std::vector<std::uint8_t>, 2> whole;
  [[nodiscard]] static constexpr std::size_t slot(attribute what) { return what == pref ? 0 : 1; }
  [[nodiscard]] static constexpr std::size_t slot(pass_id pass) { return pass == pass_id::pref ? 0 : 1; }
  float& told(attribute what, node_id n) { return folded[slot(what)][n]; }
  bool take_whole(pass_id pass, node_id n) { return std::exchange(whole[slot(pass)][n], 0) != 0; }
  // The first of a container's children whose place may have moved: its
  // places are given again from there on (none marked: from the start).
  std::vector<std::uint32_t> place_from;
  [[nodiscard]] std::size_t take_place_from(node_id n) {
    const std::uint32_t from = std::exchange(place_from[n], kUnmarked);
    return from == kUnmarked ? 0 : from;
  }
  void grow_delta(const tree& t) {
    for (const attribute what : {pref, height}) {
      auto& kept = folded[slot(what)];
      const auto had = kept.size();
      kept.resize(t.size());
      std::ranges::copy(t.values[std::to_underlying(what)] | std::views::drop(had), kept.begin() + static_cast<std::ptrdiff_t>(had));
      whole[slot(what)].resize(t.size(), 0);
    }
    place_from.resize(t.size(), kUnmarked);
  }
  static constexpr std::uint32_t kUnmarked = std::numeric_limits<std::uint32_t>::max();
  void note_place(node_id parent, std::size_t from) {
    place_from[parent] = std::min(place_from[parent], static_cast<std::uint32_t>(from));
  }
};

// ---- two dirty bits: the node's, and "something under it" ---------------------------
struct double_dirty : delta_state {
  std::size_t visits = 0;
  std::array<std::vector<std::uint8_t>, kPasses> dirty, below;
  void grow(const tree& t) {
    grow_delta(t);
    for (auto& each : dirty) each.resize(t.size(), 0);
    for (auto& each : below) each.resize(t.size(), 0);
  }
  void mark_whole(pass_id pass, node_id n, const tree& t) {
    whole[slot(pass)][n] = 1;
    mark(pass, n, t);
  }
  void added_child(tree& t, node_id p) {
    child_added(t, p);
    mark(pass_id::pref, p, t);
    mark(pass_id::height, p, t);
  }
  void mark_place(node_id parent, std::size_t from, const tree& t) {
    note_place(parent, from);
    mark(pass_id::place, parent, t);
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
struct spineless : delta_state {
  // The queue's share of the tree past which a flat pass is taken.
  static constexpr std::size_t kFlatBeyond = 8;
  std::size_t visits = 0;
  std::array<std::vector<std::uint8_t>, kPasses> queued;
  std::array<std::vector<node_id>, kPasses> heaps;
  void grow(const tree& t) {
    grow_delta(t);
    size = t.size();
    for (auto& each : queued) each.resize(t.size(), 0);
  }
  void mark_whole(pass_id pass, node_id n, const tree& t) {
    whole[slot(pass)][n] = 1;
    mark(pass, n, t);
  }
  void added_child(tree& t, node_id p) {
    child_added(t, p);
    mark(pass_id::pref, p, t);
    mark(pass_id::height, p, t);
  }
  void mark_place(node_id parent, std::size_t from, const tree& t) {
    note_place(parent, from);
    mark(pass_id::place, parent, t);
  }
  // Passes to be run flat: decided while marking, once the queue passes
  // its share -- the queue then grows no more.
  std::array<bool, kPasses> flat{};
  std::size_t size = 0;
  void push(pass_id pass, node_id n) {
    if (flat[index(pass)] || std::exchange(queued[index(pass)][n], 1)) return;
    auto& heap = heaps[index(pass)];
    if ((heap.size() + 1) * kFlatBeyond > size) {
      flat[index(pass)] = true;
      for (node_id each : heap) queued[index(pass)][each] = 0;
      queued[index(pass)][n] = 0;
      heap.clear();
      return;
    }
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
      // The queue taken in order -- until it passes its share, before the
      // pass or while it runs (a preorder pass marking every child): then
      // the rest of the pass is one flat pass over the arrays, the marks
      // for it dropped meanwhile, since it reaches every node anyway.
      while (true) {
        if (flat[p]) {
          flat_pass(*this, t, pass);
          flat[p] = false;
          break;
        }
        if (heap.empty())
          break;
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
