// The helpers the compose tests share -- the models, the rows and the windows
// of the tests, as mux makes them -- in one include: one unit of all the tests
// was more than a small box compiles, so they are sixteen, by what they test.
// Included after the imports; what it declares is in the unnamed namespace.
#pragma once

#include "made.h"

namespace {


using namespace skiff;
using namespace skiff::compose;


struct ReadReceipts {
  bool on = true;
};
struct DisplayName {
  std::string text;
};
struct Count {
  int n = 0;
};
struct AccountT {
  std::string who;
  DisplayName name;
  ReadReceipts receipts;
};
struct SettingsT {
  model::Keyed<std::string, AccountT> accounts;
  Count count;
};
struct Root {
  model::Tracked<SettingsT> settings;
};

struct WriteSettings {
  bool replaces(const WriteSettings &) const { return true; }
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

using Effect = std::variant<WriteSettings>;
struct Reactions {
  auto on(model::Changed<SettingsT>, const SettingsT &) const {
    return WriteSettings{};
  }
};
using Model = model::Model<Root, Logged<Reactions, Effect>>;

Model twoAccounts() {
  Root root;
  auto &list = root.settings.fValue.accounts;
  list.put("@a:x.org", AccountT{"@a:x.org", {"A"}, {true}});
  list.put("@b:x.org", AccountT{"@b:x.org", {"B"}, {false}});
  return Model(std::move(root));
}

// Leaves, as a program's widgets would be.
struct Switch : scene::Node {
  bool fOn = false;
  int fReads = 0;
  void read(const ReadReceipts &now) {
    fOn = now.on;
    ++fReads;
  }
  auto onPress() { return bind::own(model::flip); }
};
struct Chevron : scene::Node {
  bool fOpen = false;
  template <class T> void read(const T &now) { fOpen = now.on; }
  auto onPress() { return bind::own(model::flip); }
};
struct Label : scene::Node {
  std::string fText;
  void read(const DisplayName &now) { fText = now.text; }
};
struct Number : scene::Node {
  int fValue = 0;
  template <class T> void read(const T &now) { fValue = now.n; }
};
struct Pad : scene::Node {};

struct Removed {};
inline auto accountEvents() {
  return handlers(handle<Removed>([](const auto &here) {
    return model::take<AccountT>(model::key<std::string>(here));
  }));
}
using AccountEvents = decltype(accountEvents());

// Local: which rows are open, each its own; nothing of it in the model.
struct Expanded {
  bool on = false;
};

auto accountRow() {
  return scoped<AccountT>(
      accountEvents(),
      local<Expanded>(
          column(row(bound<DisplayName>(Label{}),
                     bound<Expanded>(Chevron{})),
                 bound<ReadReceipts>(Switch{}),
                 onClick(Removed{}, Pad{}))));
}
auto accountsPage() {
  return column(vbox(6.0f), each<std::string, AccountT>([] { return accountRow(); }));
}
using Page = decltype(accountsPage());

// The parts of a row, by where they are in the expression: the scope, the
// local and the column are one node, the column's.
// The parts of a row, found as they are -- bound to what they read -- each
// time they are asked for: the n-th of each kind is the n-th row's.
using NameNode = bind::Bound<DisplayName, part_t<Label>>;
using ChevronNode = bind::Bound<Expanded, part_t<Chevron>>;
using SwitchNode = bind::Bound<ReadReceipts, part_t<Switch>>;
struct RowParts {
  NameNode *name;
  ChevronNode *chevron;
  SwitchNode *toggle;
};
NameNode &nameOf(const RowParts &row) { return *row.name; }
ChevronNode &chevronOf(const RowParts &row) { return *row.chevron; }
SwitchNode &switchOf(const RowParts &row) { return *row.toggle; }
struct NoReactions {};

// A message's menu as mux makes it: a card sized by what it holds, at
// least 220 across, its first part (the quick reactions, which alone have a
// width of their own) hidden for a message that cannot be reacted to --
// still as wide as its least, its rows across it.
namespace menu_card_width {
namespace c = skiff::compose;
struct Head : scene::Node {
  Head() { this->apply({.width = 244.0f, .height = 36.0f}); }
};
struct Item : scene::Node {
  Item() { this->apply({.fillX = true, .height = 33.0f}); }
};
inline auto card(bool head) {
  return c::onClick(scene::Taken{},
      c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth, .minWidth = 220.0f, .padding = {6.0f, 0.0f, 6.0f, 0.0f}}),
          c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), c::visible(head, c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), Head()))),
          c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Item(), Item(), Item())));
}
}  // namespace menu_card_width

// A page held whole by its own state (local) -- a form's draft over all of
// its fields -- whose header is pinned, scrolled: the header held at the
// top, shifted back as far as the page scrolled, the rest moving under it.
// The local, a node of one part, was taken for the header: the whole page
// pinned, standing still while its bar moved.
namespace pinned_local_page {
namespace c = skiff::compose;
struct PinnedHeader : scene::Node {
  PinnedHeader() { this->apply({.fillX = true, .height = 40.0f}); }
};
struct PageBody : scene::Node {
  PageBody() { this->apply({.fillX = true, .height = 1000.0f}); }
};
struct Draft {
  int at = 0;
  friend bool operator==(const Draft &, const Draft &) = default;
};
inline auto page() { return c::local<Draft>(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), PinnedHeader(), PageBody())); }
using page_t = std::variant<decltype(page())>;
inline auto view() {
  auto show = [](page_t &, const int &) {};
  auto same = [](const int &a, const int &b) { return a == b; };
  auto side = [](const int &) { return 1.0f; };
  auto pin = [](const int &) { return std::optional(scene::Spec{.depth = 1.0f}); };
  return c::page_view<int, page_t>(show, same, side, pin, page_t(std::in_place_index<0>, page()), {.fill = true});
}
}  // namespace pinned_local_page

// A column sliding in (appearing_across) keeps its fill: a row in it is as
// wide as the place it slides into, and a text growing in the row gets what
// the row leaves. Outside a release build the slide's starting spec reached
// the column before the wrapper was adopted, and it was laid out 0 wide.
namespace {
struct MeasuredWidth : skiff::scene::Node {
  float *fWidth = nullptr;
  explicit MeasuredWidth(float *width, skiff::scene::Spec spec = {}) : fWidth(width) { this->apply(spec); }
  void layoutChildren() { *fWidth = this->fState.bounds().width(); }
};
} // namespace

// A plain node pressed in a bound one -- a theme card, setting the theme
// its card binds -- changes the bound part: in a build that erases the
// bound node's content (the pressed node is then not the bound one) as in
// one that does not.
namespace theme_tests {
struct Look {
  int theme = 0;
};
struct Root {
  skiff::model::Tracked<Look> look;
};
using field = skiff::model::Field<&Look::theme>;
inline auto card(int theme) {
  namespace c = skiff::compose;
  return SKIFF_NAMED(theme_card, c::bound<field>(c::onClick(skiff::bind::own(skiff::model::setTo(theme)),
                                                           c::column(c::vbox(0.0f, {.width = 10.0f, .height = 10.0f})))));
}
}  // namespace theme_tests

