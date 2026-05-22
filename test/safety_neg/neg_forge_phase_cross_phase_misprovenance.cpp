// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing `Tagged<T, source::ForgePhase<'A'>>` (Phase-A,
// INGEST output) to a function whose signature demands
// `Tagged<T, source::ForgePhase<'I'>>` (Phase-I, SCHEDULE input).
// Despite identical payload type T, the two ForgePhase tag
// specializations are UNRELATED types — phantom Phase is a template
// non-type parameter, not a runtime field; the type system gives no
// implicit conversion between phases.
//
// FIXY-V-058 enrichment discipline (Tagged.h source::ForgePhase<P>):
//   ForgePhase<P> tags a value's PROVENANCE in Forge's 12-phase
//   pipeline (FORGE.md §5).  Cross-phase composition demands explicit
//   retag<source::ForgePhase<Next>>() — admitted only for forward
//   edges (A→B→C→...→L) via the FIXY-V-023 retag_policy catalog;
//   back-edges and skip-edges are rejected by default.
//
//   In production, this guards Forge's topological phase ordering:
//
//     void schedule_kernel(  // Phase I = SCHEDULE
//       Tagged<KernelGraph, source::ForgePhase<'I'>> g);
//     // schedule_kernel(post_ingest_graph);  // compile-time reject
//                                             // (Phase 'A' ≠ 'I')
//     auto post_compile = std::move(post_ingest).retag<
//       source::ForgePhase<'H'>>();  // through admitted forward edges
//     // ... walk catalog to Phase I ...
//     schedule_kernel(std::move(post_scheduled));  // OK
//
// HS14 — pairs with neg_transport_posture_mismatch.cpp for the
// 2-fixture floor on V-058 source-tag enrichments:
//   1. ForgePhase cross-phase mis-provenance:   non-type-parameter
//      (char) tag-identity gate (THIS file).
//   2. TransportPosture mismatch:                non-type-parameter
//      (enum class) tag-identity gate (peer fixture).
// Together they pin both new tag families' phantom-non-type-parameter
// discipline at the same level Tagged-with-class-type tags get via
// neg_tagged_unrelated_source_mismatch.cpp.
//
// [GCC-WRAPPER-TEXT] — Tagged<T, ForgePhase<'A'>> ≠ Tagged<T, ForgePhase<'I'>>.

#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <utility>

namespace {
    // Phase-I (SCHEDULE) consumer — accepts only post-COMPILE output
    // that has progressed through the catalog to Phase I.  In Forge
    // production this is forge/Phases/Schedule.h's entry boundary.
    [[maybe_unused]] void schedule_kernel(
        ::crucible::safety::Tagged<std::uint64_t,
                                   ::crucible::safety::source::ForgePhase<'I'>>
            /*kernel_graph_hash*/)
    {
        // body irrelevant — the call-site type-check IS the test.
    }
}

// Anchor: legitimate Phase-I call so the file is self-contained.
[[maybe_unused]] static void anchor_phase_i_call() {
    auto scheduled = ::crucible::safety::mint_tagged<
        ::crucible::safety::source::ForgePhase<'I'>, std::uint64_t>(
            0xCAFEBABEULL);
    schedule_kernel(std::move(scheduled));
}

// VIOLATION: Phase-A (INGEST) output handed to a Phase-I (SCHEDULE)
// consumer.  Tagged<T, ForgePhase<'A'>> and Tagged<T, ForgePhase<'I'>>
// are unrelated template instantiations — no implicit conversion.
// GCC rejects with "cannot convert ... ForgePhase<(char)65> ...
// ForgePhase<(char)73>" or similar typed-argument mismatch.
[[maybe_unused]] static void offending_phase_a_into_phase_i_slot() {
    auto ingested = ::crucible::safety::mint_tagged<
        ::crucible::safety::source::ForgePhase<'A'>, std::uint64_t>(
            0xCAFEBABEULL);
    schedule_kernel(std::move(ingested));  // ERROR: 'A' ≠ 'I'
}

int main() { return 0; }
