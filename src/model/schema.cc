// skiff.model:schema -- a model's parts as data: what a debug build binds
// by at run time (the module is skiff.model: see model.cc).
//
// A release build finds every part by its type while it is compiled: a place
// is a type, its path; each lookup, edit and change is made for that path.
// Outside one, that is what made a program's binding walks, and the units
// that instantiate them, take minutes: one copy of every walk for every
// place a node is bound to. Here the same parts are data. Each type the
// model holds has a shape, made once from the type where the model is made;
// a place is the steps from the root to a part -- a member, into a tracked
// value, through an external one, into a list's element by its key, into an
// optional's value or a variant's alternative -- and the keys it goes
// through; a part is found by a search along the shapes, once for each kind
// of place. What a step is, is the same as the static path's.
export module skiff.model:schema;

import std;
import skiff.aggregate;
import :core;
import :places;

export namespace skiff::model::rt {

// A type, as the runtime layer names it: the address of a variable made for
// it -- one in a program, whichever unit asks.
using TypeKey = const void *;
template <class T> inline constexpr char kTag = 0;
template <class T> [[nodiscard]] constexpr TypeKey keyOf() { return &kTag<T>; }

// A list's key, erased: held, and compared by its own type's ==.
class AnyKey {
public:
  AnyKey() = default;
  template <class K>
    requires(!std::same_as<std::remove_cvref_t<K>, AnyKey>)
  explicit AnyKey(K key)
      : fType(keyOf<K>()), fValue(std::make_shared<const K>(std::move(key))),
        fEqual(+[](const void *a, const void *b) { return *static_cast<const K *>(a) == *static_cast<const K *>(b); }) {}
  [[nodiscard]] TypeKey type() const { return fType; }
  // The key, where it is a K.
  template <class K> [[nodiscard]] const K *as() const {
    return fType == keyOf<K>() ? static_cast<const K *>(fValue.get()) : nullptr;
  }
  friend bool operator==(const AnyKey &a, const AnyKey &b) {
    return a.fType == b.fType && (a.fType == nullptr || a.fEqual(a.fValue.get(), b.fValue.get()));
  }

private:
  TypeKey fType = nullptr;
  std::shared_ptr<const void> fValue;
  bool (*fEqual)(const void *, const void *) = nullptr;
};

// The steps of a place, as the static path's: Member<I>, Into, Through,
// At<K>, IfThere, Alt<I>.
namespace step {
struct member {
  std::size_t index = 0;
  friend bool operator==(member, member) = default;
};
struct into {
  friend bool operator==(into, into) = default;
};
struct through {
  friend bool operator==(through, through) = default;
};
struct at {
  friend bool operator==(at, at) = default;
};
struct if_there {
  friend bool operator==(if_there, if_there) = default;
};
struct alt {
  std::size_t index = 0;
  friend bool operator==(alt, alt) = default;
};
} // namespace step
using Step = std::variant<step::member, step::into, step::through, step::at, step::if_there, step::alt>;
using Steps = std::vector<Step>;

// A place: its steps from the root, and the keys of its lists, in order.
struct Place {
  Steps fSteps;
  std::vector<AnyKey> fKeys;
  friend bool operator==(const Place &, const Place &) = default;
};
// Where one place is within another, or holds it: a change at one can be
// seen at the other where their keys agree, as far as both go.
[[nodiscard]] inline bool related(const Place &a, const Place &b) {
  const std::size_t steps = std::min(a.fSteps.size(), b.fSteps.size());
  const std::size_t keys = std::min(a.fKeys.size(), b.fKeys.size());
  return std::ranges::equal(a.fSteps | std::views::take(steps), b.fSteps | std::views::take(steps)) &&
         std::ranges::equal(a.fKeys | std::views::take(keys), b.fKeys | std::views::take(keys));
}
// Whether `inner` is `outer` or below it.
[[nodiscard]] inline bool within(const Place &inner, const Place &outer) {
  return inner.fSteps.size() >= outer.fSteps.size() && related(inner, outer);
}

// ---- shapes ------------------------------------------------------------------

struct Shape;
// What a shape is: a plain value (nothing below it the search goes into), an
// aggregate's members, a tracked part's value, an external part's, a keyed
// list's elements, an optional's value, a variant's alternatives. Each with
// how its part is reached, read and written.
namespace kind {
struct plain {};
struct aggregate {
  std::vector<const Shape *> fMembers;
  void *(*fGet)(void *, std::size_t);
  const void *(*fLook)(const void *, std::size_t);
};
struct tracked {
  const Shape *fValue;
  void *(*fGet)(void *);
  const void *(*fLook)(const void *);
  Revision *(*fRevision)(void *);
  Revision (*fSeen)(const void *);
};
struct external {
  const Shape *fValue;
  const void *(*fLook)(const void *);  // nothing where it points at nothing
  Revision *(*fRevision)(void *);
  Revision (*fSeen)(const void *);
};
struct keyed {
  TypeKey fKey;
  const Shape *fElement;  // the shape of the element's value
  // The element of a key -- its Tracked<T> -- or nothing.
  void *(*fFind)(void *, const AnyKey &);
  const void *(*fLookFind)(const void *, const AnyKey &);
  // Into an element (a Tracked<T>): its value, and its revision.
  void *(*fElementValue)(void *);
  const void *(*fLookElementValue)(const void *);
  Revision *(*fElementRevision)(void *);
  Revision (*fElementSeen)(const void *);
  Revision *(*fRevision)(void *);
  Revision (*fSeen)(const void *);
  std::size_t (*fSize)(const void *);
  AnyKey (*fKeyAt)(const void *, std::size_t);
};
struct optional {
  const Shape *fValue;
  void *(*fGet)(void *);
  const void *(*fLook)(const void *);
};
struct variant {
  std::vector<const Shape *> fAlternatives;
  std::size_t (*fIndex)(const void *);
  void *(*fGet)(void *, std::size_t);
  const void *(*fLook)(const void *, std::size_t);
};
} // namespace kind
using Kind = std::variant<kind::plain, kind::aggregate, kind::tracked, kind::external, kind::keyed, kind::optional,
                          kind::variant>;
struct Shape {
  TypeKey fType;
  Kind fKind;
};

template <class T> const Shape &shapeOf();

namespace detail {
template <class T> struct KindOf {
  static Kind make() { return kind::plain{}; }
};
template <Decomposable T> struct KindOf<T> {
  static Kind make() {
    return kind::aggregate{
        [] {
          return [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::vector<const Shape *>{&shapeOf<typename MemberTypes<T>::template at<I>>()...};
          }(std::make_index_sequence<MemberTypes<T>::size>{});
        }(),
        +[](void *self, std::size_t i) -> void * { return memberOf(*static_cast<T *>(self), i); },
        +[](const void *self, std::size_t i) -> const void * { return memberOf(*static_cast<const T *>(self), i); }};
  }
  template <class Self> static auto *memberOf(Self &self, std::size_t i) {
    using Out = std::conditional_t<std::is_const_v<Self>, const void *, void *>;
    Out found = nullptr;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      ((i == I ? void(found = std::addressof(aggregate::get<I>(self))) : void()), ...);
    }(std::make_index_sequence<MemberTypes<T>::size>{});
    return found;
  }
};
template <class T> struct KindOf<Tracked<T>> {
  static Kind make() {
    return kind::tracked{&shapeOf<T>(), +[](void *self) -> void * { return &static_cast<Tracked<T> *>(self)->fValue; },
                         +[](const void *self) -> const void * { return &static_cast<const Tracked<T> *>(self)->fValue; },
                         +[](void *self) -> Revision * { return &static_cast<Tracked<T> *>(self)->fRevision; },
                         +[](const void *self) { return static_cast<const Tracked<T> *>(self)->fRevision; }};
  }
};
template <class T> struct KindOf<External<T>> {
  static Kind make() {
    return kind::external{&shapeOf<T>(),
                          +[](const void *self) -> const void * {
                            const auto &part = *static_cast<const External<T> *>(self);
                            return part.fValue ? &part.fValue.get() : nullptr;
                          },
                          +[](void *self) -> Revision * { return &static_cast<External<T> *>(self)->fRevision; },
                          +[](const void *self) { return static_cast<const External<T> *>(self)->fRevision; }};
  }
};
template <class K, class T> struct KindOf<Keyed<K, T>> {
  using List = Keyed<K, T>;
  static Kind make() {
    return kind::keyed{
        keyOf<K>(),
        &shapeOf<T>(),
        +[](void *self, const AnyKey &key) -> void * {
          const K *wanted = key.as<K>();
          return wanted ? static_cast<List *>(self)->findItem(*wanted) : nullptr;
        },
        +[](const void *self, const AnyKey &key) -> const void * {
          const K *wanted = key.as<K>();
          return wanted ? static_cast<const List *>(self)->findItem(*wanted) : nullptr;
        },
        +[](void *element) -> void * { return &static_cast<Tracked<T> *>(element)->fValue; },
        +[](const void *element) -> const void * { return &static_cast<const Tracked<T> *>(element)->fValue; },
        +[](void *element) -> Revision * { return &static_cast<Tracked<T> *>(element)->fRevision; },
        +[](const void *element) { return static_cast<const Tracked<T> *>(element)->fRevision; },
        +[](void *self) -> Revision * { return &static_cast<List *>(self)->fRevision; },
        +[](const void *self) { return static_cast<const List *>(self)->fRevision; },
        +[](const void *self) { return static_cast<const List *>(self)->size(); },
        +[](const void *self, std::size_t i) { return AnyKey(static_cast<const List *>(self)->keyAt(i)); }};
  }
};
template <class T> struct KindOf<std::optional<T>> {
  static Kind make() {
    return kind::optional{&shapeOf<T>(),
                          +[](void *self) -> void * {
                            auto &part = *static_cast<std::optional<T> *>(self);
                            return part ? &*part : nullptr;
                          },
                          +[](const void *self) -> const void * {
                            const auto &part = *static_cast<const std::optional<T> *>(self);
                            return part ? &*part : nullptr;
                          }};
  }
};
template <template <class...> class V, class... Ts>
  requires VariantLike<V<Ts...>>
struct KindOf<V<Ts...>> {
  using Var = V<Ts...>;
  template <class Self> static auto *alternative(Self &self, std::size_t i) {
    using Out = std::conditional_t<std::is_const_v<Self>, const void *, void *>;
    Out found = nullptr;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
      using std::get;
      ((self.index() == I && i == I ? void(found = std::addressof(get<I>(self))) : void()), ...);
    }(std::index_sequence_for<Ts...>{});
    return found;
  }
  static Kind make() {
    return kind::variant{std::vector<const Shape *>{&shapeOf<Ts>()...},
                         +[](const void *self) { return static_cast<const Var *>(self)->index(); },
                         +[](void *self, std::size_t i) -> void * { return alternative(*static_cast<Var *>(self), i); },
                         +[](const void *self, std::size_t i) -> const void * {
                           return alternative(*static_cast<const Var *>(self), i);
                         }};
  }
};
} // namespace detail

