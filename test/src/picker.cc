import std;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;

#include "gtest/gtest-macros.h"

// A people picker, written with the combinators and run: a field's query
// goes through a debounce into the list (>>), beside the Invite button (+,
// placed apart); the dialog's selection is its own (local); what it picks
// leaves it as an event (Up), and the page makes that an edit of the model,
// whose reaction asks to invite (its effect type deduced).

namespace {

using namespace skiff;
using namespace skiff::compose;

using Name = model::Named<"name", std::string>;
using Invited = model::Named<"invited", bool>;
struct Person {
  std::string id;
  Name name;
  Invited invited;
};
struct Root {
  model::Tracked<model::Keyed<std::string, Person>> people;
};

struct Invite {
  std::string id;
  const std::string &key() const { return id; }
};
struct Reactions {
  auto on(model::Changed<Invited>, const auto &at) const {
    return model::part(at).value ? std::optional(Invite{model::key<0>(at)}) : std::nullopt;
  }
};
using Model = model::Model<Root, Reactions>;  // effects: deduced
static_assert(std::same_as<Model::Effect, std::variant<Invite>>);

// Stand-ins for skiff-widgets' widgets.
struct QueryChanged {
  std::string text;
};
struct Field : scene::Node {
  std::optional<QueryChanged> fTyped;
  void type(std::string text) { fTyped = QueryChanged{std::move(text)}; }
  std::optional<QueryChanged> onPress() { return std::exchange(fTyped, std::nullopt); }
};
struct Label : scene::Node {
  std::string fText;
  void read(const Name &now) { fText = now.value; }
};
struct Pad : scene::Node {};

struct Picked {
  std::string id;
};
struct Done {};
using Selected = model::Named<"selected", std::optional<std::string>>;

auto personRow(const Person &p) {
  return onClick(Picked{p.id}, bound<Name>(Label{}));
}
using Rows = decltype(each<std::string, Person>(personRow));
// The list: rows of people, and the query it was last given.
struct PeopleList : Box<Rows> {
  using In = model::Types<QueryChanged>;
  std::string fQuery;
  PeopleList() : Box<Rows>(vbox(2), each<std::string, Person>(personRow)) {}
  void on(const QueryChanged &q) { fQuery = q.text; }
};

auto inviteDialog() {
  return local<Selected>(
      handlers(handle<Picked>([](const Picked &p, const auto &) {
                 return model::over<Selected>(model::setTo(Selected{p.id}));
               }),
               handle<Done>([](const Done &, const Selected &now) {
                 return model::Up<std::optional<Picked>>{
                     now.value ? std::optional(Picked{*now.value}) : std::nullopt};
               })),
      // (Parenthesised: + binds tighter than >>, and the button is beside
      // the pipe, not fed by it.)
      place(vbox(8), (Field{} >> debounce<QueryChanged>(250) >> PeopleList{}) +
                         onClick(Done{}, Pad{})));
}
auto peoplePage() {
  return scoped<Root>(handlers(handle<Picked>([](const Picked &p, const auto &) {
                        return model::over<Invited>(model::setTo(Invited{true}), p.id);
                      })),
                      column(vbox(12), inviteDialog()));
}
using Page = decltype(peoplePage());

Model twoPeople() {
  Root root;
  root.people.fValue.put("a", Person{"a", {"Ann"}, {false}});
  root.people.fValue.put("b", Person{"b", {"Bob"}, {false}});
  return Model(std::move(root));
}

// Where the parts are, by the expression's shape.
auto &dialogOf(Page &page) { return std::get<0>(page.fParts); }
auto &wiringOf(Page &page) { return std::get<0>(dialogOf(page).fParts); }
auto &fieldOf(Page &page) { return wiringOf(page).fPipe.from.fPipe.from; }
auto &quietOf(Page &page) { return wiringOf(page).fPipe.from.fPipe.to; }
auto &listOf(Page &page) { return wiringOf(page).fPipe.to; }
// Where they are, to press: the dialog, its pipe (or the button beside it),
// the field's pipe (or the list), then the field or the debounce -- or the
// list's rows, then a row.
const scene::Path kField{0, 0, 0, 0};
const scene::Path kQuiet{0, 0, 0, 1};
const scene::Path kDone{0, 1};
scene::Path rowAt(std::uint32_t row) { return {0, 0, 1, 0, row}; }

TEST(Picker, ThePeopleAreShown) {
  Model m = twoPeople();
  Page page = peoplePage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  auto &rows = std::get<0>(listOf(page).fParts).fRows;
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].fText, "Ann");
  EXPECT_EQ(rows[1].fText, "Bob");
}

TEST(Picker, AQueryGoesThroughTheDebounceIntoTheList) {
  Model m = twoPeople();
  Page page = peoplePage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  fieldOf(page).type("b");
  EXPECT_TRUE(bind::press(page, m, kField));
  EXPECT_EQ(listOf(page).fQuery, "");  // still quiet: not yet
  quietOf(page).update(0.0);
  quietOf(page).update(300.0);
  EXPECT_TRUE(bind::press(page, m, kQuiet));
  EXPECT_EQ(listOf(page).fQuery, "b");
}

TEST(Picker, APickIsTheDialogsOwnAndItsResultBecomesAnInvite) {
  Model m = twoPeople();
  Page page = peoplePage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  auto &rows = std::get<0>(listOf(page).fParts).fRows;
  EXPECT_TRUE(bind::press(page, m, rowAt(1)));  // Bob
  EXPECT_EQ(dialogOf(page).fState.look<Selected>()->value, std::optional<std::string>("b"));
  EXPECT_FALSE(m.look(model::placeOf<Invited, Root>(std::string("b")))->value);
  EXPECT_EQ(m.outbox().size(), 0u);
  EXPECT_TRUE(bind::press(page, m, kDone));
  EXPECT_TRUE(m.look(model::placeOf<Invited, Root>(std::string("b")))->value);
  const auto effects = m.outbox().drain();
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(std::get<Invite>(effects[0]).id, "b");
  binding.refresh(page, m);
  EXPECT_EQ(rows[1].fText, "Bob");
}

} // namespace
