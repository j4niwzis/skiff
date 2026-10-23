// Rows kept by key, nodes mounted in place, and projections: a part read through a function, a style or a visibility from it.
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


namespace {
// A row of a list fed views by key: how many were made tells which were
// kept.
struct Shown : skiff::scene::Node {
  Shown(int key, const std::string &view) : fKey(key), fText(view) {}
  int fKey;
  std::string fText;
};
struct MakeShown {
  int *fMade;
  Shown operator()(const std::pair<int, std::string> &item) const {
    ++*fMade;
    return Shown(item.first, item.second);
  }
};
} // namespace

TEST(Compose, RowsReadByKeyAreKeptWhereTheirViewIsTheSame) {
  int made = 0;
  skiff::nodes::MemoRows<int, std::string, Shown, MakeShown> rows(MakeShown{&made});
  using Item = std::pair<int, std::string>;
  EXPECT_TRUE(rows.read(std::vector<Item>{{1, "a"}, {2, "b"}, {3, "c"}}));
  EXPECT_EQ(made, 3);
  // The same: nothing made.
  EXPECT_FALSE(rows.read(std::vector<Item>{{1, "a"}, {2, "b"}, {3, "c"}}));
  EXPECT_EQ(made, 3);
  // One view changed, one gone, the order turned: one made again.
  EXPECT_TRUE(rows.read(std::vector<Item>{{3, "c"}, {1, "A"}}));
  EXPECT_EQ(made, 4);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows.fRows[0].fRow.fKey, 3);
  EXPECT_EQ(rows.fRows[1].fRow.fText, "A");
}

namespace {
struct MountedFacts {
  int value = 0;
  friend bool operator==(MountedFacts, MountedFacts) = default;
};
// A control can keep a reference to itself and cannot be moved.
struct MountedControl : scene::Node {
  const MountedControl *owner = this;
  int value;
  explicit MountedControl(int given) : value(given) {}
  MountedControl(const MountedControl &) = delete;
  MountedControl(MountedControl &&) = delete;
};
struct MountedArguments {
  int *made;
  auto operator()(const MountedFacts &facts) const {
    ++*made;
    return std::tuple{facts.value};
  }
};
struct ShortcutScreen : scene::Node {
  using Node::onKey;
  std::optional<Count> onKey(scene::phase::bubble, const scene::key::down& key,
                            scene::Reply& reply) {
    if (key.key != scene::keys::kUp || !key.modifiers.has<scene::modifier::control>())
      return std::nullopt;
    reply.handle();
    return Count{1};
  }
};
struct ShownShortcutScreen : ShortcutScreen {
  using ShortcutScreen::onKey;
};
template <class N>
concept BoundByValue = requires(N&& node) {
  bound<MountedFacts>(std::move(node));
};
// Value combinators must reject an immovable node before their bodies
// instantiate. Bound's in-place constructor still accepts its arguments.
static_assert(!BoundByValue<MountedControl>);
static_assert(!std::constructible_from<
              bind::Bound<MountedFacts, MountedControl>, MountedControl>);
static_assert(std::constructible_from<
              bind::Bound<MountedFacts, MountedControl>, std::in_place_t, int>);
static_assert(!std::constructible_from<scene::Scene<MountedControl>, MountedControl>);
static_assert(std::constructible_from<scene::Scene<MountedControl>, std::in_place_t, int>);
} // namespace
}  // namespace

// Match a program that explicitly instantiates its scene in one module
// unit: unavailable value constructors must not instantiate their bodies.
template class skiff::scene::Scene<MountedControl>;

