import std;
import splice;
import gtest;
import skia;
import skiff.nodes;
import skiff.paint;
import skiff.scene;

#include "gtest/gtest-macros.h"

namespace {

using namespace skiff::scene;
using skiff::nodes::Box;
using skiff::nodes::Flow;
using skiff::nodes::FlowOptions;
using skiff::nodes::ScrollContainer;
using skiff::nodes::Text;

const auto kViewport = skia::SkRect::MakeWH(100.0f, 60.0f);

TEST(Animation, DoesNotSettleBeforeApproachReachesItsTarget) {
  float value = 0.0015f;
  EXPECT_FALSE(skiff::paint::settled(value, 0.0f));

  int frames = 0;
  while (!skiff::paint::settled(value, 0.0f)) {
    value = skiff::paint::approach(value, 0.0f, 110.0f, 16.0);
    ASSERT_LT(++frames, 20);
  }

  EXPECT_FLOAT_EQ(value, 0.0f);
}

struct Card;
struct Panel;
struct OwnText;
struct Moved;
struct Widget;
struct ProbeRole;

inline constexpr skia::SkColor kOriginal = 0xff102030;
inline constexpr skia::SkColor kCard = 0xff405060;
inline constexpr skia::SkColor kSelected = 0xff708090;
inline constexpr skia::SkColor kInheritedText = 0xffa0b0c0;
inline constexpr skia::SkColor kOwnText = 0xffd0e0f0;

// A widget of its own, styled by its own type.
struct WidgetBox : Box<> {
  using Box<>::Box;
};

// A node that counts how often it is styled.
struct StyleProbe : Node {
  int fApplications = 0;
  void applyNodeStyle(const Style &, bool) { ++fApplications; }
};

// A node that counts how often it is laid out.
struct LayoutProbe : Node {
  int fLayouts = 0;
  void measure(const skia::SkRect &) { ++fLayouts; }
};

// A node that counts clicks and draws its hover.
struct ClickProbe : Node {
  int fClicks = 0;
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool hoverChangesAppearance() const { return true; }
  [[nodiscard]] bool onClick(float, float) {
    ++fClicks;
    return true;
  }
};

// A node that writes down every event it sees, and can take the pointer.
template <class... Children> struct InputProbe : Node {
  InputProbe(std::vector<std::string> *events, std::string name,
             bool capture = false, Children... children)
      : fChildren(std::move(children)...), fEvents(events),
        fName(std::move(name)), fCapture(capture) {}

  void forEachChild(auto &&f) {
    std::apply([&](auto &...each) { (f(each), ...); }, fChildren);
  }
  [[nodiscard]] bool acceptsInput() const { return true; }

  static int number(phase::capture) { return 0; }
  static int number(phase::target) { return 1; }
  static int number(phase::bubble) { return 2; }

  void note(int at, std::string_view what) {
    if (fEvents != nullptr) {
      fEvents->push_back(std::format("{}:{}{}", fName, what, at));
    }
  }
  // Every pointer event is written down; a capturing probe takes the press
  // and the moves at the target.
  template <class Phase, class Input>
  void onPointer(const Phase &at, const Input &, PointerReply &) {
    ++fPointerEvents;
    this->note(number(at), "");
  }
  void onPointer(phase::target at, const pointer::down &, PointerReply &reply) {
    ++fPointerEvents;
    this->note(number(at), "");
    if (fCapture) {
      reply.capturePointer();
      reply.requestFocus();
      reply.handle();
    }
  }
  void onPointer(phase::target at, const pointer::move &, PointerReply &reply) {
    ++fPointerEvents;
    this->note(number(at), "");
    if (fCapture) {
      reply.handle();
    }
  }
  template <class Phase, class Input>
  void onKey(const Phase &at, const Input &, Reply &) {
    this->note(number(at), "key:");
  }
  [[nodiscard]] Semantics semantics() const {
    Semantics out;
    out.fRole = semantic_role::button{};
    out.fLabel = fName;
    out.fActions = {semantic_action::focus{}, semantic_action::activate{}};
    return out;
  }

  std::tuple<Children...> fChildren;
  int fPointerEvents = 0;
  std::vector<std::string> *fEvents;
  std::string fName;
  bool fCapture;
};
InputProbe(std::vector<std::string> *, const char *) -> InputProbe<>;
InputProbe(std::vector<std::string> *, const char *, bool) -> InputProbe<>;

struct CardTheme {
  static constexpr auto styles =
      makeStyleSheet()
          .rule(selectAny(), {.alpha = 0.9f})
          .rule(select<Box, Card>(),
                {.width = 20.0f, .height = 5.0f, .backgroundColour = kCard})
          .rule(select<Box, Card>().when(states::kSelected),
                {.width = 30.0f, .backgroundColour = kSelected})
          .rule(select<Box, Card>().when(states::kHover), {.scale = 1.2f})
          .rule(select<Box, Card>().when(states::kDisabled),
                {.alpha = 0.25f})
          .rule(selectAny<Moved>(), {.y = 42.0f})
          .rule(select<Box, Card>().atMostWidth(500.0f), {.height = 12.0f});
};

struct TextTheme {
  static constexpr auto styles =
      makeStyleSheet()
          .rule(selectAny<Panel>(),
                {.colour = kInheritedText, .fontSize = 18.0f, .fontBold = true})
          .rule(select<Text, OwnText>(),
                {.colour = kOwnText, .fontBold = false});
};

struct FocusTheme {
  static constexpr auto styles = makeStyleSheet().rule(
      select<InputProbe>().when(states::kFocus), {.alpha = 0.65f});
};

struct WidgetTheme {
  static constexpr auto styles = makeStyleSheet().rule(
      select<WidgetBox, Widget>(), {.width = 64.0f, .backgroundColour = kCard});
};

struct ProbeTheme {
  static constexpr auto styles =
      makeStyleSheet().rule(select<StyleProbe, ProbeRole>(), {.alpha = 0.9f});
};

// ---- styles

struct CardScreen : Node {
  struct parts_t {
    Box<> card = make<Box>({.width = 10.0f, .height = 6.0f, .roles = {role<Card>}},
                           kOriginal);
  } parts;
};

TEST(Style, ResolvesTypedRolesStatesAndViewport) {
  Scene<CardScreen> scene{std::in_place};
  Box<> &card = scene.root().parts.card;
  scene.setStyleSheet<CardTheme>();
  scene.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));

  EXPECT_FLOAT_EQ(card.fState.width(), 20.0f);
  EXPECT_FLOAT_EQ(card.fState.height(), 12.0f);
  EXPECT_FLOAT_EQ(card.fState.alpha(), 0.9f);
  EXPECT_EQ(card.colour(), kCard);

  card.setSelected(true);
  scene.update(1.0);
  EXPECT_FLOAT_EQ(card.fState.width(), 30.0f);
  EXPECT_EQ(card.colour(), kSelected);

  card.setDisabled(true);
  scene.update(2.0);
  EXPECT_FLOAT_EQ(card.fState.alpha(), 0.25f);

  card.setDisabled(false);
  card.setSelected(false);
  scene.update(3.0);
  scene.layoutIfNeeded(skia::SkRect::MakeWH(800.0f, 300.0f));
  EXPECT_FLOAT_EQ(card.fState.height(), 5.0f);

  scene.setHover(1.0f, 1.0f);
  EXPECT_TRUE(card.hovered());
  EXPECT_FLOAT_EQ(card.fState.scale(), 1.2f);

  scene.clearStyleSheet();
  EXPECT_FLOAT_EQ(card.fState.width(), 10.0f);
  EXPECT_FLOAT_EQ(card.fState.height(), 6.0f);
  EXPECT_FLOAT_EQ(card.fState.alpha(), 1.0f);
  EXPECT_FLOAT_EQ(card.fState.scale(), 1.0f);
  EXPECT_EQ(card.colour(), kOriginal);
}

