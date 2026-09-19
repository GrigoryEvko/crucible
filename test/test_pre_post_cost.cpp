// SPDX-License-Identifier: Apache-2.0
//
// CRUCIBLE_PRE claims to cost nothing under NDEBUG: it should compile to
// the same single `[[assume]]` hint a reader would write by hand.  The two
// probes below have identical bodies and differ only in which form states
// the precondition, so equal section sizes mean equal machine code.
//
// This translation unit is built with `-O3 -DNDEBUG` instead of the
// preset's debug flags: NDEBUG selects the release expansion of the macro,
// `-O3` folds the `if consteval` branch away, and sanitizers are off
// because they change code generation.  Every other test keeps the debug
// flags.
//
// The comparison is only meaningful while CRUCIBLE_CONTRACT_OBSERVABLE is
// undefined, which makes the hardening fence expand to nothing.  This
// translation unit does not define it.

#include <crucible/safety/_Pre.h>

#include <cstddef>
#include <cstdio>

extern "C" {

// Declared because `-Werror=missing-declarations` demands it.  The
// extern "C" linkage keeps both probes on plain symbol names, so mangling
// cannot perturb the section-size comparison.
int crucible_pre_probe_fn(int x) noexcept;
int crucible_assume_probe_fn(int x) noexcept;

// Without `noinline` the optimizer folds the probe into main and there is
// nothing left to measure.  The section attribute is what makes the
// bounding symbols exist.
[[gnu::noinline, gnu::section("crucible_pre_probe")]]
int crucible_pre_probe_fn(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    return x * 2;
}

// The baseline: the invariant reaches the optimizer and nothing is
// emitted.  The other probe has to match this.
[[gnu::noinline, gnu::section("crucible_assume_probe")]]
int crucible_assume_probe_fn(int x) noexcept {
    [[assume(x > 0)]];
    return x * 2;
}

// The linker generates `__start_<name>` and `__stop_<name>` only for a
// section whose name is a valid C identifier.  A name containing a dot,
// such as `.text.foo`, gets no such symbols, which is why both section
// names here are plain identifiers.
extern char __start_crucible_pre_probe[];
extern char __stop_crucible_pre_probe[];
extern char __start_crucible_assume_probe[];
extern char __stop_crucible_assume_probe[];

}  // extern "C"

int main() {
    // Unreferenced, the probes are discarded along with their sections.
    int volatile sink = 0;
    sink += crucible_pre_probe_fn(7);
    sink += crucible_assume_probe_fn(7);
    if (sink != 28) {
        std::fprintf(stderr, "test_pre_post_cost: probe sink wrong (%d)\n", sink);
        return 1;
    }

    // A cast on these subtractions would trip `-Werror=useless-cast`.
    std::ptrdiff_t const pre_size = __stop_crucible_pre_probe - __start_crucible_pre_probe;
    std::ptrdiff_t const assume_size = __stop_crucible_assume_probe - __start_crucible_assume_probe;

    // The tolerance is zero on purpose, and the delta in the message is
    // the byte count of the regression.  Should function-section alignment
    // ever pad one probe and not the other, the fix is
    // `[[gnu::aligned(1)]]` on the probe declarations.  Widening the
    // tolerance instead would hide exactly what this compares.
    if (pre_size != assume_size) {
        std::fprintf(stderr,
                     "test_pre_post_cost: CRUCIBLE_PRE NOT zero-cost under "
                     "NDEBUG\n"
                     "  CRUCIBLE_PRE  probe .text section = %td bytes\n"
                     "  bare [[assume]] probe .text section = %td bytes\n"
                     "  delta = %td bytes\n"
                     "\n"
                     "CRUCIBLE_PRE must emit no runtime code under NDEBUG.\n"
                     "A non-zero delta means it now emits some.\n"
                     "\n"
                     "Disassemble this binary and compare the two probe\n"
                     "bodies for the diverging instructions.\n",
                     pre_size, assume_size, pre_size - assume_size);
        return 1;
    }

    // Two empty sections also compare equal.  The linker can still drop
    // both if the sink path is itself optimized away.
    if (pre_size <= 0 || assume_size <= 0) {
        std::fprintf(stderr,
                     "test_pre_post_cost: probe section was empty "
                     "(pre=%td, assume=%td)\n",
                     pre_size, assume_size);
        return 1;
    }

    return 0;
}
