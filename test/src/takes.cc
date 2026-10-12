import std;
import gtest;
import skiff.model;
import skiff.bind;

#include "gtest/gtest-macros.h"

// What an overload set takes, found while compiling (skiff.bind:takes):
// what a debug build's runtime routing is made from.

namespace {
struct Opened {};
struct Closed {};
struct Moved {};
struct Place {};
struct Handlers {
  int on(const Opened &) const { return 1; }
  int on(Closed) const { return 2; }
  int on(const Moved &, const Place &) const { return 3; }
};
struct Part {
  void apply(const Opened &) {}
  void apply(const Moved &) {}
};
struct Nothing {};
} // namespace

using skiff::model::Types;
static_assert(std::same_as<skiff::bind::takes::OnEvents<Handlers>, Types<Opened, Closed, Moved>>);
static_assert(std::same_as<skiff::bind::takes::Applied<Part>, Types<Opened, Moved>>);
static_assert(std::same_as<skiff::bind::takes::OnEvents<Nothing>, Types<>>);

TEST(Takes, WhatAnOverloadSetTakesIsFoundWhileCompiling) { SUCCEED(); }
