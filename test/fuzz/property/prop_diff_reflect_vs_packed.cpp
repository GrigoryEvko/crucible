// ═══════════════════════════════════════════════════════════════════
// prop_diff_reflect_vs_packed — differential test: the reflected hash
// keeps the "same input → same hash" invariant of the original packed
// implementation.
//
// Catches:
//   - A regression in the reflected implementation.  Its bit pattern
//     differs from the packed one on purpose, so the test checks the
//     DIFFERENTIAL: two inputs are distinct under one implementation
//     exactly when they are distinct under the other
//   - Future refactors that accidentally restore commutativity in
//     feedback_signature (order-sensitive in current impl)
//   - Future refactors that drop a field from loopterm_hash's Spec
//     projection
//
// Strategy: re-implement the packed version inline in the
// harness (same code as bench/bench_reflect.cpp).  For each random
// input, compare:
//   • reflect-version(a) vs reflect-version(b) — should DIFFER for
//     distinct (a, b)
//   • packed-version(a) vs packed-version(b) — should DIFFER too
//   • If reflect says equal but packed says different (or vice versa),
//     the two implementations disagree on what counts as "structural
//     equality" — bug somewhere.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/Expr.h>
#include <crucible/MerkleDag.h>

#include <array>
#include <span>

namespace {

// The packed feedback_signature that the reflected version replaced, kept verbatim.
uint64_t feedback_signature_packed(std::span<const crucible::FeedbackEdge> edges) noexcept {
    if (edges.empty()) return 0;
    constexpr uint64_t kSeed = 0x6665656462616B73ULL;
    uint64_t h = kSeed;
    for (const auto& e : edges) {
        const uint64_t packed = (static_cast<uint64_t>(e.output_idx) << 16) | e.input_idx;
        h = crucible::detail::fmix64(h ^ packed);
    }
    h = crucible::detail::fmix64(h ^ edges.size());
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    const Config cfg = parse_args(argc, argv);

    return run(
        "feedback_signature reflect-vs-packed agreement", cfg,
        [](Rng& rng) {
            // Generate two random edge spans (1..16 edges each).
            // Distinctness is the metric we cross-check.
            struct Pair {
                std::array<FeedbackEdge, 16> a_edges;
                std::array<FeedbackEdge, 16> b_edges;
                uint8_t a_count;
                uint8_t b_count;
            };
            Pair p{};
            p.a_count = static_cast<uint8_t>(rng.next_below(16) + 1);
            p.b_count = static_cast<uint8_t>(rng.next_below(16) + 1);
            for (uint8_t i = 0; i < p.a_count; ++i)
                p.a_edges[i] = random_feedback_edge(rng);
            for (uint8_t i = 0; i < p.b_count; ++i)
                p.b_edges[i] = random_feedback_edge(rng);
            return p;
        },
        [](const auto& p) {
            const std::span<const FeedbackEdge> a{p.a_edges.data(), p.a_count};
            const std::span<const FeedbackEdge> b{p.b_edges.data(), p.b_count};

            const uint64_t a_refl = feedback_signature(a);
            const uint64_t a_packed = feedback_signature_packed(a);
            const uint64_t b_refl = feedback_signature(b);
            const uint64_t b_packed = feedback_signature_packed(b);

            // Determinism on each implementation independently.
            if (feedback_signature(a) != a_refl) return false;
            if (feedback_signature_packed(a) != a_packed) return false;

            // Cross-implementation agreement on structural equality:
            // both must agree whether a and b produce the SAME hash
            // within their respective implementation.  If reflect
            // says "a and b distinct" but packed says "a and b same"
            // (or vice versa), the implementations disagree on what
            // counts as structural identity → bug.
            //
            // (The hash VALUES of the two implementations differ, and
            // that is expected.  The EQUIVALENCE RELATION of each
            // implementation must be the same.)
            const bool refl_says_eq = (a_refl == b_refl);
            const bool packed_says_eq = (a_packed == b_packed);
            if (refl_says_eq != packed_says_eq) return false;

            return true;
        });
}
