// skiff.compose:time -- time, as events (the module is skiff.compose: see
// compose.cc). Components with no picture: every<E>(ms) and debounce<M>(ms).
export module skiff.compose:time;

import std;
import splice;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import :wiring;

export namespace skiff::compose {
// ---- time --------------------------------------------------------------------

// An E every so many milliseconds: each come due as it ticks, a press of
// it, answered with as many as are due.
template <class E> struct Every : scene::Node {
  using Out = model::Types<E>;
  double fPeriod = 1000.0;
  double fNext = -1.0;
  std::size_t fDue = 0;
  explicit Every(double period) : fPeriod(period) {}
  void update(double nowMs) {
    if (fNext < 0.0)
      fNext = nowMs + fPeriod;
    const auto due = nowMs < fNext ? std::size_t{0} : static_cast<std::size_t>((nowMs - fNext) / fPeriod) + 1;
    fNext += static_cast<double>(due) * fPeriod;
    fDue += due;
    if (fDue > 0)
      scene::pressLater(fState);
  }
  std::vector<E> onPress() { return std::vector<E>(std::exchange(fDue, 0)); }
  bool wantsTick() const { return true; }
};
template <class E> Every<E> every(double periodMs) { return Every<E>(periodMs); }

// What it is given, sent on once nothing more has come for a while: then
// a press of it, answered with it.
template <class M> struct Debounce : scene::Node {
  using In = model::Types<M>;
  using Out = model::Types<M>;
  std::optional<M> fReady;
  double fQuiet = 300.0;
  double fNow = 0.0;
  double fAt = 0.0;
  std::optional<M> fLatest;
  explicit Debounce(double quiet) : fQuiet(quiet) {}
  void on(const M &message) {
    fLatest = message;
    fAt = fNow;
  }
  void update(double nowMs) {
    fNow = nowMs;
    if (fLatest && nowMs - fAt >= fQuiet) {
      fReady = std::exchange(fLatest, std::nullopt);
      scene::pressLater(fState);
    }
  }
  std::optional<M> onPress() { return std::exchange(fReady, std::nullopt); }
  bool wantsTick() const { return true; }
};
template <class M> Debounce<M> debounce(double quietMs) {
  return Debounce<M>(quietMs);
}
// The same, its message what feeds it: `a >> debounce(250)` debounces what
// a sends, found from a (its Out), not said again.
struct DebounceOf {
  double fQuiet = 300.0;
};
inline DebounceOf debounce(double quietMs) { return {quietMs}; }
template <class L> struct OnlyMessageOf {
  static_assert(false, "skiff::compose: debounce(ms) after something that sends more than one kind of "
                       "message, or none: say which, debounce<M>(ms)");
};
template <class M> struct OnlyMessageOf<model::Types<M>> {
  using type = M;
};
// A press answered with maybe one, or several: each one of them.
template <class M> struct OnlyMessageOf<model::Types<std::optional<M>>> {
  using type = M;
};
template <class M> struct OnlyMessageOf<model::Types<std::vector<M>>> {
  using type = M;
};
template <class A>
  requires std::derived_from<A, scene::Node>
auto operator>>(A from, DebounceOf d) {
  using M = typename OnlyMessageOf<OutOf<A>>::type;
  return Pipe<A, Debounce<M>>(std::move(from), Debounce<M>(d.fQuiet));
}

} // namespace skiff::compose
