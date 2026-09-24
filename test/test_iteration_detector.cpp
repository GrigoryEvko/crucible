// End-to-end tests drive the detector, but only along its ordinary path.
// What needs direct coverage is where a signature match and an iteration
// boundary disagree: a signature that occurs more than once per iteration,
// a body built from repeated layers, a period that stops holding, and the
// refuted period that must survive a restart.

#include <crucible/IterationDetector.h>
#include <crucible/IterationDetectorState.h>
#include <crucible/safety/_ScopedView.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <vector>

using crucible::IterationDetector;
using crucible::SchemaHash;
using crucible::ShapeHash;

namespace {

constexpr uint32_t K = IterationDetector::K;

SchemaHash H(uint64_t v) { return SchemaHash{v}; }

struct Op {
    uint64_t schema = 0;
    uint64_t shape = 0;
};

// Every boundary the detector reports over a stream, as the absolute op
// index at which check() returned true and the length it reported.
struct Boundary {
    uint64_t op_index = 0;
    uint32_t length = 0;
};

std::vector<Boundary> feed(IterationDetector& detector, const std::vector<Op>& body, uint32_t iterations,
                           uint64_t first_op_index = 0, uint32_t first_offset = 0) {
    std::vector<Boundary> boundaries;
    uint64_t op_index = first_op_index;
    const uint64_t total = uint64_t{iterations} * body.size();
    for (uint64_t n = 0; n < total; ++n, ++op_index) {
        const Op& op = body[(first_offset + n) % body.size()];
        if (detector.check(SchemaHash{op.schema}, ShapeHash{op.shape}))
            boundaries.push_back({op_index, detector.last_completed_len});
    }
    return boundaries;
}

// The smallest period of the stream body^infinity that is K or more: the
// smallest multiple of the primitive period of the body. O(L^2).
uint32_t expected_period(const std::vector<Op>& body) {
    const auto length = static_cast<uint32_t>(body.size());
    uint32_t primitive = length;
    for (uint32_t divisor = 1; divisor < length; ++divisor) {
        if (length % divisor != 0) continue;
        bool is_period = true;
        for (uint32_t i = 0; i < length && is_period; ++i) {
            is_period = body[i].schema == body[(i + divisor) % length].schema
                        && body[i].shape == body[(i + divisor) % length].shape;
        }
        if (is_period) {
            primitive = divisor;
            break;
        }
    }
    uint32_t period = primitive;
    while (period < K)
        period += primitive;
    return period;
}

// How many of the body's circular K-op windows equal the one at phase. O(L*K).
uint32_t window_count(const std::vector<Op>& body, uint32_t phase) {
    const auto length = static_cast<uint32_t>(body.size());
    uint32_t count = 0;
    for (uint32_t other = 0; other < length; ++other) {
        bool same = true;
        for (uint32_t i = 0; i < K && same; ++i) {
            const Op& a = body[(phase + i) % length];
            const Op& b = body[(other + i) % length];
            same = a.schema == b.schema && a.shape == b.shape;
        }
        count += same ? 1u : 0u;
    }
    return count;
}

// Checks that a stream settles on its true period: the tail of the report is
// all one length, that length is the true period, and consecutive reports are
// exactly one period apart.
void assert_settles_on(const std::vector<Boundary>& boundaries, uint32_t period, size_t tail) {
    assert(boundaries.size() > tail);
    for (size_t i = boundaries.size() - tail; i < boundaries.size(); ++i) {
        assert(boundaries[i].length == period);
        assert(boundaries[i].op_index - boundaries[i - 1].op_index == period);
    }
}

uint64_t next_random(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// The ops PyTorch records for one no_grad forward of Linear(128,128)+ReLU x3
// then Linear(128,10), as the recording shows them: aten::linear records ten
// nested ops in post-order, ReLU records two. Schema and shape hashes are the
// recorded ones. Layers 0 to 2 are identical op for op, so the signature
// matches at every layer and any two adjacent layers form a square.
std::vector<Op> inference_mlp_body() {
    constexpr uint64_t AS_STRIDED = 0xd5788a09ae51260bULL;
    constexpr uint64_t TRANSPOSE = 0xcf885c3f9f1760a9ULL;
    constexpr uint64_t T = 0x5c094a679434c76dULL;
    constexpr uint64_t EXPAND = 0x08b1975c9ab62e93ULL;
    constexpr uint64_t COPY = 0x2370b5ef15fdbe37ULL;
    constexpr uint64_t RESOLVE_CONJ = 0x6d1be48448f5b708ULL;
    constexpr uint64_t ADDMM = 0xc3aa7240d6723aacULL;
    constexpr uint64_t LINEAR = 0xbaac84e811a921f6ULL;
    constexpr uint64_t CLAMP_MIN = 0x7452edb687fccec9ULL;
    constexpr uint64_t RELU = 0x4e408bc8ab40f455ULL;

    const std::vector<Op> hidden = {
        {AS_STRIDED, 0xe8173d7fadd93585ULL}, {TRANSPOSE, 0xe8173d7fadd93585ULL}, {T, 0xe8173d7fadd93585ULL},
        {AS_STRIDED, 0xcffdb162079a442cULL}, {EXPAND, 0xcffdb162079a442cULL},    {COPY, 0x2441b799b8336b25ULL},
        {RESOLVE_CONJ, 0x48cf127885f01d25ULL}, {RESOLVE_CONJ, 0x48cf127885f01d25ULL}, {ADDMM, 0x1ec472dd81226a98ULL},
        {LINEAR, 0xf96bd5f4ed8bab4cULL},     {CLAMP_MIN, 0x48cf127885f01d25ULL}, {RELU, 0x48cf127885f01d25ULL},
    };
    const std::vector<Op> head = {
        {AS_STRIDED, 0x7ef93346e220078fULL}, {TRANSPOSE, 0x7ef93346e220078fULL}, {T, 0x7ef93346e220078fULL},
        {AS_STRIDED, 0x98b982a64e5976e6ULL}, {EXPAND, 0x98b982a64e5976e6ULL},    {COPY, 0xf67909131a9e601dULL},
        {RESOLVE_CONJ, 0x0137544d7ba3f1efULL}, {RESOLVE_CONJ, 0x48cf127885f01d25ULL}, {ADDMM, 0x79016729776b50c0ULL},
        {LINEAR, 0xd6d938c419a8e840ULL},
    };

    std::vector<Op> body;
    for (int layer = 0; layer < 3; ++layer)
        body.insert(body.end(), hidden.begin(), hidden.end());
    body.insert(body.end(), head.begin(), head.end());
    return body;
}

void test_signature_build_requires_K_ops() {
    IterationDetector d;
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(4)));
    assert(d.signature_len.get() == 4);
    assert(!d.check(H(5)));
    assert(d.signature_len.get() == 5);
    std::printf("  test_signature_build:            PASSED\n");
}

