// The benchmark on Google Benchmark: the same chat and the same edits, laid
// out by skiff itself -- its scene, its nodes, compose's rows and columns,
// a MemoRows list fed (key, view) items as mux feeds its lists, built
// without Skia on its stand-in (real geometry, text measured, nothing
// drawn) -- and by the schedule's engines. An edit to skiff's chat changes
// a message's view: MemoRows makes that row again, and scene::layout lays
// out what that invalidated, as a program's frame does.
#include <benchmark/benchmark.h>

import std;
import skia;
import skiff.scene;
import skiff.nodes.flow;
import skiff.nodes.text;
import skiff.nodes.scroll;
import skiff.compose;
import layout_bench.edits;

namespace {

namespace c = skiff::compose;
namespace scene = skiff::scene;

// ---- skiff's chat --------------------------------------------------------------------
// A message as its row shows it: how many words it says, and whether its
// tick is the wide one.
struct message_view {
  std::size_t words = 0;
  bool wide = false;
  friend bool operator==(const message_view&, const message_view&) = default;
};
using item = std::tuple<std::size_t, message_view>;
// Its words: deterministic, of varied lengths, by its key.
[[nodiscard]] std::string said(std::size_t key, std::size_t words) {
  static constexpr std::array<std::string_view, 8> kWords{"a", "chat", "message", "with", "several", "words", "of", "lengths"};
  return std::views::iota(std::size_t{0}, words) | std::views::transform([&](std::size_t i) { return kWords[(key * 7 + i * 3) % kWords.size()]; }) |
         std::views::join_with(' ') | std::ranges::to<std::string>();
}
inline constexpr skia::SkColor kInk = 0xff202020u;
// A message's row: its avatar; its name, its text (wrapping where it
// reaches the row's width) and its time with the tick by it.
inline constexpr auto make_row = [](const item& one) {
  const auto& [key, view] = one;
  return c::build(c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 4.0f, 4.0f, 4.0f}}),
      c::column(c::vbox(0.0f, {.width = 32.0f, .height = 32.0f})),
      c::column(c::vbox(2.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX, .padding = {6.0f, 6.0f, 6.0f, 6.0f}}),
          skiff::nodes::Text(said(key + 1, 2), 13.0f, kInk), skiff::nodes::Text(said(key, view.words), 13.0f, kInk),
          c::row(c::hbox(4.0f, {.autoSize = scene::axes::kBoth}), skiff::nodes::Text("12:34", 11.0f, kInk),
                 c::column(c::vbox(0.0f, {.width = view.wide ? 16.0f : 12.0f, .height = 12.0f}))))));
};
using rows_t = skiff::nodes::MemoRows<std::size_t, message_view, std::invoke_result_t<decltype(make_row), const item&>, std::remove_cv_t<decltype(make_row)>>;

// The chat as a program shows it: its list in a scroll view 800 high --
// what is out of view not walked by the frame -- the root of a scene, and each
// edit followed by a whole frame -- the tick, the walk that finds what is
// dirty, the layout, the walk that collects what to repaint -- as a
// program's window has it, not the layout alone.
struct skiff_chat {
  std::vector<item> items;
  scene::Scene<skiff::nodes::ScrollContainer<rows_t>> view{std::in_place, rows_t{make_row}};
  float window = 640.0f;
  double now = 0.0;
  rows_t& list() { return std::get<0>(view.root().fChildren); }
  explicit skiff_chat(std::size_t messages, bool laid_out = true) {
    std::mt19937 random(42);
    std::uniform_int_distribution<std::size_t> count(3, 60);
    items = std::views::iota(std::size_t{0}, messages) | std::views::transform([&](std::size_t key) { return item{key, {count(random), false}}; }) |
            std::ranges::to<std::vector>();
    list().fStack.spacingY = 4.0f;
    list().apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 8.0f, 8.0f, 8.0f}});
    view.root().apply({.fill = true});
    list().read(items);
    if (laid_out) lay_out();
  }
  // One frame, each of its phases timed: where a frame's time goes.
  std::array<double, 3> phase_us{};
  std::size_t frames = 0;
  void lay_out() {
    now += 16.0;
    const auto a = std::chrono::steady_clock::now();
    view.update(now);
    const auto b = std::chrono::steady_clock::now();
    view.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, window, 800.0f));
    const auto c = std::chrono::steady_clock::now();
    benchmark::DoNotOptimize(view.finishFrame());
    const auto d = std::chrono::steady_clock::now();
    phase_us[0] += std::chrono::duration<double, std::micro>(b - a).count();
    phase_us[1] += std::chrono::duration<double, std::micro>(c - b).count();
    phase_us[2] += std::chrono::duration<double, std::micro>(d - c).count();
    ++frames;
  }
  // The frame's phases and the walks' counts, per frame, as counters: the
  // tick, markDirty with the layout, the damage walk; nodes each walk came
  // to, and those laid out anew.
  void report(benchmark::State& state, const scene::WalkCounts& before) {
    const auto& after = scene::walkCounts();
    const auto per = [&](double v) { return benchmark::Counter(v / static_cast<double>(std::max<std::size_t>(frames, 1))); };
    state.counters["tick_us"] = per(phase_us[0]);
    state.counters["layout_us"] = per(phase_us[1]);
    state.counters["damage_us"] = per(phase_us[2]);
    state.counters["dirty_n"] = per(static_cast<double>(after.dirty - before.dirty));
    state.counters["layout_n"] = per(static_cast<double>(after.layout - before.layout));
    state.counters["laid_n"] = per(static_cast<double>(after.laidOut - before.laidOut));
    state.counters["damage_n"] = per(static_cast<double>(after.damage - before.damage));
  }
  void reset() {
    phase_us = {};
    frames = 0;
  }
};