// mux's appearance page: a style's text fields, in a padded column of a
// scope in a scope, each a named column of a label and a bound field.
namespace style_tests {
namespace c = skiff::compose;
struct Style {
  std::string font;
  std::string code;
};
struct Look {
  Style style;
  int theme = 0;
};
struct Root {
  skiff::model::Tracked<Look> look;
};
struct Field : scene::Node {
  float *fWidth = nullptr;
  explicit Field(float *width) : fWidth(width) {
    fState.fRelativeSizeAxes = scene::axes::kX;
    fState.fWidth = 1.0f;
    fState.fHeight = 30.0f;
  }
  void layoutChildren() { *fWidth = this->fState.bounds().width(); }
  void read(const std::string &) {}
};
template <auto Member> inline auto text_field(float *width) {
  return SKIFF_NAMED(style_text_field, c::column(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      nodes::Text("Label", 13.0f, 0xff000000u),
      c::bound<skiff::model::Field<Member>>(c::styled({.fillX = true, .height = 36.0f},
          Field(width)))));
}
inline auto controls(float *width) {
  return SKIFF_NAMED(style_controls, c::scoped<Style>(c::handlers(), c::column(
      c::vbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {8.0f, 20.0f, 8.0f, 20.0f}}),
      nodes::Text("FONTS AND THEME", 13.0f, 0xff000000u, true),
      text_field<&Style::font>(width), text_field<&Style::code>(width))));
}
}  // namespace style_tests

// A view named over a view of its own (a page whose content is a named view
// that closes it on Escape): the same Held, named again -- laid out as it,
// filling what it is put in, the outer name the one told.
namespace named_twice {
namespace c = skiff::compose;
inline auto inner(float *wide) {
  return SKIFF_NAMED(closes_on_escape, c::column(c::vbox(0.0f, {.fill = true}), c::styled({.fillX = true}, MeasuredWidth(wide, {.height = 10.0f}))));
}
inline auto outer(float *wide) { return SKIFF_NAMED(accounts_view, inner(wide)); }
}  // namespace named_twice

// A clamped width in a column as wide as it, beside a growing part (mux's
// chats screen: the side, then the chat): as wide as the column it made --
// laid out a second time in that column's own width, it clamped itself to a
// share of it and came out narrower.
namespace side_tests {
namespace c = skiff::compose;
struct SideUp {
  bool up = true;
};
struct ShownRoot {
  skiff::model::Tracked<SideUp> shown;
};
inline auto side(float *wide) {
  return SKIFF_NAMED(side_column, c::projected<SideUp>([](const SideUp &) { return c::WidthLimit{.wanted = 253.0f}; },
      c::clamped_width({}, c::column(c::vbox(0.0f, {.fill = true}), c::styled({.fillX = true}, MeasuredWidth(wide, {.height = 20.0f}))))));
}
}  // namespace side_tests

// A node that asks, as it ticks, on its own behalf (a window list at its
// top, asking for what is above it): its press found by its id, in a named
// view, and delivered to the program by its type.
namespace edge_press {
namespace c = skiff::compose;
struct AtTop {
  bool older = true;
};
struct Seen {};
struct Asker : scene::Node {
  using Answer = std::variant<AtTop, Seen>;
  int fTicks = 0;
  Asker() { this->apply({.fillX = true, .height = 40.0f}); }
  void update(double) {
    if (++fTicks == 2)
      scene::pressLater(this->fState);
  }
  [[nodiscard]] bool wantsTick() const { return fTicks < 3; }
  [[nodiscard]] bool settling() const { return fTicks < 3; }
  std::optional<Answer> onPress() { return Answer(AtTop{}); }
};
struct Asked {
  int asked = 0;
  void take(const AtTop &) { ++asked; }
  void take(const Seen &) {}
};
inline auto view() {
  return SKIFF_NAMED(timeline_list, c::column(c::vbox(0.0f, {.fill = true}), Asker()));
}
}  // namespace edge_press

// A window over a list (mux's timeline), scrolled to its top with nothing
// above it held: it asks for what is older, and the program hears it.
// A face that measures, for tests of words' widths: DejaVu Sans, where the
// machine has it -- skia-lite's default face, and a SkFont made without one,
// measure nothing (as skiff main's tests have it).
namespace test_font {
struct TestFont {
  skia::SkFont font;
  bool ok = false;
  TestFont() {
    auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
    auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
    if (!face)
      return;
    skiff::paint::fonts().setPrimary(face);
    font = skia::SkFont(face);
    skiff::paint::defaultFont() = &font;
    ok = true;
  }
  ~TestFont() { skiff::paint::defaultFont() = nullptr; }
};
}  // namespace test_font
namespace window_edge {
namespace c = skiff::compose;
struct FeedTold {
  std::optional<int> of;
  c::WindowAim<std::string> aim;
  friend bool operator==(const FeedTold &, const FeedTold &) = default;
};
struct Feed {
  int count = 30;
  int first = 0;  // the oldest's number: lower, history come in above
};
struct FeedRoot {
  model::Tracked<FeedTold> told;
  model::Tracked<Feed> feed;
};
using FeedModel = model::Model<FeedRoot, bind::NoReactions>;
using Item = std::tuple<std::string, int>;
struct Src {
  using Told = FeedTold;
  static std::size_t count(const FeedModel &m, const Told &) { return static_cast<std::size_t>(m.root().feed.fValue.count); }
  static std::string key(const FeedModel &m, const Told &, std::size_t i) { return std::format("m{}", m.root().feed.fValue.first + static_cast<int>(i)); }
  static std::optional<std::size_t> index(const FeedModel &m, const Told &t, const std::string &id) {
    for (std::size_t i = 0; i < count(m, t); ++i)
      if (key(m, t, i) == id)
        return i;
    return std::nullopt;
  }
  static bool counts(const FeedModel &, const Told &, std::size_t) { return true; }
  static model::Revision revision(const FeedModel &m, const Told &) { return m.revision(); }
  static std::vector<Item> items(const FeedModel &m, const Told &t, std::size_t from, std::size_t to, const std::optional<std::string> &) {
    return std::views::iota(from, to) | std::views::transform([&](std::size_t i) { return Item{key(m, t, i), static_cast<int>(i)}; }) |
           std::ranges::to<std::vector>();
  }
};
struct RowNode : scene::Node {
  RowNode() { this->apply({.fillX = true, .height = 40.0f}); }
};
struct MakeFeedRow {
  RowNode operator()(const Item &) const { return RowNode{}; }
};
struct Tag {};
using FeedWindow = c::WindowFrom<Src, FeedModel, std::string, int, RowNode, MakeFeedRow, c::NoLocate, Tag>;
struct Heard {
  int older = 0;
  int aimed = 0;
  void take(const c::ListEdge<Tag> &edge) { older += edge.older ? 1 : 0; }
  void take(const c::ListAimed<std::string, Tag> &) { ++aimed; }
  void take(const c::ListSeen<std::string, Tag> &) {}
};
}  // namespace window_edge