void test_boundary_needs_two_equal_iterations() {
    IterationDetector d;
    const SchemaHash sig[5] = {H(100), H(200), H(300), H(400), H(500)};
    for (auto h : sig)
        assert(!d.check(h));
    // One repeat is one iteration and nothing to compare it with.
    for (auto h : sig)
        assert(!d.check(h));
    assert(!d.confirmed);
    assert(d.boundaries_detected.get() == 0);
    // The third copy completes the square. The boundary fires on its last op.
    assert(!d.check(sig[0]));
    assert(!d.check(sig[1]));
    assert(!d.check(sig[2]));
    assert(!d.check(sig[3]));
    assert(d.check(sig[4]));
    assert(d.confirmed);
    assert(d.last_completed_len == 5);
    assert(d.boundaries_detected.get() == 1);
    assert(d.ops_since_boundary.get() == K);
    std::printf("  test_boundary_two_iterations:    PASSED\n");
}

void test_unequal_iterations_do_not_fire() {
    // Three signature matches whose gaps differ. The old matcher reported a
    // boundary here, of whatever length the last gap had.
    IterationDetector d;
    const SchemaHash sig[5] = {H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig)
        assert(!d.check(h));
    for (auto h : sig)
        assert(!d.check(h));
    for (uint64_t i = 0; i < 10; i++)
        assert(!d.check(H(1000 + i)));
    for (auto h : sig)
        assert(!d.check(h));
    assert(d.boundaries_detected.get() == 0);
    assert(!d.confirmed);
    std::printf("  test_unequal_iterations:         PASSED\n");
}

