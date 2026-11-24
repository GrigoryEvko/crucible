// Negative-compile test.  This file must fail to compile.
//
// The counts of a TraceGraph are const, so the assignment of a graph is
// deleted.  An assignment of one graph over a built graph would write each
// count of the built graph after its build.
//
// Expected diagnostic: the copy assignment of TraceGraph is deleted.

#include <crucible/TraceGraph.h>

int main() {
    crucible::TraceGraph built{};
    crucible::TraceGraph const other{};
    built = other;
    return 0;
}
