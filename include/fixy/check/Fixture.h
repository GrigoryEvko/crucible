#pragma once

// The fixtures of the Check family: the arrays that a test builds for the
// code under test.
//
//   run_of(test, alloc, arena, values...)   a new array of the arena that
//                                            holds the values, in order
//
// Each operation takes a context that owns the Test capability (THE TEST
// CONTEXT of fixy/check/Check.h), so a unit that ships calls no operation of
// this header.  The name stays qualified, as ::fixy::check::run_of, because
// the session network and the net reflection have functions of that name.
//
// The header is in the layer X7_check of utils/rule-table.txt.

#include <fixy/check/Check.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Region.h>

#include <cstddef>

namespace fixy::check {

// The values in a new array of the arena, in order.  The type of the first
// value is the element type, and each other value converts to it.  alloc is
// the allocation capability that the arena takes.  The arena ends the
// process when the count is past the largest count of a View, as each array
// of the arena does.  Complexity: O(n) in the count of the values.
template <TestContext Ctx, class Marker, class First, class... Rest>
[[nodiscard]] ::foundation::core::View<First>
run_of(Ctx const& /*test*/, Marker alloc, ::foundation::core::BumpArena& arena, First first, Rest... rest) noexcept {
    ::foundation::core::View<First> const run = arena.array(alloc, 1 + sizeof...(Rest), first);
    if constexpr (sizeof...(Rest) > 0) {
        std::size_t position = 1;
        ((*run.get(position++).expect("a run holds one element for each value") = static_cast<First>(rest)), ...);
    }
    return run;
}

}  // namespace fixy::check
