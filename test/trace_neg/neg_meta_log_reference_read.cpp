// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The MetaLog gives no reference into a slot.  After the consumer releases a
// record, the producer writes a new record into its slot, so a reader copies
// a record with copy_run.  The member at is absent.

#include <crucible/MetaLog.h>

int main() {
    ::crucible::MetaLog log;
    [[maybe_unused]] const auto& record = log.at(::crucible::MetaIndex{0});
    return 0;
}
