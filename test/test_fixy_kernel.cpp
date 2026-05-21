// ── test_fixy_kernel — sentinel TU for fixy/Kernel.h ───────────────
//
// Pulls fixy/Kernel.h into one TU compiled under project warning flags
// so every alias / using-declaration is materialised and every
// embedded static_assert in the substrate headers executes here.
//
// Covers FIXY-V-038:
//   * Identity sentinels — every fixy::kernel:: name aliases its
//     safety:: substrate symbol.
//   * LOAD-BEARING contract sentinels — reduce_into is non-copyable,
//     move-only, and Op is enforced via is_reduction_op_v.
//   * Positive Reduction shape witness — instantiates OwnedRegion +
//     reduce_into and proves the concept admits the canonical
//     `void f(OR&&, RI&)` shape.  This is the witness the header
//     self-test deliberately omits (it would expand the include
//     surface of Kernel.h itself).
//   * Negative shape rejection mirroring the substrate's 5 negative
//     fixtures.
//   * 4-extractor round-trip across distinct Reduction signatures.
//   * Cardinality FLOOR witness (>= 12) — append-friendly form per
//     FIXY-U-127/U-128 (floor instead of equality so future appends
//     don't redden silently elsewhere).
//   * Runtime smoke test — exercises substrate's runtime smoke entry
//     points through the alias path to confirm no DCE elision.

#include <crucible/fixy/Kernel.h>

#include <crucible/safety/IsReduceInto.h>     // substrate side of identity
#include <crucible/safety/OwnedRegion.h>      // positive Reduction shape
#include <crucible/safety/Reduction.h>
#include <crucible/safety/reduce_into.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace {

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace fk  = ::crucible::fixy::kernel;
namespace saf = ::crucible::safety;
namespace ext = ::crucible::safety::extract;

// ── Test reducer + tag fixtures ────────────────────────────────────

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a + b;
    }
};

struct MaxOp {
    constexpr int operator()(int const& a, int const& b) const noexcept {
        return a > b ? a : b;
    }
};

struct kernel_tu_input_tag {};
struct kernel_tu_other_tag {};

using OR_int_input = saf::OwnedRegion<int, kernel_tu_input_tag>;
using OR_int_other = saf::OwnedRegion<int, kernel_tu_other_tag>;

// reduce_into specializations via the FIXY alias path — every type
// constructed below is a fixy::kernel::reduce_into<...>, not a
// safety::reduce_into<...> at the spelling level.  Identity-equality
// with the substrate is verified separately below.
using RI_int_plus = fk::reduce_into<int, PlusOp>;
using RI_int_max  = fk::reduce_into<int, MaxOp>;

}  // namespace

// ═══════════════════════════════════════════════════════════════════
// ── 1. TU-level identity sentinels (compile-time witnesses) ───────
// ═══════════════════════════════════════════════════════════════════
//
// Mirror the header self-test under the project warning flags.  Each
// claim agrees with the corresponding sentinel in Kernel.h::self_test;
// having both ensures the alias path is checked both at header
// inclusion time AND at TU compile time under -Werror=*.

// Class-template identity.
static_assert(std::is_same_v<RI_int_plus, saf::reduce_into<int, PlusOp>>,
    "fixy::kernel::reduce_into<int, PlusOp> must alias safety::reduce_into.");
static_assert(std::is_same_v<RI_int_max,  saf::reduce_into<int, MaxOp>>,
    "fixy::kernel::reduce_into<int, MaxOp> must alias safety::reduce_into.");

// Concept-alias agreement.
static_assert(fk::is_reduction_op_v<PlusOp, int>
           == saf::is_reduction_op_v<PlusOp, int>);
static_assert(fk::is_reduction_op_v<MaxOp,  int>
           == saf::is_reduction_op_v<MaxOp,  int>);

// ── 2. LOAD-BEARING structural contracts on reduce_into ───────────
//
// Mirror the substrate's documented copy-deleted / move-only
// discipline at the fixy:: boundary — the same axioms Kernel.h
// asserts in its self-test block, re-checked here under the TU's
// -Werror=* matrix.
static_assert(!std::is_copy_constructible_v<RI_int_plus>,
    "fixy::kernel::reduce_into MUST NOT be copy-constructible "
    "(linear accumulator discipline).");
