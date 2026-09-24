// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Readers on other threads read a sealed schema table with no lock.  A write
// to the entry array that does not go through register_name has no view to
// prove the table open and no seal phase to refuse it.  The entry array is
// private, so no such write compiles.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable table;
    table.seal();
    table.entries_[0].hash = crucible::SchemaHash{0x42};
    return 0;
}
