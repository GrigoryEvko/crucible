// End-to-end tests drive the detector, but only along its ordinary path.
// What needs direct coverage is where a signature match and an iteration
// boundary disagree: a signature that occurs more than once per iteration,
// a body built from repeated layers, a run of equal groups inside one
// iteration, a period that stops holding, and the refuted period that must
// survive a restart.

#include <crucible/IterationDetector.h>
#include <crucible/IterationDetectorState.h>
#include <fixy/ScopedView.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::IterationDetector;
using crucible::SchemaHash;
using crucible::ShapeHash;

namespace {

constexpr uint32_t K = IterationDetector::K;

// The allocation capability of each detector of the test.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

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

// The boundaries of one feed.  The longest stream of the test has 2688 ops,
// and each period has K ops or more, so a feed reports at most 538.
inline constexpr std::size_t kBoundaryCapacity = 1024;
using Boundaries = ::fixy::FixedList<Boundary, kBoundaryCapacity>;

// The ops of the recorded MLP of inference_mlp_body.
inline constexpr std::size_t kMlpOps = 46;

// The count of the iterations of a body in a feed.
enum class Iterations : std::uint32_t {
};

// The absolute index of the first op of a feed in the stream of the detector.
enum class StreamOp : std::uint64_t {
};

// A position in a body, such as the first op of a feed or the first op of a
// window.
enum class BodyOffset : std::uint32_t {
};

// A period of a stream, as a count of ops.
enum class Period : std::uint32_t {
};

// The count of the last boundaries that assert_settles_on checks.
enum class TailCount : std::size_t {
};

// The op of the body at position, which wraps around the end of the body.
[[nodiscard]] Op op_at(::fixy::View<Op const> body, BodyOffset position) {
    return *body.get(std::to_underlying(position) % body.size()).expect("a position modulo the size is in the body");
}

// The boundaries of iterations copies of body, from the op of body at
// first_offset.  The first op of the stream has the index first_op_index.
[[nodiscard]] Boundaries feed(IterationDetector& detector, ::fixy::View<Op const> body, Iterations iterations,
                              StreamOp first_op_index = StreamOp{0}, BodyOffset first_offset = BodyOffset{0}) {
    Boundaries boundaries{};
    uint64_t op_index = std::to_underlying(first_op_index);
    const uint64_t total = uint64_t{std::to_underlying(iterations)} * body.size();
    for (uint64_t n = 0; n < total; ++n, ++op_index) {
        const Op op = *body.get((std::to_underlying(first_offset) + n) % body.size())
                           .expect("a position modulo the size is in the body");
        if (detector.check(kAlloc, SchemaHash{op.schema}, ShapeHash{op.shape}))
            static_cast<void>(boundaries.append(Boundary{op_index, detector.last_completed_len})
                                  .expect("the boundaries of one feed fit kBoundaryCapacity"));
    }
    return boundaries;
}

// The boundary at position of a feed.
[[nodiscard]] Boundary boundary_at(Boundaries const& boundaries, std::size_t position) {
    return *boundaries.get(position).expect("the feed reported the boundary");
}

// The smallest period of the stream body^infinity that is K or more: the
// smallest multiple of the primitive period of the body. O(L^2).
[[nodiscard]] Period expected_period(::fixy::View<Op const> body) {
    const auto length = static_cast<uint32_t>(body.size());
    uint32_t primitive = length;
    for (uint32_t divisor = 1; divisor < length; ++divisor) {
        if (length % divisor != 0) continue;
        bool is_period = true;
        for (uint32_t i = 0; i < length && is_period; ++i) {
            const Op here = op_at(body, BodyOffset{i});
            const Op there = op_at(body, BodyOffset{i + divisor});
            is_period = here.schema == there.schema && here.shape == there.shape;
        }
        if (is_period) {
            primitive = divisor;
            break;
        }
    }
    uint32_t period = primitive;
    while (period < K)
        period += primitive;
    return Period{period};
}

// How many of the body's circular K-op windows equal the one at phase. O(L*K).
[[nodiscard]] uint32_t window_count(::fixy::View<Op const> body, BodyOffset phase) {
    const auto length = static_cast<uint32_t>(body.size());
    uint32_t count = 0;
    for (uint32_t other = 0; other < length; ++other) {
        bool same = true;
        for (uint32_t i = 0; i < K && same; ++i) {
            const Op a = op_at(body, BodyOffset{std::to_underlying(phase) + i});
            const Op b = op_at(body, BodyOffset{other + i});
            same = a.schema == b.schema && a.shape == b.shape;
        }
        count += same ? 1u : 0u;
    }
    return count;
}

// Checks that a stream settles on its true period: the tail of the report is
// all one length, that length is the true period, and consecutive reports are
// exactly one period apart.
void assert_settles_on(Boundaries const& boundaries, Period period, TailCount tail) {
    CRUCIBLE_TEST_ASSERT(kTest, boundaries.size() > std::to_underlying(tail));
    for (size_t i = boundaries.size() - std::to_underlying(tail); i < boundaries.size(); ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, boundary_at(boundaries, i).length == std::to_underlying(period));
        CRUCIBLE_TEST_ASSERT(kTest, boundary_at(boundaries, i).op_index - boundary_at(boundaries, i - 1).op_index
                                        == std::to_underlying(period));
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
[[nodiscard]] ::fixy::FixedList<Op, kMlpOps> inference_mlp_body() {
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

    const ::fixy::FixedArray<Op, 12> hidden = {
        Op{AS_STRIDED, 0xe8173d7fadd93585ULL},
        Op{TRANSPOSE, 0xe8173d7fadd93585ULL},
        Op{T, 0xe8173d7fadd93585ULL},
        Op{AS_STRIDED, 0xcffdb162079a442cULL},
        Op{EXPAND, 0xcffdb162079a442cULL},
        Op{COPY, 0x2441b799b8336b25ULL},
        Op{RESOLVE_CONJ, 0x48cf127885f01d25ULL},
        Op{RESOLVE_CONJ, 0x48cf127885f01d25ULL},
        Op{ADDMM, 0x1ec472dd81226a98ULL},
        Op{LINEAR, 0xf96bd5f4ed8bab4cULL},
        Op{CLAMP_MIN, 0x48cf127885f01d25ULL},
        Op{RELU, 0x48cf127885f01d25ULL},
    };
    const ::fixy::FixedArray<Op, 10> head = {
        Op{AS_STRIDED, 0x7ef93346e220078fULL},
        Op{TRANSPOSE, 0x7ef93346e220078fULL},
        Op{T, 0x7ef93346e220078fULL},
        Op{AS_STRIDED, 0x98b982a64e5976e6ULL},
        Op{EXPAND, 0x98b982a64e5976e6ULL},
        Op{COPY, 0xf67909131a9e601dULL},
        Op{RESOLVE_CONJ, 0x0137544d7ba3f1efULL},
        Op{RESOLVE_CONJ, 0x48cf127885f01d25ULL},
        Op{ADDMM, 0x79016729776b50c0ULL},
        Op{LINEAR, 0xd6d938c419a8e840ULL},
    };

    ::fixy::FixedList<Op, kMlpOps> body{};
    for (int layer = 0; layer < 3; ++layer) {
        for (const Op op : hidden)
            static_cast<void>(body.append(op).expect("the MLP has 46 ops"));
    }
    for (const Op op : head)
        static_cast<void>(body.append(op).expect("the MLP has 46 ops"));
    return body;
}

// The ops of one iteration of a loop of aten::_foreach_add over 12 tensors of
// one extent, then a relu of one output, as the CPU path records them.  Each
// tensor gives one group of six ops in post-order (empty_strided, copy_,
// _to_copy, to.dtype, add.Tensor, add.Scalar), so the iteration holds a run
// of 12 equal groups.  Then come _foreach_add, clamp_min and relu.
inline constexpr std::size_t kForeachTensors = 12;
inline constexpr std::size_t kForeachGroupOps = 6;
inline constexpr std::size_t kForeachOps = kForeachTensors * kForeachGroupOps + 3;

[[nodiscard]] ::fixy::FixedList<Op, kForeachOps> foreach_add_body() {
    ::fixy::FixedList<Op, kForeachOps> body{};
    for (std::size_t tensor = 0; tensor < kForeachTensors; ++tensor) {
        for (uint64_t member = 0; member < kForeachGroupOps; ++member)
            static_cast<void>(body.append(Op{0x610 + member, 0x16}).expect("the loop has 75 ops"));
    }
    for (uint64_t tail = 0; tail < 3; ++tail)
        static_cast<void>(body.append(Op{0x620 + tail, 0x16}).expect("the loop has 75 ops"));
    return body;
}

// The ops of one iteration of a loop of aten::_fused_adam_ over 10
// parameters.  The CPU kernel reads each step tensor of rank zero with
// item(), which runs _local_scalar_dense, so the iteration holds a run of
// ten equal pairs, then _fused_adam_ itself.
inline constexpr std::size_t kAdamParams = 10;
inline constexpr std::size_t kAdamOps = 2 * kAdamParams + 1;

[[nodiscard]] ::fixy::FixedList<Op, kAdamOps> fused_adam_body() {
    ::fixy::FixedList<Op, kAdamOps> body{};
    for (std::size_t param = 0; param < kAdamParams; ++param) {
        static_cast<void>(body.append(Op{0x710, 0x0}).expect("the loop has 21 ops"));
        static_cast<void>(body.append(Op{0x711, 0x0}).expect("the loop has 21 ops"));
    }
    static_cast<void>(body.append(Op{0x712, 0x8}).expect("the loop has 21 ops"));
    return body;
}

[[nodiscard]] bool is_same_op(Op left, Op right) { return left.schema == right.schema && left.shape == right.shape; }

// When the foreground of the model takes a region that the detector published.
enum class Uptake : std::uint8_t {
    // At the op after the boundary that published the region.
    AtOnce,
    // At the first op of the next iteration of the loop.
    NextIteration,
};

// What the foreground of the model does with an op.
enum class ForegroundMode : std::uint8_t {
    Records,
    Aligns,
    Replays,
};

// The ops of a region of the model, and the ops that the detector received
// since its last restart.  The longest body of a model run has 75 ops, and a
// run feeds at most 40 iterations of it.
inline constexpr std::size_t kRegionCapacity = 128;
inline constexpr std::size_t kRecordedCapacity = 4096;
using RegionOps = ::fixy::FixedList<Op, kRegionCapacity>;

// The state of a model run at its end.
struct ForegroundRun {
    ForegroundMode mode = ForegroundMode::Records;
    // The length of the region that the foreground replays at the end, or
    // zero when it does not replay.
    std::size_t region_length = 0;
    uint32_t divergences = 0;
};

// Feeds iterations copies of body through a model of the foreground of the
// Vigil.  The foreground records each op into the detector.  A boundary
// publishes the iteration that it completed as a region, and the foreground
// takes it at uptake.  Then it aligns: it records nothing until the last K
// ops equal the first K ops of the region.  Then it replays: it records
// nothing while each op equals the next op of the region.  An op that does
// not is a divergence: the detector restarts, the op is not recorded, and the
// foreground records again from the next op.  Complexity: O(ops * K), with
// O(P) for each published region.
[[nodiscard]] ForegroundRun run_with_foreground(IterationDetector& detector, ::fixy::View<Op const> body,
                                                Iterations iterations, Uptake uptake) {
    ForegroundRun run{};
    ::fixy::FixedList<Op, kRecordedCapacity> recorded{};
    RegionOps region{};
    RegionOps pending{};
    bool has_pending = false;
    uint64_t aligned_ops = 0;
    uint64_t cursor = 0;
    const uint64_t total = uint64_t{std::to_underlying(iterations)} * body.size();
    for (uint64_t n = 0; n < total; ++n) {
        const Op op = op_at(body, BodyOffset{static_cast<uint32_t>(n % body.size())});
        const bool starts_iteration = n % body.size() == 0;
        if (run.mode == ForegroundMode::Records && has_pending && (uptake == Uptake::AtOnce || starts_iteration)) {
            region.clear();
            for (const Op pending_op : pending)
                static_cast<void>(region.append(pending_op).expect("a region fits kRegionCapacity"));
            has_pending = false;
            aligned_ops = 0;
            run.mode = ForegroundMode::Aligns;
        }
        if (run.mode == ForegroundMode::Records) {
            static_cast<void>(recorded.append(op).expect("the ops since a restart fit kRecordedCapacity"));
            if (!detector.check(kAlloc, SchemaHash{op.schema}, ShapeHash{op.shape})) continue;
            const std::size_t length = detector.last_completed_len;
            const std::size_t first = recorded.size() - K - length;
            pending.clear();
            for (std::size_t offset = 0; offset < length; ++offset)
                static_cast<void>(
                    pending.append(op_at(recorded.view(), BodyOffset{static_cast<uint32_t>(first + offset)}))
                        .expect("a region fits kRegionCapacity"));
            has_pending = true;
            continue;
        }
        if (run.mode == ForegroundMode::Aligns) {
            ++aligned_ops;
            if (aligned_ops < K) continue;
            bool is_aligned = true;
            for (uint32_t i = 0; i < K && is_aligned; ++i)
                is_aligned = is_same_op(op_at(body, BodyOffset{static_cast<uint32_t>((n + 1 - K + i) % body.size())}),
                                        op_at(region.view(), BodyOffset{i}));
            if (!is_aligned) continue;
            run.mode = ForegroundMode::Replays;
            cursor = K;
            continue;
        }
        if (is_same_op(op, op_at(region.view(), BodyOffset{static_cast<uint32_t>(cursor % region.size())}))) {
            ++cursor;
            continue;
        }
        ++run.divergences;
        detector.restart_after_divergence();
        recorded.clear();
        has_pending = false;
        run.mode = ForegroundMode::Records;
    }
    run.region_length = run.mode == ForegroundMode::Replays ? region.size() : 0;
    return run;
}

void test_signature_build_requires_K_ops() {
    IterationDetector d;
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(1)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(2)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(3)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(4)));
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 4);
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(5)));
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 5);
    ::fixy::check::pass(kTest, "  test_signature_build:            PASSED\n");
}

