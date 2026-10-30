// SPDX-License-Identifier: AGPL-3.0-only
// SKIFF_NAMED(name, expression...): what the expression makes, as a type of
// its own called name -- a view named where it is written.
//
//   inline auto accounts_side(const palette& colours) {
//     return SKIFF_NAMED(accounts_side, c::column(...));
//   }
//
// What it does is compose::Named's (CRTP); the macro gives it its name. Its
// content's event handlers are declared again at its level: a type derived
// from a node otherwise calls Node's deducing-this defaults, not the node's.
// The type is a struct deriving from what the expression made, so it is
// everything that was: read, bound, wrapped and walked the same; returned
// as itself in every build, so that what is overloaded on a view's type
// tells views apart outside a release build too. Given to a combinator, it
// is kept as a part (compose::as_part): held outside a release build, the
// combinator made for Held as for every other view. It is what
// the compiler names in its errors -- accounts_side, not the whole of the
// composed type -- and what skiff names it by at run time (viewName()): in a
// layout's report (the view a part that sticks out is in), in a trace. A local class's mangled name is its enclosing
// function and its own name, short whatever it derives from.
//
// A macro, because only the preprocessor turns a name into both a type and
// its spelling; included in a unit's global module fragment, as module
// interfaces cannot export one.
#pragma once

// What a named view is over: compose::Named, the scene's, unless the build
// says otherwise -- skiff.ui on the page says web::Named. Either way it is
// what the expression made, named, with the hooks declared again at its
// level.
#ifndef SKIFF_NAMED_BASE
#define SKIFF_NAMED_BASE ::skiff::compose::Named
#endif

#define SKIFF_NAMED(name, ...)                                                                                          \
  [&] {                                                                                                                \
    auto skiff_named_made = [&] { return __VA_ARGS__; };                                                               \
    struct name : SKIFF_NAMED_BASE<name, decltype(skiff_named_made())> {                                         \
      using SKIFF_NAMED_BASE<name, decltype(skiff_named_made())>::Named;                                        \
      using SKIFF_NAMED_BASE<name, decltype(skiff_named_made())>::onKey;                                        \
      using SKIFF_NAMED_BASE<name, decltype(skiff_named_made())>::onPointer;                                    \
      using SKIFF_NAMED_BASE<name, decltype(skiff_named_made())>::onText;                                       \
      using SKIFF_NAMED_BASE<name, decltype(skiff_named_made())>::onSemantic;                                   \
      static constexpr const char *viewName() { return #name; }                                                        \
    };                                                                                                                 \
    return name(skiff_named_made());                                                                                   \
  }()

// SKIFF_NAMED_AS(tag, name, expression...): the same, and a point the program
// may shape the view at (compose::point, shape_view): tag is a type the
// program declares for it, the view's name in its API -- what a plugin is
// put on. SKIFF_NAMED_OF(tag, name, facts, expression...): and what it was
// made from, handed to what shapes it.
#define SKIFF_NAMED_SHAPED(name, shaped)                                                                                \
  [&] {                                                                                                                \
    auto skiff_named_made = [&] { return shaped; };                                                                    \
    struct name : ::skiff::compose::Named<name, decltype(skiff_named_made())> {                                         \
      using ::skiff::compose::Named<name, decltype(skiff_named_made())>::Named;                                        \
      using ::skiff::compose::Named<name, decltype(skiff_named_made())>::onKey;                                        \
      using ::skiff::compose::Named<name, decltype(skiff_named_made())>::onPointer;                                    \
      using ::skiff::compose::Named<name, decltype(skiff_named_made())>::onText;                                       \
      using ::skiff::compose::Named<name, decltype(skiff_named_made())>::onSemantic;                                   \
      static constexpr const char *viewName() { return #name; }                                                        \
    };                                                                                                                 \
    return name(skiff_named_made());                                                                                   \
  }()
#define SKIFF_NAMED_AS(tag, name, ...)                                                                                  \
  SKIFF_NAMED_SHAPED(name, shape_view(::skiff::compose::point<tag>{}, [&] { return __VA_ARGS__; }()))
#define SKIFF_NAMED_OF(tag, name, facts, ...)                                                                           \
  SKIFF_NAMED_SHAPED(name, shape_view(::skiff::compose::point<tag>{}, [&] { return __VA_ARGS__; }(), facts))
