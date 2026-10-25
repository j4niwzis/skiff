import std;
import splice;
import gtest;
import skiff.scene;
import skiff.model;
import skiff.bind;
import skiff.compose;

#include "gtest/gtest-macros.h"
#include "made.h"

// A people picker, written with the combinators and run: a field's query
// goes through a debounce -- of what the field sends, found from it -- into
// the list (>>), beside the Invite button (+,
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
// The program's part of a model's reactions, for a test: the requests they
// answer with taken -- kept as a program keeps what it is asked, an effect
// replacing an older one of its kind (replaces(), key()) -- to be looked at.
template <class Effect> struct Log {
  std::vector<Effect> fItems;
  template <class E> constexpr void take(const E &effect) {
    for (auto &each : fItems)
      if (std::holds_alternative<E>(each) && replaces(effect, std::get<E>(each))) {
        each = Effect(effect);
        return;
      }
    fItems.push_back(Effect(effect));
  }
  constexpr std::vector<Effect> drain() { return std::exchange(fItems, {}); }
  constexpr std::size_t size() const { return fItems.size(); }

private:
  template <class E>
    requires requires(const E &e) { e.replaces(e); }
  static constexpr bool replaces(const E &effect, const E &older) {
    return effect.replaces(older);
  }
  template <class E>
    requires(requires(const E &e) { e.key() == e.key(); } && !requires(const E &e) { e.replaces(e); })
  static constexpr bool replaces(const E &effect, const E &older) {
    return effect.key() == older.key();
  }
  template <class E> static constexpr bool replaces(const E &, const E &) { return false; }
};
// One on(request) for each request, not a template: the runtime layer finds
// the events reactions take by their on(event)s, one type each.
template <class Self, class E> struct Takes {
  constexpr skiff::model::Nothing on(const E &effect) const {
    static_cast<const Self &>(*this).log.take(effect);
    return {};
  }
};
template <class R, class Effect> struct Logged;
template <class R, class... E>
struct Logged<R, std::variant<E...>> : R, Takes<Logged<R, std::variant<E...>>, E>... {
  mutable Log<std::variant<E...>> log;
  using R::on;
  using Takes<Logged<R, std::variant<E...>>, E>::on...;
};

struct Reactions {
  auto on(model::Changed<Invited>, const auto &at) const {
    return model::part(at).value ? std::optional(Invite{model::key<0>(at)}) : std::nullopt;
  }
};
// What the reactions ask for, an Invite, taken by the program's part of them.
using Model = model::Model<Root, Logged<Reactions, std::variant<Invite>>>;

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

auto inviteHandlers() {
  return handlers(handle<Picked>([](const Picked &p, const auto &) {
                    return model::over<Selected>(model::setTo(Selected{p.id}));
                  }),
                  handle<Done>([](const Done &, const Selected &now) {
                    return model::Up<std::optional<Picked>>{
                        now.value ? std::optional(Picked{*now.value}) : std::nullopt};
                  }));
}
// (Parenthesised: + binds tighter than >>, and the button is beside the
// pipe, not fed by it.)
auto inviteWiring() { return Field{} >> debounce(250) >> PeopleList{}; }
auto invitePlaced() { return place(vbox(8), inviteWiring() + onClick(Done{}, Pad{})); }
auto inviteDialog() { return local<Selected>(inviteHandlers(), invitePlaced()); }
using Dialog = bind::Local<Selected, decltype(inviteHandlers()), part_t<decltype(invitePlaced())>>;
using Wiring = decltype(inviteWiring());
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
// Where the parts are: found as they were made.
Dialog &dialogOf(Page &page) { return made_as<Dialog>(page); }
Wiring &wiringOf(Page &page) { return made_as<Wiring>(page); }
// Its people's names, as the list shows them.
std::vector<bind::Bound<Name, part_t<Label>> *> namesOf(Page &page) { return all_made_as<bind::Bound<Name, part_t<Label>>>(page); }
auto &fieldOf(Page &page) { return wiringOf(page).fPipe.from.fPipe.from; }
auto &quietOf(Page &page) { return wiringOf(page).fPipe.from.fPipe.to; }
auto &listOf(Page &page) { return wiringOf(page).fPipe.to; }
// Where they are, to press: the dialog, its pipe (or the button beside it),
// the field's pipe (or the list), then the field or the debounce -- or the
// list's rows, then a row.
// The paths found to each, through whatever holds it.
scene::Path fieldPath(Page &page) { return pressPathTo(page, fieldOf(page).fState); }
scene::Path quietPath(Page &page) { return pressPathTo(page, quietOf(page).fState); }
scene::Path donePath(Page &page) { return pressPathTo(page, made_as<OnClick<Done, part_t<Pad>>>(page).fState); }
using RowClick = OnClick<Picked, part_t<decltype(bound<Name>(Label{}))>>;
scene::Path rowAt(Page &page, std::uint32_t row) { return pressPathTo(page, all_made_as<RowClick>(page).at(row)->fState); }

TEST(Picker, ThePeopleAreShown) {
  Model m = twoPeople();
  Page page = peoplePage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  const auto rows = namesOf(page);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0]->fText, "Ann");
  EXPECT_EQ(rows[1]->fText, "Bob");
}

TEST(Picker, AQueryGoesThroughTheDebounceIntoTheList) {
  Model m = twoPeople();
  Page page = peoplePage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  fieldOf(page).type("b");
  EXPECT_TRUE(bind::press(page, m, fieldPath(page)));
  EXPECT_EQ(listOf(page).fQuery, "");  // still quiet: not yet
  quietOf(page).update(0.0);
  quietOf(page).update(300.0);
  EXPECT_TRUE(bind::press(page, m, quietPath(page)));
  EXPECT_EQ(listOf(page).fQuery, "b");
}

TEST(Picker, APickIsTheDialogsOwnAndItsResultBecomesAnInvite) {
  Model m = twoPeople();
  Page page = peoplePage();
  bind::Binding<Model> binding;
  binding.refresh(page, m);
  EXPECT_TRUE(bind::press(page, m, rowAt(page, 1)));  // Bob
  EXPECT_EQ(dialogOf(page).fModel.look<Selected>()->value, std::optional<std::string>("b"));
  EXPECT_FALSE(m.look(model::placeOf<Invited, Root>(std::string("b")))->value);
  EXPECT_EQ(m.reactions().log.size(), 0u);
  EXPECT_TRUE(bind::press(page, m, donePath(page)));
  EXPECT_TRUE(m.look(model::placeOf<Invited, Root>(std::string("b")))->value);
  const auto effects = m.reactions().log.drain();
  ASSERT_EQ(effects.size(), 1u);
  EXPECT_EQ(std::get<Invite>(effects[0]).id, "b");
  binding.refresh(page, m);
  EXPECT_EQ(namesOf(page).at(1)->fText, "Bob");
}

} // namespace