// A part read in a scope that does not hold it -- each level's choices,
// read where one level's are edited (mux's room events): found from the
// root, where it is there once, and read again as it changes.
namespace read_from_root {
namespace c = skiff::compose;
using namespace window_edge;
inline std::string &said_now() {
  static std::string kept;
  return kept;
}
struct Said : scene::Node {
  void read(const std::string &now) { said_now() = now; }
};
inline auto scoped_page() {
  return c::scoped<Feed>(c::handlers(), c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::projected<FeedTold>([](const FeedTold &now) { return now.of && *now.of > 5 ? std::string("Collapse") : std::string("Expand"); }, Said())));
}
}  // namespace read_from_root

// A picture fitted over a named view (mux's: Fitted over its picture_box):
// the wrapper's size its own, the view filling it -- with a Held& helper
// beside a catch-all, the named view took the catch-all, and the picture
// was laid out 0 by 0, the caption alone shown.
namespace fitted_named {
namespace c = skiff::compose;
struct NoRatio {
  std::optional<float> operator()(auto &) const { return std::nullopt; }
};
inline auto box(float *wide) {
  return SKIFF_NAMED(picture_box, c::column(c::vbox(0.0f, {}), c::styled({.fill = true}, MeasuredWidth(wide))));
}
}  // namespace fitted_named

// Rows coming in moving (a message that has just come): made after the
// window is open, they fade in and are seen, whole, once they are still.
namespace window_arrive {
namespace c = skiff::compose;
using namespace window_edge;
struct MakeArriving {
  compose::Held operator()(const Item &) const { return compose::Held(c::appearing(true, RowNode{})); }
};
using ArrivingWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeArriving, c::NoLocate, Tag>;
struct Plain {
  static constexpr auto styles = scene::makeStyleSheet().rule(scene::selectAny(), {.alpha = 1.0f});
};
}  // namespace window_arrive

// A row swiped to answer it: it follows the finger as it is dragged -- its
// tick moves it, so a drag has to bring the walk to it -- and goes back
// when let go.
namespace swipe_follow {
namespace c = skiff::compose;
// What is pressed in the row (a message's text): a swipe is taken on its
// way down to it.
struct ShiftProbe : scene::Node {
  ShiftProbe() { this->apply({.fillX = true, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct Answered {};
}  // namespace swipe_follow

// mux's picture in a message, as it is put together: its preview and its
// picture -- marked, under what turns while it comes -- fill the fitted box,
// both ways: laid out 0 high, neither is drawn, so neither is asked for, and
// the box's plate shows alone, grey.
namespace picture_like {
namespace c = skiff::compose;
struct ShownImage : scene::Node {
  [[nodiscard]] const skia::Sp<skia::SkImage> *image() const { return nullptr; }
};
struct Turning : scene::Node {
  Turning() { this->apply({.width = 20.0f, .height = 20.0f}); }
};
struct PictureTag {};
struct NoRatio {
  std::optional<float> operator()(auto &) const { return std::nullopt; }
};
inline auto picture_box() {
  auto preview = c::visible(true, c::styled({.fill = true, .cornerRadius = 10.0f}, ShownImage()));
  auto picture = c::Marked<PictureTag, ShownImage>(c::visible(true, c::styled({.fill = true, .cornerRadius = 10.0f}, ShownImage())));
  auto loader = c::visible(false, c::styled({.place = scene::anchor::kCentre}, Turning()));
  return SKIFF_NAMED(picture_box, c::column(c::vbox(0.0f, {.cornerRadius = 10.0f, .background = 0xff808080u, .masking = true}),
                                            std::move(preview), c::while_coming(c::vbox(0.0f, {.fill = true}), std::move(picture), std::move(loader))));
}
using fitted_t = c::Fitted<NoRatio, decltype(picture_box())>;
struct Open {};
}  // namespace picture_like

// A row the chat's settings hide, as mux makes one: hidden where it is made,
// then wrapped -- coming in, held off -- and still out of the list's flow,
// no gap where it is.
namespace hidden_row {
namespace c = skiff::compose;
struct Line : scene::Node {
  Line() { this->apply({.fillX = true, .height = 40.0f}); }
};
struct Asked {
  std::optional<int> operator()(auto &, float, float) const { return 1; }
};
inline auto row(bool shown) {
  auto inner = c::onRightPressAt(Asked{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), Line()));
  inner.setVisible(shown);
  return compose::Held(c::appearing(false, std::move(inner)));
}
}  // namespace hidden_row

// A message's row, as mux makes it: a right press anywhere in it -- on its
// text, which can be selected -- asks the row's menu; a click alone does
// not answer the swipe.
namespace row_menu {
namespace c = skiff::compose;
struct Menu {
  int at = 0;
};
struct Ask {
  std::optional<Menu> operator()(auto &, float, float) const { return Menu{1}; }
};
struct Reply {};
struct Show {
  void operator()(nodes::Text &, float) const {}
};
inline auto row() {
  nodes::Text words("hello there", 13.0f, 0xff000000u);
  words.setSelectable(true);
  // Its size said: this test's fonts measure nothing.
  words.apply({.width = 200.0f, .height = 20.0f});
  auto inner = c::row(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY}), c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), std::move(words)));
  auto mark = c::styled({.place = scene::anchor::kCentreRight, .width = 28.0f, .height = 28.0f}, nodes::Text("", 12.0f, 0u));
  return c::onRightPressAt(Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                            c::swiped(48.0f, 120.0f, [] { return Reply{}; }, Show{}, std::move(mark), std::move(inner))));
}
}  // namespace row_menu

namespace row_menu_parts {
namespace c = skiff::compose;
struct Probe : scene::Node {
  Probe() { this->apply({.width = 100.0f, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
template <class N> std::size_t rightPressed(N node) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(node)));
  tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 400.0f, 400.0f));
  (void)tree.finishFrame();
  scene::hostWork().answers.clear();
  (void)tree.dispatchPointer(scene::pointer::down{20.0f, 8.0f, 3});
  (void)tree.dispatchPointer(scene::pointer::up{20.0f, 8.0f, 3});
  return std::exchange(scene::hostWork().answers, {}).size();
}
}  // namespace row_menu_parts

// A button hidden as it is made and shown by what it reads -- mux's call
// button, shown_if(callable, visible(false, button)): shown once the model
// says so, and pressed.
namespace shown_later {
namespace c = skiff::compose;
using namespace window_edge;
struct Pressed {};
struct Probe : scene::Node {
  Probe() { this->apply({.width = 40.0f, .height = 40.0f}); }
};
inline auto button() {
  return c::shown_if<FeedTold>([](const FeedTold &now) { return now.of == 2; },
                               c::visible(false, c::styled({.alignSelf = scene::align::kStart}, c::onClick(Pressed{}, Probe()))));
}
}  // namespace shown_later