struct TextPanel : Node {
  struct parts_t {
    Text inherited = make<Text>({}, "Inherited", 11.0f, kOriginal, false);
    Text overridden =
        make<Text>({.roles = {role<OwnText>}}, "Own", 12.0f, kOriginal, true);
  } parts;
};

TEST(Style, InheritsTextPropertiesAndAllowsOverrides) {
  Scene<TextPanel> scene{std::in_place};
  scene.root().fState.apply({.roles = {role<Panel>}});
  Text &inherited = scene.root().parts.inherited;
  Text &overridden = scene.root().parts.overridden;
  scene.setStyleSheet<TextTheme>();

  EXPECT_EQ(inherited.colour(), kInheritedText);
  EXPECT_FLOAT_EQ(inherited.fontSize(), 18.0f);
  EXPECT_TRUE(inherited.bold());
  EXPECT_EQ(overridden.colour(), kOwnText);
  EXPECT_FLOAT_EQ(overridden.fontSize(), 18.0f);
  EXPECT_FALSE(overridden.bold());

  scene.clearStyleSheet();
  EXPECT_EQ(inherited.colour(), kOriginal);
  EXPECT_FLOAT_EQ(inherited.fontSize(), 11.0f);
  EXPECT_FALSE(inherited.bold());
  EXPECT_EQ(overridden.colour(), kOriginal);
  EXPECT_FLOAT_EQ(overridden.fontSize(), 12.0f);
  EXPECT_TRUE(overridden.bold());
}

struct LaterScreen : Node {
  struct parts_t {
    std::optional<Box<>> card;
  } parts;
};

TEST(Style, StylesNodesAddedAfterTheSheetIsInstalled) {
  Scene<LaterScreen> scene{std::in_place};
  scene.layoutIfNeeded(skia::SkRect::MakeWH(800.0f, 300.0f));
  scene.setStyleSheet<CardTheme>();

  Box<> &card = scene.root().parts.card.emplace(
      make<Box>({.roles = {role<Card>}, .selected = true}, kOriginal));
  scene.update(1.0);

  EXPECT_FLOAT_EQ(card.fState.width(), 30.0f);
  EXPECT_FLOAT_EQ(card.fState.height(), 5.0f);
  EXPECT_FLOAT_EQ(card.fState.alpha(), 0.9f);
  EXPECT_EQ(card.colour(), kSelected);

  // A state change restyles the node, but y is the application's: no rule
  // mentions it, so the cascade must not undo a run-time move.
  card.setPosition(card.fState.x(), 17.0f);
  card.setDisabled(true);
  scene.update(2.0);
  EXPECT_FLOAT_EQ(card.fState.y(), 17.0f);

  card.addStyleRole<Moved>();
  scene.update(3.0);
  EXPECT_FLOAT_EQ(card.fState.y(), 42.0f);
  card.removeStyleRole<Moved>();
  scene.update(4.0);
  EXPECT_FLOAT_EQ(card.fState.y(), 17.0f);
}

struct WidgetScreen : Node {
  struct parts_t {
    WidgetBox widget = placed({.roles = {role<Widget>}}, WidgetBox(kOriginal));
  } parts;
};

TEST(Style, SelectsANodeTypeOfItsOwnWithoutRtti) {
  Scene<WidgetScreen> scene{std::in_place};
  scene.setStyleSheet<WidgetTheme>();
  EXPECT_FLOAT_EQ(scene.root().parts.widget.fState.width(), 64.0f);
  EXPECT_EQ(scene.root().parts.widget.colour(), kCard);
}

struct AnyBoxTheme {
  static constexpr auto styles =
      makeStyleSheet().rule(select<Box>(), {.alpha = 0.5f});
};
struct TwoBoxes : Node {
  struct parts_t {
    Box<> plain{kOriginal};
    Box<Box<>> holding{kOriginal, Box<>(kOriginal)};
  } parts;
};

TEST(Style, ARuleForATemplateMatchesEverySpecialisation) {
  Scene<TwoBoxes> scene{std::in_place};
  scene.setStyleSheet<AnyBoxTheme>();
  EXPECT_FLOAT_EQ(scene.root().parts.plain.fState.alpha(), 0.5f);
  EXPECT_FLOAT_EQ(scene.root().parts.holding.fState.alpha(), 0.5f);
  EXPECT_FLOAT_EQ(std::get<0>(scene.root().parts.holding.fChildren).fState.alpha(),
                  0.5f);
  EXPECT_FLOAT_EQ(scene.root().fState.alpha(), 1.0f);
}

// ---- state and damage

struct OneBox : Node {
  struct parts_t {
    Box<> child = make<Box>(
        {.x = 12.0f, .y = 8.0f, .width = 40.0f, .height = 20.0f}, kCard);
  } parts;
};

