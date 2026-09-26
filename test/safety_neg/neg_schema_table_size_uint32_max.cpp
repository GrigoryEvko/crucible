// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SchemaTable::SizeCounter is BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>.
// The value 0xFFFFFFFF is past every size that the counter can hold, and it
// would point far past the entries array.  The door mint_bounded_monotonic
// must refuse it.
//
// This fixture is the wide miss.  It fails when the counter becomes a plain
// Monotonic or a plain uint32_t, and only the check in register_name stays.
// The companion fixture neg_schema_table_size_above_cap is the edge of the
// bound.
//
// Expected diagnostic: the bound precondition of the BoundedMonotonic
// constructor fails in a constant expression.

#include <crucible/SchemaTable.h>
#include <fixy/Mutation.h>

#include <climits>
#include <cstdint>

int main() {
    constexpr crucible::SchemaTable::SizeCounter bad =
        ::fixy::mint_bounded_monotonic<uint32_t, crucible::SCHEMA_TABLE_CAP>(uint32_t{UINT32_MAX});
    (void)bad;
    return 0;
}
