// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: minting ValidTraceNodeKindRaw with UINT8_MAX during constant
// evaluation.
//
// ValidTraceNodeKindRaw is ::fixy::Refined<bounded_above<TERMINAL>,
// uint8_t>, and ::fixy::mint_refined<kValidTraceNodeKindBound> is its one
// door.  The door's predicate refuses a byte past TERMINAL and the
// constant evaluation fails.
//
// Companion fixture to neg_trace_node_kind_above_terminal.cpp:
//   - That one is the boundary edge (= TERMINAL + 1).
//   - This one is the wide miss (= UINT8_MAX).  It catches a refinement
//     that loses its bound and accepts any byte.

#include <crucible/MerkleDag.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidTraceNodeKindRaw bad =
        ::fixy::mint_refined<crucible::kValidTraceNodeKindBound>(uint8_t{UINT8_MAX});
    (void)bad;
    return 0;
}
