// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidMetaAppendCount with the value
// MetaLog::CAPACITY + 1 in a constant expression.  This is the boundary
// edge of the cap on the count that MetaLog::try_append takes.
//
// ValidMetaAppendCount is fixy::Refined<fixy::bounded_above<MetaLog::CAPACITY>,
// uint32_t>, and MetaLog::CAPACITY is 1 << 20.  The admitted range is
// [0, CAPACITY]: an empty append is well-formed, and one batch can fill
// the whole buffer.  A count above CAPACITY can never succeed, so the
// producer would get MetaIndex::none() from every retry.
//
// Companion fixture: neg_metalog_append_count_uint32_max.cpp
//   * This one is the boundary edge (CAPACITY + 1).  It catches a bound
//     that widens by one or more.
//   * That one is the wide miss (UINT32_MAX).  It catches a bound that
//     is removed.
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

int main() {
    using Count = crucible::ValidMetaAppendCount;
    constexpr Count bad =
        ::fixy::mint_refined<Count::predicate_type{}, Count::value_type>(crucible::MetaLog::CAPACITY + 1u);
    (void)bad;
    return 0;
}