// A type's shape: made the first time it is asked for, once.
template <class T> const Shape &shapeOf() {
  static const Shape made{keyOf<T>(), detail::KindOf<T>::make()};
  return made;
}

// ---- what is wanted, and where it is ------------------------------------------

// A part looked for: by its type, or by a field of an aggregate -- the
// aggregate's type, and which member.
struct Want {
  TypeKey fType = nullptr;
  std::optional<std::size_t> fMember;  // a field's
};
template <class T> struct WantOfT {
  static constexpr Want make() { return {keyOf<T>(), std::nullopt}; }
};
template <auto M> struct WantOfT<Field<M>> {
  static constexpr Want make() {
    using C = typename model::detail::MemberPointer<decltype(M)>::Class;
    return {keyOf<C>(), model::detail::kIndexOfMember<M>};
  }
};
template <class T> [[nodiscard]] constexpr Want wantOf() { return WantOfT<T>::make(); }
// The type of what is wanted: its own, a field's member's.
template <class T> struct ValueOfT {
  using type = T;
};
template <auto M> struct ValueOfT<Field<M>> {
  using type = typename model::detail::MemberPointer<decltype(M)>::Type;
};

// Every place below a shape a type is at -- not looked for again below one
// found -- as the static search finds them.
void search(const Shape &in, TypeKey want, Steps &way, std::vector<Steps> &found);
namespace detail {
struct SearchInto {
  TypeKey fWant;
  Steps &fWay;
  std::vector<Steps> &fFound;
  void go(Step step, const Shape &next) const {
    fWay.push_back(step);
    search(next, fWant, fWay, fFound);
    fWay.pop_back();
  }
  void operator()(const kind::plain &) const {}
  void operator()(const kind::aggregate &k) const {
    for (std::size_t i = 0; i < k.fMembers.size(); ++i)
      go(step::member{i}, *k.fMembers[i]);
  }
  void operator()(const kind::tracked &k) const { go(step::into{}, *k.fValue); }
  void operator()(const kind::external &k) const { go(step::through{}, *k.fValue); }
  void operator()(const kind::keyed &k) const { go(step::at{}, *k.fElement); }
  void operator()(const kind::optional &k) const { go(step::if_there{}, *k.fValue); }
  void operator()(const kind::variant &k) const {
    for (std::size_t i = 0; i < k.fAlternatives.size(); ++i)
      go(step::alt{i}, *k.fAlternatives[i]);
  }
};
} // namespace detail
inline void search(const Shape &in, TypeKey want, Steps &way, std::vector<Steps> &found) {
  if (in.fType == want) {
    found.push_back(way);
    return;
  }
  std::visit(detail::SearchInto{want, way, found}, in.fKind);
}
// Every way from a shape to what is wanted: a field's, the way to its
// aggregate, and on into the member.
[[nodiscard]] inline std::vector<Steps> pathsTo(const Shape &in, const Want &want) {
  Steps way;
  std::vector<Steps> found;
  search(in, want.fType, way, found);
  if (want.fMember)
    for (Steps &each : found)
      each.push_back(step::member{*want.fMember});
  return found;
}
// The one way, where there is one: else nothing -- none, or more than one,
// which the static search would not compile.
[[nodiscard]] inline std::optional<Steps> pathTo(const Shape &in, const Want &want) {
  auto found = pathsTo(in, want);
  if (found.size() != 1)
    return std::nullopt;
  return std::move(found.front());
}
// The shape at the end of a way: each step taken where it fits its kind --
// a member of an aggregate, into a tracked part -- and none where one does
// not.
namespace detail {
struct StepInto {
  const Shape *operator()(const kind::aggregate &k, step::member s) const {
    return s.index < k.fMembers.size() ? k.fMembers[s.index] : nullptr;
  }
  const Shape *operator()(const kind::tracked &k, step::into) const { return k.fValue; }
  const Shape *operator()(const kind::external &k, step::through) const { return k.fValue; }
  const Shape *operator()(const kind::keyed &k, step::at) const { return k.fElement; }
  const Shape *operator()(const kind::optional &k, step::if_there) const { return k.fValue; }
  const Shape *operator()(const kind::variant &k, step::alt s) const {
    return s.index < k.fAlternatives.size() ? k.fAlternatives[s.index] : nullptr;
  }
  // A step against another kind.
  const Shape *operator()(const auto &, const auto &) const { return nullptr; }
};
} // namespace detail
[[nodiscard]] inline const Shape *shapeAt(const Shape &in, const Steps &way) {
  const Shape *at = &in;
  for (const Step &each : way)
    at = at == nullptr ? nullptr : std::visit(detail::StepInto{}, at->fKind, each);
  return at;
}

