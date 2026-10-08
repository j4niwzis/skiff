// skiff.compose:processes -- processors, and time (the module is
// skiff.compose: see compose.cc).
export module skiff.compose:processes;

import std;
import splice;
import skiff.scene;
import skiff.nodes;
import skiff.model;
import skiff.bind;
import :wiring;

export namespace skiff::compose {
// ---- processors --------------------------------------------------------------

// A step changing the state in place, as an edit of it.
template <class P, class E> struct InPlace {
  E fEvent;
  void operator()(typename P::State &state) const { P::step(state, fEvent); }
};

template <class P, class E>
concept PureStep = requires(typename P::State s, const E &e) {
  { P::step(std::move(s), e) } -> std::same_as<typename P::State>;
};
template <class P, class E>
concept PureStepSending = requires(typename P::State s, const E &e) {
  { P::step(std::move(s), e).first } -> std::convertible_to<typename P::State>;
  P::step(std::move(s), e).second;
};
template <class P, class E>
concept InPlaceStep = requires(typename P::State &s, const E &e) {
  { P::step(s, e) } -> std::same_as<void>;
} && !requires(typename P::State &&s, const E &e) { P::step(std::move(s), e); };

// A processor's steps, as a Local's handlers: given the state, answering
// with its new value (and what is sent on).
template <class P> struct Steps {
  using State = typename P::State;
  template <class E>
    requires PureStep<P, E>
  auto on(const E &event, const State &now) const {
    return model::over<State>(model::setTo(P::step(State(now), event)));
  }
  template <class E>
    requires(PureStepSending<P, E> && !PureStep<P, E>)
  auto on(const E &event, const State &now) const {
    auto [next, sent] = P::step(State(now), event);
    return std::tuple{model::over<State>(model::setTo(State(std::move(next)))),
                      model::Up<decltype(sent)>{std::move(sent)}};
  }
  template <class E>
    requires InPlaceStep<P, E>
  auto on(const E &event, const State &) const {
    return model::over<State>(InPlace<P, E>{event});
  }
};

template <class P> using ViewOf = decltype(P::view());
template <class P>
using Processor =
    bind::Local<typename P::State, Steps<P>, Box<ViewOf<P>>, true>;
template <class P> Processor<P> process(typename P::State initial = {}) {
  return Processor<P>(Steps<P>{}, Box<ViewOf<P>>(vbox(), P::view()),
                      std::move(initial));
}

// ---- time --------------------------------------------------------------------

// An E every so many milliseconds.
template <class E> struct Every : scene::Node, bind::Emits<E> {
  double fPeriod = 1000.0;
  double fNext = -1.0;
  explicit Every(double period) : fPeriod(period) {}
  void update(double nowMs) {
    if (fNext < 0.0)
      fNext = nowMs + fPeriod;
    while (nowMs >= fNext) {
      this->emit(E{});
      fNext += fPeriod;
    }
  }
  bool wantsTick() const { return true; }
};
template <class E> Every<E> every(double periodMs) { return Every<E>(periodMs); }

// What it is given, sent on once nothing more has come for a while.
template <class M> struct Debounce : scene::Node, bind::Emits<M> {
  using In = model::Types<M>;
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
    if (fLatest && nowMs - fAt >= fQuiet)
      this->emit(*std::exchange(fLatest, std::nullopt));
  }
  bool wantsTick() const { return true; }
};
template <class M> Debounce<M> debounce(double quietMs) {
  return Debounce<M>(quietMs);
}

} // namespace skiff::compose
