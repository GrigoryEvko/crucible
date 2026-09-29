// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidMetaAppendCount with the value UINT32_MAX in a
// constant expression.  This is the wide miss of the cap on the count
// that MetaLog::try_append takes.
//
// ValidMetaAppendCount is fixy::Refined<fixy::bounded_above<MetaLog::CAPACITY>,
// uint32_t>, and MetaLog::CAPACITY is 1 << 20.  UINT32_MAX is more than
// 4,095 times the cap.  Without the gate, a caller that took UINT32_MAX
// as a count would get MetaIndex::none() from every retry, or, on a path
// that skipped the runtime check, copy far past the end of the buffer.
//
// Companion fixture: neg_metalog_append_count_above_max.cpp
//   * That one is the boundary edge (CAPACITY + 1).
//   * This one is the wide miss (UINT32_MAX).  It catches a bound that
//     is removed, so that ValidMetaAppendCount becomes a plain uint32_t.
//
// mint_refined is the only checked door of fixy::Refined.  In a constant
// evaluation, the failed check of the predicate makes the expression not
// constant, so the initialization of a constexpr variable is ill-formed.
//
// The mint reads its predicate and its value type from the alias.  If the
// alias changes its bound, this fixture checks the new bound, not a copy
// of the old one.

#include <crucible/MetaLog.h>

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    using Count = crucible::ValidMetaAppendCount;
    constexpr Count bad = ::fixy::mint_refined<Count::predicate_type{}, Count::value_type>(UINT32_MAX);
    (void)bad;
    return 0;
}