// ---- looking, and walking to edit -----------------------------------------------

// A part as it is now, and the revision it was seen at: the innermost
// tracked part's at or above it, else the whole model's -- as Seen<T> is.
struct Seen {
  const void *fValue = nullptr;
  const Shape *fShape = nullptr;
  Revision fRevision = 0;
  bool fTracked = false;
  explicit operator bool() const { return fValue != nullptr; }
};
namespace detail {
// A tracked part passed: its revision, the innermost so far.
struct Passing {
  Seen &fSeen;
  void note(Revision revision) const {
    fSeen.fRevision = revision;
    fSeen.fTracked = true;
  }
};
// One step of a look: into the part it goes to, or nothing where its key is
// not in its list, its optional is empty, its variant holds another.
struct LookStep {
  const void *fAt;
  const std::vector<AnyKey> &fKeys;
  std::size_t &fKey;
  Passing fPassing;
  using Next = std::pair<const void *, const Shape *>;
  Next operator()(const kind::aggregate &k, step::member s) const { return {k.fLook(fAt, s.index), k.fMembers[s.index]}; }
  Next operator()(const kind::tracked &k, step::into) const {
    fPassing.note(k.fSeen(fAt));
    return {k.fLook(fAt), k.fValue};
  }
  Next operator()(const kind::external &k, step::through) const {
    fPassing.note(k.fSeen(fAt));
    return {k.fLook(fAt), k.fValue};
  }
  Next operator()(const kind::keyed &k, step::at) const {
    fPassing.note(k.fSeen(fAt));
    if (fKey >= fKeys.size())
      return {nullptr, nullptr};
    const void *element = k.fLookFind(fAt, fKeys[fKey++]);
    if (element == nullptr)
      return {nullptr, nullptr};
    fPassing.note(k.fElementSeen(element));
    return {k.fLookElementValue(element), k.fElement};
  }
  Next operator()(const kind::optional &k, step::if_there) const { return {k.fLook(fAt), k.fValue}; }
  Next operator()(const kind::variant &k, step::alt s) const {
    return {k.fIndex(fAt) == s.index ? k.fLook(fAt, s.index) : nullptr, k.fAlternatives[s.index]};
  }
  Next operator()(const auto &, const auto &) const { return {nullptr, nullptr}; }
};
// The part itself tracked -- a tracked part, a list, an external one -- is
// its own innermost.
struct EndRevision {
  const void *fAt;
  std::optional<Revision> operator()(const kind::tracked &k) const { return k.fSeen(fAt); }
  std::optional<Revision> operator()(const kind::external &k) const { return k.fSeen(fAt); }
  std::optional<Revision> operator()(const kind::keyed &k) const { return k.fSeen(fAt); }
  std::optional<Revision> operator()(const auto &) const { return std::nullopt; }
};
} // namespace detail
// A part of a model, from its root (whose shape is `root`, whole at
// `whole`): nothing where its place leads nowhere.
[[nodiscard]] inline Seen look(const Shape &root, const void *at, Revision whole, const Place &place) {
  Seen seen{.fValue = nullptr, .fShape = nullptr, .fRevision = whole, .fTracked = false};
  const Shape *shape = &root;
  std::size_t key = 0;
  for (const Step &each : place.fSteps) {
    const auto [next, below] = std::visit(detail::LookStep{at, place.fKeys, key, {seen}}, shape->fKind, each);
    if (next == nullptr)
      return {};
    at = next;
    shape = below;
  }
  if (const auto own = std::visit(detail::EndRevision{at}, shape->fKind))
    detail::Passing{seen}.note(*own);
  seen.fValue = at;
  seen.fShape = shape;
  return seen;
}