void skiff_first_layout(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    // Made, never laid out: every node's first layout.
    auto chat = std::make_unique<skiff_chat>(static_cast<std::size_t>(state.range(0)), false);
    state.ResumeTiming();
    chat->lay_out();
    state.PauseTiming();
    chat.reset();
    state.ResumeTiming();
  }
}
void skiff_type_word(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  chat.reset();
  const scene::WalkCounts before = scene::walkCounts();
  std::size_t step = 0;
  for (auto _ : state) {
    auto& [key, view] = chat.items[(step++ * 7919) % chat.items.size()];
    ++view.words;
    chat.list().read(chat.items);
    chat.lay_out();
  }
  chat.report(state, before);
}
// The same edit taken apart: MemoRows reading the list (its keys and views
// compared, the row made again) without the layout, and the layout alone.
void skiff_type_word_read(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  std::size_t step = 0;
  for (auto _ : state) {
    auto& [key, view] = chat.items[(step++ * 7919) % chat.items.size()];
    ++view.words;
    chat.list().read(chat.items);
    state.PauseTiming();
    chat.lay_out();
    state.ResumeTiming();
  }
}
void skiff_type_word_layout(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  std::size_t step = 0;
  for (auto _ : state) {
    state.PauseTiming();
    auto& [key, view] = chat.items[(step++ * 7919) % chat.items.size()];
    ++view.words;
    chat.list().read(chat.items);
    state.ResumeTiming();
    chat.lay_out();
  }
}
void skiff_widen_tick(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  chat.reset();
  const scene::WalkCounts before = scene::walkCounts();
  std::size_t step = 0;
  for (auto _ : state) {
    auto& [key, view] = chat.items[(step++ * 7919) % chat.items.size()];
    view.wide = !view.wide;
    chat.list().read(chat.items);
    chat.lay_out();
  }
  chat.report(state, before);
}
void skiff_resize(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  chat.reset();
  const scene::WalkCounts before = scene::walkCounts();
  std::size_t step = 0;
  for (auto _ : state) {
    chat.window = step++ % 2 == 0 ? 900.0f : 640.0f;
    chat.lay_out();
  }
  chat.report(state, before);
}
void skiff_add_message(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  chat.reset();
  const scene::WalkCounts before = scene::walkCounts();
  for (auto _ : state) {
    chat.items.push_back(item{chat.items.size(), {20, false}});
    chat.list().read(chat.items);
    chat.lay_out();
  }
  chat.report(state, before);
}
void skiff_add_message_read(benchmark::State& state) {
  skiff_chat chat(static_cast<std::size_t>(state.range(0)));
  for (auto _ : state) {
    chat.items.push_back(item{chat.items.size(), {20, false}});
    chat.list().read(chat.items);
    state.PauseTiming();
    chat.lay_out();
    state.ResumeTiming();
  }
}

// ---- the schedule's engines ------------------------------------------------------------
using namespace layout_bench;
template <class Engine, class Edit>
void schedule_edit(benchmark::State& state) {
  chat c = make_chat(static_cast<std::size_t>(state.range(0)), 640.0f, 42);
  full{}.run(c.nodes);
  Engine engine;
  engine.grow(c.nodes);
  std::size_t step = 0;
  for (auto _ : state) {
    apply(Edit{}, c, engine, step++);
    engine.run(c.nodes);
  }
  if (!same_as_fresh(c)) state.SkipWithError("the result differs from a layout from nothing");
}
void schedule_first_layout(benchmark::State& state) {
  chat c = make_chat(static_cast<std::size_t>(state.range(0)), 640.0f, 42);
  for (auto _ : state) {
    full engine;
    engine.run(c.nodes);
    benchmark::DoNotOptimize(c.nodes.values);
  }
}

}  // namespace

// A chat of 100, 1,000 and 10,000 messages; times in microseconds.
void sizes(benchmark::internal::Benchmark* b) { b->Arg(100)->Arg(1000)->Arg(10000)->Unit(benchmark::kMicrosecond); }
BENCHMARK(skiff_first_layout)->Apply(sizes);
BENCHMARK(schedule_first_layout)->Apply(sizes);
BENCHMARK(skiff_type_word)->Apply(sizes);
BENCHMARK(skiff_type_word_read)->Apply(sizes);
BENCHMARK(skiff_type_word_layout)->Apply(sizes);
BENCHMARK(schedule_edit<full, type_word>)->Apply(sizes);
BENCHMARK(schedule_edit<double_dirty, type_word>)->Apply(sizes);
BENCHMARK(schedule_edit<spineless, type_word>)->Apply(sizes);
BENCHMARK(skiff_widen_tick)->Apply(sizes);
BENCHMARK(schedule_edit<double_dirty, widen_tick>)->Apply(sizes);
BENCHMARK(schedule_edit<spineless, widen_tick>)->Apply(sizes);
BENCHMARK(skiff_resize)->Apply(sizes);
BENCHMARK(schedule_edit<full, resize>)->Apply(sizes);
BENCHMARK(schedule_edit<spineless, resize>)->Apply(sizes);
BENCHMARK(skiff_add_message)->Apply(sizes);
BENCHMARK(skiff_add_message_read)->Apply(sizes);
BENCHMARK(schedule_edit<double_dirty, new_message>)->Apply(sizes);
BENCHMARK(schedule_edit<spineless, new_message>)->Apply(sizes);
BENCHMARK_MAIN();