void test_boundary_needs_two_equal_iterations() {
    IterationDetector d;
    const ::fixy::FixedArray<SchemaHash, 5> sig{H(100), H(200), H(300), H(400), H(500)};
    for (auto h : sig)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, h));
    // One repeat is one iteration and nothing to compare it with.
    for (auto h : sig)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, h));
    CRUCIBLE_TEST_ASSERT(kTest, !d.confirmed);
    CRUCIBLE_TEST_ASSERT(kTest, d.boundaries_detected.get() == 0);
    // The third copy completes the square. The boundary fires on its last op.
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, sig.at(::fixy::index<0>())));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, sig.at(::fixy::index<1>())));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, sig.at(::fixy::index<2>())));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, sig.at(::fixy::index<3>())));
    CRUCIBLE_TEST_ASSERT(kTest, d.check(kAlloc, sig.at(::fixy::index<4>())));
    CRUCIBLE_TEST_ASSERT(kTest, d.confirmed);
    CRUCIBLE_TEST_ASSERT(kTest, d.last_completed_len == 5);
    CRUCIBLE_TEST_ASSERT(kTest, d.boundaries_detected.get() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, d.ops_since_boundary.get() == K);
    ::fixy::check::pass(kTest, "  test_boundary_two_iterations:    PASSED\n");
}

