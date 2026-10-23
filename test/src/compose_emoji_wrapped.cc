// The emoji card under all its wrappers, as mux holds it, laying out little a frame. Heavy to compile: built where SKIFF_TESTS_HEAVY is on.
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

TEST(Compose, TheEmojiCardUnderItsWrappersLaysOutLittleAFrame) {
  using namespace card_isolate;
  run("spec_for over flicked (mux)", as_mux());
  run("spec_for, no flicked", no_flick());
  run("styled over flicked", styled_flick());
  run("styled only", styled_only());
}

}  // namespace