// mux's chats screen and its keys: named, its keys asked on their way down
// (keys_for, capture) and on their way up (onKeys) -- a key the field with
// the focus does not take answered once, not twice (Ctrl+Up stepped two
// messages).
namespace screen_keys_once {
namespace c = skiff::compose;
using namespace window_edge;
struct Field : scene::Node {
  Field() { this->apply({.width = 200.0f, .height = 30.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
  [[nodiscard]] bool focusable() const { return true; }
  [[nodiscard]] bool takesFocusOnPress() const { return true; }
};
struct Up {};
struct Caught {};
struct UpKeys {
  std::optional<Up> operator()(const scene::key::down &press) const {
    return press.key == scene::keys::kUp ? std::optional<Up>(Up{}) : std::nullopt;
  }
};
struct CaptureKeys {
  std::optional<Caught> operator()(const int &, const scene::key::down &) const { return std::nullopt; }
};
inline auto screen() {
  return SKIFF_NAMED(keys_screen, c::keys_for<FeedTold, scene::phase::capture>([](const FeedTold &) { return 0; }, CaptureKeys{},
      c::onKeys(UpKeys{}, c::column(c::vbox(0.0f, {.fill = true}), Field()))));
}
}  // namespace screen_keys_once

// The row's menu, asked by a right press, delivered: taken by the program
// (the sink given), not only kept.
namespace row_menu_delivered {
struct Heard {
  int menus = 0;
  void take(const row_menu::Menu &) { ++menus; }
  void take(const row_menu::Reply &) {}
};
}  // namespace row_menu_delivered

// A key's answer delivered as mux delivers what was kept: to each model in
// turn until one says it took it -- taken once, and said so by the first.
namespace screen_keys_delivered {
struct Heard {
  int ups = 0;
  void take(const screen_keys_once::Up &) { ++ups; }
  void take(const screen_keys_once::Caught &) {}
};
}  // namespace screen_keys_delivered

// A message's picture whose thumbnail comes after it was first drawn: woken
// where it waits, it finds it -- laid out again, repainted.
namespace waiting_picture {
namespace c = skiff::compose;
using namespace window_edge;
struct Store {
  static inline skia::Sp<skia::SkImage> image;
  static inline scene::Waiters waiting;
};
struct FromStore {
  std::string key;
  const skia::Sp<skia::SkImage> *operator()() const { return &Store::image; }
  scene::Waiters &waiters() const { return Store::waiting; }
};
using Picture = nodes::Image<FromStore>;
struct PictureTag {};
struct MakePictureRow {
  compose::Held operator()(const Item &) const {
    return compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
                                   c::Marked<PictureTag, Picture>(c::visible(true, c::styled({.width = 100.0f, .height = 80.0f}, Picture({"k"}))))));
  }
};
using PictureWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakePictureRow, c::NoLocate, Tag>;
}  // namespace waiting_picture

// The same, the picture as mux's picture_box holds it: fitted, pressed to
// open, its preview under it, the picture marked under what turns while it
// comes.
namespace waiting_picture_box {
namespace c = skiff::compose;
using namespace window_edge;
using waiting_picture::Picture;
using waiting_picture::PictureTag;
struct Polled {
  std::string key;
  const skia::Sp<skia::SkImage> *operator()() const { return nullptr; }
};
struct Turning : scene::Node {
  Turning() { this->apply({.width = 20.0f, .height = 20.0f}); }
  [[nodiscard]] const skia::Sp<skia::SkImage> *image() const { return nullptr; }
};
struct NoRatio {
  std::optional<float> operator()(auto &) const { return std::nullopt; }
};
inline auto box() {
  // Over the box, out of its flow, as mux now places it: in the flow, the
  // picture went under it, past the box's bottom, cut off.
  auto preview = c::visible(true, c::styled({.place = scene::anchor::kTopLeft, .fill = true, .cornerRadius = 10.0f}, nodes::Image<Polled>({"k"})));
  auto picture = c::Marked<PictureTag, Picture>(c::visible(true, c::styled({.fill = true, .cornerRadius = 10.0f}, Picture({"k"}))));
  auto loader = c::visible(false, c::styled({.place = scene::anchor::kCentre}, Turning()));
  return SKIFF_NAMED(picture_box, c::column(c::vbox(0.0f, {.cornerRadius = 10.0f, .background = 0xff808080u, .masking = true}),
                                            std::move(preview), c::while_coming(c::vbox(0.0f, {.fill = true}), std::move(picture), std::move(loader))));
}
using fitted_t = c::Fitted<NoRatio, decltype(box())>;
struct Open {};
struct MakeBoxRow {
  compose::Held operator()(const Item &) const {
    return compose::Held(c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
        c::onClick(Open{}, fitted_t(c::Fit{.width = 100.0f, .height = 80.0f, .most = 430.0f, .least = 100.0f}, NoRatio{}, box()))));
  }
};
using BoxWindow = c::WindowFrom<Src, FeedModel, std::string, int, compose::Held, MakeBoxRow, c::NoLocate, Tag>;
}  // namespace waiting_picture_box

// A card popped at the pointer -- a message's menu: at the pointer once laid
// out, with only the frames it asks for -- not left where it was first laid
// out, in the window's top left corner.
namespace popped_menu {
namespace c = skiff::compose;
using namespace window_edge;
struct Card : scene::Node {
  Card() { this->apply({.width = 120.0f, .height = 90.0f}); }
};
inline auto menu() {
  return c::column(c::vbox(0.0f, {.fill = true}),
                   c::popped_at<FeedTold>([](const FeedTold &now) -> std::optional<c::PopAt> {
                     if (now.of != 2)
                       return std::nullopt;
                     return c::PopAt{.from = skia::SkRect::MakeXYWH(150.0f, 120.0f, 0.0f, 0.0f)};
                   }, 8.0f, c::styled({}, Card())));
}
}  // namespace popped_menu

// A row flashed, then another: the first does not flash again as the flash
// leaves it (Ctrl+Up lit two messages at each step).
namespace wash_once {
namespace c = skiff::compose;
using namespace window_edge;
// Which row was painted with what wash, as it was: the rows' own record.
inline std::vector<std::pair<int, skia::SkColor>> &painted() {
  static std::vector<std::pair<int, skia::SkColor>> kept;
  return kept;
}
struct Probe : scene::Node {
  int fWhich = 0;
  explicit Probe(int which) : fWhich(which) { scene::Node::apply({.width = 100.0f, .height = 40.0f}); }
  void apply(const scene::Spec &spec) {
    if (spec.background)
      painted().emplace_back(fWhich, *spec.background);
    scene::Node::apply(spec);
  }
};
inline auto row(int which) {
  return c::washed_for<FeedTold>(0xff3080ffu, 56.0f, 80.0f,
                                 [which](const FeedTold &now) { return c::Wash{.flash = now.aim.key == std::to_string(which) ? now.aim.number : 0}; },
                                 Probe(which));
}
}  // namespace wash_once