void test_unequal_iterations_do_not_fire() {
    // Three signature matches whose gaps differ. The old matcher reported a
    // boundary here, of whatever length the last gap had.
    IterationDetector d;
    const ::fixy::FixedArray<SchemaHash, 5> sig{H(1), H(2), H(3), H(4), H(5)};
    for (auto h : sig)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, h));
    for (auto h : sig)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, h));
    for (uint64_t i = 0; i < 10; i++)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(1000 + i)));
    for (auto h : sig)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, h));
    CRUCIBLE_TEST_ASSERT(kTest, d.boundaries_detected.get() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, !d.confirmed);
    ::fixy::check::pass(kTest, "  test_unequal_iterations:         PASSED\n");
}

void test_broken_period_is_refuted() {
    IterationDetector d;
    const ::fixy::FixedArray<Op, 5> body{Op{1, 0}, Op{2, 0}, Op{3, 0}, Op{4, 0}, Op{5, 0}};
    const Boundaries boundaries = feed(d, body.view(), Iterations{4});
    CRUCIBLE_TEST_ASSERT(kTest, boundaries.size() != 0);
    CRUCIBLE_TEST_ASSERT(kTest, d.confirmed);

    // The next iteration breaks after three ops.
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(1)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(2)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(3)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(999)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(1)));
    CRUCIBLE_TEST_ASSERT(kTest, !d.confirmed);
    CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ == 1);
    ::fixy::check::pass(kTest, "  test_broken_period:              PASSED\n");
}

