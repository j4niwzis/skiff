// skiff.model:model -- the model, batches, its inbox and scopes (the module
// is skiff.model: see model.cc).
export module skiff.model:model;

import std;
import skiff.aggregate;
import :effects;

export namespace skiff::model {
// A model, its reactions, and the effects they asked for.
template <class Root, class Reactions, class EffectOrDeduced = DeducedEffects> class Model {
public:
  using Effect = typename detail::EffectOf<Root, Reactions, EffectOrDeduced>::type;
  using RootType = Root;
  using ReactionsType = Reactions;
  using EffectType = Effect;

  constexpr Model() = default;
  constexpr explicit Model(Root root, Reactions reactions = {})
      : fRoot(std::move(root)), fReactions(std::move(reactions)) {
    // What was put together before is the model's first state: stamped, so
    // that nothing in it looks new at the first edit.
    fRevision = 1;
    detail::stamp(fRoot, fRevision, true, true);
  }

  constexpr const Root &root() const { return fRoot; }
  // A copy of the whole of it, as it is now: what another thread may read
  // while this one goes on changing the model.
  constexpr Root snapshot() const { return fRoot; }
  constexpr Revision revision() const { return fRevision; }
  constexpr Outbox<Effect> &outbox() { return fOutbox; }
  // What moved since they were last taken: by tracked place, the keys of
  // each one -- what a targeted refresh goes straight to.
  constexpr const Changes<Root> &changes() const { return fChanges; }
  constexpr Changes<Root> takeChanges() { return std::exchange(fChanges, {}); }

  // A part as it is now, and its revision; nothing where a key on its way
  // is not in its list.
  template <class P, class K>
  constexpr Seen<TypeAt<Root, P>> look(const Place<Root, P, K> &place) const {
    Looking<TypeAt<Root, P>> v{.fRevision = fRevision};
    detail::NoOwner none;
    if (!detail::walk<0>(fRoot, place.fKeys, v, none, P{}))
      return {};
    return {v.fFound, v.fRevision, v.fTracked};
  }
  // (Want a type, or a Field<&C::m>: what is seen is the part's own type.)
  template <class Want> constexpr auto look() const {
    return look(placeOf<Want, Root>());
  }

  // An edit made: the part changed, the revisions on its way moved, and the
  // reactions to every part it went through asked. False: it was not there.
  template <class P, class F, class K>
  constexpr bool apply(const Edit<Root, P, F, K> &e) {
    const bool own = !fInBatch;
    if (own)
      beginBatch();
    ++fRevision;
    Applying<F> v{*this, e.fChange};
    detail::NoOwner none;
    const bool there = detail::walk<0>(fRoot, e.fPlace.fKeys, v, none, P{});
    if (!v.fSawTracked)
      fUntrackedEdit = true;
    if (own)
      endBatch();
    return there;
  }
  // A change of the one Want in the whole model.
  template <class Want, class F, class... Keys>
  constexpr bool apply(const Over<Want, F, Keys...> &o) {
    return apply(edit(Place<Root, Path<>>{}, o));
  }
  // Several, as one batch: each applied in turn. And all or none: only
  // where every one's part is there.
  template <class... E> constexpr bool applyEach(const E &...edits) {
    bool all = true;
    // Comma sequences the edits; bitwise-and does not specify their order.
    ((all = apply(edits) && all), ...);
    return all;
  }
  template <class... E> constexpr bool applyAll(const E &...edits) {
    if (!(isThere(edits) && ...))
      return false;
    return applyBatch(edits...);
  }
  // Several as one batch, its reactions told once, at the end, of the state
  // it ends in: every edit made first, then each part on their ways told
  // once -- a part two edits went through, once -- so an effect from a
  // batch is the batch's, not one per edit.
  template <class... E> constexpr bool applyBatch(const E &...edits) {
    beginBatch();
    bool all = true;
    ((all = apply(edits) && all), ...);
    endBatch();
    return all;
  }
  // A batch said by its two ends, for edits that come from many places --
  // a drain over a whole screen, an inbox. Nested ones are one batch.
  constexpr void beginBatch() {
    if (fBatchDepth++ != 0)
      return;
    fInBatch = true;
    fBatchStart = fRevision;
    fTouched.clear();
  }
  constexpr void endBatch() {
    if (--fBatchDepth != 0)
      return;
    if !consteval {
      std::ranges::sort(fTouched, std::less<const void *>{});
    }
    tellAll<Path<>>(std::as_const(fRoot), detail::NoOwner{}, std::tuple<>{}, true);
    fTargets.clear();
    if (std::exchange(fUntrackedEdit, false))
      logged<Path<>>(std::tuple<>{});
    fTouched.clear();
    fInBatch = false;
  }

  // A part kept elsewhere pointed at anew, or told it moved: the revisions
  // on its way moved, its readers reading it again.
  template <class Want, class... Keys>
  constexpr bool publish(Shared<Want> value, Keys... keys) {
    const auto place = placeOf<External<Want>, Root>(std::move(keys)...);
    using P = typename decltype(place)::PathType;
    return apply(Edit<Root, P, Pointing<Want>>{place, Pointing<Want>{std::move(value)}});
  }

  // An event sent from within scopes, the innermost first: taken by the
  // first with a handler for it. One that none takes does not compile.
  template <class E, class... Scopes>
  constexpr void send(const E &event, const Scopes &...scopes) {
    route(event, scopes...);
  }

  // An edit of the one Want below a place, from a change said over it.
  template <class Want, class P, class K, class F, class... Keys>
  static constexpr auto edit(const Place<Root, P, K> &here,
                             const Over<Want, F, Keys...> &o) {
    auto place = std::apply(
        [&](const auto &...keys) { return placeBelow<Want>(here, keys...); },
        o.fKeys);
    return Edit<Root, typename decltype(place)::PathType, F>{place,
                                                             o.fChange};
  }

private:
  template <class T> struct Pointing {
    Shared<T> fValue;
    constexpr void operator()(External<T> &external) const {
      external.fValue = fValue;
    }
  };

  // The reactions of a batch: every part an edit of it went through, told
  // once, inner ones first, as the batch left them -- found by a walk that
  // goes only where a revision moved in the batch.
  constexpr bool touched(const void *part) const {
    if consteval {
      return std::ranges::contains(fTouched, part);
    } else {
      return std::ranges::binary_search(fTouched, part, std::less<const void *>{});
    }
  }
  // And the innermost tracked places that moved logged, with their keys, by
  // their types: a change is logged where it is, not again at every tracked
  // part around it -- one around it that shows it sees it by its place. But
  // a part an edit was made at whole is logged itself, and what is in it
  // not: one change, not one per element.
  template <class P, class Keys> constexpr void logged(const Keys &keys) {
    std::get<kTrackedIndex<Root, P>>(fChanges).push_back(
        std::apply([](const auto &...k) { return KeysOf<P>(k...); }, keys));
  }
  constexpr bool target(const void *part) const {
    return std::ranges::contains(fTargets, part);
  }
  // Each returns whether it logged anything.
  template <class P, class N, class O, class Keys>
  constexpr bool tellAll(const N &part, const O &owner, const Keys &keys, bool log) {
    if (touched(std::addressof(part)))
      react(part, detail::ownerOf(part, owner), keys);
    return false;
  }
  template <class P, Decomposable N, class O, class Keys>
  constexpr bool tellAll(const N &part, const O &owner, const Keys &keys, bool log) {
    const bool below = [&]<std::size_t... I>(std::index_sequence<I...>) {
      return ((tellAll<Join<P, Path<Member<I>>>>(aggregate::get<I>(part), owner, keys, log) |
               tellField<N, I>(aggregate::get<I>(part), owner, keys)) |
              ...);
    }(std::make_index_sequence<MemberTypes<N>::size>{});
    if (touched(std::addressof(part)))
      react(part, detail::ownerOf(part, owner), keys);
    return below;
  }
  template <class P, class T, class O, class Keys>
  constexpr bool tellAll(const Tracked<T> &part, const O &owner, const Keys &keys, bool log) {
    if (part.fRevision <= fBatchStart)
      return false;
    const bool whole = log && target(std::addressof(part.fValue));
    const bool below = tellAll<Join<P, Path<Into>>>(part.fValue, owner, keys, log && !whole);
    if (log && !below)
      logged<Join<P, Path<Into>>>(keys);
    return log;
  }
  template <class P, class T, class O, class Keys>
  constexpr bool tellAll(const External<T> &part, const O &, const Keys &keys, bool log) {
    if (part.fRevision <= fBatchStart || !log)
      return false;
    logged<Join<P, Path<Through>>>(keys);
    return true;
  }
  template <class P, class K, class T, class O, class Keys>
  constexpr bool tellAll(const Keyed<K, T> &list, const O &, const Keys &keys, bool log) {
    if (list.fRevision <= fBatchStart)
      return false;
    const bool whole = log && target(std::addressof(list)) && !list.fReshaped;
    bool any = false;
    if (std::exchange(list.fReshaped, false) && log) {
      logged<P>(keys);
      any = true;
    }
    using Element = Join<P, Path<At<K>>>;
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Tracked<T> &element = list.elementAt(i);
      if (element.fRevision <= fBatchStart)
        continue;
      const auto inner = std::tuple_cat(keys, std::tuple<const K &>{list.keyAt(i)});
      const bool logging = log && !whole;
      const bool elementWhole = logging && target(std::addressof(element.fValue));
      const bool below = tellAll<Element>(element.fValue, element.fValue, inner,
                                          logging && !elementWhole);
      if (logging && !below)
        logged<Element>(inner);
      any = any || logging;
    }
    if (whole) {
      logged<P>(keys);
      any = true;
    }
    return any;
  }
  template <class P, class T, class O, class Keys>
  constexpr bool tellAll(const std::optional<T> &part, const O &owner, const Keys &keys, bool log) {
    const bool below = part ? tellAll<Join<P, Path<IfThere>>>(*part, owner, keys, log) : false;
    if (touched(std::addressof(part)))
      react(part, detail::ownerOf(part, owner), keys);
    return below;
  }
  template <class P, template <class...> class V, class... Ts, class O, class Keys>
    requires VariantLike<V<Ts...>>
  constexpr bool tellAll(const V<Ts...> &part, const O &owner, const Keys &keys, bool log) {
    const bool below = [&]<std::size_t... I>(std::index_sequence<I...>) {
      using std::get;
      return ((part.index() == I
                   ? tellAll<Join<P, Path<Alt<I>>>>(get<I>(part), owner, keys, log)
                   : false) |
              ...);
    }(std::index_sequence_for<Ts...>{});
    if (touched(std::addressof(part)))
      react(part, detail::ownerOf(part, owner), keys);
    return below;
  }