TEST(State, DamagesNodesWithoutStateStyleRules) {
  Scene<OneBox> scene{std::in_place};
  scene.layoutIfNeeded(kViewport);
  (void)scene.finishFrame();

  scene.root().parts.child.setSelected(true);
  EXPECT_FALSE(scene.finishFrame().fDamage.isEmpty());

  scene.root().parts.child.setDisabled(true);
  EXPECT_FALSE(scene.finishFrame().fDamage.isEmpty());
}

struct ProbeScreen : Node {
  struct parts_t {
    StyleProbe probe = make<StyleProbe>(
        {.width = 40.0f, .height = 20.0f, .roles = {role<ProbeRole>}});
  } parts;
};

TEST(State, HoverOnlyRestylesNodesWithHoverRules) {
  Scene<ProbeScreen> scene{std::in_place};
  scene.setStyleSheet<ProbeTheme>();
  scene.layoutIfNeeded(kViewport);
  const int applications = scene.root().parts.probe.fApplications;
  (void)scene.finishFrame();

  scene.setHover(10.0f, 10.0f);
  EXPECT_TRUE(scene.root().parts.probe.hovered());
  EXPECT_EQ(scene.root().parts.probe.fApplications, applications);
  EXPECT_TRUE(scene.finishFrame().fDamage.isEmpty());
}

struct HalfText : Node {
  struct parts_t {
    Text text = make<Text>({.width = 0.5f, .relativeSize = axes::kX}, "short",
                           14.0f, skia::kWhite);
  } parts;
};

TEST(TextLayout, RelativeWidthIsOwnedByLayout) {
  skia::SkFont font;
  Text::setFont(&font);
  Scene<HalfText> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(240.0f, 80.0f));
  EXPECT_FLOAT_EQ(scene.root().parts.text.bounds().width(), 120.0f);
  Text::setFont(nullptr); // the font is this test's
}

// Wrapped text follows its parent's width from one layout to the next:
// the width it wraps in is not written over the one it was given.
struct WrappedTexts : Node {
  struct parts_t {
    Text filling = make<Text>({.fillX = true}, "one two three four five six seven eight nine ten",
                              14.0f, skia::kWhite);
    Text unsized{"one two three four five six seven eight nine ten", 14.0f, skia::kWhite};
  } parts;
  WrappedTexts() {
    parts.filling.setWrapped(true);
    parts.unsized.setWrapped(true);
  }
};

TEST(TextLayout, WrappedTextFollowsItsParentsWidth) {
  skia::SkFont font;
  Text::setFont(&font);
  Scene<WrappedTexts> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(240.0f, 400.0f));
  EXPECT_FLOAT_EQ(scene.root().parts.filling.bounds().width(), 240.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.unsized.bounds().width(), 240.0f);
  scene.layoutIfNeeded(skia::SkRect::MakeWH(120.0f, 400.0f));
  EXPECT_FLOAT_EQ(scene.root().parts.filling.bounds().width(), 120.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.unsized.bounds().width(), 120.0f);
  scene.layoutIfNeeded(skia::SkRect::MakeWH(480.0f, 400.0f));
  EXPECT_FLOAT_EQ(scene.root().parts.filling.bounds().width(), 480.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.unsized.bounds().width(), 480.0f);
  Text::setFont(nullptr); // the font is this test's
}

// ---- children held in other ways

struct Switching : Node {
  struct parts_t {
    splice::variant<Box<>, Text> body{std::in_place_type<Box<>>, kCard};
  } parts;
};

TEST(Children, AVariantAlternativeIsLaidOutAndOldIdsStopResolving) {
  Scene<Switching> scene{std::in_place};
  splice::get<Box<>>(scene.root().parts.body).apply({.width = 30.0f, .height = 10.0f});
  scene.layoutIfNeeded(kViewport);
  const NodeId before = splice::get<Box<>>(scene.root().parts.body).id();
  EXPECT_FLOAT_EQ(splice::get<Box<>>(scene.root().parts.body).bounds().width(), 30.0f);
  (void)scene.finishFrame();

  scene.root().parts.body.emplace<Text>("now text", 12.0f, kOriginal);
  EXPECT_TRUE(scene.layoutIfNeeded(kViewport));
  EXPECT_FALSE(scene.finishFrame().fDamage.isEmpty());
  scene.focus(before);
  EXPECT_EQ(scene.focusedId(), 0u);
}

struct Rows : Node {
  struct parts_t {
    std::vector<Box<>> rows;
    std::vector<AnyNode> mixed;
  } parts;
};

TEST(Children, AVectorOfNodesAndOfAnyNodes) {
  Scene<Rows> scene{std::in_place};
  scene.root().parts.rows.push_back(make<Box>({.width = 10.0f, .height = 5.0f}, kCard));
  scene.root().parts.rows.push_back(make<Box>({.y = 5.0f, .width = 10.0f, .height = 5.0f}, kCard));
  scene.root().parts.mixed.emplace_back(make<Box>({.y = 10.0f, .width = 20.0f, .height = 5.0f}, kCard));
  scene.root().parts.mixed.emplace_back(make<ClickProbe>({.y = 20.0f, .width = 20.0f, .height = 10.0f}));
  EXPECT_TRUE(scene.layoutIfNeeded(kViewport));
  EXPECT_FLOAT_EQ(scene.root().parts.mixed[0].state().fBounds.width(), 20.0f);

  PointerEvent down = pointer::down{5.0f, 25.0f};
  EXPECT_TRUE(scene.dispatchPointer(down));
  ASSERT_NE(scene.root().parts.mixed[1].get<ClickProbe>(), nullptr);
  EXPECT_EQ(scene.root().parts.mixed[1].get<ClickProbe>()->fClicks, 1);
  EXPECT_EQ(scene.root().parts.mixed[0].get<ClickProbe>(), nullptr);
}

