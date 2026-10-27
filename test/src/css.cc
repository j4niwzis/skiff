// SPDX-License-Identifier: AGPL-3.0-only
import std;
import gtest;
import skiff.web;

#include "gtest/gtest-macros.h"

namespace {
namespace css = skiff::web::css;
using skiff::web::Markup;

// A declaration only accepts values belonging to its property.
static_assert(std::is_constructible_v<css::position, css::relative>);
static_assert(std::is_constructible_v<css::width, css::pixels>);
static_assert(!std::is_constructible_v<css::position, css::pixels>);
static_assert(!std::is_constructible_v<css::width, css::relative>);
static_assert(!std::is_constructible_v<css::overflow, css::row>);

TEST(Css, LayoutValuesHaveOneCssWriter) {
  EXPECT_EQ(css::name(css::position{css::relative{}}), "position");
  EXPECT_EQ(css::value(css::position{css::relative{}}), "relative");
  EXPECT_EQ(css::value(css::width{css::calculated{{100}, {28}}}), "calc(100% - 28px)");
  EXPECT_EQ(css::value(css::height{css::percent{75.125f}}), "75.125%");
  EXPECT_EQ(css::value(css::min_width{css::zero{}}), "0");
  EXPECT_EQ(css::value(css::width{css::max_content{}}), "max-content");
  EXPECT_EQ(css::value(css::flex{css::flex_values{1, 1, css::pixels{0}}}), "1 1 0px");
  EXPECT_EQ(css::value(css::flex{css::flex_values{0, 1, css::auto_{}}}), "0 1 auto");
  EXPECT_EQ(css::value(css::gap{css::gaps{css::pixels{4}, css::pixels{8}}}), "4px 8px");
}

TEST(Css, CompoundValuesKeepTheirStructureAndOrder) {
  const css::transform transform{css::transforms{{css::translate{css::percent{-50}, css::percent{-50}},
      css::translate{css::pixels{4}, css::pixels{8}}, css::scale{1.5f}}}};
  EXPECT_EQ(css::value(transform), "translate(-50%,-50%) translate(4px,8px) scale(1.5)");
  EXPECT_EQ(css::value(css::padding{css::sides{css::zero{}, css::pixels{4}}}), "0 4px");
  EXPECT_EQ(css::value(css::margin{css::sides{css::pixels{1}, css::pixels{2}, css::pixels{3}, css::pixels{4}}}), "1px 2px 3px 4px");
  EXPECT_EQ(css::value(css::border{css::line{css::pixels{2}, css::argb{0xff123456}}}), "2px solid rgba(18,52,86,1)");
  EXPECT_EQ(css::value(css::box_shadow{css::shadow{css::zero{}, css::pixels{2}, css::pixels{4}, css::argb{0xff000000}}}),
      "0 2px 4px rgba(0,0,0,1)");
  EXPECT_EQ(css::value(css::background_image{css::linear_gradient{css::argb{0xff000000}, css::argb{0xffffffff}}}),
      "linear-gradient(rgba(0,0,0,1),rgba(255,255,255,1))");
  EXPECT_EQ(css::value(css::text_decoration{css::decoration{true, true}}), "line-through underline");
}

TEST(Css, WidgetGradientsFiltersAndTintKeepTheirCssValues) {
  EXPECT_EQ(css::value(css::range_track{css::directed_gradient{css::to_right{}, {
      {css::argb{0xff112233}, {css::zero{}, css::percent{50}}},
      {css::argb{0xff445566}, {css::percent{50}, css::percent{100}}}}}}),
      "linear-gradient(to right,rgba(17,34,51,1) 0 50%,rgba(68,85,102,1) 50% 100%)");
  EXPECT_EQ(css::value(css::filter{css::blur{css::pixels{4}}}), "blur(4px)");
  EXPECT_EQ(css::value(css::box_shadow{css::shadow{css::zero{}, css::zero{}, css::zero{}, css::argb{0xff123456}, css::vmax{100}, true}}),
      "inset 0 0 0 100vmax rgba(18,52,86,1)");
  EXPECT_EQ(css::value(css::transform{css::transforms{{css::translate_x{css::zero{}}}}}), "translateX(0)");
}

TEST(Css, ReplacingAndRemovingADeclarationPreservesOtherProperties) {
  css::Declarations rules;
  EXPECT_TRUE(css::set(rules, css::width{css::pixels{10}}));
  EXPECT_TRUE(css::set(rules, css::height{css::pixels{20}}));
  EXPECT_FALSE(css::set(rules, css::width{css::pixels{10}}));
  EXPECT_TRUE(css::set(rules, css::width{css::percent{50}}));
  ASSERT_EQ(rules.size(), 2u);
  EXPECT_EQ(css::value(*css::find<css::width>(rules)), "50%");
  EXPECT_EQ(css::value(rules.back()), "20px");
  EXPECT_TRUE(css::erase<css::width>(rules));
  EXPECT_FALSE(css::erase<css::width>(rules));
  EXPECT_EQ(css::find<css::width>(rules), nullptr);
  EXPECT_NE(css::find<css::height>(rules), nullptr);
}

TEST(Css, OpaqueExtensionsRemainOpaqueAndCanReplaceTypedDeclarations) {
  css::Declarations rules;
  css::set(rules, css::width{css::pixels{10}});
  css::set(rules, "width", "var(--custom-width)");
  ASSERT_EQ(rules.size(), 1u);
  EXPECT_EQ(css::value(*css::find<css::width>(rules)), "var(--custom-width)");
  css::set(rules, "--custom-width", "24px");
  css::set(rules, "width", "");
  EXPECT_EQ(css::find<css::width>(rules), nullptr);
  EXPECT_EQ(css::value(*css::find(rules, "--custom-width")), "24px");
}

TEST(Css, DocumentUpdatesRemoveOldStylesAndWriteTypedValues) {
  namespace dom = skiff::web::dom;
  dom::reset();
  const auto element = dom::create("div");
  dom::append(dom::root(), element);
  Markup before;
  before.style(css::width{css::pixels{10}});
  before.style(css::overflow{css::hidden{}});
  skiff::web::detail::writeOwn(element, before, {});
  Markup after = before;
  after.eraseStyle<css::width>();
  after.style(css::overflow{css::auto_{}});
  skiff::web::detail::writeOwn(element, after, before);
  const auto html = dom::dump(dom::root());
  EXPECT_EQ(html.find("width:"), std::string::npos);
  EXPECT_NE(html.find("overflow:auto"), std::string::npos);
  EXPECT_EQ(html.find("overflow:hidden"), std::string::npos);
}
} // namespace