  template <class P, class F, class K>
  constexpr bool isThere(const Edit<Root, P, F, K> &e) const {
    return static_cast<bool>(look(e.fPlace));
  }
  template <class Want, class F, class... Keys>
  constexpr bool isThere(const Over<Want, F, Keys...> &o) const {
    return isThere(edit(Place<Root, Path<>>{}, o));
  }

  template <class T> struct Looking {
    const T *fFound = nullptr;
    Revision fRevision = 0;
    bool fTracked = false;
    constexpr void at(const T &part, const auto &, const auto &) { fFound = &part; }
    template <std::size_t K, class N>
    constexpr void left(const N &, const auto &, const auto &) {}
    template <std::size_t K, class N>
      requires requires(const N &n) { n.fRevision; }
    constexpr void left(const N &part, const auto &, const auto &) {
      if (!fTracked) {
        fRevision = part.fRevision;
        fTracked = true;
      }
    }
  };
  template <class F> struct Applying {
    Model &fModel;
    const F &fChange;
    bool fSawTracked = false;
    template <class N> constexpr void at(N &part, const auto &, const auto &) {
      fModel.fTargets.push_back(std::addressof(part));
      detail::change(fChange, part);
      // The tracked parts in what was made: all of them where the part was
      // replaced whole; where it was changed in place, those still unstamped
      // -- an element just put. Else a part taken away and put back between
      // two looks, both at 0, was taken for unchanged.
      detail::stamp(part, fModel.fRevision, detail::kReplaces<F, N>);
      fModel.taken(part);
    }
    template <class N>
      requires std::is_const_v<N>
    constexpr void at(N &, const auto &, const auto &) {
      static_assert(false, "skiff::model: a part kept elsewhere (External) "
                           "is read through the model, not edited through "
                           "it: change it where it is kept, then publish()");
    }
    template <std::size_t K, class N>
    constexpr void left(N &part, const auto &, const auto &) {
      fModel.fTouched.push_back(std::addressof(part));
    }
    template <std::size_t K, class N>
      requires requires(N &n) { n.fRevision; }
    constexpr void left(N &part, const auto &, const auto &) {
      part.fRevision = fModel.fRevision;
      fSawTracked = true;
    }
  };