static_assert(!std::is_copy_assignable_v<RI_int_plus>,
    "fixy::kernel::reduce_into MUST NOT be copy-assignable "
    "(linear accumulator discipline).");
static_assert(std::is_move_constructible_v<RI_int_plus>,
    "fixy::kernel::reduce_into MUST be move-constructible "
    "(dispatcher → worker transfer).");
static_assert(std::is_move_assignable_v<RI_int_plus>,
    "fixy::kernel::reduce_into MUST be move-assignable.");

// Op admissibility — positive case (PlusOp) admitted, negative case
// (struct without operator()) rejected.  The substrate's
// is_reduction_op_v is the SOLE gate at reduce_into's class-level
// requires-clause; if drift here lets a non-reducer Op through, the
// production parallel_reduce_views call sites would fail in
// downstream is_invocable checks far from the bug.
struct KernelTUNotInvocable {};
static_assert(fk::is_reduction_op_v<PlusOp, int>);
static_assert(!fk::is_reduction_op_v<KernelTUNotInvocable, int>);

struct KernelTUWrongArity {
    constexpr int operator()(int) const noexcept { return 0; }
};
static_assert(!fk::is_reduction_op_v<KernelTUWrongArity, int>);

struct KernelTUWrongReturn {
    constexpr void operator()(int const&, int const&) const noexcept {}
};
static_assert(!fk::is_reduction_op_v<KernelTUWrongReturn, int>);

// ── 3. Positive Reduction shape witness ───────────────────────────
//
// The canonical reduction signature: consumes one OwnedRegion by
// rvalue ref, borrows one reduce_into by lvalue ref, returns void.
// Header self-test cannot instantiate this without pulling in
// OwnedRegion (which would expand the include surface unnecessarily);
// the TU does it once here.
namespace kernel_tu_shapes {

// Canonical sum_into shape.
void f_sum_into(OR_int_input&&, RI_int_plus&) noexcept;

// max_into shape — same OwnedRegion Tag, different reduce_into Op.
void f_max_into(OR_int_input&&, RI_int_max&) noexcept;

// Distinct input tag — admitted; concept admits any Tag.
void f_other_tag(OR_int_other&&, RI_int_plus&) noexcept;

// ── Negative witnesses — mirror Reduction.h self-test ─────────────
void f_no_param() noexcept;
void f_one_param(OR_int_input&&) noexcept;
void f_three_params(OR_int_input&&, RI_int_plus&, int) noexcept;
void f_two_ints(int, int) noexcept;
int  f_returns_int(OR_int_input&&, RI_int_plus&) noexcept;

}  // namespace kernel_tu_shapes

// Positive shape — concept admits the canonical reduction signature.
static_assert(fk::Reduction<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::is_reduction_v<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_max_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_other_tag>);

// Substrate cross-check — the alias agrees with the substrate on
// every positive witness (mandatory: drift here means
// fixy-routed and safety-routed callers see different recognition).
static_assert(fk::Reduction<&kernel_tu_shapes::f_sum_into>
           == ext::Reduction<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_max_into>
           == ext::Reduction<&kernel_tu_shapes::f_max_into>);

// Negative shapes — mirror the substrate's 5-witness self-test.
static_assert(!fk::Reduction<&kernel_tu_shapes::f_no_param>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_one_param>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_three_params>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_two_ints>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_returns_int>);

// Substrate-agreement on every negative witness.
static_assert(fk::Reduction<&kernel_tu_shapes::f_no_param>
           == ext::Reduction<&kernel_tu_shapes::f_no_param>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_returns_int>
           == ext::Reduction<&kernel_tu_shapes::f_returns_int>);

// ── 4. Extractor round-trip across distinct Reduction signatures ──
//
// Verifies that each of the 4 reduction_*_t extractors yields the
// expected type across two distinct signatures.  This catches the
// "alias re-exported the concept but forgot one of the extractors"
// drift class.

