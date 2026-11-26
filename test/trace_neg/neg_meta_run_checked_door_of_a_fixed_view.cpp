// Negative-compile test.  This file must fail to compile.
//
// mint_checked_meta_run checks the count of a View of the dynamic extent at
// run time.  A View of a fixed extent takes mint_meta_run, whose constraint
// bounds the extent with no check at run time, so the checked door refuses
// it.
//
// Expected diagnostic: the fixed extent fails MetaRunDynamicExtent.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

using FourRecords = ::fixy::View<const ::crucible::TensorMeta, 4>;

[[nodiscard]] ::crucible::MetaRun checked_run_of_a_fixed_view(FourRecords records) noexcept;

::crucible::MetaRun checked_run_of_a_fixed_view(FourRecords records) noexcept {
    return ::crucible::mint_checked_meta_run(records);
}

int main() { return 0; }