// The way to a part to edit it, walked: the part, and what the edit goes
// through -- each part whose revision it moves, and the rest, which its
// reactions are told of. As the static walk's Applying collects them.
struct Walked {
  void *fPart = nullptr;
  const Shape *fShape = nullptr;
  std::vector<Revision *> fRevisions;  // every tracked part on the way, the part's own too
  std::vector<const void *> fTouched;  // every other part on the way, the part too
};
namespace detail {
struct EditStep {
  void *fAt;
  const std::vector<AnyKey> &fKeys;
  std::size_t &fKey;
  Walked &fWalked;
  using Next = std::pair<void *, const Shape *>;
  Next operator()(const kind::aggregate &k, step::member s) const {
    fWalked.fTouched.push_back(fAt);
    return {k.fGet(fAt, s.index), k.fMembers[s.index]};
  }
  Next operator()(const kind::tracked &k, step::into) const {
    fWalked.fRevisions.push_back(k.fRevision(fAt));
    return {k.fGet(fAt), k.fValue};
  }
  // An external part is read through the model, not edited through it.
  Next operator()(const kind::external &, step::through) const { return {nullptr, nullptr}; }
  Next operator()(const kind::keyed &k, step::at) const {
    fWalked.fRevisions.push_back(k.fRevision(fAt));
    if (fKey >= fKeys.size())
      return {nullptr, nullptr};
    void *element = k.fFind(fAt, fKeys[fKey++]);
    if (element == nullptr)
      return {nullptr, nullptr};
    fWalked.fRevisions.push_back(k.fElementRevision(element));
    return {k.fElementValue(element), k.fElement};
  }
  Next operator()(const kind::optional &k, step::if_there) const {
    fWalked.fTouched.push_back(fAt);
    return {k.fGet(fAt), k.fValue};
  }
  Next operator()(const kind::variant &k, step::alt s) const {
    fWalked.fTouched.push_back(fAt);
    return {k.fIndex(fAt) == s.index ? k.fGet(fAt, s.index) : nullptr, k.fAlternatives[s.index]};
  }
  Next operator()(const auto &, const auto &) const { return {nullptr, nullptr}; }
};
struct EndEdit {
  void *fAt;
  Walked &fWalked;
  void operator()(const kind::tracked &k) const { fWalked.fRevisions.push_back(k.fRevision(fAt)); }
  void operator()(const kind::keyed &k) const { fWalked.fRevisions.push_back(k.fRevision(fAt)); }
  void operator()(const auto &) const { fWalked.fTouched.push_back(fAt); }
};
} // namespace detail
// Walked to edit: nothing where the place leads nowhere.
[[nodiscard]] inline std::optional<Walked> walkToEdit(const Shape &root, void *at, const Place &place) {
  Walked walked;
  const Shape *shape = &root;
  std::size_t key = 0;
  for (const Step &each : place.fSteps) {
    const auto [next, below] = std::visit(detail::EditStep{at, place.fKeys, key, walked}, shape->fKind, each);
    if (next == nullptr)
      return std::nullopt;
    at = next;
    shape = below;
  }
  std::visit(detail::EndEdit{at, walked}, shape->fKind);
  walked.fPart = at;
  walked.fShape = shape;
  return walked;
}

