// Negative-compile test.  This file must fail to compile.
//
// copy_record of the MetaLog takes the position of the record as a
// MetaIndex.  A raw count names no position, and the constructor of a
// MetaIndex is explicit, so a raw integer does not convert.
//
// Expected diagnostic: no conversion from the integer to a MetaIndex.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

[[nodiscard]] ::crucible::TensorMeta copy_at_a_raw_count(::crucible::MetaLog const& log) noexcept;

::crucible::TensorMeta copy_at_a_raw_count(::crucible::MetaLog const& log) noexcept {
    return log.copy_record(uint64_t{5});
}

int main() { return 0; }
