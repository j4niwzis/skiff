// Scopes led by keys; clicks on rows in a selection; a named list's wheel; edits made where a part is.
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

// Screens as combinator expressions, bound to a model; local and hidden
// state; components wired by what they send (>>, +); a processor; time; a
// Binding doing only what can have happened; events whose types are
// deduced.

#include "compose_common.inc"

namespace {


// A tree bound to two models: the settings, where an account is at its
// address, and what is shown, holding a copy of the account in a page's
// facts. The account's scope is the settings' -- the shown model, holding
// the account only some other way, passes it by.
namespace scope_leads {
namespace c = skiff::compose;
struct LeadAccount {
  int n = 0;
};
struct LeadSettings {
  model::Keyed<std::string, LeadAccount> accounts;
};
struct LeadPage {
  LeadAccount account;
};
struct LeadShown {
  model::Tracked<LeadPage> page;
};
struct LeadReads : scene::Node {
  int n = -1;
  void read(int now) { n = now; }
};
}  // namespace scope_leads
TEST(Compose, AScopeIsOfTheModelItsKeysLeadInNotOneHoldingItsPartAnotherWay) {
  using namespace scope_leads;
  auto node = c::scoped<LeadAccount>(c::handlers(), c::bound<model::Field<&LeadAccount::n>>(LeadReads{}), std::string("a"));
  model::Model<LeadSettings, bind::NoReactions> settings{LeadSettings{}};
  (void)settings.apply(model::put<LeadAccount>(std::string("a"), LeadAccount{7}));
  model::Model<LeadShown, bind::NoReactions> shown{LeadShown{.page = {LeadPage{LeadAccount{3}}}}};
  bind::Binding<decltype(shown)> showing;
  bind::Binding<decltype(settings)> setting;
  showing.refresh(node, shown);
  setting.refresh(node, settings);
  showing.refresh(node, shown);
  const LeadReads *reads = scene::findNode<LeadReads>(node);
  ASSERT_NE(reads, nullptr);
  EXPECT_EQ(reads->n, 7);
}
// A list in selection mode (spanned_for on): a click on a row says a span
// of that one row, done -- as mux's chat selects or lets go of a message.
namespace click_span {
namespace c = skiff::compose;
using namespace window_edge;
struct Spans {
  int spans = 0;
  std::string first, last;
  void take(const c::ListSpan<std::string, Tag> &one) {
    if (one.done) {
      ++spans;
      first = one.first;
      last = one.last;
    }
  }
  void take(const c::ListEdge<Tag> &) {}
  void take(const c::ListAimed<std::string, Tag> &) {}
  void take(const c::ListSeen<std::string, Tag> &) {}
};
}  // namespace click_span
TEST(Compose, AClickOnARowInSelectionSaysItsSpan) {
  using namespace click_span;
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::spanned_for<FeedTold>([](const FeedTold &now) { return now.of == 2; }, bind::Bound<FeedTold, FeedWindow>(std::move(window)))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 2}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  Spans heard;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    for (const auto &one : std::exchange(scene::hostWork().answers, {}))
      (void)bind::answer(tree.root(), m, one, &heard);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{150.0f, 200.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{150.0f, 200.0f, 1});
  for (double at = 600.0; at < 700.0; at += 16.0)
    frame(at);
  std::println("click in selection: {} span(s), {} to {}", heard.spans, heard.first, heard.last);
  EXPECT_EQ(heard.spans, 1);
  EXPECT_EQ(heard.first, heard.last);
}
TEST(Compose, AClickOnARowInSelectionSaysItsSpanWhereTheWrapperIsNamed) {
  using namespace click_span_named;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), list()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 2}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  click_span::Spans heard;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    for (const auto &one : std::exchange(scene::hostWork().answers, {}))
      (void)bind::answer(tree.root(), m, one, &heard);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  scene::hostWork().pressed.clear();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{150.0f, 200.0f, 1});
  (void)tree.dispatchPointer(scene::pointer::up{150.0f, 200.0f, 1});
  for (double at = 600.0; at < 700.0; at += 16.0)
    frame(at);
  std::println("named wrapper: {} span(s)", heard.spans);
  EXPECT_EQ(heard.spans, 1);
}
TEST(Compose, ANamedListScrollsByTheWheelAndByADragOverItsRows) {
  using namespace named_wheel;
  const auto wheel = [](auto &tree) {
    for (int notch = 0; notch < 30; ++notch)
      (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, 120.0f});
  };
  const auto drag = [](auto &tree) {
    (void)tree.dispatchPointer(scene::pointer::down{150.0f, 100.0f, 1});
    for (int step = 1; step <= 20; ++step)
      (void)tree.dispatchPointer(scene::pointer::move{150.0f, 100.0f + 10.0f * static_cast<float>(step)});
    (void)tree.dispatchPointer(scene::pointer::up{150.0f, 300.0f, 1});
  };
  const float wheeled = moved_by(named(), wheel);
  const float dragged = moved_by(named(), drag);
  std::println("named list: moved {} by the wheel, {} by a drag", wheeled, dragged);
  EXPECT_GT(wheeled, 100.0f);
  EXPECT_GT(dragged, 100.0f);
}
// A press answering an edit itself -- a picker's choice: made in the Local
// holding its part; and one in a scope of another model's part, pressed
// with each model: made by the model the scope is of, passed by the other.
namespace pressed_edit {
namespace c = skiff::compose;
struct PickDraft {
  int choice = 0;
  friend bool operator==(const PickDraft &, const PickDraft &) = default;
};
struct PickReads : scene::Node {
  int n = -1;
  void read(int now) { n = now; }
};
struct PickBox : scene::Node {
  PickBox() { this->apply({.width = 40.0f, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct Picked {
  auto operator()() const { return model::over<PickDraft>(model::setTo(PickDraft{5})); }
};
struct Counted {
  auto operator()() const { return model::over<scope_leads::LeadAccount>(model::setTo(scope_leads::LeadAccount{9})); }
};
struct NoSink {
  void take(int) {}
};
}  // namespace pressed_edit
TEST(Compose, APressAnsweringAnEditMakesItWhereItsPartIs) {
  using namespace pressed_edit;
  using scope_leads::LeadAccount;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::local<PickDraft>(c::column(c::vbox(0.0f, {.width = 200.0f, .height = 40.0f}),
          c::row(c::hbox(0.0f, {.width = 200.0f, .height = 40.0f}), c::onPress(Picked{}, PickBox{}),
                 c::bound<model::Field<&PickDraft::choice>>(PickReads{})))),
      c::scoped<LeadAccount>(c::handlers(), c::row(c::hbox(0.0f, {.width = 200.0f, .height = 40.0f}), c::onPress(Counted{}, PickBox{}),
                                                   c::bound<model::Field<&LeadAccount::n>>(PickReads{})), std::string("a"))));
  model::Model<scope_leads::LeadSettings, bind::NoReactions> settings{scope_leads::LeadSettings{}};
  (void)settings.apply(model::put<LeadAccount>(std::string("a"), LeadAccount{7}));
  model::Model<scope_leads::LeadShown, bind::NoReactions> shown{scope_leads::LeadShown{}};
  bind::Binding<decltype(settings)> setting;
  bind::Binding<decltype(shown)> showing;
  NoSink sink;
  const auto frame = [&](double at) {
    setting.refresh(tree.root(), settings);
    showing.refresh(tree.root(), shown);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 200.0f));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)(bind::press(tree.root(), shown, way, &sink) || bind::press(tree.root(), settings, way, &sink));
  };
  for (double at = 0.0; at < 100.0; at += 16.0)
    frame(at);
  for (const float y : {20.0f, 60.0f}) {
    (void)tree.dispatchPointer(scene::pointer::down{20.0f, y, 1});
    (void)tree.dispatchPointer(scene::pointer::up{20.0f, y, 1});
    for (double at = 100.0; at < 200.0; at += 16.0)
      frame(at);
  }
  const PickReads *picked = scene::findNode<PickReads>(tree.root());
  ASSERT_NE(picked, nullptr);
  EXPECT_EQ(picked->n, 5);
  EXPECT_EQ(settings.look(model::placeOf<LeadAccount, scope_leads::LeadSettings>(std::string("a")))->n, 9);
}

