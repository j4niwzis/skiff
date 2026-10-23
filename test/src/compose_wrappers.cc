// A wrapper over a text; a rows' view derived from a page's draft; a derived of one part with a callable type.
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

TEST(Compose, AWrapperOverATextIsItsSizeAtTheNextLayout) {
  using namespace wrapped_text;
  using namespace window_edge;
  // Measured with a font (the stand-in's): as wide as it is long.
  test_font::TestFont font;
  if (!font.ok)
    GTEST_SKIP() << "Needs DejaVu Sans to measure the words";
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::column(c::vbox(0.0f, {.autoSize = scene::axes::kBoth}),
          c::onPress(Pressed{}, c::text_of<Feed>([](const Feed &now) { return std::string(static_cast<std::size_t>(now.count), 'x'); },
                                                 nodes::Text("", 13.0f, 0xff000000u))))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 3}}}};
  bind::Binding<FeedModel> binding;
  const auto view = skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f);
  const auto widths = [&] {
    std::vector<float> out;
    for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
      out.push_back(one.bounds.width());
    return out;
  };
  binding.refresh(tree.root(), m);
  tree.layoutIfNeeded(view);
  (void)m.apply(model::over<Feed>(model::setTo(Feed{.count = 40})));
  binding.refresh(tree.root(), m);
  tree.layoutIfNeeded(view);
  const auto now = widths();
  std::print("widths after the text grew:");
  for (float w : now)
    std::print(" {}", w);
  std::println("");
  ASSERT_GE(now.size(), 3u);
  // The box around the wrapper as wide as the text in it, at this layout.
  EXPECT_GE(now[1] + 0.5f, now.back());
  // And the text its new size: 40 characters, not the 3 it had.
  EXPECT_GT(now.back(), 200.0f);
}
// mux's room members page: a Local's draft, and under it a projection of a
// part of the model mounting a view whose rows are derived from the draft
// (the search, the filter) -- made, and read from the draft, at once.
namespace members_like {
namespace c = skiff::compose;
using namespace window_edge;
struct MemberDraft {
  std::string query;
  std::size_t filter = 0;
  friend bool operator==(const MemberDraft &, const MemberDraft &) = default;
};
struct Listing {
  int rows = -1;
  friend bool operator==(const Listing &, const Listing &) = default;
};
struct RowsShown : scene::Node {
  int rows = -1;
  explicit RowsShown(int n) : rows(n) { this->apply({.width = 10.0f, .height = 10.0f}); }
};
inline auto listing_view(int basis) {
  return SKIFF_NAMED(member_listing_view, c::derived<MemberDraft>([basis](const MemberDraft &now) { return Listing{basis + static_cast<int>(now.filter)}; },
      c::mount<RowsShown, Listing>([](const Listing &listing) { return std::tuple{RowsShown(listing.rows)}; }, {.fillX = true, .autoSize = scene::axes::kY})));
}
using listing_t = decltype(listing_view(0));
inline auto page() {
  return SKIFF_NAMED(members_page, c::local<MemberDraft>(c::column(c::vbox(0.0f, {.fill = true}),
      c::projected<Feed>([](const Feed &now) { return std::optional<int>(now.count); },
          c::mount<listing_t, int>([](int count) { return std::tuple{listing_view(count)}; }, {.fillX = true, .autoSize = scene::axes::kY})))));
}
inline auto page_no_draft() {
  return c::local<MemberDraft>(c::column(c::vbox(0.0f, {.fill = true}),
      c::projected<Feed>([](const Feed &now) { return std::optional<int>(now.count); },
          c::mount<RowsShown, int>([](int count) { return std::tuple{RowsShown(count)}; }, {.fillX = true, .autoSize = scene::axes::kY}))));
}
inline auto page_no_outer_mount() {
  return c::local<MemberDraft>(c::column(c::vbox(0.0f, {.fill = true}), listing_view(7)));
}
template <class Page> int rows_of_page(Page made) {
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), std::move(made)));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 7}}}};
  bind::Binding<FeedModel> binding;
  for (double at = 0.0; at < 100.0; at += 16.0) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    (void)tree.finishFrame();
  }
  const RowsShown *rows = scene::findNode<RowsShown>(tree.root());
  return rows ? rows->rows : -1;
}
}  // namespace members_like
TEST(Compose, ARowsViewDerivedFromAPagesDraftIsMade) {
  using namespace members_like;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), page()));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 7}}}};
  bind::Binding<FeedModel> binding;
  for (double at = 0.0; at < 100.0; at += 16.0) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 300.0f, 300.0f));
    (void)tree.finishFrame();
  }
  const RowsShown *rows = scene::findNode<RowsShown>(tree.root());
  std::println("rows made: {}, showing {}", rows != nullptr, rows ? rows->rows : -1);
  EXPECT_EQ(rows_of_page(page_no_outer_mount()), 7);
  ASSERT_NE(rows, nullptr);
  EXPECT_EQ(rows->rows, 7);
}
// derived of one part with a callable type, as mux's calls plugin writes it:
// one derived, not two.
namespace derived_callable {
namespace c = skiff::compose;
using namespace window_edge;
struct CountOf {
  int operator()(const Feed &now) const { return now.count; }
};
struct Shows : scene::Node {
  int n = -1;
  explicit Shows(int now) : n(now) { this->apply({.width = 10.0f, .height = 10.0f}); }
};
}  // namespace derived_callable
TEST(Compose, DerivedOfOnePartWithACallableTypeIsAProjection) {
  using namespace derived_callable;
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}), c::derived<Feed>(CountOf{}, c::mount<Shows, int>([](int n) { return std::tuple{Shows(n)}; }, {.fillX = true, .autoSize = scene::axes::kY}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{.count = 5}}}};
  bind::Binding<FeedModel> binding;
  binding.refresh(tree.root(), m);
  const Shows *shown = scene::findNode<Shows>(tree.root());
  ASSERT_NE(shown, nullptr);
  EXPECT_EQ(shown->n, 5);
}

}  // namespace
