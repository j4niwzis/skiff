// What the schedule costs while compiling: LAYOUT_BENCH_KINDS kinds of node,
// each with rules of its own and code of its own for every pass, checked
// against the schedule (LAYOUT_BENCH_CHECK) or not, and walked as the build
// walks them -- the variant visited (release) or through tables (Debug). The
// same file built with the check and without says what it adds; built with
// 16, 64 and 256 kinds, how that grows.
import std;
import layout_bench.grammar;

using namespace layout_bench;
using enum attribute;
using enum place;

namespace {

inline constexpr bool kErased = LAYOUT_BENCH_ERASED != 0;
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
template <std::size_t... I>
consteval bool all_checked(std::index_sequence<I...>) {
  return (check<synthetic<I>>().ok && ...);
}
static_assert(!kChecked || all_checked(std::make_index_sequence<kKinds>{}));

template <class Seq> struct kinds_of;
template <std::size_t... I> struct kinds_of<std::index_sequence<I...>> {
  using type = std::variant<synthetic<I>...>;
};
using kind_t = kinds_of<std::make_index_sequence<kKinds>>::type;

struct flat {
  std::vector<kind_t> kinds;
  std::vector<std::vector<std::uint32_t>> children;
  std::array<std::vector<float>, kAttributes> values;
  float& at(attribute what, std::uint32_t n) { return values[std::to_underlying(what)][n]; }
};

template <class Visit>
decltype(auto) on_kind(const kind_t& kind, Visit&& visit) {
  if constexpr (kErased) {
    using result = decltype(std::visit(visit, kind));
    using entry = result (*)(const kind_t&, Visit&);
    static constexpr auto table = []<std::size_t... I>(std::index_sequence<I...>) {
      return std::array<entry, sizeof...(I)>{+[](const kind_t& k, Visit& v) -> result { return v(std::get<I>(k)); }...};
    }(std::make_index_sequence<kKinds>{});
    return table[kind.index()](kind, visit);
  } else {
    return std::visit(std::forward<Visit>(visit), kind);
  }
}

// Every pass's code, for each kind: what a container and what a leaf
// compute, with the kind's own numbers.
template <std::size_t I>
void visit_one(flat& t, std::uint32_t n, std::size_t pass, const synthetic<I>& k) {
  const auto kids = t.children[n] | std::views::transform([&](std::uint32_t c) { return c; });
  const auto sum = [&](attribute what) { return std::ranges::fold_left(kids | std::views::transform([&](std::uint32_t c) { return t.at(what, c); }), 0.0f, std::plus{}); };
  if constexpr (synthetic<I>::holds) {
    switch (pass) {
      case 0: t.at(pref, n) = sum(pref) + 2.0f * k.pad; break;
      case 1: for (std::uint32_t c : kids) t.at(width, c) = std::min(t.at(pref, c), t.at(width, n) - 2.0f * k.pad); break;
      case 2: t.at(height, n) = sum(height) + k.gap * static_cast<float>(t.children[n].size()); break;
      default: {
        float at = k.pad;
        for (std::uint32_t c : kids) {
          t.at(x, c) = at;
          t.at(y, c) = k.pad;
          at += t.at(width, c) + k.gap;
        }
      }
    }
  } else {
    switch (pass) {
      case 0: t.at(pref, n) = k.pad * 10.0f; break;
      case 2: t.at(height, n) = std::ceil(t.at(pref, n) / std::max(1.0f, t.at(width, n))) * k.gap; break;
      default: break;
    }
  }
}

}  // namespace

int main(int argc, char**) {
  flat t;
  const std::size_t size = 1000 + static_cast<std::size_t>(argc);
  t.kinds = std::views::iota(std::size_t{0}, size) | std::views::transform([](std::size_t i) {
              return [&]<std::size_t... I>(std::index_sequence<I...>) {
                kind_t out;
                ((i % kKinds == I ? (out = synthetic<I>{}, 0) : 0), ...);
                return out;
              }(std::make_index_sequence<kKinds>{});
            }) | std::ranges::to<std::vector>();
  t.children.resize(size);
  for (std::uint32_t n = 1; n < size; ++n) t.children[(n - 1) / 4].push_back(n);
  for (auto& each : t.values) each.assign(size, 0.0f);
  t.at(width, 0) = 640.0f;
  for (std::size_t pass = 0; pass < kSchedule.size(); ++pass) {
    const bool up = kSchedule[pass].walk == order::post;
    for (std::size_t i = 0; i < size; ++i) {
      const auto n = static_cast<std::uint32_t>(up ? size - 1 - i : i);
      on_kind(t.kinds[n], [&](const auto& k) { visit_one(t, n, pass, k); });
    }
  }
  std::println("{}", t.at(height, 0));
}