// A message's menu as mux makes it: a card popped at the press, read from
// the menu's own local state -- seen, at its place, with only the frames it
// asks for.
namespace local_menu {
namespace c = skiff::compose;
struct MenuState {
  int unrolled = 0;
  friend bool operator==(const MenuState &, const MenuState &) = default;
};
struct Card : scene::Node {
  Card() { this->apply({.width = 120.0f, .height = 90.0f}); }
};
inline auto menu(float x, float y) {
  const c::PopAt at{.from = skia::SkRect::MakeXYWH(x, y, 0.0f, 0.0f)};
  return c::local<MenuState>(c::column(c::vbox(0.0f, {.fill = true}),
      c::popped_at<MenuState>([at](const MenuState &) { return std::optional(at); }, 8.0f, c::styled({}, Card()))));
}
}  // namespace local_menu

// A right press anywhere over a message's row, as mux makes one -- its
// avatar out of the flow, its bubble held, a name, the words (selectable),
// the time over the bubble's corner -- asks the row's menu: no point of it
// left without.
namespace row_grid {
namespace c = skiff::compose;
struct Face : scene::Node {
  Face() { this->apply({.width = 40.0f, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct OpenFace {
  int operator()() const { return 1; }
};
struct NoLine {
  std::optional<int> operator()(auto &, float, float) const { return std::nullopt; }
};
inline auto row() {
  nodes::Text name("Someone", 13.0f, 0xff000000u, true);
  name.apply({.width = 120.0f, .height = 16.0f});
  nodes::Text words("hello there, a regular message", 13.0f, 0xff000000u);
  words.setSelectable(true);
  words.apply({.width = 220.0f, .height = 36.0f});
  nodes::Text time("12:44", 11.0f, 0xff000000u);
  time.apply({.place = scene::anchor::kBottomRight, .y = 2.0f, .width = 40.0f, .height = 14.0f});
  auto bubble = compose::Held(c::column(c::vbox(2.0f, {.autoSize = scene::axes::kBoth, .padding = {6.0f, 10.0f, 6.0f, 10.0f},
                                                        .cornerRadius = 12.0f, .background = 0xffeeeeeeu}),
                                        std::move(name), std::move(words), std::move(time)));
  auto face = c::styled({.place = scene::anchor::kBottomLeft, .x = -48.0f, .y = -1.0f}, c::onPress(OpenFace{}, Face(), "face"));
  auto inner = c::onClickAt(NoLine{}, c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                                                          .padding = {8.0f, 0.0f, 1.0f, 48.0f}}),
                                                          nodes::justify::start{}),
                                             std::move(face), std::move(bubble)));
  return c::onRightPressAt(row_menu::Ask{}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::swiped(48.0f, 120.0f, [] { return row_menu::Reply{}; }, row_menu::Show{}, nodes::Text("", 12.0f, 0u), std::move(inner))));
}
}  // namespace row_grid

// A menu's 'Viewed' row hovered: the list of who viewed opens beside the
// row, where the row is in the window -- not in a corner.
namespace viewed_list {
namespace c = skiff::compose;
struct MenuState {
  bool on = false;
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  friend bool operator==(const MenuState &, const MenuState &) = default;
};
struct Hovered {
  bool on = false;
  skia::SkRect box;
};
struct Says {
  Hovered operator()(bool on, const skia::SkRect &box) const { return {on, box}; }
};
struct Events {
  auto on(const Hovered &one, const MenuState &now) const {
    auto next = now;
    next.on = one.on;
    if (one.on) {
      next.left = one.box.fLeft;
      next.top = one.box.fTop;
      next.right = one.box.fRight;
      next.bottom = one.box.fBottom;
    }
    return model::over<MenuState>(model::setTo(std::move(next)));
  }
};
struct Sized : scene::Node {
  Sized(float w, float h) { this->apply({.width = w, .height = h}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
inline auto menu() {
  const c::PopAt at{.from = skia::SkRect::MakeXYWH(150.0f, 120.0f, 0.0f, 0.0f)};
  return c::local<MenuState>(Events{}, c::column(c::vbox(0.0f, {.fill = true}),
      c::popped_at<MenuState>([at](const MenuState &) { return std::optional(at); }, 8.0f,
          c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), Sized(120.0f, 30.0f), c::hovering(Says{}, Sized(120.0f, 24.0f)))),
      c::popped_at<MenuState>([](const MenuState &now) -> std::optional<c::PopAt> {
        if (!now.on)
          return std::nullopt;
        return c::PopAt{.from = skia::SkRect::MakeLTRB(now.left, now.top, now.right, now.bottom), .beside = true, .overlap = 6.0f};
      }, 8.0f, c::hovering(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}), Says{}, Sized(100.0f, 80.0f)))));
}
struct Sink {};
}  // namespace viewed_list

// The call button as mux's chat head holds it: named, shown by what it
// reads, hidden as it is made, in a row sized by its buttons, in the head's
// row -- shown, it is as big as it is, and pressed.
namespace head_button {
namespace c = skiff::compose;
using namespace window_edge;
using shown_later::Pressed;
using shown_later::Probe;
inline auto button() {
  return SKIFF_NAMED(extend, c::shown_if<FeedTold>([](const FeedTold &now) { return now.of == 2; },
                                                   c::visible(false, c::styled({.alignSelf = scene::align::kMiddle}, c::onClick(Pressed{}, Probe())))));
}
inline auto head() {
  const auto chat = [](const Feed &) { return true; };
  return c::shown_for<Feed>(chat, c::column(c::vbox(0.0f, {.fillX = true, .height = 56.0f}),
      c::onClick(Pressed{}, c::row(c::hbox(12.0f, {.fillX = true, .grow = scene::axes::kY}),
                c::column(c::vbox(3.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}), Probe()),
                c::shown_for<Feed>(chat, c::row(c::hbox(12.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}), button())),
                c::shown_for<Feed>(chat, c::styled({.alignSelf = scene::align::kMiddle}, Probe()))))));
}
}  // namespace head_button

