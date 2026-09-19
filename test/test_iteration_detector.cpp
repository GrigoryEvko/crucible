// End-to-end tests drive the detector, but only along its ordinary
// path.  What needs direct coverage is the awkward part of the state
// machine: a break in the middle of a match, an overlapping restart,
// and the two-match confirmation before a boundary fires.

#include <crucible/IterationDetector.h>
#include <crucible/IterationDetectorState.h>
#include <crucible/safety/_ScopedView.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using crucible::IterationDetector;
using crucible::SchemaHash;

static SchemaHash H(uint64_t v) { return SchemaHash{v}; }

// Returns the index at which check() first returns true, or UINT32_MAX
// if it never does.
template <std::size_t N>
static uint32_t first_boundary(IterationDetector& d, const SchemaHash (&seq)[N]) {
    for (uint32_t i = 0; i < N; i++) {
        if (d.check(seq[i])) return i;
    }
    return UINT32_MAX;
}

static void test_signature_build_requires_K_ops() {
    IterationDetector d;
    // Four ops is one short of a full signature.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.signature_len.get() == 4);
    assert(!d.check(H(5)));
    assert(d.signature_len.get() == 5);
    std::printf("  test_signature_build:         PASSED\n");
}

static void test_first_match_is_candidate_not_boundary() {
    IterationDetector d;
    // Five ops make one signature.
    SchemaHash sig[5] = {H(10), H(20), H(30), H(40), H(50)};
    for (auto h : sig)
        assert(!d.check(h));
    // The first repeat only proposes a candidate.  No boundary yet.
    assert(!d.check(H(10)));
    assert(!d.check(H(20)));
    assert(!d.check(H(30)));
    assert(!d.check(H(40)));
    assert(!d.check(H(50)));  // the fifth match confirms and still returns false
    assert(d.confirmed);
    std::printf("  test_first_match_candidate:   PASSED\n");
}

static void test_second_match_is_boundary() {
    IterationDetector d;
    SchemaHash sig[5] = {H(100), H(200), H(300), H(400), H(500)};
    for (auto h : sig)
        assert(!d.check(h));
    for (auto h : sig)
        assert(!d.check(h));
    assert(d.confirmed);
    assert(d.boundaries_detected.get() == 0);
    // The second repeat is what fires a boundary, on its last op.
    assert(!d.check(sig[0]));
    assert(!d.check(sig[1]));
    assert(!d.check(sig[2]));
    assert(!d.check(sig[3]));
    assert(d.check(sig[4]));
    assert(d.boundaries_detected.get() == 1);
    std::printf("  test_second_match_boundary:   PASSED\n");
}

static void test_mid_match_break_resets_cleanly() {
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig)
        assert(!d.check(h));
    for (auto h : sig)
        assert(!d.check(h));
    assert(d.confirmed);

    // Three ops of the signature, then a stranger.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(999)));  // the break
    // Matching resumes from zero, so a full signature is needed again.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.check(H(5)));  // boundary after full match from scratch
    std::printf("  test_mid_match_break:         PASSED\n");
}

static void test_overlap_at_boundary() {
    // Seeing the first signature op where the fourth was expected must
    // start a fresh match at position one, because the op just seen
    // counts towards the new attempt rather than being discarded.
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig)
        assert(!d.check(h));
    for (auto h : sig)
        assert(!d.check(h));
    assert(d.confirmed);

    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(1)));  // the overlapping restart, leaving position one
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.check(H(5)));  // and only four more ops are needed, not five
    std::printf("  test_overlap_at_boundary:     PASSED\n");
}

static void test_reset_clears_everything() {
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig)
        (void)d.check(h);
    for (auto h : sig)
        (void)d.check(h);
    assert(d.confirmed);

    d.reset();
    assert(!d.confirmed);
    assert(d.signature_len.get() == 0);
    assert(d.boundaries_detected.get() == 0);
    assert(d.ops_since_boundary.get() == 0);
    assert(d.last_completed_len == 0);
    // A fresh detector has to rebuild its signature from nothing.
    for (uint32_t i = 0; i < 4; i++)
        assert(!d.check(H(100 + i)));
    assert(d.signature_len.get() == 4);
    std::printf("  test_reset:                   PASSED\n");
}

static void test_ops_since_boundary_counts_correctly() {
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig)
        (void)d.check(h);
    for (auto h : sig)
        (void)d.check(h);
    // Confirming a candidate rewinds the counter to the signature length.
    assert(d.ops_since_boundary.get() == IterationDetector::K);
    for (uint64_t i = 0; i < 10; i++)
        (void)d.check(H(1000 + i));
    assert(d.ops_since_boundary.get() == IterationDetector::K + 10);
    // When the boundary fires, the completed length is the counter minus
    // the signature, which is the iteration length with its own
    // signature ops included.
    for (auto h : sig)
        (void)d.check(h);
    assert(d.ops_since_boundary.get() == IterationDetector::K);
    assert(d.last_completed_len == 10 + IterationDetector::K);
    std::printf("  test_ops_since_boundary:      PASSED\n");
}

static void test_typestate_witness_minting() {
    // Minting a view in the state that matches the detector's phase must
    // succeed.  The mismatched cases are pinned by negative-compile
    // fixtures, and the two halves together bracket the mint's gate.
    using crucible::iter_det_state::Building;
    using crucible::iter_det_state::Steady;
    using crucible::safety::mint_view;

    IterationDetector d;
    // A signature shorter than K puts the detector in Building.
    assert(d.signature_len.get() == 0);
    {
        auto building_view = mint_view<Building>(d);
        assert(&building_view.carrier() == &d);
        assert(building_view->signature_len.get() == 0);
    }

    // A full signature puts it in Steady.
    SchemaHash sig[5] = {H(11), H(22), H(33), H(44), H(55)};
    for (auto h : sig)
        (void)d.check(h);
    assert(d.signature_len.get() == IterationDetector::K);
    {
        auto steady_view = mint_view<Steady>(d);
        assert(&steady_view.carrier() == &d);
        assert(steady_view->signature_len.get() == IterationDetector::K);
    }

    d.reset();
    assert(d.signature_len.get() == 0);
    {
        auto rebuilt_view = mint_view<Building>(d);
        assert(&rebuilt_view.carrier() == &d);
        assert(rebuilt_view->signature_len.get() == 0);
    }
    std::printf("  test_typestate_witness:       PASSED\n");
}

static void test_cache_line_layout_is_stable() {
    // The detector occupies exactly two cache lines with its hot fields
    // on the first.  Nothing else would fail loudly if that changed.
    static_assert(sizeof(IterationDetector) == 128, "IterationDetector must be 2 cache lines (128 B)");
    static_assert(offsetof(IterationDetector, expected_hash_) == 0);
    static_assert(offsetof(IterationDetector, signature) == 8);
    static_assert(offsetof(IterationDetector, match_pos_) == 48);
    static_assert(offsetof(IterationDetector, boundaries_detected) == 64);
    std::printf("  test_layout:                  PASSED (128 B)\n");
}

int main() {
    test_signature_build_requires_K_ops();
    test_first_match_is_candidate_not_boundary();
    test_second_match_is_boundary();
    test_mid_match_break_resets_cleanly();
    test_overlap_at_boundary();
    test_reset_clears_everything();
    test_ops_since_boundary_counts_correctly();
    test_typestate_witness_minting();
    test_cache_line_layout_is_stable();
    std::printf("test_iteration_detector: 9 groups, all passed\n");
    return 0;
}
