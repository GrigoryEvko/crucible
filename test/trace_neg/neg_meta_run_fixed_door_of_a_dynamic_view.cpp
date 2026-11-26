// Negative-compile test.  This file must fail to compile.
//
// mint_meta_run checks nothing at run time, so it takes only a View of a
// fixed extent that its constraint bounds.  A View of the dynamic extent can
// hold more records than the log, and it takes the checked door
// mint_checked_meta_run.
//
// Expected diagnostic: the dynamic extent fails MetaRunFixedExtent.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

[[nodiscard]] ::crucible::MetaRun run_of_a_dynamic_view(::fixy::View<const ::crucible::TensorMeta> records) noexcept;

::crucible::MetaRun run_of_a_dynamic_view(::fixy::View<const ::crucible::TensorMeta> records) noexcept {
    return ::crucible::mint_meta_run(records);
}

int main() { return 0; }