// The call button exactly as mux's calls plugin makes it: its icon button a
// named view (SKIFF_NAMED, a press over a column), styled, hidden as made,
// shown by what it reads -- in the head's row of plugin buttons, under the
// head's own projection.
namespace named_call {
namespace c = skiff::compose;
using namespace window_edge;
struct Chosen {};
inline auto icon_like() {
  return SKIFF_NAMED(icon_button, c::onPress([] { return Chosen{}; },
      c::column(c::vbox(0.0f, {.width = 36.0f, .height = 36.0f, .cornerRadius = 18.0f}),
                c::styled({.fill = true}, nodes::Box<>(0))), "Call"));
}
inline auto call_button() {
  return SKIFF_NAMED(extend, c::shown_if<FeedTold>([](const FeedTold &now) { return now.of == 2; },
      c::visible(false, c::styled({.alignSelf = scene::align::kMiddle}, icon_like()))));
}
inline auto head() {
  const auto chat = [](const Feed &) { return true; };
  return c::shown_for<Feed>(chat, c::column(c::vbox(0.0f, {.fillX = true, .height = 56.0f}),
      c::row(c::hbox(12.0f, {.fillX = true, .grow = scene::axes::kY}),
             c::column(c::vbox(3.0f, {.autoSize = scene::axes::kY, .grow = scene::axes::kX, .alignSelf = scene::align::kMiddle}), nodes::Box<>(0)),
             c::shown_for<Feed>(chat, c::row(c::hbox(12.0f, {.autoSize = scene::axes::kBoth, .alignSelf = scene::align::kMiddle}), call_button())))));
}
}  // namespace named_call

// The same button, its part held as mux's shown model holds a plugin's
// layer: in a list of aggregates, a head and a tail, under the root.
namespace nested_button {
namespace c = skiff::compose;
using namespace window_edge;
template <class... Parts> struct List {};
template <class Head, class... Rest> struct List<Head, Rest...> {
  Head head;
  List<Rest...> tail;
};
struct Root {
  model::Tracked<Feed> feed;
  List<model::Tracked<FeedTold>> layers;
};
using Model = model::Model<Root, bind::NoReactions>;
}  // namespace nested_button

// A panel mounted as the model asks, placed by another part it reads --
// mux's emoji panel, by the window's shape: placed and sized at once, not
// left as made (the top left corner) until that other part moves.
namespace placed_panel {
namespace c = skiff::compose;
using namespace window_edge;
inline auto panel() {
  return c::column(c::vbox(0.0f, {.fill = true}),
      c::spec_for<Feed>([](const Feed &now) {
        return scene::Spec{.place = scene::anchor::kTopLeft, .x = 150.0f, .y = static_cast<float>(now.count) * 4.0f, .width = 200.0f,
                           .height = 100.0f, .background = 0xffeeeeeeu};
      },
          c::column(c::vbox(0.0f, {}), shown_later::Probe())));
}
}  // namespace placed_panel

// mux's settings button whose words are read (action_said: Expand, then
// Collapse): a spec read over a press over a row its words size, at the end
// of a tile's row -- pressed where it is drawn, its words read in after it
// was made, empty.
namespace said_button {
namespace c = skiff::compose;
using namespace window_edge;
struct Expand {};
// Words as a text has them, with no font here: as wide as they are long,
// measured where they are read -- none as made.
struct Words : scene::Node {
  Words() { this->apply({.width = 0.0f, .height = 14.0f}); }
  void read(const std::string &now) {
    this->apply({.width = 7.0f * static_cast<float>(now.size()), .height = 14.0f});
    this->invalidateLayout();
  }
};
inline auto button() {
  return SKIFF_NAMED(action_said, c::spec_for<Feed>([](const Feed &) { return scene::Spec{.alpha = 1.0f, .disabled = false}; },
      c::onPress([] { return Expand{}; },
          c::row(c::hbox(0.0f, {.height = 32.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX,
                                .alignSelf = scene::align::kMiddle, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .background = 0xffddddddu}),
                 c::projected<Feed>([](const Feed &now) { return now.count == 30 ? std::string("Expand") : std::string("Collapse"); },
                                    c::styled({.alignSelf = scene::align::kMiddle}, Words()))))));
}
inline auto tile() {
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::column(c::vbox(4.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                shown_later::Probe()),
      c::styled({.alignSelf = scene::align::kMiddle}, button()));
}
}  // namespace said_button

// The same, with a text as mux's: its words read into a Text made empty,
// measured with a font.
namespace said_text {
namespace c = skiff::compose;
using namespace window_edge;
inline auto button() {
  return SKIFF_NAMED(action_said, c::spec_for<Feed>([](const Feed &) { return scene::Spec{.alpha = 1.0f, .disabled = false}; },
      c::onPress([] { return said_button::Expand{}; },
          c::row(c::hbox(0.0f, {.height = 32.0f, .relativeSize = scene::axes::kNone, .autoSize = scene::axes::kX,
                                .alignSelf = scene::align::kMiddle, .padding = {0.0f, 12.0f, 0.0f, 12.0f}, .background = 0xffddddddu}),
                 c::text_for<Feed>([](const Feed &now) { return now.count == 30 ? std::string("Expand") : std::string("Collapse"); },
                                   c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("", 12.0f, 0xff000000u)))))));
}
inline auto tile() {
  return c::row(c::hbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::column(c::vbox(4.0f, {.relativeSize = scene::axes::kNone, .autoSize = scene::axes::kY, .grow = scene::axes::kX}),
                shown_later::Probe()),
      c::styled({.alignSelf = scene::align::kMiddle}, button()));
}
}  // namespace said_text

// mux's emoji panel, as it is made: a card placed and sized by the window's
// shape it reads (spec_for), over a flick across it, its tabs over a panel
// that grows to fill it -- its groups' bar along its bottom -- and the
// plugins' pages. The card as big as said, everything in it inside it.
namespace emoji_card {
namespace c = skiff::compose;
using namespace window_edge;
struct Flick {};
inline auto panel() {
  return c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}),
                   c::styled({.fillX = true, .grow = scene::axes::kY}, shown_later::Probe()),
                   c::row(c::hbox(4.0f, {.fillX = true, .height = 30.0f, .background = 0xff0000ffu}), shown_later::Probe()));
}
inline auto card() {
  auto card = c::spec_for<Feed>([](const Feed &) {
    return scene::Spec{.place = scene::anchor::kTopLeft, .x = 40.0f, .y = 100.0f, .width = 345.0f, .height = 400.0f, .cornerRadius = 8.0f,
                       .background = 0xffffffffu};
  },
      c::flicked(90.0f, [](bool) { return std::optional<Flick>(); },
          c::column(c::vbox(0.0f, {}),
              c::row(c::hbox(4.0f, {.fillX = true, .height = 36.0f}), shown_later::Probe()),
              c::styled({.fillX = true, .grow = scene::axes::kY, .masking = true}, panel()),
              // The plugins' pages: there only while one is the page -- shown
              // beside the emoji, it grew too and took half the card.
              c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY, .visible = false})))));
  return c::column(c::vbox(0.0f, {.fill = true}), std::move(card));
}
}  // namespace emoji_card