void test_sub_period_signature_mlp_aten_level() {
    // The eleven ATen ops of the inference MLP, schema only: t, addmm, relu
    // three times, then t, addmm. The signature t addmm relu t addmm matches
    // at offsets 0, 3 and 6 of every iteration. The old matcher reported
    // boundaries of 6 and 5 ops in turn, and every other replay diverged.
    ::fixy::FixedList<Op, 11> body{};
    for (int layer = 0; layer < 3; ++layer) {
        static_cast<void>(body.append(Op{1, 0}).expect("the body has 11 ops"));
        static_cast<void>(body.append(Op{2, 0}).expect("the body has 11 ops"));
        static_cast<void>(body.append(Op{3, 0}).expect("the body has 11 ops"));
    }
    static_cast<void>(body.append(Op{1, 0}).expect("the body has 11 ops"));
    static_cast<void>(body.append(Op{2, 0}).expect("the body has 11 ops"));

    IterationDetector d;
    const Boundaries boundaries = feed(d, body.view(), Iterations{40});
    CRUCIBLE_TEST_ASSERT(kTest, boundaries.size() != 0);
    for (const auto& boundary : boundaries)
        CRUCIBLE_TEST_ASSERT(kTest, boundary.length == 11);
    // The square is complete after two iterations. The first boundary then
    // waits for the earliest window that occurs once per iteration, t addmm
    // relu t addmm occurs three times, and 2 3 1 2 1 at offset 7 once.
    CRUCIBLE_TEST_ASSERT(kTest, boundary_at(boundaries, 0).op_index == 2 * 11 + 7 + K - 1);
    assert_settles_on(boundaries, Period{11}, TailCount{boundaries.size() - 1});
    ::fixy::check::pass(kTest, "  test_sub_period_aten_level:      PASSED\n");
}

