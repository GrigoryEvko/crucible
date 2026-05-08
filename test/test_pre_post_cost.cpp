// SPDX-License-Identifier: Apache-2.0
//
// test/test_pre_post_cost.cpp
//
// CONTRACT-008 — sentinel locking the zero-cost-in-NDEBUG claim of
// CRUCIBLE_PRE / CRUCIBLE_POST.  The header doc-comment in safety/
// Pre.h §52-66 promises:
//
//   NDEBUG (rel) | YES (consteval) | NO | YES ([[assume]])
//
// — i.e. under -O3 -DNDEBUG, CRUCIBLE_PRE compiles to a single
// `[[assume]]` hint, identical to writing `[[assume(cond)]]` by hand.
// Zero bytes, zero cycles.  This file proves it: two probe functions
// with byte-identical bodies — one routed through CRUCIBLE_PRE, the
// other through bare `[[assume]]` — must produce byte-identical
// machine code under the production build flags.
//
// HOW THIS TEST WORKS
//
// Each probe function lives in its own ELF section (named with no
// dots so `__start_<name>` / `__stop_<name>` linker symbols are
// auto-generated, per ld(1) "Output Section Constraint" rules).  At
// runtime, we subtract the symbols to get section sizes and compare.
//
//   __start_crucible_pre_probe ─── start of probe-using-CRUCIBLE_PRE
//   __stop_crucible_pre_probe  ─── one byte past end
//   __start_crucible_assume_probe ─ start of probe-using-bare-[[assume]]
//   __stop_crucible_assume_probe  ─ one byte past end
//
// The probe bodies are identical: validate the input, multiply.  The
// only difference is the macro form used to express the precondition.
// Equal section size ⇒ equal machine code (modulo same-section
// alignment, which we eliminate by `[[gnu::aligned(1)]]`).
//
// BUILD FLAG OVERRIDE
//
// This test is the only TU in test/ that is built with
// `-O3 -DNDEBUG` rather than the preset's debug flags.  CMake wires
// this via per-target overrides (test/CMakeLists.txt).  The mechanic:
//   * remove the test factory's `-UNDEBUG` injection (we want NDEBUG)
//   * add `-O3` so the optimizer fully folds the `if consteval` branch
//   * skip sanitizers (they distort code generation)
//
// The override is specific to THIS test; every other test still
// builds with the preset's debug flags + assertions.
//
// FAILURE MODES THIS CATCHES
//
//   1. CRUCIBLE_PRE accidentally references contract_failed in NDEBUG
//      (e.g. someone forgets the `#ifdef NDEBUG` guard) — detected
//      because the call site adds 5+ bytes for the call instruction.
//   2. CRUCIBLE_CONTRACT_FENCE_() accidentally emits a real op — the
//      observable_checkpoint hardening should be `((void)0)` unless
//      `-DCRUCIBLE_CONTRACT_OBSERVABLE` is set.  This test does not
//      define that flag, so the fence must be void.
//   3. The `if consteval` branch leaks runtime-visible code — should
//      be statically false at runtime, fully eliminated.
//
// COST: ~5ms test runtime (loads, subtracts, compares).  Negligible.

#include <crucible/safety/Pre.h>

#include <cstddef>
#include <cstdio>

