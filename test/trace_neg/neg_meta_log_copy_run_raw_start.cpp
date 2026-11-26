// Negative-compile test.  This file must fail to compile.
//
// copy_run of the MetaLog takes the start of the run as a MetaIndex, the
// position of a record in the log.  A raw count names no position, and the
// constructor of a MetaIndex is explicit, so a raw integer does not convert.
//
// Expected diagnostic: no conversion from the integer to a MetaIndex.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

void copy_from_a_raw_count(::crucible::MetaLog const& log, ::fixy::View<::crucible::TensorMeta> dst) noexcept;

void copy_from_a_raw_count(::crucible::MetaLog const& log, ::fixy::View<::crucible::TensorMeta> dst) noexcept {
    log.copy_run(uint64_t{5}, dst);
}

int main() { return 0; }