  // The reactions to a part: told where it is (on(Changed<N>, at)), or --
  // the short form -- given the element of the list it is in.
  template <class N, class O, class Keys>
    requires requires(const Reactions &r, const N &n, const O &o, const Keys &k) {
      r.on(Changed<N>{}, At_<N, O, Keys>{n, o, k});
    }
  constexpr void react(const N &part, const O &owner, const Keys &keys) {
    fOutbox.take(fReactions.on(Changed<N>{}, At_<N, O, Keys>{part, owner, keys}));
  }
  template <class N, class O, class Keys>
    requires(!requires(const Reactions &r, const N &n, const O &o, const Keys &k) {
               r.on(Changed<N>{}, At_<N, O, Keys>{n, o, k});
             } && requires(const Reactions &r, const O &o) { r.on(Changed<N>{}, o); })
  constexpr void react(const N &, const O &owner, const Keys &) {
    fOutbox.take(fReactions.on(Changed<N>{}, owner));
  }
  template <class N, class O, class Keys>
  constexpr void react(const N &, const O &, const Keys &) {}

  // And the reactions to a member as a field of its aggregate
  // (on(Changed<Field<&C::m>>, ...)): looked for only where there are some,
  // told where it is, else -- the short form -- given its list's element.
  template <class C, std::size_t I, class N, class O, class Keys>
  constexpr bool tellField(const N &part, const O &owner, const Keys &keys) {
    using Owner = std::remove_cvref_t<decltype(detail::ownerOf(part, owner))>;
    if constexpr (detail::FieldTellsAt<Reactions, C, I, N, Owner, Keys>) {
      if (touched(std::addressof(part)))
        fOutbox.take(fReactions.on(MemberChanged<C, I>{},
                                   At_<N, Owner, Keys>{part, detail::ownerOf(part, owner), keys}));
    } else if constexpr (detail::FieldTellsShort<Reactions, C, I, Owner>) {
      if (touched(std::addressof(part)))
        fOutbox.take(fReactions.on(MemberChanged<C, I>{}, detail::ownerOf(part, owner)));
    }
    return false;
  }