// A node that is also a range of its own items, marked as a node.
struct Listing : Node {
  struct parts_t {
    std::vector<Box<>> items;
  } parts;
  auto begin() { return parts.items.begin(); }
  auto end() { return parts.items.end(); }
};
} // namespace
template <> inline constexpr bool skiff::scene::kTreatAsNode<Listing> = true;
namespace {

struct HasListing : Node {
  struct parts_t {
    Listing listing;
  } parts;
};

TEST(Children, ARangeMarkedAsANodeIsWalkedAsOne) {
  Scene<HasListing> scene{std::in_place};
  scene.root().parts.listing.apply({.width = 50.0f, .height = 20.0f});
  scene.root().parts.listing.parts.items.push_back(
      make<Box>({.width = 10.0f, .height = 5.0f}, kCard));
  scene.layoutIfNeeded(kViewport);
  EXPECT_FLOAT_EQ(scene.root().parts.listing.bounds().width(), 50.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.listing.parts.items[0].bounds().width(), 10.0f);
}

// ---- input

TEST(Input, PropagatesCaptureTargetAndBubbleInOrder) {
  std::vector<std::string> events;
  Scene<InputProbe<InputProbe<>>> scene{
      std::in_place, &events, "root", false,
      make<InputProbe>({.width = 40.0f, .height = 20.0f}, &events, "child")};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(kViewport);

  PointerEvent down = pointer::down{10.0f, 10.0f};
  EXPECT_FALSE(scene.dispatchPointer(down));
  EXPECT_EQ(events, (std::vector<std::string>{"root:0", "child:1", "root:2"}));

  // A press does not move the focus to a node that does not take it on a
  // press (takesFocusOnPress): the keys go where the focus is, given here.
  scene.focus(std::get<0>(scene.root().fChildren).id());
  events.clear();
  EXPECT_FALSE(scene.dispatchKey(key::down{}));
  EXPECT_EQ(events, (std::vector<std::string>{"root:key:0", "child:key:1",
                                              "root:key:2"}));
}

struct Dragged : Node {
  struct parts_t {
    InputProbe<> drag;
  } parts;
  explicit Dragged(std::vector<std::string> *events)
      : parts{.drag = make<InputProbe>({.width = 40.0f, .height = 20.0f}, events,
                              "drag", true)} {}
};

TEST(Input, PointerCaptureSurvivesLeavingTheControl) {
  std::vector<std::string> events;
  Scene<Dragged> scene{std::in_place, &events};
  InputProbe<> &child = scene.root().parts.drag;
  scene.layoutIfNeeded(kViewport);

  PointerEvent down = pointer::down{10.0f, 10.0f};
  EXPECT_TRUE(scene.dispatchPointer(down));
  EXPECT_EQ(scene.capturedId(), child.id());
  EXPECT_EQ(scene.focusedId(), child.id());

  PointerEvent move = pointer::move{500.0f, 500.0f};
  EXPECT_TRUE(scene.dispatchPointer(move));
  EXPECT_EQ(child.fPointerEvents, 2);

  PointerEvent up = pointer::up{500.0f, 500.0f};
  (void)scene.dispatchPointer(up);
  EXPECT_EQ(scene.capturedId(), 0u);
}

TEST(Input, ModalLayerBlocksPointerAndAccessibilityBehindIt) {
  std::vector<std::string> events;
  Scene<InputProbe<>> behind{std::in_place, &events, "behind"};
  Scene<Group<>> modal{std::in_place};
  behind.state().apply({.fill = true});
  modal.state().apply({.fill = true});
  behind.layoutIfNeeded(kViewport);
  modal.layoutIfNeeded(kViewport);
  const std::array<InputRouter::Layer, 2> layers = {
      InputRouter::Layer{behind.handle(), false},
      InputRouter::Layer{modal.handle(), true}};
  InputRouter router;
  router.setLayers(layers);

  PointerEvent down = pointer::down{10.0f, 10.0f};
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(behind.root().fPointerEvents, 0);
  EXPECT_TRUE(router.semantics().empty());

  const auto behindTree = behind.semanticsTree();
  ASSERT_EQ(behindTree.size(), 1u);
  EXPECT_TRUE(router.semantic(behindTree[0].fId, semantic_action::activate{}));
  EXPECT_EQ(behind.root().fPointerEvents, 0);
}

TEST(Input, ModalScopeCancelsCoveredCaptureAndRestoresPriorFocus) {
  std::vector<std::string> events;
  Scene<InputProbe<>> behind{std::in_place, &events, "behind", true};
  Scene<Group<>> modal{std::in_place};
  behind.state().apply({.fill = true});
  modal.state().apply({.fill = true});
  behind.layoutIfNeeded(kViewport);
  modal.layoutIfNeeded(kViewport);
  InputRouter router;
  const std::array<InputRouter::Layer, 1> base = {
      InputRouter::Layer{behind.handle(), false}};

  PointerEvent down = pointer::down{10.0f, 10.0f};
  // Pressed through the scene directly, as some wrappers do to read their
  // callback at once: the router adopts the capture and still cancels it
  // when a modal covers the scene.
  EXPECT_TRUE(behind.dispatchPointer(down));
  EXPECT_EQ(behind.capturedId(), behind.root().id());
  EXPECT_EQ(behind.focusedId(), behind.root().id());

  const std::array<InputRouter::Layer, 2> covered = {
      InputRouter::Layer{behind.handle(), false},
      InputRouter::Layer{modal.handle(), true}};
  router.setLayers(covered);
  EXPECT_EQ(behind.capturedId(), 0u);

  router.setLayers(base);
  EXPECT_EQ(behind.focusedId(), behind.root().id());
}

TEST(Input, TabTraversesAcrossSceneRoots) {
  std::vector<std::string> events;
  Scene<InputProbe<>> first{std::in_place, &events, "first"};
  Scene<InputProbe<>> second{std::in_place, &events, "second"};
  first.state().apply({.fill = true});
  second.state().apply({.fill = true});
  first.setStyleSheet<FocusTheme>();
  second.setStyleSheet<FocusTheme>();
  first.layoutIfNeeded(kViewport);
  second.layoutIfNeeded(kViewport);
  const std::array<InputRouter::Layer, 2> layers = {
      InputRouter::Layer{first.handle(), false},
      InputRouter::Layer{second.handle(), false}};
  InputRouter router;
  router.setLayers(layers);

  KeyEvent tab = key::down{keys::kTab};
  EXPECT_TRUE(router.key(tab));
  EXPECT_EQ(first.focusedId(), first.root().id());
  EXPECT_EQ(second.focusedId(), 0u);
  EXPECT_FLOAT_EQ(first.root().fState.alpha(), 0.65f);

  EXPECT_TRUE(router.key(tab));
  EXPECT_EQ(first.focusedId(), 0u);
  EXPECT_EQ(second.focusedId(), second.root().id());
  EXPECT_FLOAT_EQ(first.root().fState.alpha(), 1.0f);
  EXPECT_FLOAT_EQ(second.root().fState.alpha(), 0.65f);

  EXPECT_TRUE(router.key(key::down{keys::kTab, modifiers::kShift}));
  EXPECT_EQ(first.focusedId(), first.root().id());
}

struct Placed : Node {
  struct parts_t {
    InputProbe<> probe;
  } parts;
  Placed(std::vector<std::string> *events, const char *name, float x)
      : parts{.probe = make<InputProbe>({.x = x, .width = 40.0f, .height = 40.0f},
                               events, name, true)} {}
};

TEST(Input, PointerFocusHasOneOwnerAcrossSceneRoots) {
  std::vector<std::string> events;
  Scene<Placed> firstScene{std::in_place, &events, "first", 0.0f};
  Scene<Placed> secondScene{std::in_place, &events, "second", 60.0f};
  firstScene.layoutIfNeeded(kViewport);
  secondScene.layoutIfNeeded(kViewport);
  const NodeId first = firstScene.root().parts.probe.id();
  const NodeId second = secondScene.root().parts.probe.id();
  const std::array<InputRouter::Layer, 2> layers = {
      InputRouter::Layer{firstScene.handle(), false},
      InputRouter::Layer{secondScene.handle(), false}};
  InputRouter router;
  router.setLayers(layers);

  PointerEvent down = pointer::down{10.0f, 10.0f};
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(firstScene.focusedId(), first);
  EXPECT_FALSE(router.pointer(pointer::up{10.0f, 10.0f}));

  down = pointer::down{70.0f, 10.0f};
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(firstScene.focusedId(), 0u);
  EXPECT_EQ(secondScene.focusedId(), second);

  const auto firstSemantics = firstScene.semanticsTree();
  ASSERT_EQ(firstSemantics.size(), 1u);
  EXPECT_TRUE(router.semantic(firstSemantics[0].fId, semantic_action::focus{}));
  EXPECT_EQ(firstScene.focusedId(), first);
  EXPECT_EQ(secondScene.focusedId(), 0u);
}

TEST(Input, DestroyedSceneMakesRetainedLayerInert) {
  std::vector<std::string> events;
  InputRouter router;
  {
    Scene<InputProbe<>> scene{std::in_place, &events, "temporary", true};
    scene.state().apply({.fill = true});
    scene.layoutIfNeeded(kViewport);
    const std::array<InputRouter::Layer, 1> layers = {
        InputRouter::Layer{scene.handle(), false}};
    router.setLayers(layers);

    PointerEvent down = pointer::down{10.0f, 10.0f};
    EXPECT_TRUE(router.pointer(down));
    EXPECT_EQ(scene.capturedId(), scene.root().id());
  }

  PointerEvent move = pointer::move{20.0f, 20.0f};
  EXPECT_FALSE(router.pointer(move));
  EXPECT_TRUE(router.semantics().empty());

  KeyEvent tab = key::down{keys::kTab};
  EXPECT_FALSE(router.key(tab));
  router.setLayers({});

  Scene<InputProbe<>> behind{std::in_place, &events, "behind", true};
  behind.state().apply({.fill = true});
  behind.layoutIfNeeded(kViewport);
  {
    Scene<Group<>> modal{std::in_place};
    const std::array<InputRouter::Layer, 2> layers = {
        InputRouter::Layer{behind.handle(), false},
        InputRouter::Layer{modal.handle(), true}};
    router.setLayers(layers);
  }

  // An expired modal is not an invisible shield over live layers.
  EXPECT_EQ(router.semantics().size(), 1u);
  PointerEvent down = pointer::down{10.0f, 10.0f};
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(behind.capturedId(), behind.root().id());
}

// ---- frames and layout

struct OneGroup : Node {
  struct parts_t {
    Group<> child = make<Group<>>({.width = 20.0f, .height = 10.0f});
  } parts;
};

TEST(Frame, RuntimePropertiesInvalidateAndReportContinuationTogether) {
  Scene<OneGroup> scene{std::in_place};
  Group<> &child = scene.root().parts.child;
  scene.layoutIfNeeded(kViewport);
  (void)scene.finishFrame();

  child.setPosition(30.0f, 5.0f);
  scene.layoutIfNeeded(kViewport);
  const FrameResult moved = scene.finishFrame();
  EXPECT_FALSE(moved.fDamage.isEmpty());
  EXPECT_FALSE(moved.fWantsAnotherFrame);

  child.apply({.alpha = 0.5f});
  EXPECT_FALSE(scene.finishFrame().fDamage.isEmpty());

  child.moveToX(60.0f, 100.0);
  EXPECT_TRUE(scene.finishFrame().fWantsAnotherFrame);
}

struct Branches : Node {
  struct parts_t {
    Group<LayoutProbe> dirty{make<LayoutProbe>({.width = 20.0f, .height = 10.0f})};
    Group<LayoutProbe> clean{make<LayoutProbe>({.width = 20.0f, .height = 10.0f})};
  } parts;
  Branches() {
    parts.dirty.apply({.width = 50.0f, .height = 60.0f});
    parts.clean.apply({.x = 50.0f, .width = 50.0f, .height = 60.0f});
  }
};

TEST(Layout, SkipsCleanSiblingSubtrees) {
  Scene<Branches> scene{std::in_place};
  LayoutProbe &dirty = std::get<0>(scene.root().parts.dirty.fChildren);
  LayoutProbe &clean = std::get<0>(scene.root().parts.clean.fChildren);

  EXPECT_TRUE(scene.layoutIfNeeded(kViewport));
  EXPECT_EQ(dirty.fLayouts, 1);
  EXPECT_EQ(clean.fLayouts, 1);

  dirty.setSize(25.0f, 10.0f);
  EXPECT_TRUE(scene.layoutIfNeeded(kViewport));
  EXPECT_EQ(dirty.fLayouts, 2);
  EXPECT_EQ(clean.fLayouts, 1);

  EXPECT_FALSE(scene.layoutIfNeeded(kViewport));
  EXPECT_EQ(dirty.fLayouts, 2);
  EXPECT_EQ(clean.fLayouts, 1);
}

struct OneProbe : Node {
  struct parts_t {
    LayoutProbe child = make<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  } parts;
};

TEST(Layout, PaintOnlyAnimationDoesNotDirtyLayout) {
  Scene<OneProbe> scene{std::in_place};
  LayoutProbe &child = scene.root().parts.child;
  scene.layoutIfNeeded(kViewport);
  ASSERT_EQ(child.fLayouts, 1);

  child.apply({.alpha = 0.8f});
  EXPECT_FALSE(scene.layoutIfNeeded(kViewport));
  EXPECT_EQ(child.fLayouts, 1);

  child.fadeTo(0.5f, 100.0);
  scene.update(10.0);
  scene.setHover(10.0f, 5.0f);
  ASSERT_TRUE(child.hovered());
  EXPECT_FALSE(scene.layoutIfNeeded(kViewport));
  EXPECT_EQ(child.fLayouts, 1);
  EXPECT_TRUE(scene.finishFrame().fWantsAnotherFrame);

  child.moveToX(30.0f, 100.0);
  scene.update(20.0);
  EXPECT_TRUE(scene.layoutIfNeeded(kViewport));
  EXPECT_EQ(child.fLayouts, 2);
}

TEST(Layout, FlowRearrangesOnlyChildrenWhosePositionChanged) {
  using Column = Flow<LayoutProbe, LayoutProbe, LayoutProbe>;
  Scene<Column> scene{
      std::in_place, FlowOptions{},
      make<LayoutProbe>({.width = 20.0f, .height = 10.0f}),
      make<LayoutProbe>({.width = 20.0f, .height = 10.0f}),
      make<LayoutProbe>({.width = 20.0f, .height = 10.0f})};
  scene.state().apply({.fill = true});
  auto &[first, second, third] = scene.root().fChildren;
  scene.layoutIfNeeded(kViewport);
  ASSERT_FLOAT_EQ(second.bounds().fTop, 10.0f);
  ASSERT_FLOAT_EQ(third.bounds().fTop, 20.0f);
  const int firstLayouts = first.fLayouts;
  const int secondLayouts = second.fLayouts;
  const int thirdLayouts = third.fLayouts;

  first.setSize(20.0f, 20.0f);
  scene.layoutIfNeeded(kViewport);
  EXPECT_FLOAT_EQ(second.bounds().fTop, 20.0f);
  EXPECT_FLOAT_EQ(third.bounds().fTop, 30.0f);
  EXPECT_EQ(first.fLayouts, firstLayouts + 1);
  // Only moved: carried where it goes as it was laid out, not laid out again.
  EXPECT_EQ(second.fLayouts, secondLayouts);
  EXPECT_EQ(third.fLayouts, thirdLayouts);

  const int movedSecondLayouts = second.fLayouts;
  const int movedThirdLayouts = third.fLayouts;
  first.setSize(25.0f, 20.0f);
  scene.layoutIfNeeded(kViewport);
  EXPECT_EQ(first.fLayouts, firstLayouts + 2);
  EXPECT_EQ(second.fLayouts, movedSecondLayouts);
  EXPECT_EQ(third.fLayouts, movedThirdLayouts);
}

TEST(Layout, ProgrammaticScrollInvalidatesItsSubtree) {
  Scene<ScrollContainer<LayoutProbe>> scene{
      std::in_place, make<LayoutProbe>({.width = 100.0f, .height = 200.0f})};
  scene.state().apply({.fill = true});
  LayoutProbe &content = std::get<0>(scene.root().fChildren);
  scene.layoutIfNeeded(kViewport);
  ASSERT_FLOAT_EQ(content.bounds().fTop, 0.0f);

  scene.root().setCurrent(20.0f);
  EXPECT_TRUE(scene.layoutIfNeeded(kViewport));
  // Shifted, not moved: shown 20 up, laid out where it was -- a scroll costs
  // the same however long the list.
  EXPECT_FLOAT_EQ(content.shownBounds().fTop, -20.0f);
  EXPECT_FLOAT_EQ(content.bounds().fTop, 0.0f);
}

TEST(Input, ScrollDragCancelsDeferredChildClick) {
  Scene<ScrollContainer<ClickProbe>> scene{
      std::in_place, make<ClickProbe>({.width = 100.0f, .height = 200.0f})};
  scene.state().apply({.fill = true});
  ClickProbe &child = std::get<0>(scene.root().fChildren);
  scene.layoutIfNeeded(kViewport);
  scene.update(10.0);

  PointerEvent down = pointer::down{20.0f, 20.0f};
  EXPECT_TRUE(scene.dispatchPointer(down));
  EXPECT_EQ(child.fClicks, 0);
  EXPECT_FALSE(child.hovered());

  EXPECT_TRUE(scene.dispatchPointer(pointer::move{20.0f, 40.0f}));
  EXPECT_EQ(scene.capturedId(), scene.root().id());

  EXPECT_TRUE(scene.dispatchPointer(pointer::up{20.0f, 40.0f}));
  EXPECT_EQ(child.fClicks, 0);

  EXPECT_TRUE(scene.dispatchPointer(pointer::down{20.0f, 20.0f}));
  EXPECT_TRUE(scene.dispatchPointer(pointer::up{20.0f, 20.0f}));
  EXPECT_EQ(child.fClicks, 1);
}

TEST(Input, HorizontalGestureDoesNotBecomeVerticalScroll) {
  Scene<ScrollContainer<ClickProbe>> scene{
      std::in_place, make<ClickProbe>({.width = 100.0f, .height = 200.0f})};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(kViewport);
  scene.update(10.0);

  PointerEvent down = pointer::down{20.0f, 20.0f};
  EXPECT_TRUE(scene.dispatchPointer(down));

  scene.dispatchPointer(pointer::move{40.0f, 22.0f});
  EXPECT_EQ(scene.capturedId(), 0u);

  scene.dispatchPointer(pointer::move{40.0f, 50.0f});
  EXPECT_EQ(scene.capturedId(), 0u);
  EXPECT_FLOAT_EQ(scene.root().current(), 0.0f);
}

// ---- motion levels

TEST(Motion, EachLevelMovesWhatItShould) {
  using namespace skiff::paint;
  auto &level = motionLevel();
  const Motion was = level;
  Eased knob{0.0f, 70.0f, movement::subtle{}};
  Eased panel{0.0f, 70.0f, movement::sweeping{}};

  level = motion::full{};
  knob.setTarget(1.0f);
  panel.setTarget(1.0f);
  EXPECT_TRUE(knob.moving());
  EXPECT_TRUE(panel.moving());
  EXPECT_TRUE(knob.step(16.0));
  EXPECT_GT(knob.value(), 0.0f);
  EXPECT_LT(knob.value(), 1.0f);

  level = motion::reduced{};
  knob.jump(0.0f);
  panel.jump(0.0f);
  knob.setTarget(1.0f);
  panel.setTarget(1.0f);
  EXPECT_TRUE(knob.moving());
  EXPECT_FALSE(panel.moving());
  EXPECT_FLOAT_EQ(panel.value(), 1.0f);

  level = motion::none{};
  knob.jump(0.0f);
  knob.setTarget(1.0f);
  EXPECT_FALSE(knob.moving());
  EXPECT_FLOAT_EQ(knob.value(), 1.0f);

  level = was;
}

TEST(Motion, AnEasedValueArrivesExactly) {
  using namespace skiff::paint;
  Eased value{0.0f, 30.0f, movement::subtle{}};
  value.setTarget(1.0f);
  double now = 0.0;
  for (int frame = 0; frame < 200 && value.moving(); ++frame) {
    now += 16.0;
    (void)value.step(now);
  }
  EXPECT_FALSE(value.moving());
  EXPECT_EQ(value.value(), 1.0f);
}

TEST(Motion, ATweenTakesItsTimeEitherWay) {
  using namespace skiff::paint;
  auto &level = motionLevel();
  const Motion was = level;
  level = motion::full{};
  Tween value{0.0f, 200.0f, movement::sweeping{}};
  const auto frames = [&value](float to) {
    value.setTarget(to);
    int count = 0;
    double now = 1000.0;
    while (value.moving() && count < 1000) {
      now += 16.0;
      (void)value.step(now);
      ++count;
    }
    return count;
  };
  const int out = frames(1.0f);
  EXPECT_EQ(value.value(), 1.0f);
  const int back = frames(0.0f);
  EXPECT_EQ(value.value(), 0.0f);
  EXPECT_EQ(out, back);
  EXPECT_LE(out, 14);

  level = motion::reduced{};
  value.setTarget(1.0f);
  EXPECT_FALSE(value.moving());
  level = was;
}

// A node's time moves only with frames: a press between frames restyles the
// tree, and must not hand the nodes the last frame's time as if it were now.
struct Clocked : Node {
  std::vector<double> seen;
  void update(double nowMs) { seen.push_back(nowMs); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};

TEST(Frames, APressBetweenFramesDoesNotRunTheNodesClock) {
  Scene<Clocked> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.update(1000.0);
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 100.0f));
  (void)scene.finishFrame();
  ASSERT_EQ(scene.root().seen.size(), 1u);
  (void)scene.click(10.0f, 10.0f);
  scene.focus(scene.root().id());
  EXPECT_EQ(scene.root().seen.size(), 1u);
  scene.update(61000.0);
  ASSERT_EQ(scene.root().seen.size(), 2u);
  EXPECT_EQ(scene.root().seen.back(), 61000.0);
}

