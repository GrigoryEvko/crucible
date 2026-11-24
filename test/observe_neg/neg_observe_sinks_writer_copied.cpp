// Negative compile test.  This file must fail to compile.
//
// The writer end of a set of observation sinks is the one writer of each
// cell, so the cells take no run-time test for a second writer.  A copy of the
// writer end would give the set a second writer, and the two would race in the
// seqlock of a cell.  The writer end has no copy: the door writer() makes the
// one end, and the claim of that door refuses a second live end.

#include <crucible/observe/Observation.h>

int main() {
    ::crucible::observe::ObservationSink sink{};
    ::crucible::observe::ObservationSink::Writer first = sink.writer();
    ::crucible::observe::ObservationSink::Writer second = first;
    second.record(::fixy::index<0>(), ::crucible::observe::Observation{});
    return 0;
}
