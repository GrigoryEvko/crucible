// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CallSiteTable::Lineno is fixy::NonNegative<int32_t>, and the checked
// mint is its only door.  This fixture gives the mint the smallest
// int32_t.  It catches an unsigned counter that narrows to int32_t and
// loses its sign bit, or a damaged line number from the Python frame.
//
// The companion fixture neg_call_site_lineno_negative gives the mint -1,
// the largest refused value.

#include <crucible/CallSiteTable.h>

#include <fixy/Refined.h>

#include <cstdint>
#include <limits>

int main() {
    constexpr crucible::CallSiteTable::Lineno bad =
        ::fixy::mint_refined<::fixy::non_negative>(std::numeric_limits<int32_t>::min());
    (void)bad;
    return 0;
}
