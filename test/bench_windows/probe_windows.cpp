// The probe of the scan of the timed windows (check_windows.py, THE PROBE).
//
// Two rows read one cell that no body writes.  run_kept_row gives the cell to
// the input sink of the harness first, so each call of its body reads the
// cell.  run_lost_row reads the cell with no input sink.  The optimizer then
// reads the cell one time before the timed loop, and the loop holds only its
// counter.  The check must accept the first row and refuse the second.  The
// test compiles the probe and reads its object, and it runs no row.

#include "bench_harness.h"

#include <cstdint>

namespace {

// The cell of the two rows.  Each row only reads it.
std::uint64_t probe_cell = 7;

// A row whose body reads the cell after its input sink.
[[gnu::used, gnu::noinline]] bench::Report run_kept_row() {
    return bench::run("kept", [] {
        bench::mark_changed(probe_cell);
        std::uint64_t const value = probe_cell;
        bench::do_not_optimize(value);
    });
}

// A row whose body reads the cell with no input sink: the control of the
// check.
[[gnu::used, gnu::noinline]] bench::Report run_lost_row() {
    return bench::run("lost", [] {
        std::uint64_t const value = probe_cell;
        bench::do_not_optimize(value);
    });
}

}  // namespace