// The pointer's shape is the node's under it, or the nearest above it with
// one.
struct Shaped : Node {
  struct parts_t {
    ClickProbe edge = make<ClickProbe>({.x = 0.0f, .width = 10.0f, .height = 100.0f});
    ClickProbe plain = make<ClickProbe>({.x = 50.0f, .width = 10.0f, .height = 100.0f});
  } parts;
  Shaped() { parts.edge.fState.setCursor(cursor::resize_horizontal{}); }
};

TEST(Cursor, ThePointersShapeIsTheNodesUnderIt) {
  Scene<Shaped> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 100.0f));
  scene.setHover(5.0f, 50.0f);
  EXPECT_EQ(scene.cursor(), Cursor{cursor::resize_horizontal{}});
  scene.setHover(55.0f, 50.0f);
  EXPECT_EQ(scene.cursor(), Cursor{cursor::arrow{}});
  scene.state().setCursor(cursor::text{});
  EXPECT_EQ(scene.cursor(), Cursor{cursor::text{}});
}

// A node that takes the focus on a press, as a text field does.
struct FocusOnPress : ClickProbe {
  [[nodiscard]] bool takesFocusOnPress() const { return true; }
};
struct ShapedFocus : Node {
  struct parts_t {
    ClickProbe edge = make<ClickProbe>({.width = 10.0f, .height = 100.0f});
    FocusOnPress plain = make<FocusOnPress>({.x = 50.0f, .width = 10.0f, .height = 100.0f});
  } parts;
};