  // What a change took from a list, however it took it: its reactions
  // told, with what it was and its key, and then let go.
  template <class N> constexpr void taken(N &) {}
  template <class K, class T> constexpr void taken(Keyed<K, T> &list) {
    for (auto &[key, element] : std::exchange(list.fTaken, {}))
      removed(element->fValue, key);
  }
  template <class T, class K>
    requires requires(const Reactions &r, const T &t, const K &k) {
      r.on(Removed<T>{}, t, k);
    }
  constexpr void removed(const T &last, const K &key) {
    fOutbox.take(fReactions.on(Removed<T>{}, last, key));
  }
  template <class T, class K> constexpr void removed(const T &, const K &) {}

  // What a handler answered, done: each edit applied below the innermost
  // of the scopes (from the one that took the event out) where its part is
  // found once -- else in the whole model; an event passed up sent on.
  template <class... Scopes>
  constexpr void answer(Nothing, const Scopes &...) {}
  template <class... A, class... Scopes>
  constexpr void answer(const std::tuple<A...> &all, const Scopes &...s) {
    std::apply([&](const auto &...each) { (answer(each, s...), ...); }, all);
  }
  template <class E, class Taken, class... Outer>
  constexpr void answer(const Up<E> &up, const Taken &, const Outer &...o) {
    route(up.fEvent, o...);
  }
  template <class Want, class F, class... Keys>
  constexpr void answer(const Over<Want, F, Keys...> &o) {
    (void)apply(o);
  }
  template <class Want, class F, class... Keys, class S, class... Outer>
    requires(kFound<Want, typename S::PlaceType::Target> == 1)
  constexpr void answer(const Over<Want, F, Keys...> &o, const S &scope,
                        const Outer &...) {
    (void)apply(edit(scope.fPlace, o));
  }
  template <class Want, class F, class... Keys, class S, class... Outer>
  constexpr void answer(const Over<Want, F, Keys...> &o, const S &,
                        const Outer &...outer) {
    answer(o, outer...);
  }