void test_broken_period_is_refuted() {
    IterationDetector d;
    std::vector<Op> body = {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}};
    auto boundaries = feed(d, body, 4);
    assert(!boundaries.empty());
    assert(d.confirmed);

    // The next iteration breaks after three ops.
    assert(!d.check(H(1)));
    assert(!d.check(H(2)));
    assert(!d.check(H(3)));
    assert(!d.check(H(999)));
    assert(!d.check(H(1)));
    assert(!d.confirmed);
    assert(d.refuted_count_ == 1);
    std::printf("  test_broken_period:              PASSED\n");
}

void test_sub_period_signature_mlp_aten_level() {
    // The eleven ATen ops of the inference MLP, schema only: t, addmm, relu
    // three times, then t, addmm. The signature t addmm relu t addmm matches
    // at offsets 0, 3 and 6 of every iteration. The old matcher reported
    // boundaries of 6 and 5 ops in turn, and every other replay diverged.
    std::vector<Op> body;
    for (int layer = 0; layer < 3; ++layer) {
        body.push_back({1, 0});
        body.push_back({2, 0});
        body.push_back({3, 0});
    }
    body.push_back({1, 0});
    body.push_back({2, 0});

    IterationDetector d;
    auto boundaries = feed(d, body, 40);
    assert(!boundaries.empty());
    for (const auto& boundary : boundaries)
        assert(boundary.length == 11);
    // The square is complete after two iterations. The first boundary then
    // waits for the earliest window that occurs once per iteration, t addmm
    // relu t addmm occurs three times, and 2 3 1 2 1 at offset 7 once.
    assert(boundaries.front().op_index == 2 * 11 + 7 + K - 1);
    assert_settles_on(boundaries, 11, boundaries.size() - 1);
    std::printf("  test_sub_period_aten_level:      PASSED\n");
}

void test_repeated_layers_recorded_mlp() {
    // The recorded stream. Layers 0 and 1 form a square of 12 ops, so the
    // detector may accept 12 first. Layer 3 breaks that period, the period is
    // refuted, and the detector settles on the 46-op iteration.
    const auto body = inference_mlp_body();
    assert(body.size() == 46);

    IterationDetector d;
    auto boundaries = feed(d, body, 30);
    assert_settles_on(boundaries, 46, 20);
    size_t wrong = 0;
    for (const auto& boundary : boundaries)
        wrong += boundary.length != 46 ? 1u : 0u;
    assert(wrong <= 2);

    // The iteration begins at a window that occurs once per period, so the
    // foreground cannot align at a hidden layer. Every such window touches
    // the head layer, and the earliest one starts four ops before it.
    assert(d.phase_is_unique);
    const uint64_t iteration_start = boundaries.back().op_index + 1 - K;
    const auto phase = static_cast<uint32_t>(iteration_start % 46);
    assert(window_count(body, phase) == 1);
    assert(phase == 32);
    std::printf("  test_repeated_layers_mlp:        PASSED (%zu early boundaries)\n", wrong);
}

