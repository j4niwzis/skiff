// SPDX-License-Identifier: AGPL-3.0-only
// SKIFF_NAMED(name, expression...): what the expression makes, as a type of
// its own called name -- a view named where it is written.
//
//   inline auto accounts_side(const palette& colours) {
//     return SKIFF_NAMED(accounts_side, c::column(...));
//   }
//
// The type is a struct deriving from what the expression made, so it is
// everything that was: read, bound, wrapped and walked the same. It is what
// the compiler names in its errors -- accounts_side, not the whole of the
// composed type -- and what skiff names it by at run time (viewName()): in a
// layout's report (the view a part that sticks out is in), in a trace. A local class's mangled name is its enclosing
// function and its own name, short whatever it derives from.
//
// A macro, because only the preprocessor turns a name into both a type and
// its spelling; included in a unit's global module fragment, as module
// interfaces cannot export one.
#pragma once

#define SKIFF_NAMED(name, ...)                                                                                          \
  [&] {                                                                                                                \
    auto skiff_named_made = [&] { return __VA_ARGS__; };                                                               \
    using skiff_named_base = decltype(skiff_named_made());                                                             \
    struct name : skiff_named_base {                                                                                   \
      static constexpr const char *viewName() { return #name; }                                                        \
      void layoutChildren() {                                                                                          \
        const ::skiff::scene::ViewLaidOut here{viewName()};                                                            \
        skiff_named_base::layoutChildren();                                                                            \
      }                                                                                                                \
    };                                                                                                                 \
    return ::skiff::compose::erased(name{skiff_named_made()});                                                         \
  }()