// A press focuses -- a node that takes the focus on a press -- without
// showing it; the keyboard shows it.
TEST(Focus, APressFocusesWithoutShowingIt) {
  Scene<ShapedFocus> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 100.0f));
  const std::array layers{InputRouter::Layer{scene.handle(), false}};
  InputRouter router;
  router.setLayers(layers);
  router.pointer(PointerEvent{pointer::down{55.0f, 50.0f}});
  router.pointer(PointerEvent{pointer::up{55.0f, 50.0f}});
  EXPECT_TRUE(scene.root().parts.plain.focused());
  EXPECT_FALSE(scene.root().parts.plain.showsFocus());
  router.key(KeyEvent{key::down{keys::kTab, Modifiers{}, false}});
  EXPECT_TRUE(focusVisible());
}

// A box sized by what it holds is as wide as that, whatever is anchored in
// it: a badge at its corner is placed in the box, and does not widen it.
struct Badged : skiff::nodes::Stack {
  struct parts_t {
    Box<> content = make<Box>({.width = 30.0f, .height = 10.0f}, kCard);
    Box<> badge = make<Box>({.place = anchor::kBottomRight, .width = 8.0f, .height = 8.0f}, kCard);
  } parts;
};
struct HoldsBadged : Node {
  struct parts_t {
    Badged badged = make<Badged>({.autoSize = axes::kBoth, .maxWidth = 200.0f});
  } parts;
};