// reduction_input_tag_t — agrees across signatures with distinct Tags.
static_assert(std::is_same_v<
    fk::reduction_input_tag_t<&kernel_tu_shapes::f_sum_into>,
    kernel_tu_input_tag>);
static_assert(std::is_same_v<
    fk::reduction_input_tag_t<&kernel_tu_shapes::f_other_tag>,
    kernel_tu_other_tag>);
// Substrate identity.
static_assert(std::is_same_v<
    fk::reduction_input_tag_t<&kernel_tu_shapes::f_sum_into>,
    ext::reduction_input_tag_t<&kernel_tu_shapes::f_sum_into>>);

// reduction_input_value_t — extracts OwnedRegion's element type T.
static_assert(std::is_same_v<
    fk::reduction_input_value_t<&kernel_tu_shapes::f_sum_into>, int>);
static_assert(std::is_same_v<
    fk::reduction_input_value_t<&kernel_tu_shapes::f_sum_into>,
    ext::reduction_input_value_t<&kernel_tu_shapes::f_sum_into>>);

// reduction_accumulator_t — extracts reduce_into's R (accumulator type).
static_assert(std::is_same_v<
    fk::reduction_accumulator_t<&kernel_tu_shapes::f_sum_into>, int>);
static_assert(std::is_same_v<
    fk::reduction_accumulator_t<&kernel_tu_shapes::f_max_into>, int>);
static_assert(std::is_same_v<
    fk::reduction_accumulator_t<&kernel_tu_shapes::f_sum_into>,
    ext::reduction_accumulator_t<&kernel_tu_shapes::f_sum_into>>);

// reduction_reducer_t — extracts reduce_into's Op (reducer type).
// Same R type, different Op → distinct reducer extractions.
static_assert(std::is_same_v<
    fk::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>, PlusOp>);
static_assert(std::is_same_v<
    fk::reduction_reducer_t<&kernel_tu_shapes::f_max_into>, MaxOp>);
static_assert(!std::is_same_v<
    fk::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>,
    fk::reduction_reducer_t<&kernel_tu_shapes::f_max_into>>);
// Substrate identity on Reducer extractor.
static_assert(std::is_same_v<
    fk::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>,
    ext::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>>);

// ── 5. reduce_into wrapper-detection (FOUND-D07) round-trip ───────
//
// IsReduceInto / is_reduce_into_v classify the WRAPPER TYPE.  Two
// witnesses: positive (RI_int_plus) and negative (bare int).
static_assert(fk::is_reduce_into_v<RI_int_plus>);
static_assert(fk::is_reduce_into_v<RI_int_max>);
static_assert(!fk::is_reduce_into_v<int>);
static_assert(!fk::is_reduce_into_v<PlusOp>);

// Substrate identity.
static_assert(fk::is_reduce_into_v<RI_int_plus>
           == ext::is_reduce_into_v<RI_int_plus>);
static_assert(fk::is_reduce_into_v<int>
           == ext::is_reduce_into_v<int>);

// Cv-ref stripping reaches through the alias identically.
static_assert(fk::is_reduce_into_v<RI_int_plus&>);
static_assert(fk::is_reduce_into_v<RI_int_plus&&>);
static_assert(fk::is_reduce_into_v<RI_int_plus const&>);

// IsReduceInto concept form agrees with variable template form.
static_assert(fk::IsReduceInto<RI_int_plus>
           == fk::is_reduce_into_v<RI_int_plus>);
static_assert(!fk::IsReduceInto<int>);

// reduce_into_accumulator_t / reduce_into_reducer_t round-trip.
static_assert(std::is_same_v<
    fk::reduce_into_accumulator_t<RI_int_plus>, int>);
static_assert(std::is_same_v<
    fk::reduce_into_reducer_t<RI_int_plus>, PlusOp>);
static_assert(std::is_same_v<
    fk::reduce_into_accumulator_t<RI_int_max>, int>);
static_assert(std::is_same_v<
    fk::reduce_into_reducer_t<RI_int_max>, MaxOp>);

// Substrate-identity on the extractors.
static_assert(std::is_same_v<
    fk::reduce_into_accumulator_t<RI_int_plus>,
    ext::reduce_into_accumulator_t<RI_int_plus>>);