void test_repeated_layers_recorded_mlp() {
    // The recorded stream. Layers 0 and 1 form a square of 12 ops, so the
    // detector may accept 12 first. Layer 3 breaks that period, the period is
    // refuted, and the detector settles on the 46-op iteration.
    const auto body = inference_mlp_body();
    CRUCIBLE_TEST_ASSERT(kTest, body.size() == 46);

    IterationDetector d;
    const Boundaries boundaries = feed(d, body.view(), Iterations{30});
    assert_settles_on(boundaries, Period{46}, TailCount{20});
    size_t wrong = 0;
    for (const auto& boundary : boundaries)
        wrong += boundary.length != 46 ? 1u : 0u;
    CRUCIBLE_TEST_ASSERT(kTest, wrong <= 2);

    // The iteration begins at a window that occurs once per period, so the
    // foreground cannot align at a hidden layer. Every such window touches
    // the head layer, and the earliest one starts four ops before it.
    CRUCIBLE_TEST_ASSERT(kTest, d.phase_is_unique);
    const uint64_t iteration_start = boundary_at(boundaries, boundaries.size() - 1).op_index + 1 - K;
    const auto phase = static_cast<uint32_t>(iteration_start % 46);
    CRUCIBLE_TEST_ASSERT(kTest, window_count(body.view(), BodyOffset{phase}) == 1);
    CRUCIBLE_TEST_ASSERT(kTest, phase == 32);
    ::fixy::check::pass(kTest, "  test_repeated_layers_mlp:        PASSED ({} early boundaries)\n", wrong);
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
        const Op op = op_at(body.view(), BodyOffset{static_cast<uint32_t>(op_index % 46)});
        if (d.check(kAlloc, SchemaHash{op.schema}, ShapeHash{op.shape}) && d.last_completed_len == 12) {
            accepted_layer = true;
            break;
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, accepted_layer);
    CRUCIBLE_TEST_ASSERT(kTest, d.phase_is_unique);

    d.restart_after_divergence();
    CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ == 1);
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 0);

    // Resume one op into a hidden layer.
    const Boundaries boundaries = feed(d, body.view(), Iterations{20}, StreamOp{0}, BodyOffset{13});
    CRUCIBLE_TEST_ASSERT(kTest, boundaries.size() != 0);
    for (const auto& boundary : boundaries)
        CRUCIBLE_TEST_ASSERT(kTest, boundary.length == 46);
    assert_settles_on(boundaries, Period{46}, TailCount{boundaries.size() - 1});
    ::fixy::check::pass(kTest, "  test_refuted_survives_restart:   PASSED\n");
}

void test_divergence_after_a_second_period_refutes_nothing() {
    // The layer square is reported and published, the detector then sees it
    // break and accepts the true period. The foreground was replaying the
    // layer region, and that is what diverges. The restart must not refute
    // the true period, or the next search can only find two iterations.
    const auto body = inference_mlp_body();
    constexpr ::fixy::FixedArray<bool, 2> kTruePeriodReported{false, true};
    for (const bool true_period_reported : kTruePeriodReported) {
        IterationDetector d;
        uint64_t op_index = 0;
        for (; op_index < 46 * 6; ++op_index) {
            const Op op = op_at(body.view(), BodyOffset{static_cast<uint32_t>(op_index % 46)});
            const bool fired = d.check(kAlloc, SchemaHash{op.schema}, ShapeHash{op.shape});
            if (d.period_ == 46 && (!true_period_reported || fired)) break;
        }
        CRUCIBLE_TEST_ASSERT(kTest, d.period_ == 46);
        CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ == 1);

        d.restart_after_divergence();
        CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ == 1);
        const Boundaries boundaries = feed(d, body.view(), Iterations{20}, StreamOp{0}, BodyOffset{13});
        for (const auto& boundary : boundaries)
            CRUCIBLE_TEST_ASSERT(kTest, boundary.length == 46);
        assert_settles_on(boundaries, Period{46}, TailCount{boundaries.size() - 1});
    }
    ::fixy::check::pass(kTest, "  test_second_period_not_refuted:  PASSED\n");
}

void test_gap_in_the_recording_does_not_refute() {
    // The foreground records nothing while it aligns or replays, so the
    // stream the detector sees can skip ops with no restart. The gap breaks
    // the true period once. It must hold again afterwards.
    ::fixy::FixedList<Op, 8> body{};
    for (uint64_t i = 0; i < 8; ++i)
        static_cast<void>(body.append(Op{0x100 + i, 0x200 + i}).expect("the body has 8 ops"));
    IterationDetector d;
    const Boundaries before = feed(d, body.view(), Iterations{4});
    CRUCIBLE_TEST_ASSERT(kTest, before.size() != 0);
    // Resume three ops into an iteration.
    const Boundaries after = feed(d, body.view(), Iterations{20}, StreamOp{0}, BodyOffset{3});
    CRUCIBLE_TEST_ASSERT(kTest, !d.is_refuted(8, d.period_body_sum_));
    assert_settles_on(after, Period{8}, TailCount{10});
    ::fixy::check::pass(kTest, "  test_gap_does_not_refute:        PASSED\n");
}