TEST(Layout, AnAnchoredChildDoesNotWidenAContentSizedBox) {
  Scene<HoldsBadged> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  const auto &badged = scene.root().parts.badged;
  EXPECT_FLOAT_EQ(badged.bounds().width(), 30.0f);
  EXPECT_FLOAT_EQ(badged.bounds().height(), 10.0f);
  EXPECT_LE(badged.parts.badge.bounds().fRight, badged.bounds().fRight + 0.5f);
}

// Nor does a child that fills it: its width is the box's to give. The box
// is as wide as its other children, and the one that fills it fills that.
struct Filled : skiff::nodes::Stack {
  struct parts_t {
    Box<> sized = make<Box>({.width = 40.0f, .height = 10.0f}, kCard);
    Box<> filling = make<Box>({.fillX = true, .height = 6.0f}, kCard);
  } parts;
};
struct HoldsFilled : Node {
  struct parts_t {
    Filled filled = make<Filled>({.autoSize = axes::kBoth, .maxWidth = 200.0f});
  } parts;
};

TEST(Layout, AFillingChildDoesNotWidenAContentSizedBox) {
  Scene<HoldsFilled> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  const auto &filled = scene.root().parts.filled;
  EXPECT_FLOAT_EQ(filled.bounds().width(), 40.0f);
  EXPECT_FLOAT_EQ(filled.parts.filling.bounds().width(), 40.0f);
}

