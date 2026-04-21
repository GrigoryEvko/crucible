// Direct tests for IterationDetector's state machine.  The detector
// is covered indirectly via bench/end-to-end tests, but its subtle
// paths (mid-match break with overlap, two-match confirmation, K=5
// signature build) deserve explicit coverage.

#include <crucible/IterationDetector.h>

#include <cassert>
#include <cstdint>
#include <cstdio>

using crucible::IterationDetector;
using crucible::SchemaHash;

static SchemaHash H(uint64_t v) { return SchemaHash{v}; }

// Feed a sequence; return the index at which check() first returned true,
// or UINT32_MAX if never.
template <std::size_t N>
static uint32_t first_boundary(IterationDetector& d, const SchemaHash (&seq)[N]) {
    for (uint32_t i = 0; i < N; i++) {
        if (d.check(seq[i])) return i;
    }
    return UINT32_MAX;
}

static void test_signature_build_requires_K_ops() {
    IterationDetector d;
    // Feed K-1 ops; detector is still building signature.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.signature_len == 4);
    assert(!d.check(H(5)));
    assert(d.signature_len == 5);
    std::printf("  test_signature_build:         PASSED\n");
}

static void test_first_match_is_candidate_not_boundary() {
    IterationDetector d;
    // K=5 ops build the signature.
    SchemaHash sig[5] = {H(10), H(20), H(30), H(40), H(50)};
    for (auto h : sig) assert(!d.check(h));
    // Repeating the signature → first match → candidate, NOT boundary.
    assert(!d.check(H(10)));
    assert(!d.check(H(20)));
    assert(!d.check(H(30)));
    assert(!d.check(H(40)));
    assert(!d.check(H(50)));  // K-th match — confirm candidate, return false
    assert(d.confirmed);
    std::printf("  test_first_match_candidate:   PASSED\n");
}

static void test_second_match_is_boundary() {
    IterationDetector d;
    SchemaHash sig[5] = {H(100), H(200), H(300), H(400), H(500)};
    for (auto h : sig) assert(!d.check(h));
    // First repeat → candidate.
    for (auto h : sig) assert(!d.check(h));
    assert(d.confirmed);
    assert(d.boundaries_detected.get() == 0);
    // Second repeat → BOUNDARY (return true on K-th op).
    assert(!d.check(sig[0]));
    assert(!d.check(sig[1]));
    assert(!d.check(sig[2]));
    assert(!d.check(sig[3]));
    assert(d.check(sig[4]));   // ← true
    assert(d.boundaries_detected.get() == 1);
    std::printf("  test_second_match_boundary:   PASSED\n");
}

static void test_mid_match_break_resets_cleanly() {
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig) assert(!d.check(h));
    // Build confirmed state.
    for (auto h : sig) assert(!d.check(h));
    assert(d.confirmed);

    // Mid-match break: match 3 ops of signature, then feed a stranger.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(999)));  // break
    // Now must resume matching from zero; restart if next op is sig[0].
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.check(H(5)));   // boundary after full match from scratch
    std::printf("  test_mid_match_break:         PASSED\n");
}

static void test_overlap_at_boundary() {
    // Signature [a, b, c, d, e].  If we're mid-match at position 3
    // and see 'a' instead of 'd', the detector must start a fresh
    // match at position 1 (the 'a' we just saw counts toward the
    // next attempt).
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig) assert(!d.check(h));
    for (auto h : sig) assert(!d.check(h));   // confirm
    assert(d.confirmed);

    // Consume sig[0..2], then feed sig[0] again — must not reset fully.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(1)));  // overlapping restart — match_pos_ → 1
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.check(H(5)));   // boundary fires after clean match
    std::printf("  test_overlap_at_boundary:     PASSED\n");
}

static void test_reset_clears_everything() {
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig) (void)d.check(h);
    for (auto h : sig) (void)d.check(h);
    assert(d.confirmed);

    d.reset();
    assert(!d.confirmed);
    assert(d.signature_len == 0);
    assert(d.boundaries_detected.get() == 0);
    assert(d.ops_since_boundary == 0);
    assert(d.last_completed_len == 0);
    // Detector is fresh: must rebuild signature.
    for (uint32_t i = 0; i < 4; i++) assert(!d.check(H(100 + i)));
    assert(d.signature_len == 4);
    std::printf("  test_reset:                   PASSED\n");
}

static void test_ops_since_boundary_counts_correctly() {
    IterationDetector d;
    SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig) (void)d.check(h);
    for (auto h : sig) (void)d.check(h);   // candidate
    // After candidate confirmation, counter reset to K.
    assert(d.ops_since_boundary == IterationDetector::K);
    // Feed 10 more ops, then boundary fires.
    for (uint64_t i = 0; i < 10; i++) (void)d.check(H(1000 + i));
    assert(d.ops_since_boundary == IterationDetector::K + 10);
    // Trigger second match — last_completed_len = ops_since_boundary - K
    // at fire time (= iteration length including the K signature ops).
    for (auto h : sig) (void)d.check(h);
    assert(d.ops_since_boundary == IterationDetector::K);
    assert(d.last_completed_len == 10 + IterationDetector::K);
    std::printf("  test_ops_since_boundary:      PASSED\n");
}

static void test_cache_line_layout_is_stable() {
    // Load-bearing claim: IterationDetector is exactly 2 cache lines,
    // hot fields on line 0.  Codegen tests would break silently
    // without this static_assert.
    static_assert(sizeof(IterationDetector) == 128,
                  "IterationDetector must be 2 cache lines (128 B)");
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
    test_cache_line_layout_is_stable();
    std::printf("test_iteration_detector: 8 groups, all passed\n");
    return 0;
}