// A model said by its tree: each view keeps its part where it is made
// (state<T>), with what answers what is sent within; the model is the parts
// the views say, each once -- in every build, a Held saying none.
namespace stated_model {
namespace c = skiff::compose;
struct Pressed {};
struct Tally {
  int n = 0;
};
struct Seen {
  bool on = false;
};
struct Task {
  int id = 0;
};
// A part with a list in it: kept as it is, not tracked again.
struct Tasks {
  model::Keyed<int, Task> list;
};
inline int &lastTally() {
  static int n = -1;
  return n;
}
struct TallyText : nodes::Text {
  TallyText() : nodes::Text("0", 13.0f, 0u) {}
  void read(const Tally &t) {
    lastTally() = t.n;
    setText(std::to_string(t.n));
  }
};
inline auto tally() {
  return c::state<Tally>(c::handlers(c::handle<Pressed>([] {
                           return model::over<Tally>([](Tally t) {
                             ++t.n;
                             return t;
                           });
                         })),
                         c::row(c::hbox(0.0f, {.width = 200.0f, .height = 40.0f}), c::bound<Tally>(TallyText()),
                                c::onClick(Pressed{}, nodes::Text("+", 13.0f, 0u))));
}
inline auto page() {
  return c::column(c::vbox(0.0f, {.fill = true}), tally(), c::state<Seen>(nodes::Text("seen", 13.0f, 0u)),
                   c::row(c::hbox(0.0f), c::state<Tasks>(nodes::Text("tasks", 13.0f, 0u))), tally());
}
}  // namespace stated_model

TEST(Compose, AModelIsThePartsItsTreesViewsSayTheyKeep) {
  using namespace stated_model;
  using Tree = decltype(page());
  static_assert(std::same_as<bind::PartsOf<Tree>, model::Types<model::Tracked<Tally>, model::Tracked<Seen>, Tasks>>);
  static_assert(std::same_as<bind::RootOf<Tree>, model::Assembled<model::Tracked<Tally>, model::Tracked<Seen>, Tasks>>);
  bind::ModelOf<Tree> m;
  (void)m.apply(model::put<Task>(1, Task{1}));
  EXPECT_EQ(m.root().rest.rest.part.list.size(), 1u);
  scene::Scene tree(page());
  bind::Binding<bind::ModelOf<Tree>> binding;
  binding.refresh(tree.root(), m);
  EXPECT_EQ(lastTally(), 0);
  // The first tally's button: sent up to its own state's handlers.
  EXPECT_TRUE(bind::press(tree.root(), m, scene::Path{0, 1}));
  EXPECT_EQ(m.look<Tally>()->n, 1);
  binding.refresh(tree.root(), m);
  EXPECT_EQ(lastTally(), 1);
  // The second says the same part: the same one, pressed again.
  EXPECT_TRUE(bind::press(tree.root(), m, scene::Path{3, 1}));
  EXPECT_EQ(m.look<Tally>()->n, 2);
}

}  // namespace
