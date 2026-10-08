import std;
import gtest;
import skiff.model;

#include "gtest/gtest-macros.h"

// The model's rules, held while it is compiled: every check below is a
// static_assert over a constexpr model. The test only says they held.

using namespace skiff::model;

namespace {
struct ReadReceipts { bool on = true; };
struct MentionsShared { bool on = false; };
struct MentionsSealed { bool on = false; };
struct AccountT {
  std::string who;
  ReadReceipts receipts;
  MentionsShared shared;
  MentionsSealed sealed;
};
struct AppTheme { int which = 0; };
struct SettingsT {
  Tracked<AppTheme> theme;
  Keyed<std::string, AccountT> accounts;
};
struct Root {
  Tracked<SettingsT> settings;
};

struct SetMentionsSharing {
  std::string who;
  bool shared, sealed;
  constexpr bool operator==(const SetMentionsSharing &) const = default;
};
struct WriteSettings {
  constexpr bool replaces(const WriteSettings &) const { return true; }
  constexpr bool operator==(const WriteSettings &) const = default;
};
using Effect = std::variant<SetMentionsSharing, WriteSettings>;

struct Reactions {
  constexpr auto on(Changed<MentionsShared>, const AccountT &a) const {
    return SetMentionsSharing{a.who, a.shared.on, a.sealed.on};
  }
  constexpr auto on(Changed<SettingsT>, const SettingsT &) const {
    return WriteSettings{};
  }
};

// Events and a scope's handlers.
struct Removed {};
struct Renamed { std::string to; };
struct Closed {};
struct AccountEvents {
  template <class P> constexpr auto on(const Removed &, const P &here) const {
    return take<AccountT>(here.template key<std::string>());
  }
  constexpr auto on(const Renamed &r) const {
    return over<AccountT>([to = r.to](AccountT a) { a.who = to; return a; });
  }
};
struct RootEvents {
  constexpr auto on(const Closed &) const {
    return over<AppTheme>(setTo(AppTheme{7}));
  }
};

// Paths are found by type.
static_assert(kFound<ReadReceipts, Root> == 1);
static_assert(kFound<bool, Root> == 3);
static_assert(std::same_as<
    PathTo<ReadReceipts, Root>,
    Path<Member<0>, Into, Member<1>, At<std::string>, Member<1>>>);
static_assert(std::same_as<KeysOf<PathTo<ReadReceipts, Root>>,
                           std::tuple<std::string>>);
static_assert(std::same_as<TypeAt<Root, PathTo<MentionsShared, Root>>,
                           MentionsShared>);

constexpr auto withOne() {
  Root root;
  root.settings.fValue.accounts.put("@me:a.org", AccountT{.who = "@me:a.org"});
  return Model<Root, Reactions, Effect>(std::move(root));
}

constexpr bool flipsAndReacts() {
  auto m = withOne();
  const auto at = placeOf<MentionsShared, Root>(std::string("@me:a.org"));
  if (m.look(at)->on)
    return false;
  const auto before = m.look(at).fRevision;
  if (!m.apply(Edit<Root, decltype(at)::PathType, Flip>{at, flip}))
    return false;
  if (!m.look(at)->on || m.look(at).fRevision == before)
    return false;
  (void)m.apply(Edit<Root, decltype(at)::PathType, Flip>{at, flip});
  (void)m.apply(Edit<Root, decltype(at)::PathType, Flip>{at, flip});
  // Three flips: three tellings, one write.
  const auto out = m.outbox().drain();
  return out.size() == 4 &&
         std::get<SetMentionsSharing>(out[0]).shared &&
         !std::get<SetMentionsSharing>(out[2]).shared &&
         out[1] == Effect{WriteSettings{}} &&
         std::get<SetMentionsSharing>(out[3]).shared;
}
static_assert(flipsAndReacts());

constexpr bool aMissingKeyIsNothing() {
  auto m = withOne();
  const auto at = placeOf<ReadReceipts, Root>(std::string("@other:b.org"));
  return !m.look(at) &&
         !m.apply(Edit<Root, decltype(at)::PathType, Flip>{at, flip});
}
static_assert(aMissingKeyIsNothing());

constexpr bool aSiblingKeepsItsRevision() {
  auto m = withOne();
  const auto theme = m.look<AppTheme>().fRevision;
  const auto at = placeOf<ReadReceipts, Root>(std::string("@me:a.org"));
  (void)m.apply(Edit<Root, decltype(at)::PathType, Flip>{at, flip});
  return m.look<AppTheme>().fRevision == theme;
}
static_assert(aSiblingKeepsItsRevision());

constexpr bool eventsGoUpToTheirScope() {
  auto m = withOne();
  const auto account = placeOf<AccountT, Root>(std::string("@me:a.org"));
  const auto inner = scope<AccountEvents>(account);
  const auto outer = scope<RootEvents>(Place<Root, Path<>>{});
  m.send(Renamed{"@you:a.org"}, inner, outer);
  if (m.look(account)->who != "@you:a.org")
    return false;
  m.send(Closed{}, inner, outer);   // not the account's: the root takes it
  if (m.look<AppTheme>()->which != 7)
    return false;
  m.send(Removed{}, inner, outer);  // the list above the scope: the root's
  return !m.look(account) &&
         m.root().settings.fValue.accounts.size() == 0;
}
static_assert(eventsGoUpToTheirScope());

constexpr bool putAddsAnElement() {
  auto m = withOne();
  (void)m.apply(put<AccountT>(std::string("@two:b.org"),
                              AccountT{.who = "@two:b.org"}));
  return m.root().settings.fValue.accounts.size() == 2 &&
         m.look(placeOf<AccountT, Root>(std::string("@two:b.org")));
}
static_assert(putAddsAnElement());

// Effects that say which one they are: the last of a kind and key stays.
struct Tell {
  std::string who;
  bool shared;
  constexpr const std::string &key() const { return who; }
};
using Told = std::variant<Tell>;
struct Telling {
  // Told where it is: the part, its owner, and the keys that lead to it.
  constexpr auto on(Changed<MentionsShared>, const auto &at) const {
    return Tell{at.template key<0>(), at.part().on};
  }
};
constexpr bool theLastOfAKindAndKeyStays() {
  Root root;
  root.settings.fValue.accounts.put("@a", AccountT{.who = "@a"});
  root.settings.fValue.accounts.put("@b", AccountT{.who = "@b"});
  Model<Root, Telling, Told> m(std::move(root));
  const auto a = placeOf<MentionsShared, Root>(std::string("@a"));
  const auto b = placeOf<MentionsShared, Root>(std::string("@b"));
  using P = decltype(a)::PathType;
  m.apply(Edit<Root, P, Flip>{a, flip});
  m.apply(Edit<Root, P, Flip>{b, flip});
  m.apply(Edit<Root, P, Flip>{a, flip});
  const auto out = m.outbox().drain();
  return out.size() == 2 && std::get<Tell>(out[0]).who == "@a" &&
         !std::get<Tell>(out[0]).shared && std::get<Tell>(out[1]).shared;
}
static_assert(theLastOfAKindAndKeyStays());

// An element taken: its reactions told what it was.
struct Gone {
  std::string who;
};
using GoneEffect = std::variant<Gone>;
struct OnRemoved {
  constexpr auto on(skiff::model::Removed<AccountT>, const AccountT &last, const std::string &key) const {
    return Gone{last.who + "|" + key};
  }
};
constexpr bool aTakenElementIsToldWhatItWas() {
  Root root;
  root.settings.fValue.accounts.put("@a", AccountT{.who = "A"});
  Model<Root, OnRemoved, GoneEffect> m(std::move(root));
  m.apply(take<AccountT>(std::string("@a")));
  const auto out = m.outbox().drain();
  return out.size() == 1 && std::get<Gone>(out[0]).who == "A|@a";
}
static_assert(aTakenElementIsToldWhatItWas());

// Lists: found by index, kept in their order, moved, sorted; an element
// stays where it was put whatever else comes.
constexpr bool listsKeepTheirElementsInPlace() {
  Keyed<int, std::string> list;
  list.put(3, "c");
  list.put(1, "a");
  const std::string *c = list.find(3);
  for (int i = 10; i < 40; ++i)
    list.put(i, "x");
  if (list.find(3) != c || *list.find(1) != "a" || list.keyAt(0) != 3)
    return false;
  list.move(1, 0);
  if (list.keyAt(0) != 1)
    return false;
  list.take(3);
  if (list.find(3) != nullptr || list.size() != 31)
    return false;
  list.sortBy([](const std::string &v) { return v; });
  return list.valueAt(0) == "a";
}
static_assert(listsKeepTheirElementsInPlace());

// All or none.
constexpr bool allOrNone() {
  auto m = withOne();
  const auto here = placeOf<ReadReceipts, Root>(std::string("@me:a.org"));
  const auto gone = placeOf<ReadReceipts, Root>(std::string("@nobody"));
  using P = decltype(here)::PathType;
  const bool applied = m.applyAll(Edit<Root, P, Flip>{here, flip},
                                  Edit<Root, P, Flip>{gone, flip});
  return !applied && m.look(here)->on;
}
static_assert(allOrNone());

// Parts kept elsewhere: read through the model, told when they move.
struct Timeline {
  int newest = 0;
};
struct WithExternal {
  Tracked<SettingsT> settings;
  External<Timeline> timeline;
};
constexpr bool anExternalPartIsReadThroughTheModel() {
  Model<WithExternal, Reactions, Effect> m;
  if (m.look<Timeline>())
    return false;  // pointing at nothing yet
  m.publish(share(Timeline{5}));
  const auto first = m.look<Timeline>();
  if (!first || first->newest != 5)
    return false;
  const auto firstRevision = first.fRevision;
  m.publish(share(Timeline{6}));
  const auto second = m.look<Timeline>();
  return second->newest == 6 && second.fRevision != firstRevision;
}
static_assert(anExternalPartIsReadThroughTheModel());

// Two keys of one type: by their places.
struct Topic {
  std::string text;
};
struct ChatT {
  Topic topic;
};
struct AccountWithChats {
  Keyed<std::string, ChatT> chats;
};
struct TwoLevels {
  Tracked<Keyed<std::string, AccountWithChats>> accounts;
};
struct NoReactions {};
constexpr bool twoKeysOfOneTypeByTheirPlaces() {
  TwoLevels root;
  root.accounts.fValue.put("@a", AccountWithChats{});
  root.accounts.fValue.find("@a")->chats.put("!c", ChatT{{"hi"}});
  Model<TwoLevels, NoReactions, Effect> m(std::move(root));
  const auto at = placeOf<Topic, TwoLevels>(std::string("@a"), std::string("!c"));
  return at.key<0>() == "@a" && at.key<1>() == "!c" && m.look(at)->text == "hi";
}
static_assert(twoKeysOfOneTypeByTheirPlaces());

// Removed, however the element went: by an in-place change too.
struct TakeByHand {
  std::string fKey;
  constexpr void operator()(Keyed<std::string, AccountT> &list) const { list.take(fKey); }
};
constexpr bool removedIsToldHoweverTaken() {
  Root root;
  root.settings.fValue.accounts.put("@a", AccountT{.who = "A"});
  Model<Root, OnRemoved, GoneEffect> m(std::move(root));
  const auto list = placeOf<Keyed<std::string, AccountT>, Root>();
  using P = decltype(list)::PathType;
  m.apply(Edit<Root, P, TakeByHand>{list, TakeByHand{"@a"}});
  const auto out = m.outbox().drain();
  return out.size() == 1 && std::get<Gone>(out[0]).who == "A|@a";
}
static_assert(removedIsToldHoweverTaken());

// Many put at once; the index right after puts and takes in the middle.
constexpr bool theIndexFollowsEveryChange() {
  Keyed<int, int> list;
  std::vector<std::pair<int, int>> many;
  for (int i = 0; i < 20; ++i)
    many.push_back({i * 2, i});
  list.putAll(many);
  list.put(7, 70, 3);  // in the middle
  list.take(10);
  list.put(5, 50, 0);
  for (int i = 0; i < 20; ++i)
    if (i * 2 != 10 && (list.find(i * 2) == nullptr || *list.find(i * 2) != i))
      return false;
  return *list.find(7) == 70 && *list.find(5) == 50 && list.keyAt(0) == 5 &&
         list.keyAt(4) == 7 && list.find(10) == nullptr;
}
static_assert(theIndexFollowsEveryChange());

// Through an optional, and a variant's alternative: there only while it is.
struct Draft {
  std::string text;
};
struct ChatsPage {
  int scroll = 0;
};
struct SettingsPage {
  ReadReceipts receipts;
};
struct Ui {
  std::variant<ChatsPage, SettingsPage> page;
  std::optional<Draft> editing;
};
constexpr bool partsThroughVariantsAndOptionals() {
  Model<Ui, NoReactions, Effect> m;
  if (m.look<SettingsPage>() || !m.look<ChatsPage>() || m.look<Draft>())
    return false;
  using Page = std::variant<ChatsPage, SettingsPage>;
  m.apply(over<Page>(setTo(Page(SettingsPage{}))));
  m.apply(over<std::optional<Draft>>(
      setTo(std::optional<Draft>(Draft{"x"}))));
  m.apply(over<ReadReceipts>(flip));  // into the page now shown
  return m.look<SettingsPage>() && !m.look<ChatsPage>() &&
         !m.look<ReadReceipts>()->on && m.look<Draft>()->text == "x";
}
static_assert(partsThroughVariantsAndOptionals());

// A batch: its reactions told once, of the state it ends in.
struct Counting {
  constexpr auto on(Changed<SettingsT>, const SettingsT &) const { return WriteSettings{}; }
  constexpr auto on(Changed<AccountT>, const AccountT &a) const {
    return SetMentionsSharing{a.who, a.shared.on, a.sealed.on};
  }
};
constexpr bool aBatchIsToldOnceAtTheEnd() {
  Root root;
  root.settings.fValue.accounts.put("@a", AccountT{.who = "@a"});
  Model<Root, Counting, Effect> m(std::move(root));
  const auto shared = placeOf<MentionsShared, Root>(std::string("@a"));
  const auto sealed = placeOf<MentionsSealed, Root>(std::string("@a"));
  using P = decltype(shared)::PathType;
  using Q = decltype(sealed)::PathType;
  m.applyBatch(Edit<Root, P, Flip>{shared, flip}, Edit<Root, Q, Flip>{sealed, flip},
               Edit<Root, P, Flip>{shared, flip});
  const auto out = m.outbox().drain();
  // One telling of the account, of its last state; one write.
  return out.size() == 2 && !std::get<SetMentionsSharing>(out[0]).shared &&
         std::get<SetMentionsSharing>(out[0]).sealed && out[1] == Effect{WriteSettings{}};
}
static_assert(aBatchIsToldOnceAtTheEnd());

// A borrowed place is not copied.
using Borrowed = decltype(std::declval<const Place<Root, PathTo<ReadReceipts, Root>> &>().borrowed());
static_assert(!std::is_copy_constructible_v<Borrowed>);
static_assert(std::is_move_constructible_v<Borrowed>);
static_assert(std::is_copy_constructible_v<Place<Root, PathTo<ReadReceipts, Root>>>);

// A part taken away and put back between two looks is seen as changed,
// though both it and its replacement were never edited (both at 0).
constexpr bool aPartPutBackIsSeenAsChanged() {
  auto m = withOne();
  const auto at = placeOf<AccountT, Root>(std::string("@me:a.org"));
  const auto before = m.look(at).fRevision;
  m.applyBatch(take<AccountT>(std::string("@me:a.org")),
               put<AccountT>(std::string("@me:a.org"), AccountT{.who = "other"}));
  const auto after = m.look(at);
  return after && after->who == "other" && after.fRevision != before;
}
static_assert(aPartPutBackIsSeenAsChanged());

// A snapshot shares the model, and stays as it was when the model changes:
// only what changed is copied.
constexpr bool aSnapshotStaysAsItWas() {
  auto m = withOne();
  const Root snapshot = m.snapshot();
  const auto at = placeOf<ReadReceipts, Root>(std::string("@me:a.org"));
  using P = decltype(at)::PathType;
  m.apply(Edit<Root, P, Flip>{at, flip});
  return snapshot.settings.fValue.accounts.find("@me:a.org")->receipts.on &&
         !m.look(at)->on;
}
static_assert(aSnapshotStaysAsItWas());

// The tracked places, as types, and what moved, by them.
static_assert(AllTracked<Root>::size == 4);  // settings, the theme, the list, an element
constexpr bool whatMovedIsLoggedByPlace() {
  auto m = withOne();
  const auto at = placeOf<ReadReceipts, Root>(std::string("@me:a.org"));
  using P = decltype(at)::PathType;
  m.apply(Edit<Root, P, Flip>{at, flip});  // (its list, put together before)
  (void)m.takeChanges();
  m.apply(Edit<Root, P, Flip>{at, flip});
  const auto changes = m.takeChanges();
  using List = PathTo<Keyed<std::string, AccountT>, Root>;
  using Element = Join<List, Path<At<std::string>>>;
  const auto &elements = std::get<kTrackedIndex<Root, Element>>(changes);
  return elements.size() == 1 && std::get<0>(elements[0]) == "@me:a.org" &&
         std::get<kTrackedIndex<Root, List>>(changes).empty() &&  // same members

         m.takeChanges() == Changes<Root>{};
}
static_assert(whatMovedIsLoggedByPlace());

// Less to write: a named setting, free key() and part(), edit().
using Muted = Named<"muted", bool>;
struct Quiet {
  Tracked<Muted> muted;
  Named<"volume", int> volume;
};
struct Telling2 {
  constexpr auto on(Changed<Muted>, const auto &at) const { return Tell{"x", part(at).value}; }
};
constexpr bool lessToWrite() {
  Model<Quiet, Telling2, Told> m;
  m.apply(edit(placeOf<Muted, Quiet>(), flip));
  m.apply(over<Named<"volume", int>>(setTo(Named<"volume", int>{7})));
  const auto out = m.outbox().drain();
  return m.look<Muted>()->value && m.look<Named<"volume", int>>()->value == 7 &&
         out.size() == 1 && std::get<Tell>(out[0]).shared;
}
static_assert(lessToWrite());

// Fields named by their member pointers: two of one type told apart, with
// no type of their own; a reaction to one is not told of the other.
struct Shown {
  bool receipts = false;
  bool previews = false;
  std::optional<bool> typing;
};
struct Chats {
  Keyed<std::string, Shown> chats;
};
struct SaidPreviews {
  bool on = false;
  std::string chat;
};
struct OnPreviews {
  constexpr auto on(Changed<Field<&Shown::previews>>, const auto &at) const {
    return SaidPreviews{part(at), key<0>(at)};
  }
};
static_assert(std::same_as<PathTo<Field<&Shown::previews>, Chats>,
                           Path<Member<0>, At<std::string>, Member<1>>>);
static_assert(std::same_as<EffectsOf<Chats, OnPreviews>, std::variant<SaidPreviews>>);
constexpr bool fieldsByMemberPointer() {
  Chats root;
  root.chats.put("!a", Shown{});
  Model<Chats, OnPreviews> m(std::move(root));
  m.apply(edit(placeOf<Field<&Shown::receipts>, Chats>(std::string("!a")), flip));
  const bool quietForOthers = m.outbox().size() == 0;
  m.apply(edit(placeOf<Field<&Shown::previews>, Chats>(std::string("!a")), flip));
  m.apply(edit(placeOf<Field<&Shown::typing>, Chats>(std::string("!a")),
               setTo(std::optional<bool>(true))));
  const auto out = m.outbox().drain();
  const Shown &now = m.root().chats.valueAt(0);
  return quietForOthers && out.size() == 1 && std::get<SaidPreviews>(out[0]).on &&
         std::get<SaidPreviews>(out[0]).chat == "!a" && now.receipts && now.previews &&
         now.typing == true && *m.look(placeOf<Field<&Shown::previews>, Chats>(std::string("!a")));
}
static_assert(fieldsByMemberPointer());

// A list walked as ranges: its values and keys in its order.
constexpr bool aListIsSeenAsRanges() {
  Keyed<std::string, Shown> list;
  list.put("!b", Shown{.receipts = true});
  list.put("!a", Shown{});
  return std::ranges::equal(list.keys(), std::array{std::string("!b"), std::string("!a")}) &&
         std::ranges::count_if(list.values(), &Shown::receipts) == 1;
}
static_assert(aListIsSeenAsRanges());

// An event no scope takes, taken by the reactions: an effect asked for.
struct OpenDialog {
  int which = 0;
};
struct DialogOpened {
  int which = 0;
};
struct TakesEvents {
  using Taken = Types<OpenDialog>;
  constexpr DialogOpened on(const OpenDialog &e) const { return {e.which}; }
};
static_assert(std::same_as<EffectsOf<Chats, TakesEvents>, std::variant<DialogOpened>>);
constexpr bool eventsBecomeEffects() {
  Model<Chats, TakesEvents> m;
  m.send(OpenDialog{3});
  const auto out = m.outbox().drain();
  return out.size() == 1 && std::get<DialogOpened>(out[0]).which == 3;
}
static_assert(eventsBecomeEffects());

// No effect type written: deduced from what the reactions return.
static_assert(std::same_as<EffectsOf<Root, Reactions>, std::variant<SetMentionsSharing, WriteSettings>> ||
              std::same_as<EffectsOf<Root, Reactions>, std::variant<WriteSettings, SetMentionsSharing>>);
static_assert(std::same_as<EffectsOf<Root, OnRemoved>, std::variant<Gone>>);
constexpr bool deducedEffectsWork() {
  Root root;
  root.settings.fValue.accounts.put("@a", AccountT{.who = "A"});
  Model<Root, OnRemoved> m(std::move(root));
  m.apply(take<AccountT>(std::string("@a")));
  return m.outbox().size() == 1;
}
static_assert(deducedEffectsWork());
} // namespace

TEST(Model, ItsRulesHoldWhileItIsCompiled) { SUCCEED(); }

// Edits from another thread, applied where the model lives.
TEST(Model, AnInboxTakesEditsFromOtherThreads) {
  auto m = withOne();
  using P = PathTo<ReadReceipts, Root>;
  using FlipIt = Edit<Root, P, Flip>;
  Inbox<FlipIt> inbox;
  std::thread worker([&] {
    for (int i = 0; i < 3; ++i)
      inbox.send(FlipIt{placeOf<ReadReceipts, Root>(std::string("@me:a.org")), flip});
  });
  worker.join();
  EXPECT_EQ(inbox.applyTo(m), 3u);
  EXPECT_FALSE(m.look(placeOf<ReadReceipts, Root>(std::string("@me:a.org")))->on);
}