// A row of a set width holding more than it has room for: the last child
// sticks out, and the layout says so -- once -- in overflows(). A row
// sized by its content holds the same children without a word.
struct TooNarrow : skiff::nodes::Stack {
  struct parts_t {
    Box<> first = make<Box>({.width = 80.0f, .height = 10.0f}, kCard);
    Box<> second = make<Box>({.width = 80.0f, .height = 10.0f}, kCard);
  } parts;
  TooNarrow() { this->setHorizontal(); }
};
struct HoldsTooNarrow : Node {
  struct parts_t {
    TooNarrow fixed = make<TooNarrow>({.width = 100.0f, .height = 10.0f});
    TooNarrow sized = make<TooNarrow>({.autoSize = axes::kBoth});
  } parts;
};

TEST(Layout, WhatSticksOutOfAFlowIsTold) {
  skiff::scene::overflows().clear();
  Scene<HoldsTooNarrow> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  scene.root().parts.fixed.invalidateLayout();
  scene.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  const auto told = std::exchange(skiff::scene::overflows(), {});
  ASSERT_EQ(told.size(), 1u);  // the fixed row's second box, once; the sized row, never
  EXPECT_NEAR(told.front().x, 59.0f, 0.6f);  // 60 out, less the pixel of slack for rounding
  EXPECT_FLOAT_EQ(told.front().y, 0.0f);
}

// A row with too little room: the child that gives way does -- to what the
// row leaves it -- and the one that does not keeps its width; nothing sticks
// out, and nothing is told.
struct GivesWay : skiff::nodes::Stack {
  struct parts_t {
    Box<> soft = make<Box>({.width = 80.0f, .height = 10.0f, .shrink = axes::kX}, kCard);
    Box<> rigid = make<Box>({.width = 40.0f, .height = 10.0f}, kCard);
  } parts;
  GivesWay() { this->setHorizontal(); }
};
struct HoldsGivesWay : Node {
  struct parts_t {
    GivesWay row = make<GivesWay>({.width = 100.0f, .height = 10.0f});
  } parts;
};

TEST(Layout, ARowTooNarrowShrinksWhatGivesWay) {
  skiff::scene::overflows().clear();
  Scene<HoldsGivesWay> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));
  const std::size_t told = std::exchange(skiff::scene::overflows(), {}).size();
  const auto &row = scene.root().parts.row;
  EXPECT_NEAR(row.parts.soft.bounds().width(), 60.0f, 0.6f);
  EXPECT_FLOAT_EQ(row.parts.rigid.bounds().width(), 40.0f);
  EXPECT_LE(row.parts.rigid.bounds().fRight, row.bounds().fRight + 0.5f);
  EXPECT_EQ(told, 0u);
}

// A menu: the arrows move the focus through its items, round from the last
// to the first, and never out of it.
struct ArrowMenu : skiff::nodes::Stack {
  struct parts_t {
    ClickProbe first = make<ClickProbe>({.width = 10.0f, .height = 10.0f});
    ClickProbe second = make<ClickProbe>({.width = 10.0f, .height = 10.0f});
  } parts;
  using Node::onKey;
  void onKey(phase::bubble, const key::down &press, Reply &reply) {
    if (press.key == keys::kUp || press.key == keys::kDown) {
      reply.moveFocus(press.key == keys::kUp);
    }
  }
};
struct WithMenu : Node {
  struct parts_t {
    ClickProbe outside = make<ClickProbe>({.width = 10.0f, .height = 10.0f});
    ArrowMenu menu;
  } parts;
};

TEST(Focus, ArrowsGoRoundAMenu) {
  Scene<WithMenu> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 100.0f));
  const std::array layers{InputRouter::Layer{scene.handle(), false}};
  InputRouter router;
  router.setLayers(layers);
  auto &menu = scene.root().parts.menu.parts;
  scene.focus(menu.first);
  router.key(KeyEvent{key::down{keys::kDown, Modifiers{}, false}});
  EXPECT_TRUE(menu.second.focused());
  router.key(KeyEvent{key::down{keys::kDown, Modifiers{}, false}});
  EXPECT_TRUE(menu.first.focused()) << "round to the first, not out to the node outside";
  router.key(KeyEvent{key::down{keys::kUp, Modifiers{}, false}});
  EXPECT_TRUE(menu.second.focused());
  EXPECT_FALSE(scene.root().parts.outside.focused());
}

// A Stack lays its own children out as a column: declared, not placed.
struct Declared : skiff::nodes::Stack {
  struct parts_t {
    Box<> first = make<Box>({.fillX = true, .height = 10.0f}, kCard);
    Box<> second = make<Box>({.fillX = true, .height = 20.0f}, kCard);
  } parts;
  Declared() { this->setGap(5.0f); }
};

TEST(Stack, ItsChildrenGoOneUnderAnother) {
  Scene<Declared> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 100.0f));
  EXPECT_FLOAT_EQ(scene.root().parts.first.bounds().fTop, 0.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.second.bounds().fTop, 15.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.second.bounds().width(), 100.0f);
}

// A row lines its children up across the whole of its box, as a column
// does, not across its tallest child: in the middle of a 60 px row, a 20 px
// box is 20 px down, whatever else is beside it.
struct Centred : skiff::nodes::Stack {
  struct parts_t {
    Box<> tall = make<Box>({.width = 30.0f, .height = 40.0f}, kCard);
    Box<> middle = make<Box>(
        {.width = 20.0f, .height = 20.0f, .alignSelf = align::kMiddle}, kCard);
  } parts;
  Centred() { this->setHorizontal(); }
};

TEST(Stack, ARowAlignsAcrossItsWholeHeight) {
  Scene<Centred> scene{std::in_place};
  scene.state().apply({.fill = true});
  scene.layoutIfNeeded(skia::SkRect::MakeWH(200.0f, 60.0f));
  EXPECT_FLOAT_EQ(scene.root().parts.tall.bounds().fTop, 0.0f);
  EXPECT_FLOAT_EQ(scene.root().parts.middle.bounds().fTop, 20.0f);
}

} // namespace