// mux's emoji panel at its size: its groups, ~1900 cells in all -- each a
// named view, resting over a press over its glyph -- in sections that
// scroll, a strip of tabs under them. What a frame does, idle, hovered,
// scrolled: nodes ticked and laid out, and its time.
namespace emoji_grid {
namespace c = skiff::compose;
using namespace window_edge;
struct Group {
  std::string name;
  int count = 0;
};
struct Rest {
  shown_later::Pressed operator()(bool) const { return {}; }
};
inline auto cell(int) {
  return SKIFF_NAMED(emoji_cell, c::resting(500.0, Rest{},
      c::onPress([] { return shown_later::Pressed{}; },
          c::row(c::justified(c::hbox(0.0f, {.width = 37.0f, .height = 37.0f, .cornerRadius = 6.0f, .hoverBackground = 0xffeeeeeeu}),
                              nodes::justify::middle{}),
                 c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text("x", 22.0f, 0xff000000u))), "e")));
}
using cell_t = decltype(cell(0));
inline auto section(const Group &one) {
  return c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}),
      c::styled({.fillX = true, .height = 32.0f}, nodes::Text(one.name, 13.0f, 0xff000000u)),
      c::styled({.fillX = true, .autoSize = scene::axes::kY},
          nodes::Flow<std::vector<cell_t>>({.direction = nodes::direction::horizontal{}, .spacingX = 0.0f, .spacingY = 0.0f, .wrap = true},
                                           std::views::iota(0, one.count) | std::views::transform(cell) | std::ranges::to<std::vector>())));
}
inline auto tab(const Group &one) {
  return c::styled({.width = 30.0f, .height = 30.0f, .grow = scene::axes::kX, .shrink = scene::axes::kX, .minWidth = 0.0f},
                   nodes::Text(one.name.substr(0, 1), 16.0f, 0xff000000u));
}
inline auto panel() {
  auto groups = c::sections<Group>(c::vbox(4.0f, {.fill = true}), c::vbox(0.0f), tab, section,
                                   c::in_row<Group>(c::hbox(0.0f, {.fillX = true, .height = 36.0f})));
  return c::column(c::vbox(0.0f, {.fill = true}),
      c::projected<Feed>([](const Feed &) {
        std::vector<Group> all = std::views::iota(0, 10) | std::views::transform([](int g) { return Group{std::format("Group {}", g), 190}; }) |
                                 std::ranges::to<std::vector>();
        return c::SectionsShown<Group>{all, all, true};
      }, std::move(groups)));
}
}  // namespace emoji_grid

// The same, the wrapper named where it is made (SKIFF_NAMED): a type derived
// from it, which called Node's defaults, not its own, and took no press.
namespace click_span_named {
namespace c = skiff::compose;
using namespace window_edge;
inline auto list() {
  FeedWindow window(MakeFeedRow{});
  window.apply({.fill = true});
  return SKIFF_NAMED(timeline_list, c::spanned_for<FeedTold>([](const FeedTold &now) { return now.of == 2; }, bind::Bound<FeedTold, FeedWindow>(std::move(window))));
}
}  // namespace click_span_named

