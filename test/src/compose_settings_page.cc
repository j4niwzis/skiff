// mux's Join Requests page as it is composed: a mount made from the
// settings' facts, in it a scope over the chats rekeyed by those facts, in
// that a projection of the room with a mount of its own. A unit of its own:
// added to the two models' unit, that one was more than a small box compiles.
#include "skiff/named.h"
import std;
import skia;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;
import skiff.nodes;
import skiff.paint;

#include "gtest/gtest-macros.h"

#include "made.h"

namespace {
using namespace skiff;

namespace settings_page {
namespace c = skiff::compose;
// The settings' facts: which room they are for, and what is answered.
struct Facts {
  int room = 1;
  int answered = 0;
  friend bool operator==(const Facts &, const Facts &) = default;
};
struct Room {
  int asking = 0;
  friend bool operator==(const Room &, const Room &) = default;
};
struct ShowRoot {
  model::Tracked<std::optional<Facts>> facts;
};
struct ChatsRoot {
  model::Keyed<int, Room> rooms;
};
using ShowModel = model::Model<ShowRoot, bind::NoReactions>;
using ChatsModel = model::Model<ChatsRoot, bind::NoReactions>;
struct Listing {
  int asking = 0;
  int answered = 0;
  friend bool operator==(const Listing &, const Listing &) = default;
};
inline auto listing_rows(const Listing &now) { return c::build(c::column(c::vbox(0.0f), nodes::Text(std::format("{} asking", now.asking - now.answered), 13.0f, 0u))); }
using ListingRows = decltype(listing_rows(Listing{}));
struct MakeListing {
  std::tuple<ListingRows> operator()(const Listing &now) const { return {listing_rows(now)}; }
};
// As mux's knock_view: in the room the facts say (rekeyed), the room's
// requests read into a listing, mounted.
struct ListingOf {
  int answered = 0;
  Listing operator()(const Room &room) const { return {room.asking, answered}; }
};
inline auto knock_view(int answered) {
  return c::build(c::rekeyed_by<std::optional<Facts>>([](const std::optional<Facts> &now) { return std::tuple(now ? now->room : 0); },
      c::scoped<Room>(c::handlers(), c::projected<Room>(ListingOf{answered}, c::mount<ListingRows, Listing>(MakeListing{})), 0)));
}
using KnockView = decltype(knock_view(0));
struct MakeKnockView {
  std::tuple<KnockView> operator()(int answered) const { return {knock_view(answered)}; }
};
struct AnsweredOf {
  std::optional<int> operator()(const std::optional<Facts> &now) const { return now ? std::optional<int>(now->answered) : std::nullopt; }
};
}  // namespace settings_page

// mux's Join Requests page as it is composed: mounted from the settings'
// facts (rows_of), in it a scope over the chats rekeyed by those facts
// (in_managed_chat), in that a projection of the room with a mount of its
// own (knock_listing_view) -- the settings opened as the chats stand still.
TEST(Compose, ARoomsRequestsMountedUnderTheSettingsAreShown) {
  namespace c = skiff::compose;
  using namespace settings_page;
  auto tree = c::build(c::column(c::vbox(0.0f), c::projected<std::optional<Facts>>(AnsweredOf{}, c::mount<KnockView, int>(MakeKnockView{}))));
  ShowModel shown{ShowRoot{}};
  ChatsRoot rooms;
  rooms.rooms.put(7, Room{31});
  ChatsModel chats(std::move(rooms));
  bind::Binding<ShowModel> bs;
  bind::Binding<ChatsModel> bc;
  bs.refresh(tree, shown);
  bc.refresh(tree, chats);
  EXPECT_TRUE(all_made_as<ListingRows>(tree).empty());
  // The settings opened for room 7.
  (void)shown.apply(model::edit(model::placeOf<std::optional<Facts>, ShowRoot>(), model::setTo(std::optional<Facts>(Facts{.room = 7}))));
  for (int frame = 0; frame < 3; ++frame) {
    bs.refresh(tree, shown);
    bc.refresh(tree, chats);
  }
  const auto rows = all_made_as<ListingRows>(tree);
  EXPECT_EQ(all_made_as<KnockView>(tree).size(), 1u);
  ASSERT_EQ(rows.size(), 1u);
  const auto texts = all_made_as<nodes::Text>(*rows.front());
  ASSERT_FALSE(texts.empty());
  EXPECT_EQ(texts.front()->text(), "31 asking");
}
// The same pieces, one taken away at a time: each still shows the room.
TEST(Compose, ARoomsRequestsShownPieceByPiece) {
  namespace c = skiff::compose;
  using namespace settings_page;
  const auto run = [](auto tree, const char *what) {
    ShowModel shown{ShowRoot{}};
    ChatsRoot rooms;
    rooms.rooms.put(7, Room{31});
    ChatsModel chats(std::move(rooms));
    bind::Binding<ShowModel> bs;
    bind::Binding<ChatsModel> bc;
    bs.refresh(tree, shown);
    bc.refresh(tree, chats);
    (void)shown.apply(model::edit(model::placeOf<std::optional<Facts>, ShowRoot>(), model::setTo(std::optional<Facts>(Facts{.room = 7}))));
    for (int frame = 0; frame < 3; ++frame) {
      bs.refresh(tree, shown);
      bc.refresh(tree, chats);
    }
    EXPECT_EQ(all_made_as<ListingRows>(tree).size(), 1u) << what;
  };
  // No mount above: the rekeyed scope and its own mount.
  run(c::build(c::column(c::vbox(0.0f), c::rekeyed_by<std::optional<Facts>>([](const std::optional<Facts> &now) { return std::tuple(now ? now->room : 0); },
      c::scoped<Room>(c::handlers(), c::projected<Room>(ListingOf{0}, c::mount<ListingRows, Listing>(MakeListing{})), 0)))), "no mount above");
  // The mount above, the scope on room 7 from the start: no rekey.
  struct MakeFixed {
    auto operator()(int) const {
      return std::tuple{c::build(c::scoped<Room>(c::handlers(), c::projected<Room>(ListingOf{0}, c::mount<ListingRows, Listing>(MakeListing{})), 7))};
    }
  };
  using Fixed = std::tuple_element_t<0, decltype(MakeFixed{}(0))>;
  run(c::build(c::column(c::vbox(0.0f), c::projected<std::optional<Facts>>(AnsweredOf{}, c::mount<Fixed, int>(MakeFixed{})))), "mount above, no rekey");
  // The mount above, rekeyed, a text read in the scope (no mount in it).
  struct Said {
    std::string operator()(const Room &room) const { return std::format("{} asking", room.asking); }
  };
  struct MakeText {
    auto operator()(int) const {
      return std::tuple{c::build(c::rekeyed_by<std::optional<Facts>>([](const std::optional<Facts> &now) { return std::tuple(now ? now->room : 0); },
          c::scoped<Room>(c::handlers(), c::text_for<Room>(Said{}, nodes::Text("", 13.0f, 0u)), 0)))};
    }
  };
  using WithText = std::tuple_element_t<0, decltype(MakeText{}(0))>;
  {
    auto tree = c::build(c::column(c::vbox(0.0f), c::projected<std::optional<Facts>>(AnsweredOf{}, c::mount<WithText, int>(MakeText{}))));
    ShowModel shown{ShowRoot{}};
    ChatsRoot rooms;
    rooms.rooms.put(7, Room{31});
    ChatsModel chats(std::move(rooms));
    bind::Binding<ShowModel> bs;
    bind::Binding<ChatsModel> bc;
    bs.refresh(tree, shown);
    bc.refresh(tree, chats);
    (void)shown.apply(model::edit(model::placeOf<std::optional<Facts>, ShowRoot>(), model::setTo(std::optional<Facts>(Facts{.room = 7}))));
    for (int frame = 0; frame < 3; ++frame) {
      bs.refresh(tree, shown);
      bc.refresh(tree, chats);
    }
    std::string said = "-";
    for (auto *one : all_made_as<nodes::Text>(tree)) said = one->text();
    EXPECT_EQ(said, "31 asking") << "mount above, rekeyed, a text";
  }
}
// The facts there from the start, and a rekey between two rooms that are
// both there.
TEST(Compose, ARoomsRequestsShownAsTheScopeMovesBetweenRooms) {
  namespace c = skiff::compose;
  using namespace settings_page;
  struct Said {
    std::string operator()(const Room &room) const { return std::format("{} asking", room.asking); }
  };
  const auto make = [] {
    return c::build(c::column(c::vbox(0.0f), c::rekeyed_by<std::optional<Facts>>([](const std::optional<Facts> &now) { return std::tuple(now ? now->room : 0); },
        c::scoped<Room>(c::handlers(), c::text_for<Room>(Said{}, nodes::Text("", 13.0f, 0u)), 0))));
  };
  const auto said = [](auto &tree) {
    std::string out = "-";
    for (auto *one : all_made_as<nodes::Text>(tree)) out = one->text();
    return out;
  };
  for (const int first : {0, 5, 7}) {
    auto tree = make();
    ShowModel shown{ShowRoot{.facts = {std::optional<Facts>(Facts{.room = first})}}};
    ChatsRoot rooms;
    rooms.rooms.put(5, Room{5});
    rooms.rooms.put(7, Room{31});
    ChatsModel chats(std::move(rooms));
    bind::Binding<ShowModel> bs;
    bind::Binding<ChatsModel> bc;
    bs.refresh(tree, shown);
    bc.refresh(tree, chats);
    const std::string at_first = said(tree);
    (void)shown.apply(model::edit(model::placeOf<std::optional<Facts>, ShowRoot>(), model::setTo(std::optional<Facts>(Facts{.room = 7}))));
    for (int frame = 0; frame < 3; ++frame) {
      bs.refresh(tree, shown);
      bc.refresh(tree, chats);
    }
    EXPECT_EQ(at_first, first == 0 ? "" : first == 5 ? "5 asking" : "31 asking") << "first room " << first;
    EXPECT_EQ(said(tree), "31 asking") << "first room " << first;
  }
}
// The same with the facts a plain part, not an optional one.
namespace plain_facts {
struct Facts {
  int room = 1;
  friend bool operator==(const Facts &, const Facts &) = default;
};
struct ShowRoot {
  model::Tracked<Facts> facts;
};
using ShowModel = model::Model<ShowRoot, bind::NoReactions>;
}  // namespace plain_facts
TEST(Compose, ARoomsRequestsShownFromPlainFacts) {
  namespace c = skiff::compose;
  using settings_page::Room;
  using settings_page::ChatsRoot;
  using settings_page::ChatsModel;
  using namespace plain_facts;
  struct Said {
    std::string operator()(const Room &room) const { return std::format("{} asking", room.asking); }
  };
  auto tree = c::build(c::column(c::vbox(0.0f), c::rekeyed_by<Facts>([](const Facts &now) { return std::tuple(now.room); },
      c::scoped<Room>(c::handlers(), c::text_for<Room>(Said{}, nodes::Text("", 13.0f, 0u)), 0))));
  ShowModel shown{ShowRoot{.facts = {Facts{.room = 7}}}};
  ChatsRoot rooms;
  rooms.rooms.put(7, Room{31});
  ChatsModel chats(std::move(rooms));
  bind::Binding<ShowModel> bs;
  bind::Binding<ChatsModel> bc;
  bs.refresh(tree, shown);
  bc.refresh(tree, chats);
  std::string out = "-";
  for (auto *one : all_made_as<nodes::Text>(tree)) out = one->text();
  EXPECT_EQ(out, "31 asking");
}
// As the two models' passing test is written (its text made apart), the
// scope on room 0 first, the facts saying 7.
namespace apart_text {
struct Said {
  std::string operator()(const settings_page::Room &room) const { return std::format("{} asking", room.asking); }
};
inline auto room_text() { return skiff::compose::build(skiff::compose::text_for<settings_page::Room>(Said{}, nodes::Text("", 13.0f, 0u))); }
using RoomText = decltype(room_text());
}  // namespace apart_text
TEST(Compose, ARoomsRequestsShownInAColumnUnderTheScope) {
  namespace c = skiff::compose;
  using settings_page::Room;
  using settings_page::ChatsRoot;
  using settings_page::ChatsModel;
  using namespace plain_facts;
  using namespace apart_text;
  for (const int first : {0, 7}) {
    auto tree = c::build(c::column(c::vbox(0.0f), c::rekeyed_by<Facts>([](const Facts &now) { return std::tuple(now.room); },
        c::scoped<Room>(c::handlers(), c::column(c::vbox(0.0f), room_text()), first))));
    ShowModel shown{ShowRoot{.facts = {Facts{.room = 7}}}};
    ChatsRoot rooms;
    rooms.rooms.put(7, Room{31});
    ChatsModel chats(std::move(rooms));
    bind::Binding<ShowModel> bs;
    bind::Binding<ChatsModel> bc;
    for (int frame = 0; frame < 2; ++frame) {
      bs.refresh(tree, shown);
      bc.refresh(tree, chats);
    }
    const auto said = all_made_as<RoomText>(tree);
    ASSERT_EQ(said.size(), 1u) << "scope first on " << first;
    EXPECT_EQ(said.front()->text(), "31 asking") << "scope first on " << first;
  }
}
// The listing (a projection with a mount) under the rekeyed scope: facts a
// plain part, then an optional one.
TEST(Compose, ARoomsRequestsListingShownDirectlyUnderTheScope) {
  namespace c = skiff::compose;
  using settings_page::Room;
  using settings_page::ChatsRoot;
  using settings_page::ChatsModel;
  using settings_page::ListingOf;
  using settings_page::ListingRows;
  using settings_page::Listing;
  using settings_page::MakeListing;
  {
    using namespace plain_facts;
    auto tree = c::build(c::column(c::vbox(0.0f), c::rekeyed_by<Facts>([](const Facts &now) { return std::tuple(now.room); },
        c::scoped<Room>(c::handlers(), c::projected<Room>(ListingOf{0}, c::mount<ListingRows, Listing>(MakeListing{})), 0))));
    ShowModel shown{ShowRoot{.facts = {Facts{.room = 7}}}};
    ChatsRoot rooms;
    rooms.rooms.put(7, Room{31});
    ChatsModel chats(std::move(rooms));
    bind::Binding<ShowModel> bs;
    bind::Binding<ChatsModel> bc;
    for (int frame = 0; frame < 2; ++frame) {
      bs.refresh(tree, shown);
      bc.refresh(tree, chats);
    }
    EXPECT_EQ(all_made_as<ListingRows>(tree).size(), 1u) << "plain facts";
  }
  {
    using settings_page::Facts;
    using settings_page::ShowRoot;
    using settings_page::ShowModel;
    auto tree = c::build(c::column(c::vbox(0.0f), c::rekeyed_by<std::optional<Facts>>([](const std::optional<Facts> &now) { return std::tuple(now ? now->room : 0); },
        c::scoped<Room>(c::handlers(), c::projected<Room>(ListingOf{0}, c::mount<ListingRows, Listing>(MakeListing{})), 0))));
    ShowModel shown{ShowRoot{.facts = {std::optional<Facts>(Facts{.room = 7})}}};
    ChatsRoot rooms;
    rooms.rooms.put(7, Room{31});
    ChatsModel chats(std::move(rooms));
    bind::Binding<ShowModel> bs;
    bind::Binding<ChatsModel> bc;
    for (int frame = 0; frame < 2; ++frame) {
      bs.refresh(tree, shown);
      bc.refresh(tree, chats);
    }
    EXPECT_EQ(all_made_as<ListingRows>(tree).size(), 1u) << "optional facts";
  }
}
// As mux names its views: the scope a named view, its content a named view
// over a mount not read yet, the page under a tab hidden until it is
// chosen. A wrapper over the mount kept its first hiding (outside a release
// build every named view is one): the page stayed empty for good.
namespace named_page {
namespace c = skiff::compose;
using settings_page::Facts;
using settings_page::Room;
using settings_page::Listing;
using settings_page::ListingRows;
using settings_page::MakeListing;
inline auto listing_view(int answered) {
  return SKIFF_NAMED(knock_listing_view, c::projected<Room>(settings_page::ListingOf{answered}, c::mount<ListingRows, Listing>(MakeListing{})));
}
template <class N> auto in_managed_chat(N node) {
  return SKIFF_NAMED(in_managed_chat, c::rekeyed_by<std::optional<Facts>>([](const std::optional<Facts> &now) { return std::tuple(now ? now->room : 0); },
      c::scoped<Room>(c::handlers(), std::move(node), 0)));
}
inline auto knock_view(int answered) { return in_managed_chat(listing_view(answered)); }
using KnockView = decltype(knock_view(0));
struct MakeKnockView {
  std::tuple<KnockView> operator()(int answered) const { return {knock_view(answered)}; }
};
struct OnRequests {
  bool operator()(const std::optional<Facts> &now) const { return now && now->answered == 0 && now->room == 7; }
};
}  // namespace named_page
TEST(Compose, ARoomsRequestsInNamedViewsUnderAHiddenTabAreShown) {
  namespace c = skiff::compose;
  using namespace settings_page;
  using namespace named_page;
  scene::Scene laid(c::build(c::column(c::vbox(0.0f), c::shown_for<std::optional<Facts>>(OnRequests{},
      c::column(c::vbox(0.0f), SKIFF_NAMED(rows_of, c::projected<std::optional<Facts>>(AnsweredOf{}, c::mount<named_page::KnockView, int>(named_page::MakeKnockView{}))))))));
  auto &tree = laid.root();
  ShowModel shown{ShowRoot{}};
  ChatsRoot rooms;
  rooms.rooms.put(7, Room{31});
  ChatsModel chats(std::move(rooms));
  bind::Binding<ShowModel> bs;
  bind::Binding<ChatsModel> bc;
  (void)shown.apply(model::edit(model::placeOf<std::optional<Facts>, ShowRoot>(), model::setTo(std::optional<Facts>(Facts{.room = 7}))));
  for (int frame = 0; frame < 3; ++frame) {
    bs.refresh(tree, shown);
    bc.refresh(tree, chats);
  }
  laid.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  bool shownWhole = false;
  for (const scene::LaidOutNode &one : scene::laidOutTree(laid.root()))
    if (one.view == "in_managed_chat" && one.names)
      shownWhole = one.visible;
  EXPECT_TRUE(shownWhole);
}

}  // namespace
