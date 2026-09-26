// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidTraceNodeKindRaw with TERMINAL + 1 during
// constant evaluation.
//
// ValidTraceNodeKindRaw is ::fixy::Refined<bounded_above<TERMINAL>,
// uint8_t>, and ::fixy::mint_refined<kValidTraceNodeKindBound> is its one
// door.  A kind byte read from persisted state is untrusted, and a byte
// past TERMINAL widens to an enumerator that no switch arm matches.  The
// door's predicate refuses it and the constant evaluation fails.
//
// Companion fixture to neg_trace_node_kind_uint8_max.cpp:
//   - This one is the boundary edge (= TERMINAL + 1).  It catches a bound
//     that widens past TERMINAL.
//   - That one is the wide miss (= UINT8_MAX).  It catches a refinement
//     that loses its bound.

#include <crucible/MerkleDag.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidTraceNodeKindRaw bad = ::fixy::mint_refined<crucible::kValidTraceNodeKindBound>(
        static_cast<uint8_t>(static_cast<uint8_t>(crucible::TraceNodeKind::TERMINAL) + uint8_t{1}));
    (void)bad;
    return 0;
}
