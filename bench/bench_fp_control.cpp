// The cost of the FP control door (foundation/os/FpControl.h).
//
// A pool worker sets the FP control register of its thread to the mode of
// each task, so the cost of one write of the register is a cost of each task
// with a mode that differs from the mode of the task before it.  A write of
// MXCSR can serialize the FP pipeline, and the bench measures that too.
//
// The rows:
//   read             one read of the register
//   write, same      one write of the word that the register holds
//   write, toggle    one write of a word with the other flush mode, so each
//                    call changes the register
//   mul chain        eight dependent multiplies, with no write
//   toggle + chain   one write that changes the register, then the chain
//   scope, same      the open and the end of a scope of the mode of the
//                    register, which writes nothing
//   scope, other     the open and the end of a scope of another mode, which
//                    writes the register two times
//
// The host of the tree is x86_64, so this bench measures MXCSR.  An aarch64
// host measures FPCR with the same rows.  Each line gives the percentiles of
// one call in picoseconds, the cycles of one call, and the coefficient of
// variation in thousandths.

#include <fixy/Core.h>
#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

#include <cstdint>

#include "bench_harness.h"

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// A time in nanoseconds, as the nearest integer of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// Writes one line: the name of the row and the percentiles of its samples.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    bench::print("{}  p50={} ps  p99={} ps  max={} ps  cycles={}  cv={} permille\n", name,
                 picoseconds(measured.pct.p50), picoseconds(measured.pct.p99), picoseconds(measured.pct.max),
                 static_cast<std::uint64_t>(measured.cycles_per_op + 0.5),
                 static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// Eight dependent multiplies.  The factor is one, so the value stays the
// same, and the compiler keeps each multiply because the value goes to
// do_not_optimize.
[[nodiscard, gnu::noipa]] double multiply_chain(double value, double factor) noexcept {
    for (int step = 0; step < 8; ++step) {
        value *= factor;
    }
    return value;
}

bench::Report read_register() {
    return bench::run("read", [] {
        os::FpControlWord const word = os::FpControlWord::read();
        bench::do_not_optimize(word);
    });
}

bench::Report write_same() {
    os::FpControlWord const word = os::FpControlWord::read();
    return bench::run("write, same", [word] { os::write_fp_control(word); });
}

bench::Report write_toggle() {
    os::FpControlWord const ieee = os::FpControlWord::read().with_mode(core::kFpModeIeee);
    os::FpControlWord const flush = ieee.with_mode(core::kFpModeFlushToZero);
    bool is_flushed = false;
    bench::Report report = bench::run("write, toggle", [&is_flushed, ieee, flush] {
        is_flushed = !is_flushed;
        os::write_fp_control(is_flushed ? flush : ieee);
    });
    os::write_fp_control(ieee);
    return report;
}

bench::Report chain_alone() {
    double value = 1.0;
    return bench::run("mul chain", [&value] {
        value = multiply_chain(value, 1.0);
        bench::do_not_optimize(value);
    });
}

bench::Report toggle_and_chain() {
    os::FpControlWord const ieee = os::FpControlWord::read().with_mode(core::kFpModeIeee);
    os::FpControlWord const flush = ieee.with_mode(core::kFpModeFlushToZero);
    bool is_flushed = false;
    double value = 1.0;
    bench::Report report = bench::run("toggle + chain", [&is_flushed, &value, ieee, flush] {
        is_flushed = !is_flushed;
        os::write_fp_control(is_flushed ? flush : ieee);
        value = multiply_chain(value, 1.0);
        bench::do_not_optimize(value);
    });
    os::write_fp_control(ieee);
    return report;
}

bench::Report scope_same() {
    return bench::run("scope, same", [] {
        os::FpScope<core::kFpModeIeee> const scope = os::mint_fp_scope<core::kFpModeIeee>();
        bench::do_not_optimize(scope.epoch());
    });
}

bench::Report scope_other() {
    return bench::run("scope, other", [] {
        os::FpScope<core::kFpModeFlushToZero> const scope = os::mint_fp_scope<core::kFpModeFlushToZero>();
        bench::do_not_optimize(scope.epoch());
    });
}

}  // namespace

int main() {
    bench::elevate_priority();
    bench::print("=== fp_control: the cost of one read and one write of the FP control register\n");
    print_row("read          ", read_register());
    print_row("write, same   ", write_same());
    print_row("write, toggle ", write_toggle());
    print_row("mul chain     ", chain_alone());
    print_row("toggle + chain", toggle_and_chain());
    print_row("scope, same   ", scope_same());
    print_row("scope, other  ", scope_other());
    return 0;
}
