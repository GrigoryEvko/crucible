#pragma once

// What a lock-free ring can carry.
//
// A ring cell is written by one thread and read by another with no lock
// between them, so the copy into and out of the cell must be a plain
// byte copy: anything that runs a constructor or a destructor at those
// points would run it against a cell the other side may already be
// looking at.  Trivial copy gives the first half, trivial destruction
// the second — a cell is overwritten in place and never destroyed, so a
// type that owns anything would leak once per slot per lap.
//
// SpscRing.h and MpscRing.h both constrain their cells with this one
// concept.  Two names for one rule read as two rules, so the rule has
// one name.

#include <type_traits>

namespace fixy::concurrent {

template <typename T>
concept RingValue = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

}  // namespace fixy::concurrent
