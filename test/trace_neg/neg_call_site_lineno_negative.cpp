// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable::Lineno is fixy::NonNegative<int32_t>, and the checked
// mint is its only door.  This fixture gives the mint -1, the largest
// refused value.  It catches a predicate that drifts from "at least zero"
// to "above zero" or to "not -1".
//
// The companion fixture neg_call_site_lineno_int_min gives the mint the
// smallest int32_t, a wide miss.

#include <crucible/CallSiteTable.h>

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::CallSiteTable::Lineno bad = ::fixy::mint_refined<::fixy::non_negative>(int32_t{-1});
    (void)bad;
    return 0;
}