namespace {


TEST(Compose, ExplicitlyInstantiatedSceneKeepsImmovableRootInPlace) {
  scene::Scene<MountedControl> tree(std::in_place, 7);
  EXPECT_EQ(tree.root().owner, &tree.root());
  EXPECT_EQ(tree.root().value, 7);
}

TEST(Compose, NestedBindingsKeepImmovableControlAtItsFinalAddress) {
  using Inner = bind::Bound<MountedFacts, MountedControl>;
  using Outer = bind::Bound<int, Inner>;
  Outer node(std::in_place, std::in_place, 7);
  EXPECT_EQ(node.owner, &node);
  EXPECT_EQ(node.value, 7);
  node.setVisible(false);
  EXPECT_FALSE(node.owner->visible());
}

TEST(Compose, BoundKeepsInheritedScreenShortcuts) {
  bind::Bound<Count, ShownShortcutScreen> screen;
  scene::key::down key{.key = scene::keys::kUp,
                       .modifiers = scene::modifiers::kControl};
  scene::Reply reply;
  const auto answer = screen.onKey(scene::phase::bubble{}, key, reply);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(answer->n, 1);
  EXPECT_TRUE(reply.fHandled);
}

TEST(Compose, BoundMountConstructsAndReopensImmovableContentInPlace) {
  int made = 0;
  using Content = Mounted<MountedControl, MountedFacts, MountedArguments>;
  bind::Bound<std::optional<MountedFacts>, Content> node(
      std::in_place, MountedArguments{&made}, scene::Spec{.fill = true}, true);
  for (int value : {7, 9}) {
    node.read(std::optional{MountedFacts{value}});
    ASSERT_NE(node.shown(), nullptr);
    EXPECT_EQ(node.shown()->owner, node.shown());
    EXPECT_EQ(node.shown()->value, value);
    node.shown()->setVisible(false);
    EXPECT_FALSE(node.shown()->owner->visible());
    node.read(std::optional<MountedFacts>{});
    EXPECT_EQ(node.shown(), nullptr);
  }
  EXPECT_EQ(made, 2);
}

TEST(Compose, MountedConstructsInPlaceAndKeepsEqualFacts) {
  int made = 0;
  auto node = mount<MountedControl, MountedFacts>(MountedArguments{&made});
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(node.shown(), nullptr);
  node.read(std::optional{MountedFacts{7}});
  ASSERT_NE(node.shown(), nullptr);
  EXPECT_EQ(node.shown()->owner, node.shown());
  EXPECT_EQ(node.shown()->value, 7);
  const auto id = node.shown()->fState.fId;
  node.read(std::optional{MountedFacts{7}});
  EXPECT_EQ(made, 1);
  EXPECT_EQ(node.shown()->fState.fId, id);
  node.read(std::optional{MountedFacts{9}});
  EXPECT_EQ(made, 2);
  EXPECT_EQ(node.shown()->value, 9);
  EXPECT_EQ(node.shown()->owner, node.shown());
  node.read(std::optional<MountedFacts>{});
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(node.shown(), nullptr);
}

TEST(Compose, ProjectionsReadFieldsAndRefreshFromModelEdits) {
  auto model = twoAccounts();
  auto label = text_for<model::Field<&Count::n>>(
      [](int n) { return std::format("{} items", n); },
      nodes::Text("", 12.0f, 0u));
  bind::Binding<Model> binding;
  binding.refresh(label, model);
  EXPECT_EQ(label.text(), "0 items");
  (void)model.apply(
      model::edit(model::placeOf<Count, Root>(), model::setTo(Count{8})));
  binding.refresh(label, model);
  EXPECT_EQ(label.text(), "8 items");
}

TEST(Compose, StyleProjectionPreservesItsChildren) {
  auto model = twoAccounts();
  auto node = spec_for<Count>(
      [](const Count &now) -> scene::Spec { return {.visible = now.n > 0}; },
      row(text_for<model::Field<&Count::n>>(
          [](int n) { return std::to_string(n); },
          nodes::Text("", 12.0f, 0u))));
  bind::Binding<Model> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  (void)model.apply(
      model::edit(model::placeOf<Count, Root>(), model::setTo(Count{4})));
  binding.refresh(node, model);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(std::get<0>(node.fParts).text(), "4");
}

TEST(Compose, PollingKeepsAnExternalDeadlineAndTheNodesOwnTimer) {
  auto quiet = keep_ticking(false, column(nodes::Text("Quiet", 14.0f, 0u)));
  EXPECT_FALSE(quiet.wantsTick());
  auto deadline = keep_ticking(true, column(nodes::Text("Pending", 14.0f, 0u)));
  EXPECT_TRUE(deadline.wantsTick());
  auto timer = keep_ticking(false, every<model::Nothing>(1000.0));
  EXPECT_TRUE(timer.wantsTick());
}

TEST(Compose, NamedPressKeepsItsAccessibleLabelAndActions) {
  auto button = onClick(model::Nothing{}, nodes::Text("Save", 14.0f, 0u), "Save settings");
  const auto info = button.semantics();
  EXPECT_EQ(info.fLabel, "Save settings");
  EXPECT_EQ(info.fActions.size(), 2u);
  EXPECT_TRUE(button.acceptsInput());
  EXPECT_EQ(button.fState.cursorShape().index(), scene::Cursor(scene::cursor::hand{}).index());
}

TEST(Compose, LocalModelDoesNotHideTheNodesSceneState) {
  auto node = local<int>(nodes::Text("Local", 14.0f, 0u), 3);
  EXPECT_EQ(&scene::stateOf(node), &static_cast<scene::Node&>(node).fState);
  EXPECT_EQ(node.fModel.root(), 3);
  node.apply({.width = 80.0f});
}

TEST(Compose, NamedClickKeepsTheWrappedToggleRoleAndValue) {
  struct Toggle : scene::Node {
    scene::Semantics semantics() const {
      scene::Semantics result;
      result.fRole = scene::semantic_role::toggle{};
      result.fValue = "on";
      return result;
    }
  };
  auto node = onClick(model::Nothing{}, Toggle{}, "Encrypt local data");
  const auto info = node.semantics();
  EXPECT_EQ(info.fRole.index(), scene::SemanticRole(scene::semantic_role::toggle{}).index());
  EXPECT_EQ(info.fValue, "on");
  EXPECT_EQ(info.fLabel, "Encrypt local data");
}

TEST(Compose, TextSelectionIsDeclaredAtConstruction) {
  const nodes::Text selectable("Selectable", 14.0f, 0u, false, true);
  const nodes::Text ordinary("Ordinary", 14.0f, 0u);
  EXPECT_TRUE(selectable.selectable());
  EXPECT_FALSE(ordinary.selectable());
}

TEST(Compose, PressFactoryReadsCurrentDataWithoutRebuildingTheNode) {
  struct Event { int value; };
  struct Sink {
    std::vector<int> values;
    void take(const Event& event) { values.push_back(event.value); }
  } sink;
  model::Model<int, bind::NoReactions> model(0);
  int current = 1;
  auto button = onPress([&] { return Event{current}; }, Pad{}, "Current value");
  const auto id = button.fState.id();
  ASSERT_TRUE(bind::press(button, model, scene::Path{}, &sink));
  current = 2;
  ASSERT_TRUE(bind::press(button, model, scene::Path{}, &sink));
  EXPECT_EQ(sink.values, (std::vector<int>{1, 2}));
  EXPECT_EQ(button.fState.id(), id);
}

TEST(Compose, VisibilityProjectionReadsLocalModelFields) {
  struct Flag { bool on = false; };
  model::Model<int, bind::NoReactions> model(0);
  auto node = local<Flag>(shown_for<model::Field<&Flag::on>>(
      [](bool on) { return on; }, Pad{}), Flag{});
  bind::Binding<decltype(model)> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  node.fModel.apply(model::over<model::Field<&Flag::on>>(model::setTo(true)));
  binding.invalidate();
  binding.refresh(node, model);
  EXPECT_TRUE(node.visible());
}

TEST(Compose, AProjectionOverAProjectedNodeTellsItTheSamePart) {
  struct Flag {
    bool on = false;
    int width = 10;
  };
  struct Root {
    model::Tracked<Flag> flag;
  };
  model::Model<Root, bind::NoReactions> model;
  // Shown as one projection says, its width as the one under it does.
  auto node = shown_for<Flag>([](const Flag& now) { return now.on; },
                              spec_for<Flag>([](const Flag& now) { return scene::Spec{.width = static_cast<float>(now.width)}; }, Pad{}));
  bind::Binding<decltype(model)> binding;
  binding.refresh(node, model);
  EXPECT_FALSE(node.visible());
  EXPECT_EQ(node.fState.width(), 10.0f);
  model.apply(model::edit(model::placeOf<Flag, Root>(), model::setTo(Flag{true, 30})));
  binding.refresh(node, model);
  EXPECT_TRUE(node.visible());
  EXPECT_EQ(node.fState.width(), 30.0f);
}

TEST(Compose, ARefreshCombinesChangedPathsAndLeavesOtherBindingsAlone) {
  struct First { int value = 0; };
  struct Second { int value = 0; };
  struct Third { int value = 0; };
  struct Root {
    model::Tracked<First> first;
    model::Tracked<Second> second;
    model::Tracked<Third> third;
  };
  struct Probe : scene::Node {
    int reads = 0, value = 0;
    void read(const First& part) { value = part.value; ++reads; }
    void read(const Second& part) { value = part.value; ++reads; }
    void read(const Third& part) { value = part.value; ++reads; }
  };
  model::Model<Root, bind::NoReactions> model;
  auto page = column(bound<First>(Probe{}), bound<Second>(Probe{}), bound<Third>(Probe{}));
  bind::Binding<decltype(model)> binding;
  binding.refresh(page, model);
  model.applyBatch(model::over<First>(model::setTo(First{1})), model::over<Second>(model::setTo(Second{2})));
  binding.refresh(page, model);
  EXPECT_EQ(std::get<0>(page.fParts).reads, 2);
  EXPECT_EQ(std::get<0>(page.fParts).value, 1);
  EXPECT_EQ(std::get<1>(page.fParts).reads, 2);
  EXPECT_EQ(std::get<1>(page.fParts).value, 2);
  EXPECT_EQ(std::get<2>(page.fParts).reads, 1);
  binding.refresh(page, model);
  EXPECT_EQ(std::get<0>(page.fParts).reads, 2);
}

TEST(Compose, PointerHandlerRetainsClickOutputsAndReadsItsNode) {
  auto node = onPointer<std::string>([](auto& base, const scene::pointer::down& event, scene::PointerReply& reply) -> std::optional<std::string> {
    if (event.button != 3) return std::nullopt;
    reply.handle();
    return std::get<0>(base.fParts).text();
  }, onClick(42, column(vbox(), nodes::Text("event", 13.0f, 0))));
  static_assert(std::same_as<OutOf<decltype(node)>, model::Types<int, std::string>>);
  EXPECT_EQ(node.onPress(), 42);
  scene::PointerReply reply;
  EXPECT_EQ(node.onPointer(scene::phase::bubble{}, scene::pointer::down{.button = 3}, reply), "event");
  EXPECT_FALSE(node.onPointer(scene::phase::bubble{}, scene::pointer::down{.button = 1}, reply).has_value());
}

TEST(Compose, LocalRefreshCanEnterDynamicAndMemoizedChildren) {
  struct FlipLocal {};
  const auto makeLocal = [] {
    return local<Expanded>(
        handlers(handle<FlipLocal>([](const FlipLocal &) { return model::over<Expanded>(model::flip); })),
        onClick(FlipLocal{}, bound<Expanded>(Chevron{})));
  };
  using local_t = decltype(makeLocal());
  static_assert(bind::detail::kHasLocal<std::optional<local_t>>);
  static_assert(bind::detail::kHasLocal<std::vector<local_t>>);
  static_assert(bind::detail::kHasLocal<std::unique_ptr<local_t>>);
  static_assert(bind::detail::kHasLocal<std::shared_ptr<local_t>>);
  static_assert(bind::detail::kHasLocal<std::reference_wrapper<local_t>>);
  static_assert(bind::detail::kHasLocal<std::tuple<int, local_t>>);
  static_assert(bind::detail::kHasLocal<std::variant<int, local_t>>);
  static_assert(bind::detail::kHasLocal<spl::variant<int, local_t>>);
  using memo_t = nodes::Memo<int, local_t>;
  static_assert(bind::detail::kHasLocal<memo_t>);
  using mounted_t = Mounted<local_t, int, std::identity>;
  static_assert(bind::detail::kHasLocal<mounted_t>);
  using kept_row_t = nodes::KeptRow<int, int, local_t>;
  static_assert(bind::detail::kHasLocal<kept_row_t>);
  const auto makeItem = [makeLocal](const std::tuple<int, int> &) { return makeLocal(); };
  using memo_rows_t = nodes::MemoRows<int, int, local_t, decltype(makeItem)>;
  static_assert(bind::detail::kHasLocal<memo_rows_t>);
  using holding_t = Holding<std::variant<nodes::Text, local_t>>;
  static_assert(bind::detail::kHasLocal<holding_t>);
  using window_list_t = WindowList<int, int, local_t, decltype(makeItem)>;
  static_assert(bind::detail::kHasLocal<window_list_t>);
  const auto noopAct = [] { return scene::Taken{}; };
  const auto noopShow = [](nodes::Text &, float) {};
  using swiped_t = Swiped<decltype(noopAct), nodes::Text, decltype(noopShow), local_t>;
  static_assert(bind::detail::kHasLocal<swiped_t>);
  const auto noopRest = [](bool) { return scene::Taken{}; };
  using resting_t = Resting<decltype(noopRest), local_t>;
  static_assert(bind::detail::kHasLocal<resting_t>);
  static_assert(!bind::detail::kHasLocal<nodes::Memo<int, nodes::Text>>);
  static_assert(!bind::detail::kHasLocal<nodes::KeptRow<int, int, nodes::Text>>);
  static_assert(!bind::detail::kHasLocal<Holding<std::variant<nodes::Text>>>);
  static_assert(!bind::detail::kHasLocal<std::vector<int>>);
  static_assert(!bind::detail::kHasLocal<std::unique_ptr<nodes::Text>>);
  static_assert(!bind::detail::kHasLocal<std::tuple<int, nodes::Text>>);

  // Verify on the Release LocalPass path that editing a Local inside each
  // holder refreshes its bound child without an outer model change.
  using PlainModel = model::Model<int, bind::NoReactions>;
  const auto checkHolder = [](auto &&holder, auto &&findLocal) {
    PlainModel model(0);
    bind::Binding<PlainModel> binding;
    binding.refresh(holder, model);
    local_t &target = findLocal(holder);
    EXPECT_FALSE(target.fOpen);
    ASSERT_TRUE(bind::press(target, model, scene::Path{}));
    // Also exercise detail::LocalPass<PlainModel> directly (the Release path).
    bind::detail::LocalPass<PlainModel> pass{model};
    bind::detail::visitNode(pass, holder, model::Place<int, model::Path<>>{});
    binding.refresh(holder, model);
    EXPECT_TRUE(target.fOpen);
  };

  memo_rows_t rows(makeItem);
  rows.read(std::vector<std::tuple<int, int>>{{1, 10}});
  checkHolder(rows, [](memo_rows_t &h) -> local_t & { return h.fRows[0].fRow; });

  holding_t held(makeLocal());
  checkHolder(held, [](holding_t &h) -> local_t & { return std::get<local_t>(h.parts.shown); });

  window_list_t win(makeItem);
  win.read(WindowShown<std::tuple<int, int>, int>{.items = {{1, 10}}});
  checkHolder(win, [](window_list_t &h) -> local_t & { return h.rows().fRows[0].fRow; });

  swiped_t swipedNode(48.0f, 72.0f, noopAct, noopShow, nodes::Text("", 12.0f, 0), makeLocal());
  checkHolder(swipedNode, [](swiped_t &h) -> local_t & { return std::get<0>(h.fParts); });

  resting_t restingNode(300.0, noopRest, makeLocal());
  checkHolder(restingNode, [](resting_t &h) -> local_t & { return std::get<0>(h.fParts); });

  using page_t = spl::variant<local_t>;
  auto pages = page_view<int, page_t>(
      [makeLocal](page_t &page, int) { page.template emplace<0>(makeLocal()); },
      [](int a, int b) { return a == b; },
      [](int) { return 1.0f; },
      [](int) -> std::optional<scene::Spec> { return std::nullopt; },
      page_t(std::in_place_index<0>, makeLocal()), {});
  static_assert(bind::detail::kHasLocal<decltype(pages)>);
  checkHolder(pages, [](auto &h) -> local_t & { return spl::get<local_t>(h.page()); });
}

}  // namespace
