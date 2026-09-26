// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SchemaTable::SizeCounter is BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>.
// The door mint_bounded_monotonic admits a value in [0, CAP] and refuses
// CAP + 1 and above.  CAP itself is the full table, which the check in
// register_name catches.  CAP + 1 is never correct.
//
// This fixture is the edge of the bound.  It fails when the bound widens
// past SCHEMA_TABLE_CAP.  The companion fixture
// neg_schema_table_size_uint32_max is the wide miss, and it fails when the
// counter becomes a plain Monotonic.
//
// Expected diagnostic: the bound precondition of the BoundedMonotonic
// constructor fails in a constant expression.

#include <crucible/SchemaTable.h>
#include <fixy/Mutation.h>

#include <cstdint>

int main() {
    constexpr crucible::SchemaTable::SizeCounter bad =
        ::fixy::mint_bounded_monotonic<uint32_t, crucible::SCHEMA_TABLE_CAP>(
            static_cast<uint32_t>(crucible::SCHEMA_TABLE_CAP) + uint32_t{1});
    (void)bad;
    return 0;
}