// A body of at most seven ops, and the period that its stream settles on.  The
// ops after the length are empty, and no feed reads them.
using ShortOps = ::fixy::FixedArray<Op, 7>;
struct ShortBody {
    ShortOps ops{};
    std::size_t length = 0;
    Period period{};
};

void test_short_periods() {
    // A period below K gets its smallest multiple that is K or more.
    const Op empty{};
    const ::fixy::FixedArray<ShortBody, 6> bodies{
        ShortBody{.ops = ShortOps{Op{1, 0}, empty, empty, empty, empty, empty, empty},
                  .length = 1,
                  .period = Period{5}},  // 1 -> 5
        ShortBody{.ops = ShortOps{Op{1, 0}, Op{2, 0}, empty, empty, empty, empty, empty},
                  .length = 2,
                  .period = Period{6}},  // 2 -> 6
        ShortBody{.ops = ShortOps{Op{1, 0}, Op{2, 0}, Op{3, 0}, empty, empty, empty, empty},
                  .length = 3,
                  .period = Period{6}},  // 3 -> 6
        ShortBody{.ops = ShortOps{Op{1, 0}, Op{2, 0}, Op{1, 0}, Op{2, 0}, Op{3, 0}, Op{3, 0}, empty},
                  .length = 6,
                  .period = Period{6}},  // 6
        ShortBody{.ops = ShortOps{Op{1, 0}, Op{2, 0}, Op{1, 0}, Op{2, 0}, Op{1, 0}, Op{2, 0}, Op{3, 0}},
                  .length = 7,
                  .period = Period{7}},  // 7
        ShortBody{.ops = ShortOps{Op{1, 0}, Op{1, 0}, Op{1, 0}, Op{1, 0}, Op{1, 0}, Op{2, 0}, empty},
                  .length = 6,
                  .period = Period{6}},  // 6, the signature is five equal ops
    };
    for (const ShortBody& short_body : bodies) {
        const ::fixy::View<Op const> body =
            short_body.ops.view().window(0, short_body.length).expect("the length of a body is at most seven");
        CRUCIBLE_TEST_ASSERT(kTest, expected_period(body) == short_body.period);
        IterationDetector d;
        const auto iterations = static_cast<uint32_t>(400 / body.size());
        const Boundaries boundaries = feed(d, body, Iterations{iterations});
        assert_settles_on(boundaries, short_body.period, TailCount{10});
    }
    ::fixy::check::pass(kTest, "  test_short_periods:              PASSED\n");
}

void test_random_bodies_settle_on_true_period() {
    // Bodies over a three-letter alphabet, so the signature recurs inside
    // the body and layer squares occur by chance. Every stream must settle on
    // its true period whatever offset it starts at.
    uint64_t state = 42;
    uint32_t streams = 0;
    for (uint32_t length = 3; length <= 24; ++length) {
        for (uint32_t trial = 0; trial < 40; ++trial) {
            ::fixy::FixedList<Op, 24> body{};
            for (uint32_t position = 0; position < length; ++position)
                static_cast<void>(
                    body.append(Op{1 + next_random(state) % 3, 0}).expect("a random body has at most 24 ops"));
            const Period period = expected_period(body.view());
            const auto offset = static_cast<uint32_t>(next_random(state) % length);

            IterationDetector d;
            const uint32_t iterations = 12 + 2400 / length;
            const Boundaries boundaries = feed(d, body.view(), Iterations{iterations}, StreamOp{0}, BodyOffset{offset});
            assert_settles_on(boundaries, period, TailCount{8});
            ++streams;
        }
    }
    ::fixy::check::pass(kTest, "  test_random_bodies:              PASSED ({} streams)\n", streams);
}

// Feeds iterations copies of body, then restarts the detector as a divergence
// of the region of the accepted period does, and gives the boundaries.
[[nodiscard]] Boundaries feed_then_diverge(IterationDetector& detector, ::fixy::View<Op const> body,
                                           Iterations iterations) {
    Boundaries boundaries = feed(detector, body, iterations);
    detector.restart_after_divergence();
    return boundaries;
}

