// The probe of the scan of the timed windows (check_windows.py, THE PROBE).
//
// Two rows read one cell that no body writes.  run_kept_row gives the cell to
// the input sink of the harness first, so each call of its body reads the
// cell.  run_lost_row reads the cell with no input sink.  The optimizer then
// reads the cell one time before the timed loop, and the loop holds only its
// counter.  The check must accept the first row and refuse the second.
//
// Two more rows add one to the word of a state through its input sink.
// run_state_row reaches the state through the reference of its lambda, and
// run_with_state_row gives the state to run_with.  The check compares their
// timed loops: the loop of a row through run_with has the same instructions
// as through run.
//
// Two more rows call a print before the row on each path.  run_after_print_row
// prints through bench::print, a door of the harness that GCC cannot see into
// (THE PRINT DOORS of bench/bench_harness.h), so its row stays in hot code.
// run_after_cold_door_row prints through print_cold_form, a function of only a
// call of report().  GCC takes that function for unlikely executed, so the row
// after it is cold code.  The check must find each window of the first row in
// hot code and a window of the second row in cold code (THE PLACEMENT).  The
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

// The state of the two rows of a state: one word in memory.
struct ProbeState {
    std::uint64_t word = 0;
};

// One call of the body of a row of a state.
[[gnu::always_inline]] inline void add_one(ProbeState& state) noexcept {
    state.word += 1;
    bench::mark_changed(state.word);
}

// A row that reaches its state through the reference of its lambda.
[[gnu::used, gnu::noinline]] bench::Report run_state_row() {
    ProbeState state{};
    bench::Report report = bench::run("state", [&state] { add_one(state); });
    bench::do_not_optimize(state.word);
    return report;
}

// The same row through run_with, which holds the state.
[[gnu::used, gnu::noinline]] bench::Report run_with_state_row() {
    auto [report, state] =
        bench::run_with("state with", ProbeState{}, [](ProbeState& held) noexcept { add_one(held); });
    bench::do_not_optimize(state.word);
    return report;
}

// A row after a print of the harness.
[[gnu::used, gnu::noinline]] bench::Report run_after_print_row() {
    bench::print("the row after a print of the harness\n");
    return bench::run("after print", [] {
        bench::mark_changed(probe_cell);
        std::uint64_t const value = probe_cell;
        bench::do_not_optimize(value);
    });
}

// A door of the cold form: its body is only a call of the cold report(), so
// GCC takes it for unlikely executed.  It is the form of bench::print with no
// noipa, the control of the check.
[[gnu::noinline]] void print_cold_form(::fixy::Fmt<> const& text) noexcept { ::fixy::report(::fixy::Sink::Out, text); }

// A row after the door of the cold form: the control of THE PLACEMENT.
[[gnu::used, gnu::noinline]] bench::Report run_after_cold_door_row() {
    print_cold_form("the row after a door of the cold form\n");
    return bench::run("after cold door", [] {
        bench::mark_changed(probe_cell);
        std::uint64_t const value = probe_cell;
        bench::do_not_optimize(value);
    });
}

}  // namespace
