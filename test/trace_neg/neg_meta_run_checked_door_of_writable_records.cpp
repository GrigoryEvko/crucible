// Negative-compile test.  This file must fail to compile.
//
// mint_checked_meta_run takes a View of const records: a run of the
// metadata log reads its records, and the type gives it no writer.  A View
// of records that its holder can write gives no run.
//
// Expected diagnostic: no door of a run takes a View of writable records.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

[[nodiscard]] ::crucible::MetaRun
checked_run_of_writable_records(::fixy::View<::crucible::TensorMeta> records) noexcept;

::crucible::MetaRun checked_run_of_writable_records(::fixy::View<::crucible::TensorMeta> records) noexcept {
    return ::crucible::mint_checked_meta_run(records);
}

int main() { return 0; }
