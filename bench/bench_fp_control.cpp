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
// host measures FPCR with the same rows.

#include <cstdint>
#include <cstdio>

#include <foundation/core/FpMode.h>
#include <foundation/os/FpControl.h>

#include "bench_harness.h"

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

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
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== fp_control ===\n");
    std::printf("  The cost of one read and one write of the FP control register.\n\n");

    bench::Report reports[] = {
        read_register(), write_same(), write_toggle(), chain_alone(), toggle_and_chain(), scope_same(), scope_other(),
    };

    bench::emit_reports_text(reports);
    bench::emit_reports_json(reports, json);
    return 0;
}