// A change of a part, erased: the part's type, the change held, and how it
// is made -- as a static edit makes it: assigned where it gives a new value,
// else made in place; and the tracked parts in what it made stamped.
struct Change {
  TypeKey fPart = nullptr;
  std::shared_ptr<const void> fFn;
  void (*fApply)(void *, const void *, Revision) = nullptr;
  template <class T, class F> static Change of(F fn) {
    return {keyOf<T>(), std::make_shared<const F>(std::move(fn)), +[](void *part, const void *held, Revision now) {
              T &made = *static_cast<T *>(part);
              const F &change = *static_cast<const F *>(held);
              model::detail::change(change, made);
              model::detail::stamp(made, now, model::detail::kReplaces<F, T>);
            }};
  }
};

// ---- static paths, as runtime places ---------------------------------------------

namespace detail {
template <class S> struct StepOf;
template <std::size_t I> struct StepOf<Member<I>> {
  static Step make() { return step::member{I}; }
};
template <> struct StepOf<Into> {
  static Step make() { return step::into{}; }
};
template <> struct StepOf<Through> {
  static Step make() { return step::through{}; }
};
template <class K> struct StepOf<At<K>> {
  static Step make() { return step::at{}; }
};
template <> struct StepOf<IfThere> {
  static Step make() { return step::if_there{}; }
};
template <std::size_t I> struct StepOf<Alt<I>> {
  static Step make() { return step::alt{I}; }
};
template <class P> struct StepsOf;
template <class... S> struct StepsOf<Path<S...>> {
  static Steps make() { return Steps{StepOf<S>::make()...}; }
};
} // namespace detail
// A static place's steps, and its keys, erased: a place the runtime layer
// takes -- what a model logs its changes as, for the runtime walks.
template <class P, class Keys> [[nodiscard]] Place placeOf(const Keys &keys) {
  return {detail::StepsOf<P>::make(),
          std::apply([](const auto &...k) { return std::vector<AnyKey>{AnyKey(std::remove_cvref_t<decltype(k)>(k))...}; },
                     keys)};
}

} // namespace skiff::model::rt