void test_refuted_period_survives_divergence_restart() {
    // What the runtime does: the detector accepts the layer square, the
    // foreground replays it, diverges at the head layer and restarts the
    // detector from the op after the divergent one. The restart lands inside
    // an iteration. Without the refuted set the detector finds the same
    // layer square again from there, and the cycle never ends.
    const auto body = inference_mlp_body();
    IterationDetector d;

    uint64_t op_index = 0;
    bool accepted_layer = false;
    for (; op_index < 46 * 4; ++op_index) {
        const Op& op = body[op_index % 46];
        if (d.check(SchemaHash{op.schema}, ShapeHash{op.shape}) && d.last_completed_len == 12) {
            accepted_layer = true;
            break;
        }
    }
    assert(accepted_layer);
    assert(d.phase_is_unique);

    d.restart_after_divergence();
    assert(d.refuted_count_ == 1);
    assert(d.signature_len.get() == 0);

    // Resume one op into a hidden layer.
    auto boundaries = feed(d, body, 20, 0, 13);
    assert(!boundaries.empty());
    for (const auto& boundary : boundaries)
        assert(boundary.length == 46);
    assert_settles_on(boundaries, 46, boundaries.size() - 1);
    std::printf("  test_refuted_survives_restart:   PASSED\n");
}

void test_divergence_after_a_second_period_refutes_nothing() {
    // The layer square is reported and published, the detector then sees it
    // break and accepts the true period. The foreground was replaying the
    // layer region, and that is what diverges. The restart must not refute
    // the true period, or the next search can only find two iterations.
    const auto body = inference_mlp_body();
    for (const bool true_period_reported : {false, true}) {
        IterationDetector d;
        uint64_t op_index = 0;
        for (; op_index < 46 * 6; ++op_index) {
            const Op& op = body[op_index % 46];
            const bool fired = d.check(SchemaHash{op.schema}, ShapeHash{op.shape});
            if (d.period_ == 46 && (!true_period_reported || fired)) break;
        }
        assert(d.period_ == 46);
        assert(d.refuted_count_ == 1);

        d.restart_after_divergence();
        assert(d.refuted_count_ == 1);
        auto boundaries = feed(d, body, 20, 0, 13);
        for (const auto& boundary : boundaries)
            assert(boundary.length == 46);
        assert_settles_on(boundaries, 46, boundaries.size() - 1);
    }
    std::printf("  test_second_period_not_refuted:  PASSED\n");
}

void test_gap_in_the_recording_does_not_refute() {
    // The foreground records nothing while it aligns or replays, so the
    // stream the detector sees can skip ops with no restart. The gap breaks
    // the true period once. It must hold again afterwards.
    std::vector<Op> body;
    for (uint64_t i = 0; i < 8; ++i)
        body.push_back({0x100 + i, 0x200 + i});
    IterationDetector d;
    auto before = feed(d, body, 4);
    assert(!before.empty());
    // Resume three ops into an iteration.
    auto after = feed(d, body, 20, 0, 3);
    assert(!d.is_refuted(8, d.period_body_sum_));
    assert_settles_on(after, 8, 10);
    std::printf("  test_gap_does_not_refute:        PASSED\n");
}

