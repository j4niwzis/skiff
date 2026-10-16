// skiff.model:effects -- how a model's reactions are found (the module is
// skiff.model: see model.cc).
//
// A reaction is told where its part changed, and what it returns is
// answered there and then, as a handler's answer is (model.cc: answer) --
// an edit applied, an event routed: to the scopes it was sent in, and to
// the reactions' own on(event), where the program takes what is its own.
// Nothing is kept for later: no outbox, and no variant of every effect a
// model could ask for, found by walking every place in its root.
export module skiff.model:effects;

import std;
import skiff.aggregate;
import :places;

export namespace skiff::model {
namespace detail {
// A reaction to a member, as a field of its aggregate: told where it is, or
// -- the short form -- given the element of the list it is in.
template <class R, class C, std::size_t I, class N, class O, class Keys>
concept FieldTellsAt = requires(const R &r, const N &n, const O &o, const Keys &k) {
  r.on(MemberChanged<C, I>{}, At_<N, O, Keys>{n, o, k});
};
template <class R, class C, std::size_t I, class O>
concept FieldTellsShort = requires(const R &r, const O &o) { r.on(MemberChanged<C, I>{}, o); };
} // namespace detail
} // namespace skiff::model
