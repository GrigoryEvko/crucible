// Negative-compile test.  This file must fail to compile.
//
// MetaLog::reset moves the head and the tail of the log back to the start, so
// no other thread may read a counter while it runs.  The reset takes the pair
// of the producer end and the consumer end of one log, and only a pair door
// lends the two to one thread.  A producer that holds only its own end, while
// a second thread holds the consumer end and reads the head, has no pair to
// give, so its reset does not compile.
//
// Expected diagnostic: the producer end does not convert to the pair of ends.

#include <crucible/MetaLog.h>

#include "ring_ends.h"

int main() {
    crucible::test::with_log_producer([](crucible::MetaLog::Producer& log) noexcept { crucible::MetaLog::reset(log); });
    return 0;
}