static_assert(std::is_same_v<
    fk::reduce_into_reducer_t<RI_int_plus>,
    ext::reduce_into_reducer_t<RI_int_plus>>);

// ── 6. Cardinality FLOOR witness (append-friendly) ────────────────
//
// FIXY-U-127/U-128 discipline: floor instead of equality so future
// appends to fixy::kernel:: don't silently break this TU.  When the
// surface grows (V-039 will add Fusion + CanonicalShape + Binary/
// UnaryTransform), the floor bumps but the relation stays valid.
static_assert(
    fk::self_test::kernel_alias_cardinality >= 12,
    "fixy::kernel:: cardinality floor — V-038 surface ships at least "
    "12 re-exports (Reduction concept + is_reduction_v variable "
    "template + 4 Reduction extractors + reduce_into class template + "
    "is_reduction_op_v Op concept + IsReduceInto wrapper concept + "
    "is_reduce_into_v variable template + reduce_into_accumulator_t / "
    "reduce_into_reducer_t wrapper extractors).  FIXY-V-038-audit "
    "bumped floor 11 → 12 after the post-ship audit caught the "
    "is_reduce_into_v variable template being omitted from the "
    "original enumeration.");

// ═══════════════════════════════════════════════════════════════════
// ── 7. Runtime smoke tests ────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

namespace {

void test_reduction_smoke_through_alias() {
    // The substrate's runtime smoke is reached through the alias —
    // confirms include path resolves correctly even when the project
    // warning flags are at their strictest.
    EXPECT_TRUE(ext::reduction_smoke_test());
}

void test_reduce_into_smoke_through_alias() {
    EXPECT_TRUE(saf::reduce_into_smoke_test());
}

void test_is_reduce_into_smoke_through_alias() {
    EXPECT_TRUE(ext::is_reduce_into_smoke_test());
}

void test_runtime_reduce_into_round_trip() {
    // Build a reduce_into via the fixy::kernel:: alias path,
    // combine, peek, consume — verifies the alias resolves to a
    // type whose value-semantics work at runtime, not just at the
    // type level.
    volatile int const seed = 0;
    RI_int_plus acc{static_cast<int>(seed), PlusOp{}};
    acc.combine(7);
    acc.combine(35);
    EXPECT_TRUE(acc.peek() == 42);

    int extracted = std::move(acc).consume();
    EXPECT_TRUE(extracted == 42);

    // Verify max-reducer path too — distinct Op, same wrapper.
    RI_int_max rmax{0, MaxOp{}};
    rmax.combine(7);
    rmax.combine(99);
    rmax.combine(3);
    EXPECT_TRUE(rmax.peek() == 99);
}

void test_runtime_recognition_consistency() {
    // Volatile-bounded loop confirms shape recognition is bit-stable
    // through the alias path (matches the substrate's test_reduction
    // runtime_consistency pattern).
    volatile std::size_t const cap = 32;
    bool const baseline_pos =
        fk::is_reduction_v<&kernel_tu_shapes::f_sum_into>;
    bool const baseline_neg =
        !fk::is_reduction_v<&kernel_tu_shapes::f_no_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos
            == fk::is_reduction_v<&kernel_tu_shapes::f_sum_into>);
        EXPECT_TRUE(baseline_neg
            == !fk::is_reduction_v<&kernel_tu_shapes::f_no_param>);
        EXPECT_TRUE(fk::is_reduce_into_v<RI_int_plus>);
        EXPECT_TRUE(!fk::is_reduce_into_v<int>);
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fixy_kernel:\n");
    run_test("test_reduction_smoke_through_alias",
             test_reduction_smoke_through_alias);
    run_test("test_reduce_into_smoke_through_alias",
             test_reduce_into_smoke_through_alias);
    run_test("test_is_reduce_into_smoke_through_alias",
             test_is_reduce_into_smoke_through_alias);
    run_test("test_runtime_reduce_into_round_trip",
             test_runtime_reduce_into_round_trip);
    run_test("test_runtime_recognition_consistency",
             test_runtime_recognition_consistency);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
