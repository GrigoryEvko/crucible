// Negative-compile test.  This file must fail to compile.
//
// mint_meta_run makes a run of the metadata log from a View of a fixed extent
// with no check at run time.  Its constraint bounds the extent by the
// capacity of the log, so a View of one record more than the log holds gives
// no run.
//
// Expected diagnostic: the extent fails MetaRunFixedExtent.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

using PastCapacity = ::fixy::View<const ::crucible::TensorMeta, ::crucible::kMetaLogCapacity + 1>;

[[nodiscard]] ::crucible::MetaRun run_past_capacity(PastCapacity records) noexcept;

::crucible::MetaRun run_past_capacity(PastCapacity records) noexcept { return ::crucible::mint_meta_run(records); }

int main() { return 0; }