void test_divergence_refutes_a_power_body_and_one_longer_power_is_its_fallback() {
    // A stream of one repeated op gives the period 5, five copies of its root.
    // Each window occurs once in each copy of the root, and each copy is the
    // same op, so the foreground follows the same ops from each copy.  A
    // divergence then refutes the period.  The search accepts the next
    // multiple of the root once, and after its divergence no longer power.
    const ::fixy::FixedArray<Op, 1> body{Op{7, 0}};
    IterationDetector d;
    const Boundaries base = feed(d, body.view(), Iterations{30});
    assert_settles_on(base, Period{5}, TailCount{3});
    CRUCIBLE_TEST_ASSERT(kTest, d.phase_is_unique);
    const uint64_t base_sum = d.period_body_sum_;
    d.restart_after_divergence();
    CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ == 1);
    CRUCIBLE_TEST_ASSERT(kTest, d.is_refuted(5, base_sum));
    const Boundaries fallback = feed_then_diverge(d, body.view(), Iterations{60});
    assert_settles_on(fallback, Period{6}, TailCount{3});
    const Boundaries after = feed(d, body.view(), Iterations{60});
    CRUCIBLE_TEST_ASSERT(kTest, after.size() == 0);
    ::fixy::check::pass(kTest, "  test_power_body_refuted:         PASSED\n");
}

void test_second_divergence_leaves_no_period_until_reset_because_one_fallback_is_the_limit() {
    // The limit of the rule on powers.  A divergence refutes the true period
    // of 8 ops, and the stream gets the fallback of 16 ops, two iterations.
    // A divergence of the fallback refutes it too.  Each longer power is
    // skipped, so the stream has no period until reset().
    ::fixy::FixedList<Op, 8> body{};
    for (uint64_t i = 0; i < 8; ++i)
        static_cast<void>(body.append(Op{0x300 + i, 0x400 + i}).expect("the body has 8 ops"));
    IterationDetector d;
    const Boundaries before = feed_then_diverge(d, body.view(), Iterations{4});
    CRUCIBLE_TEST_ASSERT(kTest, before.size() != 0);
    const Boundaries fallback = feed(d, body.view(), Iterations{40});
    assert_settles_on(fallback, Period{16}, TailCount{10});
    CRUCIBLE_TEST_ASSERT(kTest, d.phase_is_unique);
    d.restart_after_divergence();
    const Boundaries after = feed(d, body.view(), Iterations{80});
    CRUCIBLE_TEST_ASSERT(kTest, after.size() == 0);
    d.reset();
    const Boundaries fresh = feed(d, body.view(), Iterations{20});
    assert_settles_on(fresh, Period{8}, TailCount{10});
    ::fixy::check::pass(kTest, "  test_one_fallback_is_the_limit:  PASSED\n");
}

void test_a_run_of_equal_groups_gives_at_most_two_periods() {
    // The run of 12 equal groups of 6 ops holds a square of each multiple of
    // 6 up to 30.  The group of 6 ops and its fallback of 12 ops break at the
    // end of the run, and each longer multiple is skipped, so the next period
    // is the iteration.
    const auto body = foreach_add_body();
    IterationDetector d;
    const Boundaries boundaries = feed(d, body.view(), Iterations{30});
    for (const auto& boundary : boundaries)
        CRUCIBLE_TEST_ASSERT(kTest, boundary.length == kForeachGroupOps || boundary.length == 2 * kForeachGroupOps
                                        || boundary.length == kForeachOps);
    assert_settles_on(boundaries, Period{static_cast<uint32_t>(kForeachOps)}, TailCount{10});
    ::fixy::check::pass(kTest, "  test_run_gives_two_periods:      PASSED\n");
}

void test_power_of_a_short_root_has_a_unique_phase() {
    // The run of ten equal pairs gives the period 6, three copies of a root
    // of two ops.  Its window occurs once in each copy of the root, so a
    // divergence refutes it, and the next period is the iteration of 21 ops.
    const auto body = fused_adam_body();
    IterationDetector d;
    bool accepted_pairs = false;
    for (uint64_t op_index = 0; op_index < kAdamOps * 4 && !accepted_pairs; ++op_index) {
        const Op op = op_at(body.view(), BodyOffset{static_cast<uint32_t>(op_index % kAdamOps)});
        accepted_pairs = d.check(kAlloc, SchemaHash{op.schema}, ShapeHash{op.shape}) && d.last_completed_len == 6;
    }
    CRUCIBLE_TEST_ASSERT(kTest, accepted_pairs);
    CRUCIBLE_TEST_ASSERT(kTest, d.phase_is_unique);
    const uint64_t body_sum = d.period_body_sum_;
    d.restart_after_divergence();
    CRUCIBLE_TEST_ASSERT(kTest, d.is_refuted(6, body_sum));
    const Boundaries boundaries = feed(d, body.view(), Iterations{20});
    for (const auto& boundary : boundaries)
        CRUCIBLE_TEST_ASSERT(kTest, boundary.length == kAdamOps);
    assert_settles_on(boundaries, Period{static_cast<uint32_t>(kAdamOps)}, TailCount{10});
    ::fixy::check::pass(kTest, "  test_short_root_unique_phase:    PASSED\n");
}