// A list named where it is made (SKIFF_NAMED), its rows taking presses: a
// wheel over a row goes up to the list, and the list scrolls; a drag too.
// Named, a type derived from the list called Node's defaults, not its own.
namespace named_wheel {
namespace c = skiff::compose;
using namespace window_edge;
struct PressedRow : scene::Node {
  PressedRow() { this->apply({.fillX = true, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
struct MakePressedRow {
  PressedRow operator()(const Item &) const { return PressedRow{}; }
};
using PressedWindow = c::WindowFrom<Src, FeedModel, std::string, int, PressedRow, MakePressedRow, c::NoLocate, Tag>;
inline auto named() {
  PressedWindow window(MakePressedRow{});
  window.apply({.fill = true});
  return SKIFF_NAMED(scrolled_list, bind::Bound<FeedTold, PressedWindow>(std::move(window)));
}
template <class List, class Move> float moved_by(List list, Move move) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(list)));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 30}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  scene::hostWork().pressed.clear();
  Heard heard;
  const auto frame = [&](double at) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
    for (const auto &way : std::exchange(scene::hostWork().pressed, {}))
      (void)bind::press(tree.root(), m, way, &heard);
    binding.refresh(tree.root(), m);
  };
  for (double at = 0.0; at < 600.0; at += 16.0)
    frame(at);
  PressedRow *row = scene::findNode<PressedRow>(tree.root());
  if (row == nullptr)
    return 0.0f;
  const float before = scene::work::drawnOffset(row->fState.fId).y;
  move(tree);
  for (double t = 600.0; t < 2000.0; t += 16.0)
    frame(t);
  return scene::work::drawnOffset(row->fState.fId).y - before;
}
}  // namespace named_wheel
// mux's emoji card, under each wrapper emoji_popup puts over it: a wrapper
// over content with a size of its own takes that size, and a scroll inside
// lays out what moved -- not, sized by its content, all of the panel's
// cells at every frame. And the panel, in a column that grows under the
// The emoji card, each wrapper of emoji_popup's in turn: which makes every
// scrolling frame lay the whole panel out again.
namespace card_isolate {
namespace c = skiff::compose;
using namespace emoji_grid;
inline auto arrow2(bool next) { return c::styled({.width = 30.0f, .height = 30.0f}, nodes::Text(next ? ">" : "<", 20.0f, 0xff000000u)); }
struct Flick2 {
  std::optional<int> operator()(bool) const { return std::nullopt; }
};
struct Query2 {
  std::string text;
  friend bool operator==(const Query2 &, const Query2 &) = default;
};
struct Preview2 {
  int on = 0;
  friend bool operator==(const Preview2 &, const Preview2 &) = default;
};
inline auto mux_tab(const Group &one) {
  auto made = SKIFF_NAMED(emoji_tab, c::row(c::justified(c::hbox(0.0f, {.width = 30.0f, .height = 30.0f, .minWidth = 24.0f, .alignSelf = scene::align::kMiddle,
                                                                      .cornerRadius = 6.0f}), nodes::justify::middle{}),
      c::visible(true, c::styled({.alignSelf = scene::align::kMiddle}, nodes::Text(one.name.substr(0, 1), 16.0f, 0xff000000u)))));
  if (!one.name.starts_with("Pack"))
    made.apply({.grow = scene::axes::kX, .shrink = scene::axes::kX, .minWidth = 0.0f});
  return made;
}
inline auto panel() {
  using query = skiff::model::Field<&Query2::text>;
  auto groups = c::sections<Group>(c::vbox(4.0f, {.fill = true}), c::vbox(0.0f), mux_tab, section,
      c::split<Group>(c::vbox(4.0f, {.fillX = true, .autoSize = scene::axes::kY}),
          [](const Group &one) { return one.name.starts_with("Pack"); },
          c::in_row<Group>(c::hbox(0.0f, {.fillX = true, .height = 36.0f})),
          c::paged<Group>(c::hbox(0.0f, {.fillX = true, .height = 30.0f}),
                          c::justified(c::hbox(0.0f, {.height = 30.0f, .grow = scene::axes::kX}), nodes::justify::middle{}), 30.0f, 60.0f,
                          arrow2, "Previous emoji packs", "Next emoji packs")));
  return c::local<Preview2>(c::column(c::vbox(0.0f, {.fill = true}),
      c::styled({.fill = true}, c::local<Query2>(c::column(c::vbox(4.0f, {.fill = true, .padding = {7.0f, 0.0f, 4.0f, 7.0f}}),
          c::styled({.fillX = true, .height = 32.0f, .margin = {0.0f, 7.0f, 0.0f, 0.0f}}, nodes::Text("Search emoji", 13.0f, 0xff000000u)),
          c::styled({.fillX = true, .grow = scene::axes::kY},
              c::projected<query>([](const std::string &) {
                std::vector<Group> all = std::views::iota(0, 4) | std::views::transform([](int g) { return Group{std::format("Pack {}", g), 30}; }) |
                                         std::ranges::to<std::vector>();
                all.append_range(std::views::iota(0, 9) | std::views::transform([](int g) { return Group{std::format("Group {}", g), 190}; }));
                return c::SectionsShown<Group>{all, all, true};
              }, std::move(groups))))))));
}
inline auto inside() {
  return c::column(c::vbox(0.0f, {}),
      c::row(c::hbox(4.0f, {.fillX = true, .height = 36.0f, .depth = 1.0f, .padding = {4.0f, 8.0f, 4.0f, 8.0f}}),
             c::styled({.width = 60.0f, .height = 28.0f}, nodes::Text("Emoji", 13.0f, 0xff000000u))),
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY, .masking = true}), panel()),
      c::column(c::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY, .visible = false}), nodes::Text("pages", 13.0f, 0xff000000u)));
}
inline scene::Spec card_spec() {
  return scene::Spec{.place = scene::anchor::kTopLeft, .x = 400.0f, .y = 100.0f, .width = 345.0f, .height = 400.0f, .cornerRadius = 8.0f,
                     .background = 0xff202020u, .border = scene::Border{0xff404040u, 1.0f}, .shadow = scene::Shadow{0x46000000u, 3.0f}};
}
inline auto as_mux() { return c::spec_for<FeedTold>([](const FeedTold &) { return card_spec(); }, c::flicked(90.0f, Flick2{}, inside())); }
inline auto no_flick() { return c::spec_for<FeedTold>([](const FeedTold &) { return card_spec(); }, inside()); }
inline auto styled_flick() { return c::styled(card_spec(), c::flicked(90.0f, Flick2{}, inside())); }
inline auto styled_only() { return c::styled(card_spec(), inside()); }
template <class Card> void run(const char *what, Card card) {
  std::println("== {}", what);
  c::state_of(card).setFloats(true);
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(card)));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  double at = 0.0;
  const auto frame = [&] {
    const scene::WalkCounts w0 = scene::walkCounts();
    (void)tree.dispatchPointer(scene::pointer::move{520.0f, 300.0f});
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 1000.0f, 700.0f));
    (void)tree.finishFrame();
    at += 16.0;
    return scene::walkCounts().laidOut - w0.laidOut;
  };
  (void)frame();
  {
    int shown = 0, all = 0;
    const auto all_nodes = scene::laidOutTree(tree.root());
    for (const scene::LaidOutNode &one : all_nodes)
      if (one.view == "emoji_tab" && one.names) {
        ++all;
        shown += one.visible && one.bounds.width() > 1.0f ? 1 : 0;
      }
    std::println("first frame: {} of {} tabs shown", shown, all);
    // Every tab its room at the first layout, the categories' too.
    EXPECT_EQ(shown, all);
  }
  for (int i = 0; i < 3; ++i)
    (void)frame();
  float footer = 0.0f;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root())) {
    if (false)
      std::println("{}{} {}x{} auto {}{}", std::string(one.depth * 2, ' '), one.bounds.fTop, one.bounds.width(), one.bounds.height(),
                   one.autoSize.has<scene::axis::x>() ? "x" : "", one.autoSize.has<scene::axis::y>() ? "y" : "");
    // The footer's rows: the categories' (36 high), the packs' (30).
    if ((one.bounds.height() == 36.0f || one.bounds.height() == 30.0f) && one.bounds.width() > 300.0f && one.bounds.fTop > 200.0f)
      footer = std::max(footer, one.bounds.fBottom);
  }
  std::uint64_t laid = 0;
  for (int notch = 0; notch < 5; ++notch) {
    (void)tree.dispatchPointer(scene::pointer::scroll{520.0f, 300.0f, 0.0f, -120.0f});
    for (int f = 0; f < 3; ++f)
      laid += frame();
  }
  std::println("footer to {}, laid out over 15 scrolling frames: {}", footer, laid);
  EXPECT_LE(footer, 500.0f);
  EXPECT_LT(laid, 1000u);
}
}  // namespace card_isolate
// A wrapper over a text (a named or pressed one, held): its text changed,
// the wrapper as big as the text at the very next layout -- not as it was.
namespace wrapped_text {
namespace c = skiff::compose;
struct Pressed {
  int operator()() const { return 0; }
};
}  // namespace wrapped_text
// A scroll view wrapped by a combinator that derives from what it wraps
// (marked, ticking): the wheel still scrolls it -- its own handler, not
// Node's default, found through the wrapper's type.
namespace wrapped_wheel {
namespace c = skiff::compose;
struct WheelTag {};
struct TallRow : scene::Node {
  TallRow() { this->apply({.fillX = true, .height = 40.0f}); }
  [[nodiscard]] bool acceptsInput() const { return true; }
};
inline auto list() {
  std::vector<TallRow> rows(30);
  auto view = nodes::ScrollContainer(c::Many<TallRow>(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY}), std::move(rows)));
  view.apply({.fill = true});
  return view;
}
template <class View> float wheeled(View view) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(view)));
  for (double at = 0.0; at < 100.0; at += 16.0) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
  }
  TallRow *row = scene::findNode<TallRow>(tree.root());
  const float before = row ? scene::work::drawnOffset(row->fState.fId).y : 0.0f;
  for (int notch = 0; notch < 10; ++notch)
    (void)tree.dispatchPointer(scene::pointer::scroll{150.0f, 200.0f, 0.0f, -120.0f});
  for (double at = 100.0; at < 1500.0; at += 16.0) {
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 400.0f));
    (void)tree.finishFrame();
  }
  return row ? before - scene::work::drawnOffset(row->fState.fId).y : -1.0f;
}
}  // namespace wrapped_wheel

}  // namespace


// A face for the tests that measure words: a default SkFont has no typeface
// and measures every word 0 wide. None found where the system keeps its
// fonts: the test is skipped.
namespace test_font {
struct TestFont {
  skia::SkFont font;
  bool ok = false;
  TestFont() {
    auto manager = skia::SkFontMgr_New_Custom_Directory("/usr/share/fonts");
    auto face = manager ? manager->matchFamilyStyle("DejaVu Sans", skia::SkFontStyle()) : nullptr;
    if (!face)
      return;
    skiff::paint::fonts().setPrimary(face);
    font = skia::SkFont(face);
    skiff::paint::defaultFont() = &font;
    ok = true;
  }
  ~TestFont() { skiff::paint::defaultFont() = nullptr; }
};
}  // namespace test_font