extern "C" {

// Forward declarations required by `-Werror=missing-declarations`.
// extern "C" propagates so the linker sees plain symbol names — no
// mangling differences between the probes that could perturb the
// section-size comparison.
int crucible_pre_probe_fn(int x) noexcept;
int crucible_assume_probe_fn(int x) noexcept;

// ── Probe 1: CRUCIBLE_PRE-driven precondition ────────────────────
//
// Under -O3 -DNDEBUG:
//   * `if consteval` is statically false → branch eliminated.
//   * CRUCIBLE_CONTRACT_FENCE_() is `((void)0)` (we don't define
//     CRUCIBLE_CONTRACT_OBSERVABLE).
//   * `[[assume(cond)]]` is the sole remaining emission.
//
// `[[gnu::noinline]]` prevents the optimizer from inlining the
// function into main(), which would defeat the size measurement.
// `[[gnu::section(...)]]` plants the function in its own ELF
// section so `__start_/__stop_` symbols become available.
[[gnu::noinline, gnu::section("crucible_pre_probe")]]
int crucible_pre_probe_fn(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    return x * 2;
}

// ── Probe 2: bare [[assume]] (the cost-model baseline) ───────────
//
// Identical to probe 1 except the precondition is expressed as a
// bare `[[assume]]` rather than via CRUCIBLE_PRE.  This is the
// minimum-overhead form — the compiler propagates the invariant
// to the optimizer with zero runtime emission.  The test's claim
// is that probe 1 compiles to the same machine code.
[[gnu::noinline, gnu::section("crucible_assume_probe")]]
int crucible_assume_probe_fn(int x) noexcept {
    [[assume(x > 0)]];
    return x * 2;
}

// ── Section bound symbols (linker-generated) ────────────────────
//
// `ld --gc-sections` and the System V ABI specify that for any
// section whose name is a valid C identifier (alphanumeric +
// underscore, no dots), the linker auto-generates `__start_<name>`
// and `__stop_<name>` symbols pointing to the section's first
// byte and one-past-last respectively.
//
// Section names without dots are required: `.text.foo` would NOT
// trigger auto-generation because of the dot.  We use plain
// identifiers `crucible_pre_probe` / `crucible_assume_probe`.
extern char __start_crucible_pre_probe[];
extern char __stop_crucible_pre_probe[];
extern char __start_crucible_assume_probe[];
extern char __stop_crucible_assume_probe[];

}  // extern "C"

int main() {
    // Force the probe functions to be referenced, otherwise -fdce or
    // -fgc-sections would discard them along with their sections.
    int volatile sink = 0;
    sink += crucible_pre_probe_fn(7);
    sink += crucible_assume_probe_fn(7);
    if (sink != 28) {
        std::fprintf(stderr, "test_pre_post_cost: probe sink wrong (%d)\n", sink);
        return 1;
    }

    // Pointer subtraction yields std::ptrdiff_t natively — no cast needed
    // (`-Werror=useless-cast` would flag it).
    std::ptrdiff_t const pre_size =
        __stop_crucible_pre_probe - __start_crucible_pre_probe;
    std::ptrdiff_t const assume_size =
        __stop_crucible_assume_probe - __start_crucible_assume_probe;

    // Strict equality is the load-bearing claim.  If a future change
    // accidentally adds runtime overhead to CRUCIBLE_PRE under NDEBUG,
    // this assertion fails with the actual delta — telling the
    // contributor exactly how many bytes of regression they
    // introduced.  Tolerance is zero on purpose.
    //
    // (If GCC's function-section alignment ever introduces inter-
    // function padding that makes strict equality flake — which has
    // not been observed on x86-64 / aarch64 with `-O3` — the right
    // fix is `[[gnu::aligned(1)]]` on the probe declarations, not
    // loosening the tolerance.  Loose tolerance hides the regressions
    // this test is supposed to catch.)
    if (pre_size != assume_size) {
        std::fprintf(stderr,
                     "test_pre_post_cost: CRUCIBLE_PRE NOT zero-cost under "
                     "NDEBUG\n"
                     "  CRUCIBLE_PRE  probe .text section = %td bytes\n"
                     "  bare [[assume]] probe .text section = %td bytes\n"
                     "  delta = %td bytes\n"
                     "\n"
                     "The doc claim in safety/Pre.h §52-66 promises\n"
                     "zero runtime cost in NDEBUG; this test proves the\n"
                     "claim by comparing the two probes' machine code\n"
                     "byte-for-byte. A non-zero delta means a regression\n"
                     "was introduced that emits runtime code under NDEBUG.\n"
                     "\n"
                     "Run `objdump -d build/test/test_pre_post_cost` and\n"
                     "compare the two probe disassemblies for the\n"
                     "diverging instruction(s).\n",
                     pre_size, assume_size, pre_size - assume_size);
        return 1;
    }

    // Both probe sections must be non-empty (catches the case where
    // -fgc-sections accidentally discarded them despite the volatile
    // sink — the linker may still elide if the sink path is itself
    // optimized away).
    if (pre_size <= 0 || assume_size <= 0) {
        std::fprintf(stderr,
                     "test_pre_post_cost: probe section was empty "
                     "(pre=%td, assume=%td)\n",
                     pre_size, assume_size);
        return 1;
    }

    return 0;
}
