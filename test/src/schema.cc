import std;
import gtest;
import skiff.model;

#include "gtest/gtest-macros.h"

// The runtime layer (skiff.model:schema): what a debug build's walks find
// parts by, looked at and edited -- held to be what the static layer finds,
// sees and does, place for place.

using namespace skiff::model;

namespace {
struct Theme {
  int which = 0;
};
struct Account {
  std::string who;
  bool muted = false;
};
struct Settings {
  Tracked<Theme> theme;
  Keyed<std::string, Account> accounts;
};
struct Root {
  Tracked<Settings> settings;
  std::optional<long> draft;
};
struct NoReactions {};
using M = Model<Root, NoReactions, std::variant<Nothing>>;

rt::Place mutedOf(std::string who) {
  return {*rt::pathTo(rt::shapeOf<Root>(), rt::wantOf<Field<&Account::muted>>()), {rt::AnyKey(std::move(who))}};
}
M withAccount() {
  M model{Root{}};
  (void)model.apply(put<Account>(std::string("a"), Account{"a", false}));
  (void)model.takeChanges();
  (void)model.takePlacesChanged();
  return model;
}
} // namespace

TEST(Schema, APartIsFoundWhereTheStaticSearchFindsIt) {
  const auto &root = rt::shapeOf<Root>();
  const auto theme = rt::pathTo(root, rt::wantOf<Theme>());
  ASSERT_TRUE(theme.has_value());
  EXPECT_EQ(*theme, (rt::placeOf<PathTo<Theme, Root>>(std::tuple<>{}).fSteps));
  const auto muted = rt::pathTo(root, rt::wantOf<Field<&Account::muted>>());
  ASSERT_TRUE(muted.has_value());
  EXPECT_EQ(*muted, (rt::placeOf<PathTo<Field<&Account::muted>, Root>>(std::tuple<std::string>{"a"}).fSteps));
  // Nowhere: none, as the static search would not compile.
  EXPECT_FALSE(rt::pathTo(root, rt::wantOf<double>()).has_value());
  EXPECT_EQ(rt::shapeAt(root, *muted)->fType, rt::keyOf<bool>());
}

TEST(Schema, ALookSeesWhatTheStaticLookSees) {
  const M model = withAccount();
  const auto seen = model.lookAt(mutedOf("a"));
  const auto typed = model.look(placeOf<Field<&Account::muted>, Root>(std::string("a")));
  ASSERT_TRUE(seen);
  ASSERT_TRUE(typed);
  EXPECT_EQ(seen.fValue, static_cast<const void *>(&*typed));
  EXPECT_EQ(seen.fRevision, typed.fRevision);
  EXPECT_EQ(seen.fTracked, typed.fTracked);
  // A key not in its list, an optional empty: nothing.
  EXPECT_FALSE(model.lookAt(mutedOf("b")));
  EXPECT_FALSE((model.lookAt(rt::Place{*rt::pathTo(rt::shapeOf<Root>(), rt::wantOf<long>()), {}})));
}

TEST(Schema, AnEditAtARuntimePlaceIsTheStaticOne) {
  M runtime = withAccount();
  M typed = withAccount();
  EXPECT_TRUE(runtime.applyAt<bool>(mutedOf("a"), setTo(true)));
  EXPECT_TRUE((typed.apply(edit(placeOf<Field<&Account::muted>, Root>(std::string("a")), setTo(true)))));
  EXPECT_EQ(runtime.revision(), typed.revision());
  const auto now = runtime.look(placeOf<Field<&Account::muted>, Root>(std::string("a")));
  const auto then = typed.look(placeOf<Field<&Account::muted>, Root>(std::string("a")));
  ASSERT_TRUE(now);
  EXPECT_TRUE(*now);
  EXPECT_EQ(now.fRevision, then.fRevision);
  EXPECT_EQ(runtime.takeChanges(), typed.takeChanges());
  // Logged as a runtime place too, where it is.
  const auto places = runtime.takePlacesChanged();
  ASSERT_EQ(places.size(), 1u);
  EXPECT_TRUE(rt::related(places.front(), mutedOf("a")));
  EXPECT_FALSE(rt::related(places.front(), mutedOf("b")));
  // Not there, or not of that type: not made.
  EXPECT_FALSE(runtime.applyAt<bool>(mutedOf("b"), setTo(true)));
  EXPECT_FALSE(runtime.applyAt<int>(mutedOf("a"), setTo(1)));
}
