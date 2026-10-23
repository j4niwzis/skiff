// A panel mounted as the model asks for it, placed by what it reads, at once.
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

TEST(Compose, APanelMountedAsAskedIsPlacedByWhatItReadsAtOnce) {
  using namespace window_edge;
  using panel_t = decltype(placed_panel::panel());
  scene::Scene tree(c::column(c::vbox(0.0f, {.fill = true}),
      c::projected<FeedTold>([](const FeedTold &now) { return now.of == 2 ? std::optional(true) : std::nullopt; },
                             c::mount<panel_t, bool>([](bool) { return std::tuple{placed_panel::panel()}; }, {.fill = true}))));
  FeedModel m{FeedRoot{.told = {FeedTold{.of = 1}}, .feed = {Feed{}}}};
  bind::Binding<FeedModel> binding;
  const auto frame = [&](double at) {
    binding.refresh(tree.root(), m);
    tree.update(at);
    tree.layoutIfNeeded(skia::SkRect::MakeXYWH(0.0f, 0.0f, 600.0f, 400.0f));
    (void)tree.finishFrame();
  };
  double at = 0.0;
  for (; at < 100.0; at += 16.0)
    frame(at);
  (void)m.apply(model::edit(model::placeOf<FeedTold, FeedRoot>(), model::setTo(FeedTold{.of = 2})));
  for (int i = 0; i < 3; ++i, at += 16.0)
    frame(at);
  std::print("{}", scene::laidOutText(tree.root()));
  bool placed = false;
  for (const scene::LaidOutNode &one : scene::laidOutTree(tree.root()))
    if (one.visible && one.bounds.fLeft == 150.0f && one.bounds.fTop == 120.0f && one.bounds.width() == 200.0f)
      placed = true;
  std::println("panel placed: {}", placed);
  EXPECT_TRUE(placed);
}

}  // namespace
