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
// The ported tree stated this predicate twice, as SpscValue in
// SpscRing.h and as RingValue in MpscRing.h, with identical bodies.
// Two names for one rule read as two rules, and a reader has to compare
// them to find out they agree.  One name, stated once.
//
// Old spelling: include/crucible/concurrent/_SpscRing.h (SpscValue) and
// include/crucible/concurrent/_MpscRing.h (RingValue).

#include <type_traits>

namespace fixy::concurrent {

template <typename T>
concept RingValue = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

}  // namespace fixy::concurrent