void test_equal_groups_replay_through_the_foreground() {
    // The two loops of the vessel plants, with the model of the foreground
    // at two latencies.  Each settles on the region of its iteration.  The
    // loop of _foreach_add diverges two times: from the region of one group,
    // then from its fallback of two groups.  The loop of _fused_adam_
    // diverges one time, because its run of 20 ops holds no square of its
    // fallback of 8 ops.
    const auto foreach_body = foreach_add_body();
    const auto adam_body = fused_adam_body();
    constexpr ::fixy::FixedArray<Uptake, 2> kUptakes{Uptake::AtOnce, Uptake::NextIteration};
    for (const Uptake uptake : kUptakes) {
        IterationDetector foreach_detector;
        const ForegroundRun foreach_run =
            run_with_foreground(foreach_detector, foreach_body.view(), Iterations{40}, uptake);
        CRUCIBLE_TEST_ASSERT(kTest, foreach_run.mode == ForegroundMode::Replays);
        CRUCIBLE_TEST_ASSERT(kTest, foreach_run.region_length == kForeachOps);
        CRUCIBLE_TEST_ASSERT(kTest, foreach_run.divergences == 2);

        IterationDetector adam_detector;
        const ForegroundRun adam_run = run_with_foreground(adam_detector, adam_body.view(), Iterations{40}, uptake);
        CRUCIBLE_TEST_ASSERT(kTest, adam_run.mode == ForegroundMode::Replays);
        CRUCIBLE_TEST_ASSERT(kTest, adam_run.region_length == kAdamOps);
        CRUCIBLE_TEST_ASSERT(kTest, adam_run.divergences == 1);
    }
    ::fixy::check::pass(kTest, "  test_equal_groups_replay:        PASSED\n");
}

void test_reset_clears_everything() {
    IterationDetector d;
    const auto body = inference_mlp_body();
    const Boundaries boundaries = feed(d, body.view(), Iterations{6});
    CRUCIBLE_TEST_ASSERT(kTest, boundaries.size() != 0);
    CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ > 0);

    d.reset();
    CRUCIBLE_TEST_ASSERT(kTest, !d.confirmed);
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, d.boundaries_detected.get() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, d.ops_since_boundary.get() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, d.last_completed_len == 0);
    CRUCIBLE_TEST_ASSERT(kTest, d.refuted_count_ == 0);
    for (uint32_t i = 0; i < 4; i++)
        CRUCIBLE_TEST_ASSERT(kTest, !d.check(kAlloc, H(100 + i)));
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 4);
    ::fixy::check::pass(kTest, "  test_reset:                      PASSED\n");
}

void test_typestate_witness_minting() {
    // Minting a view in the state that matches the detector's phase must
    // succeed.  The mismatched cases are pinned by negative-compile
    // fixtures, and the two halves together bracket the mint's gate.
    using crucible::iter_det_state::Building;
    using crucible::iter_det_state::Steady;
    using ::fixy::mint_view;

    IterationDetector d;
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 0);
    {
        auto building_view = mint_view<Building>(d);
        CRUCIBLE_TEST_ASSERT(kTest, &building_view.carrier() == &d);
        CRUCIBLE_TEST_ASSERT(kTest, building_view->signature_len.get() == 0);
    }

    const ::fixy::FixedArray<SchemaHash, 5> sig{H(11), H(22), H(33), H(44), H(55)};
    for (auto h : sig)
        (void)d.check(kAlloc, h);
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == K);
    {
        auto steady_view = mint_view<Steady>(d);
        CRUCIBLE_TEST_ASSERT(kTest, &steady_view.carrier() == &d);
        CRUCIBLE_TEST_ASSERT(kTest, steady_view->signature_len.get() == K);
    }

    d.reset();
    CRUCIBLE_TEST_ASSERT(kTest, d.signature_len.get() == 0);
    {
        auto rebuilt_view = mint_view<Building>(d);
        CRUCIBLE_TEST_ASSERT(kTest, &rebuilt_view.carrier() == &d);
        CRUCIBLE_TEST_ASSERT(kTest, rebuilt_view->signature_len.get() == 0);
    }
    ::fixy::check::pass(kTest, "  test_typestate_witness:          PASSED\n");
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
    test_divergence_refutes_a_power_body_and_one_longer_power_is_its_fallback();
    test_second_divergence_leaves_no_period_until_reset_because_one_fallback_is_the_limit();
    test_a_run_of_equal_groups_gives_at_most_two_periods();
    test_power_of_a_short_root_has_a_unique_phase();
    test_equal_groups_replay_through_the_foreground();
    test_reset_clears_everything();
    test_typestate_witness_minting();
    ::fixy::check::pass(kTest, "test_iteration_detector: 18 groups, all passed\n");
    return 0;
}
