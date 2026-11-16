// What the schedule costs while compiling: LAYOUT_BENCH_KINDS kinds of node,
// each with rules of its own and code of its own for every pass, checked
// against the schedule (LAYOUT_BENCH_CHECK) or not, and walked as the build
// walks them -- the variant visited (release) or through tables (Debug). The
// same file built with the check and without says what it adds; built with
// 16, 64 and 256 kinds, how that grows.
import std;
import splice;
import layout_bench.grammar;

using namespace layout_bench;
using enum attribute;
using enum place;

namespace {

inline constexpr bool kChecked = LAYOUT_BENCH_CHECK != 0;
inline constexpr std::size_t kKinds = LAYOUT_BENCH_KINDS;

// A kind of its own: a container where I is even, a leaf where it is odd,
// its numbers (and so its code) its own.
template <std::size_t I>
struct synthetic {
  float pad = static_cast<float>(I % 7) + 1.0f, gap = static_cast<float>(I % 5);
  static constexpr bool holds = I % 2 == 0;
  static constexpr auto rules = [] {
    if constexpr (holds)
      return std::array{computes(pref, self, {{pref, children}}), computes(width, children, {{width, self}, {pref, children}}),
                        computes(height, self, {{height, children}}), computes(x, children, {{width, children}}),
                        computes(y, children, {{height, children}})};
    else
      return std::array{computes(pref, self, {}), computes(height, self, {{width, self}})};
  }();
};
// Checked only where asked: `!kChecked || all_checked(...)` still
// instantiated and evaluated every kind's check -- the build without the
// check paid for it too (6.2 of its 20 s at 256 kinds).
template <std::size_t... I>
consteval bool all_checked(std::index_sequence<I...>) {
  return (check<synthetic<I>>().ok && ...);
}
template <bool Checked>
consteval bool checked_if() {
  if constexpr (Checked)
    return all_checked(std::make_index_sequence<kKinds>{});
  else
    return true;
}
static_assert(checked_if<kChecked>());

template <class Seq> struct kinds_of;
template <std::size_t... I> struct kinds_of<std::index_sequence<I...>> {
  using type = spl::variant<synthetic<I>...>;
};
using kind_t = kinds_of<std::make_index_sequence<kKinds>>::type;

struct flat {
  std::vector<kind_t> kinds;
  std::vector<std::vector<std::uint32_t>> children;
  std::array<std::vector<float>, kAttributes> values;
  float& at(attribute what, std::uint32_t n) { return values[std::to_underlying(what)][n]; }
};

// Every pass's code: one function for containers and one for leaves, each
// made once and given the kind's numbers -- not a template for each kind,
// whose ranges pipelines were instantiated once per kind (about 0.65 s
// each, Release or Debug alike). What stays a template per kind is the call
// that hands its numbers over.
struct numbers {
  float pad = 0.0f, gap = 0.0f;
};
[[nodiscard]] float sum_of(flat& t, std::uint32_t n, attribute what) {
  return std::ranges::fold_left(t.children[n] | std::views::transform([&](std::uint32_t c) { return t.at(what, c); }), 0.0f, std::plus{});
}
void container_pass(flat& t, std::uint32_t n, std::size_t pass, numbers k) {
  switch (pass) {
    case 0: t.at(pref, n) = sum_of(t, n, pref) + 2.0f * k.pad; break;
    case 1: for (std::uint32_t c : t.children[n]) t.at(width, c) = std::min(t.at(pref, c), t.at(width, n) - 2.0f * k.pad); break;
    case 2: t.at(height, n) = sum_of(t, n, height) + k.gap * static_cast<float>(t.children[n].size()); break;
    default: {
      float at = k.pad;
      for (std::uint32_t c : t.children[n]) {
        t.at(x, c) = at;
        t.at(y, c) = k.pad;
        at += t.at(width, c) + k.gap;
      }
    }
  }
}
void leaf_pass(flat& t, std::uint32_t n, std::size_t pass, numbers k) {
  switch (pass) {
    case 0: t.at(pref, n) = k.pad * 10.0f; break;
    case 2: t.at(height, n) = std::ceil(t.at(pref, n) / std::max(1.0f, t.at(width, n))) * k.gap; break;
    default: break;
  }
}
template <std::size_t I>
void visit_one(flat& t, std::uint32_t n, std::size_t pass, const synthetic<I>& k) {
  if constexpr (synthetic<I>::holds)
    container_pass(t, n, pass, {k.pad, k.gap});
  else
    leaf_pass(t, n, pass, {k.pad, k.gap});
}

}  // namespace

int main(int argc, char** argv) {
  flat t;
  const std::size_t size = argc > 1 ? std::stoul(argv[1]) : 1001;
  const std::size_t rounds = argc > 2 ? std::stoul(argv[2]) : 1;
  // One of each kind, made in place by its index -- no converting
  // assignment, whose overload resolution over every alternative would
  // cost kinds * kinds -- and the tree's nodes copied from them.
  static const auto one_of_each = []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<kind_t, kKinds>{kind_t(std::in_place_index<I>)...};
  }(std::make_index_sequence<kKinds>{});
  t.kinds = std::views::iota(std::size_t{0}, size) | std::views::transform([](std::size_t i) { return one_of_each[i % kKinds]; }) |
            std::ranges::to<std::vector>();
  t.children.resize(size);
  for (std::uint32_t n = 1; n < size; ++n) t.children[(n - 1) / 4].push_back(n);
  for (auto& each : t.values) each.assign(size, 0.0f);
  t.at(width, 0) = 640.0f;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t round = 0; round < rounds; ++round)
  for (std::size_t pass = 0; pass < kSchedule.size(); ++pass) {
    const bool up = kSchedule[pass].walk == order::post;
    for (std::size_t i = 0; i < size; ++i) {
      const auto n = static_cast<std::uint32_t>(up ? size - 1 - i : i);
      spl::visit([&](const auto& k) { visit_one(t, n, pass, k); }, t.kinds[n]);
    }
  }
  const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - start;
  std::println("{} ms per layout of {} nodes ({})", took.count() / static_cast<double>(rounds), size, t.at(height, 0));
}
