// Negative-compile test.  This file must fail to compile.
//
// An append of the metadata log takes a MetaRun, whose doors bound its count
// by the capacity of the log, so the append checks no count.  A plain View
// has no such bound, and no conversion makes a run of it.
//
// Expected diagnostic: no conversion from a View to a MetaRun.

#include <crucible/MetaLog.h>
#include <fixy/Core.h>

#include "ring_ends.h"

int main() {
    const ::fixy::FixedArray<crucible::TensorMeta, 1> record{};
    const ::fixy::View<const crucible::TensorMeta> records = record.view();
    crucible::test::with_log_producer(
        [records](crucible::MetaLog::Producer& log) noexcept { (void)crucible::MetaLog::try_append(log, records); });
    return 0;
}
