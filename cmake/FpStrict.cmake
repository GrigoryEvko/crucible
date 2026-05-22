# cmake/FpStrict.cmake — `crucible_fp_strict` INTERFACE library.
#
# FIXY-V-094.  THE FLOOR for FP discipline across every consumer of
# `crucible`.  Once linked, the target propagates a strict-IEEE-754
# compile-options pack that makes the Crucible numerical invariants
# (DetSafe / BITEXACT recipe tier / merkle-hash-safe canonicalize)
# survive the compiler's optimization stages.
#
# Without this floor, a contributor adding `-ffast-math` to a
# bench/recipe-tuned CMake target silently re-enables three failure
# modes at once:
#
#   1. FP REASSOCIATION (a + b + c reorders to a + (b + c) under
#      -fassociative-math) — drives V-091 F101 (DetSafe × FpMode)
#      cross-replay drift.  Same recipe, same seed, different bits.
#
#   2. FTZ / DENORMAL FLUSH (under -funsafe-math-optimizations) —
#      drives V-091 F102 (Vendor × Denormal): NV silicon treats
#      denormals as zero by default at MMA throughput, AMD/CPU oracle
#      keeps them; without strict-FP the C++ side ALSO flushes,
#      mismatching the recipe's declared denormal policy.
#
#   3. NAN / INF ELIMINATION (-ffinite-math-only) — turns
#      `std::isnan(x)` into `constexpr false`, dead-codes V-093's
#      canonicalize NaN branch, breaks merkle-hash convergence for
#      every double that ever transits a NaN bit pattern.
#
# THE FLOOR is `PUBLIC` on the `crucible` target — every test / bench
# / vessel / tool / example consuming `crucible` automatically links
# `crucible_fp_strict` transitively.  Opt-out is deliberate and
# review-discoverable: a target documents the engagement and links
# `crucible_fp_permissive` instead (NOT shipped here; deferred to a
# follow-on with explicit `grant::fp_mode<Permissive>` infrastructure
# per FIXY-V-093 and the Insights doc-block).
#
# Flag rationale (every flag closes a documented hardening hole):
#
#   -fno-fast-math                — root umbrella; this single flag
#                                   would suffice if `-ffast-math`
#                                   were the only attacker.  GCC
#                                   bundles 8 sub-flags into it; we
#                                   pin them individually below for
#                                   precise audit-discoverable
#                                   diagnostics.
#   -ffp-contract=off             — STRICT no-FMA-contraction.  GCC
#                                   default is `=fast` (?!) in some
#                                   configurations; the project
#                                   default is `=on` (FMA WITHIN a
#                                   statement, the IEEE-defined
#                                   case).  We tighten to `=off`
#                                   here because cross-vendor SASS /
#                                   AMDGPU / PJRT realize FMA at
#                                   different points in lowered
#                                   kernels — bit-identical chains
#                                   only ship when the C++ side
#                                   doesn't pre-contract.
#   -fno-associative-math         — disable `(a+b)+c → a+(b+c)`.
#                                   Drives F101.
#   -fno-reciprocal-math          — disable `x/y → x*(1/y)`.  The
#                                   `1/y` reciprocal-step introduces
#                                   a vendor-divergent ULP.
#   -fno-finite-math-only         — keep NaN/Inf in the optimizer's
#                                   model.  Drives F102 / F104 +
#                                   makes V-093 canonicalize work.
#   -fsignaling-nans              — pin sNaN preservation; necessary
#                                   for V-093's "all NaN payloads
#                                   collapse to canonical qNaN" to
#                                   work — otherwise the optimizer
#                                   may already have collapsed sNaN
#                                   bit patterns at construction.
#   -frounding-math               — codegen MUST respect rounding-
#                                   mode register state (default
#                                   assumes RN; under `-frounding-
#                                   math` the optimizer DOESN'T fold
#                                   FP expressions speculatively).
#                                   Drives V-091 F103 (Vendor × FP).
#   -ftrapping-math               — keep FP exception status flags
#                                   alive (the GCC default IS this,
#                                   but explicit pinning prevents a
#                                   `-fno-trapping-math` further
#                                   down the flag-chain from
#                                   stripping the discipline).
#
# Two flags from CLAUDE.md §V "NEVER" list are NOT in our SET but
# ARE in the `check-no-ffast-math.sh` deny-list because they would
# break the floor if applied per-TU:
#
#   -funsafe-math-optimizations   — global blanket disable; one flag
#                                   undoes our entire pack.
#   -fno-signed-zeros             — silently elides sign-of-zero
#                                   handling that V-093 relies on
#                                   for ±0.0 canonicalization.
#
# Implementation: INTERFACE library carries no compiled artifacts.
# `target_link_libraries(<consumer> PRIVATE crucible_fp_strict)`
# (or transitively via the `crucible` PUBLIC link) inherits the
# INTERFACE_COMPILE_OPTIONS for every TU compiled for that consumer.
#
# Ordering: this module MUST be `include()`d AFTER `project()` (the
# compiler probe runs at project time and would inherit our flags
# during its own internal compile, which can fail because the probe
# uses no fp-strict math).  We add to a target, not CMAKE_CXX_FLAGS,
# precisely to localize the effect.
#
# Verification: the grep-guard `scripts/check-no-ffast-math.sh`
# rejects any per-TU override that would punch through the floor.
# `test/test_fp_strict_floor.cpp` is a sentinel TU that links the
# INTERFACE target and asserts (via a static-assert proxy) that the
# floor is actually engaged — if the floor ever silently degrades to
# the project default, the sentinel reds.

if(TARGET crucible_fp_strict)
    return()
endif()

add_library(crucible_fp_strict INTERFACE)
add_library(crucible::fp_strict ALIAS crucible_fp_strict)

# Compiler-flag pack — applied via the INTERFACE_COMPILE_OPTIONS
# property.  Generator expression `$<COMPILE_LANGUAGE:CXX>` scopes
# each flag to C++ TUs only; the BPF kernel-side compile (clang -O2
# under different rules) is untouched.

target_compile_options(crucible_fp_strict INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-fast-math>
    $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-associative-math>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-reciprocal-math>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-finite-math-only>
    $<$<COMPILE_LANGUAGE:CXX>:-fsignaling-nans>
    $<$<COMPILE_LANGUAGE:CXX>:-frounding-math>
    $<$<COMPILE_LANGUAGE:CXX>:-ftrapping-math>
)

# Compile definition that downstream sentinel TUs can probe to
# witness the floor is in effect.  Headers can `#ifdef
# CRUCIBLE_FP_STRICT_FLOOR` to gate static_asserts against
# unintended downgrades.
target_compile_definitions(crucible_fp_strict INTERFACE
    CRUCIBLE_FP_STRICT_FLOOR=1
)