void test_short_periods() {
    // A period below K gets its smallest multiple that is K or more.
    const std::vector<std::vector<Op>> bodies = {
        {{1, 0}},                                         // 1 -> 5
        {{1, 0}, {2, 0}},                                 // 2 -> 6
        {{1, 0}, {2, 0}, {3, 0}},                         // 3 -> 6
        {{1, 0}, {2, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 0}}, // 6
        {{1, 0}, {2, 0}, {1, 0}, {2, 0}, {1, 0}, {2, 0}, {3, 0}},  // 7
        {{1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {2, 0}},  // 6, the signature is five equal ops
    };
    const uint32_t expected[] = {5, 6, 6, 6, 7, 6};
    for (size_t i = 0; i < bodies.size(); ++i) {
        assert(expected_period(bodies[i]) == expected[i]);
        IterationDetector d;
        const auto iterations = static_cast<uint32_t>(400 / bodies[i].size());
        auto boundaries = feed(d, bodies[i], iterations);
        assert_settles_on(boundaries, expected[i], 10);
    }
    std::printf("  test_short_periods:              PASSED\n");
}

void test_random_bodies_settle_on_true_period() {
    // Bodies over a three-letter alphabet, so the signature recurs inside
    // the body and layer squares occur by chance. Every stream must settle on
    // its true period whatever offset it starts at.
    uint64_t state = 42;
    uint32_t streams = 0;
    for (uint32_t length = 3; length <= 24; ++length) {
        for (uint32_t trial = 0; trial < 40; ++trial) {
            std::vector<Op> body(length);
            for (auto& op : body)
                op.schema = 1 + next_random(state) % 3;
            const uint32_t period = expected_period(body);
            const auto offset = static_cast<uint32_t>(next_random(state) % length);

            IterationDetector d;
            const uint32_t iterations = 12 + 2400 / length;
            auto boundaries = feed(d, body, iterations, 0, offset);
            assert_settles_on(boundaries, period, 8);
            ++streams;
        }
    }
    std::printf("  test_random_bodies:              PASSED (%u streams)\n", streams);
}

void test_restart_without_unique_phase_refutes_nothing() {
    // A stream of one repeated op has no window that occurs once per period.
    // A divergence then says nothing about the period, so it stays eligible.
    IterationDetector d;
    auto boundaries = feed(d, {{7, 0}}, 30);
    assert(!boundaries.empty());
    assert(!d.phase_is_unique);
    d.restart_after_divergence();
    assert(d.refuted_count_ == 0);
    boundaries = feed(d, {{7, 0}}, 30);
    assert_settles_on(boundaries, 5, 3);
    std::printf("  test_restart_no_unique_phase:    PASSED\n");
}

void test_reset_clears_everything() {
    IterationDetector d;
    auto boundaries = feed(d, inference_mlp_body(), 6);
    assert(!boundaries.empty());
    assert(d.refuted_count_ > 0);

    d.reset();
    assert(!d.confirmed);
    assert(d.signature_len.get() == 0);
    assert(d.boundaries_detected.get() == 0);
    assert(d.ops_since_boundary.get() == 0);
    assert(d.last_completed_len == 0);
    assert(d.refuted_count_ == 0);
    for (uint32_t i = 0; i < 4; i++)
        assert(!d.check(H(100 + i)));
    assert(d.signature_len.get() == 4);
    std::printf("  test_reset:                      PASSED\n");
}

void test_typestate_witness_minting() {
    // Minting a view in the state that matches the detector's phase must
    // succeed.  The mismatched cases are pinned by negative-compile
    // fixtures, and the two halves together bracket the mint's gate.
    using crucible::iter_det_state::Building;
    using crucible::iter_det_state::Steady;
    using crucible::safety::mint_view;

    IterationDetector d;
    assert(d.signature_len.get() == 0);
    {
        auto building_view = mint_view<Building>(d);
        assert(&building_view.carrier() == &d);
        assert(building_view->signature_len.get() == 0);
    }

    const SchemaHash sig[5] = {H(11), H(22), H(33), H(44), H(55)};
    for (auto h : sig)
        (void)d.check(h);
    assert(d.signature_len.get() == K);
    {
        auto steady_view = mint_view<Steady>(d);
        assert(&steady_view.carrier() == &d);
        assert(steady_view->signature_len.get() == K);
    }

    d.reset();
    assert(d.signature_len.get() == 0);
    {
        auto rebuilt_view = mint_view<Building>(d);
        assert(&rebuilt_view.carrier() == &d);
        assert(rebuilt_view->signature_len.get() == 0);
    }
    std::printf("  test_typestate_witness:          PASSED\n");
}

}  // namespace

int main() {
    test_signature_build_requires_K_ops();
    test_boundary_needs_two_equal_iterations();
    test_unequal_iterations_do_not_fire();
    test_broken_period_is_refuted();
    test_sub_period_signature_mlp_aten_level();
    test_repeated_layers_recorded_mlp();
    test_refuted_period_survives_divergence_restart();
    test_divergence_after_a_second_period_refutes_nothing();
    test_gap_in_the_recording_does_not_refute();
    test_short_periods();
    test_random_bodies_settle_on_true_period();
    test_restart_without_unique_phase_refutes_nothing();
    test_reset_clears_everything();
    test_typestate_witness_minting();
    std::printf("test_iteration_detector: 14 groups, all passed\n");
    return 0;
}