  // One no scope takes, taken by the reactions themselves: what they ask
  // for, an effect for the program to carry out (a dialog opened, a file
  // written) -- where they take it.
  template <class E>
    requires requires(const Reactions &r, const E &e) { r.on(e); }
  constexpr void route(const E &e) {
    fOutbox.take(fReactions.on(e));
  }
  template <class E> constexpr void route(const E &) {
    static_assert(false, "skiff::model: no scope this event was sent in, nor "
                         "any around it, has a handler for it (a scope that "
                         "means it to stop there says so: Ignore<E>)");
  }
  template <class E, class S, class... Outer>
    requires requires(const S &s, const E &e) { s.fHandlers.on(e, s.fPlace); }
  constexpr void route(const E &e, const S &s, const Outer &...outer) {
    answer(s.fHandlers.on(e, s.fPlace), s, outer...);
  }
  template <class E, class S, class... Outer>
    requires(requires(const S &s, const E &e) { s.fHandlers.on(e); } &&
             !requires(const S &s, const E &e) { s.fHandlers.on(e, s.fPlace); })
  constexpr void route(const E &e, const S &s, const Outer &...outer) {
    answer(s.fHandlers.on(e), s, outer...);
  }
  template <class E, class S, class... Outer>
  constexpr void route(const E &e, const S &, const Outer &...outer) {
    route(e, outer...);
  }

  Root fRoot{};
  Reactions fReactions{};
  Outbox<Effect> fOutbox;
  Revision fRevision = 0;
  // The batch under way: how deep, where its revisions began, and what its
  // edits went through.
  Changes<Root> fChanges{};
  bool fUntrackedEdit = false;
  std::vector<const void *> fTargets;
  int fBatchDepth = 0;
  bool fInBatch = false;
  Revision fBatchStart = 0;
  std::vector<const void *> fTouched;
};

// Edits from other threads -- a network's, a store's -- kept until the
// thread the model lives on takes them, once per frame: the model itself is
// only ever touched there. Each a value, held places and all.
template <class... Edits> class Inbox {
public:
  template <class E>
    requires(std::same_as<E, Edits> || ...)
  void send(E edit) {
    const std::scoped_lock lock(fMutex);
    fItems.emplace_back(std::move(edit));
  }
  // Every edit sent since, applied as one batch; how many there were.
  template <class M> std::size_t applyTo(M &model) {
    std::vector<std::variant<Edits...>> taken;
    {
      const std::scoped_lock lock(fMutex);
      taken.swap(fItems);
    }
    model.beginBatch();
    for (const auto &one : taken)
      std::visit([&](const auto &edit) { (void)model.apply(edit); }, one);
    model.endBatch();
    return taken.size();
  }

private:
  std::mutex fMutex;
  std::vector<std::variant<Edits...>> fItems;
};

// A scope: a place, and the handlers of the events sent within it. A
// handler is an on(event) or an on(event, place) of Handlers', answering
// with an Over, a tuple of them, an Up, or Nothing.
template <class Root, class P, class Handlers> struct Scope {
  using PlaceType = Place<Root, P>;
  PlaceType fPlace;
  Handlers fHandlers{};
};
template <class Handlers, class Root, class P, class K>
constexpr Scope<Root, P, Handlers> scope(const Place<Root, P, K> &place,
                                         Handlers handlers = {}) {
  return {place.held(), std::move(handlers)};
}

} // namespace skiff::model
