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

#include <crucible/safety/BinaryTransform.h>  // V-039 substrate-side identity
#include <crucible/safety/CanonicalShape.h>   // V-039 umbrella + dispatch
#include <crucible/safety/Fusion.h>           // V-039 composability
#include <crucible/safety/IsReduceInto.h>     // substrate side of identity
#include <crucible/safety/OwnedRegion.h>      // positive shape witnesses
#include <crucible/safety/Reduction.h>
#include <crucible/safety/UnaryTransform.h>   // V-039 substrate-side identity
#include <crucible/safety/reduce_into.h>

#include <bit>                                // V-039 std::bit_cast for FP compare
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>                        // V-039 canonical_shape_name
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

// ── FP bit-exact comparison helper ────────────────────────────────
//
// -Werror=float-equal rejects `==` on float / double in BOTH compile-
// time and runtime contexts.  Per CLAUDE.md §III remediation: bit-
// exact compare via std::bit_cast.  Acceptable for V-039's Fusion
// witnesses because every expected value (0.0, 2.0, 4.5, 5.0) is an
// exact-representable IEEE 754 double — the multiplications p_half(x)
// = x * 0.5 preserve bit exactness when the input is exactly
// representable (powers of two and small integers map to themselves).
[[nodiscard]] inline constexpr bool double_bits_eq(double a, double b) noexcept {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

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

// ═══════════════════════════════════════════════════════════════════
// ── 6. UnaryTransform positive shape witness (V-039) ──────────────
// ═══════════════════════════════════════════════════════════════════
//
// Substrate witness via OwnedRegion-typed signatures the header
// self-test deliberately omits (would expand include surface
// unnecessarily).  Mirrors test_unary_transform.cpp's positive
// fixtures but routed through fk::UnaryTransform — drift between the
// two reach paths would mean fixy-routed callers see different
// recognition from safety-routed callers.

namespace kernel_tu_unary_shapes {

using OR_in_d  = saf::OwnedRegion<double, kernel_tu_input_tag>;
using OR_out_d = saf::OwnedRegion<double, kernel_tu_other_tag>;

// Positive: in-place void return.
void f_unary_in_place(OR_int_input&&) noexcept;

// Positive: out-of-place region return with distinct Tag.
OR_int_other f_unary_out_of_place(OR_int_input&&) noexcept;

// Positive: different element type entirely (float-in, double-out).
OR_out_d f_unary_change_element(OR_in_d&&) noexcept;

// Negative: arity 2 fails arity_v == 1 clause.
void f_unary_arity_two(OR_int_input&&, OR_int_input&&) noexcept;

}  // namespace kernel_tu_unary_shapes

// Positive admission across three distinct in-place / out-of-place
// signatures — all admitted by the alias.
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::is_unary_transform_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_change_element>);

// Substrate-agreement on every positive witness.
static_assert(
    fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>
    == ext::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(
    fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>
    == ext::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);

// is_in_place_unary_transform_v — true iff void-return, false iff
// region-return.  Discriminates the two lowering paths the dispatcher
// uses (single-buffer parallel_for_views vs double-buffer).
static_assert(
    fk::is_in_place_unary_transform_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(
    !fk::is_in_place_unary_transform_v<&kernel_tu_unary_shapes::f_unary_out_of_place>);

// Extractors — input_tag, input_value, output_tag.
static_assert(std::is_same_v<
    fk::unary_transform_input_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>,
    kernel_tu_input_tag>);
static_assert(std::is_same_v<
    fk::unary_transform_input_value_t<&kernel_tu_unary_shapes::f_unary_in_place>,
    int>);

// In-place → output_tag = void (no allocation needed).
static_assert(std::is_same_v<
    fk::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>,
    void>);

// Out-of-place → output_tag = return region's Tag.
static_assert(std::is_same_v<
    fk::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_out_of_place>,
    kernel_tu_other_tag>);

// Different-element witness — exercises a non-int element type
// extraction (covers the "extractor reads input_value_t correctly
// across element-type variation" drift class).
static_assert(std::is_same_v<
    fk::unary_transform_input_value_t<&kernel_tu_unary_shapes::f_unary_change_element>,
    double>);

// Substrate identity on every extractor.
static_assert(std::is_same_v<
    fk::unary_transform_input_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>,
    ext::unary_transform_input_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>>);
static_assert(std::is_same_v<
    fk::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_out_of_place>,
    ext::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_out_of_place>>);

// Negative — arity 2 fails.
static_assert(!fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_arity_two>);

// ═══════════════════════════════════════════════════════════════════
// ── 7. BinaryTransform positive shape witness (V-039) ─────────────
// ═══════════════════════════════════════════════════════════════════

namespace kernel_tu_binary_shapes {

// Positive: in-place against lhs (void return).
void f_binary_in_place(OR_int_input&&, OR_int_input&&) noexcept;

// Positive: out-of-place region return, same Tag for lhs/rhs.
OR_int_input f_binary_same_tag(OR_int_input&&, OR_int_input&&) noexcept;

// Positive: out-of-place region return, DISTINCT lhs / rhs Tags.
OR_int_other f_binary_different_tags(OR_int_input&&, OR_int_other&&) noexcept;

// Negative: arity 1 fails.
void f_binary_arity_one(OR_int_input&&) noexcept;

// Negative: arity 3 fails.
void f_binary_arity_three(OR_int_input&&, OR_int_input&&, OR_int_input&&) noexcept;

}  // namespace kernel_tu_binary_shapes

// Positive admission across three signature variants.
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::is_binary_transform_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_same_tag>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);

// Substrate-agreement.
static_assert(
    fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>
    == ext::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(
    fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>
    == ext::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);

// In-place refinement.
static_assert(
    fk::is_in_place_binary_transform_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(
    !fk::is_in_place_binary_transform_v<&kernel_tu_binary_shapes::f_binary_same_tag>);
static_assert(
    !fk::is_in_place_binary_transform_v<&kernel_tu_binary_shapes::f_binary_different_tags>);

// Extractors — lhs/rhs tag, lhs/rhs value, output_tag.
static_assert(std::is_same_v<
    fk::binary_transform_lhs_tag_t<&kernel_tu_binary_shapes::f_binary_in_place>,
    kernel_tu_input_tag>);
static_assert(std::is_same_v<
    fk::binary_transform_rhs_tag_t<&kernel_tu_binary_shapes::f_binary_in_place>,
    kernel_tu_input_tag>);
static_assert(std::is_same_v<
    fk::binary_transform_lhs_value_t<&kernel_tu_binary_shapes::f_binary_in_place>,
    int>);
static_assert(std::is_same_v<
    fk::binary_transform_rhs_value_t<&kernel_tu_binary_shapes::f_binary_in_place>,
    int>);

// In-place → output_tag = void.
static_assert(std::is_same_v<
    fk::binary_transform_output_tag_t<&kernel_tu_binary_shapes::f_binary_in_place>,
    void>);

// Different-tag witness — lhs_tag distinct from rhs_tag, output_tag
// matches the return region.
static_assert(std::is_same_v<
    fk::binary_transform_lhs_tag_t<&kernel_tu_binary_shapes::f_binary_different_tags>,
    kernel_tu_input_tag>);
static_assert(std::is_same_v<
    fk::binary_transform_rhs_tag_t<&kernel_tu_binary_shapes::f_binary_different_tags>,
    kernel_tu_other_tag>);
static_assert(std::is_same_v<
    fk::binary_transform_output_tag_t<&kernel_tu_binary_shapes::f_binary_different_tags>,
    kernel_tu_other_tag>);

// has_same_tag_v — true iff lhs_tag == rhs_tag.  Three witnesses
// (two same-tag + one distinct-tag) exercise the predicate's truth
// table.
static_assert(
    fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(
    fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_same_tag>);
static_assert(
    !fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_different_tags>);

// Substrate identity on has_same_tag_v across both positive cases.
static_assert(
    fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_in_place>
    == ext::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(
    fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_different_tags>
    == ext::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_different_tags>);

// Negatives — arity 1 and arity 3.
static_assert(!fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_arity_one>);
static_assert(!fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_arity_three>);

// ═══════════════════════════════════════════════════════════════════
// ── 8. Fusion positive shape witness + runtime fold (V-039) ───────
// ═══════════════════════════════════════════════════════════════════
//
// Fusion's substrate ships under `crucible::safety::` (NOT `extract::`).
// Two pure noexcept int(int) functions form the canonical positive
// witness — composability does NOT depend on OwnedRegion, so positive
// cases ship at the TU level using simple int(int) functions
// (the header self-test already covers a positive case with
// kp_p_double/kp_p_inc; the TU augments with a cross-element-type
// pair and a runtime round-trip).

namespace kernel_tu_fusion_shapes {

// Pure noexcept int(int) — canonical fusable pair.
inline int p_double(int x) noexcept { return x * 2; }
inline int p_inc(int x)    noexcept { return x + 1; }

// Pure noexcept changing element type (int → double → double).
inline double p_to_double(int x)    noexcept { return static_cast<double>(x); }
inline double p_half(double x)       noexcept { return x * 0.5; }

}  // namespace kernel_tu_fusion_shapes

// Positive composability — same-element-type pair.
static_assert(fk::can_fuse_v<
    &kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);

// Positive composability — element-type-changing pair (int → double → double).
static_assert(fk::can_fuse_v<
    &kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>);

// Substrate identity on both positives.
static_assert(
    fk::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>
    == saf::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);
static_assert(
    fk::can_fuse_v<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>
    == saf::can_fuse_v<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>);

// IsFusable concept form agrees with can_fuse_v variable-template form.
static_assert(
    fk::IsFusable<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);
static_assert(
    fk::IsFusable<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>
    == fk::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);

// fuse() — compile-time round-trip with both pairs.
constexpr auto fused_double_then_inc = fk::fuse<
    &kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>();
static_assert(fused_double_then_inc(7) == 15);  // (7*2)+1
static_assert(fused_double_then_inc(0) == 1);
static_assert(fused_double_then_inc(-3) == -5);  // (-3*2)+1
static_assert(noexcept(fused_double_then_inc(7)));
// Result-type deduction — Fn2's return type propagates.
static_assert(std::is_same_v<decltype(fused_double_then_inc(0)), int>);

constexpr auto fused_to_double_then_half = fk::fuse<
    &kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>();
static_assert(double_bits_eq(fused_to_double_then_half(4),  2.0));   // 4.0 * 0.5
static_assert(double_bits_eq(fused_to_double_then_half(10), 5.0));
// Cross-element-type result deduction.
static_assert(std::is_same_v<decltype(fused_to_double_then_half(0)), double>);

// ═══════════════════════════════════════════════════════════════════
// ── 9. CanonicalShape dispatch matrix (V-039) ─────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// canonical_shape_kind_v routes each positive witness from sections
// 3 (Reduction), 6 (UnaryTransform), 7 (BinaryTransform) to the
// matching CanonicalShapeKind value.  Substrate identity verified at
// the header level (section 9); here we exercise the umbrella against
// the concrete-shape witnesses.

// Reduction-shaped function → Reduction kind.
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into>
    == fk::CanonicalShapeKind::Reduction);
static_assert(
    fk::canonical_shape_name_of_v<&kernel_tu_shapes::f_sum_into>
    == std::string_view{"Reduction"});

// UnaryTransform-shaped function → UnaryTransform kind.
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>
    == fk::CanonicalShapeKind::UnaryTransform);
static_assert(
    fk::canonical_shape_name_of_v<&kernel_tu_unary_shapes::f_unary_in_place>
    == std::string_view{"UnaryTransform"});

// BinaryTransform-shaped function → BinaryTransform kind.
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>
    == fk::CanonicalShapeKind::BinaryTransform);
static_assert(
    fk::canonical_shape_name_of_v<&kernel_tu_binary_shapes::f_binary_in_place>
    == std::string_view{"BinaryTransform"});

// Non-canonical witnesses route to NonCanonical (Fusion-fixture
// functions match no shape — they take int, not OwnedRegion).
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_fusion_shapes::p_double>
    == fk::CanonicalShapeKind::NonCanonical);
static_assert(
    fk::canonical_shape_name_of_v<&kernel_tu_fusion_shapes::p_double>
    == std::string_view{"NonCanonical"});

// All three canonical-shape witnesses ARE canonical-shape-admitted.
static_assert(fk::is_canonical_shape_v<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::is_canonical_shape_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::is_canonical_shape_v<&kernel_tu_binary_shapes::f_binary_in_place>);

// Inverse — they are NOT NonCanonical.
static_assert(!fk::is_non_canonical_v<&kernel_tu_shapes::f_sum_into>);
static_assert(!fk::is_non_canonical_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::is_non_canonical_v<&kernel_tu_binary_shapes::f_binary_in_place>);

// Substrate-identity on canonical_shape_kind_v + canonical_shape_name_of_v.
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into>
    == ext::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into>);
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>
    == ext::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(
    fk::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>
    == ext::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>);

// ═══════════════════════════════════════════════════════════════════
// ── 10. Cross-shape exclusivity matrix (V-039) ────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// CanonicalShape.h §5.6: shapes are MUTUALLY EXCLUSIVE — a function
// satisfies AT MOST ONE shape predicate.  Each positive witness from
// sections 3 / 6 / 7 admits ONLY its shape and rejects the other two
// across the D12 / D13 / D14 axis.  This is the load-bearing claim
// the dispatcher relies on: at most one branch is taken per FnPtr.

// Reduction-shaped: admitted by Reduction ONLY (rejects Unary +
// Binary).
static_assert(!fk::UnaryTransform<&kernel_tu_shapes::f_sum_into>);
static_assert(!fk::BinaryTransform<&kernel_tu_shapes::f_sum_into>);
static_assert( fk::Reduction<&kernel_tu_shapes::f_sum_into>);

// UnaryTransform-shaped: admitted by UnaryTransform ONLY (rejects
// Reduction + Binary).
static_assert( fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::BinaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::Reduction<&kernel_tu_unary_shapes::f_unary_in_place>);

// BinaryTransform-shaped: admitted by BinaryTransform ONLY (rejects
// Unary + Reduction).
static_assert(!fk::UnaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert( fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(!fk::Reduction<&kernel_tu_binary_shapes::f_binary_in_place>);

// Repeat the matrix for the out-of-place variants — confirms the
// exclusivity holds independent of in-place / out-of-place choice.
static_assert( fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);
static_assert(!fk::BinaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);
static_assert(!fk::Reduction<&kernel_tu_unary_shapes::f_unary_out_of_place>);

static_assert(!fk::UnaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);
static_assert( fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);
static_assert(!fk::Reduction<&kernel_tu_binary_shapes::f_binary_different_tags>);

// ═══════════════════════════════════════════════════════════════════
// ── 11. Cardinality FLOOR witness (append-friendly) ───────────────
// ═══════════════════════════════════════════════════════════════════
//
// FIXY-U-127/U-128 discipline: floor instead of equality so future
// appends to fixy::kernel:: don't silently break this TU.  V-038
// established floor 12; V-039 bumps to 38 (adds UnaryTransform 6 +
// BinaryTransform 9 + CanonicalShape umbrella 8 + Fusion 3 = +26).
static_assert(
    fk::self_test::kernel_alias_cardinality >= 38,
    "fixy::kernel:: cardinality floor — V-039 surface ships at least "
    "38 re-exports: 12 V-038 [reduce_into 2 + Reduction-shape 6 + "
    "reduce_into wrapper-detection 4] + 26 V-039 [UnaryTransform 6 + "
    "BinaryTransform 9 + CanonicalShape umbrella 8 + Fusion 3].  "
    "Substrate adoption of new shape recognizers MUST bump this "
    "floor.  FIXY-V-038-audit established the floor-vs-equality form "
    "after the V-038 off-by-one was caught post-ship.");

// ═══════════════════════════════════════════════════════════════════
// ── 12. Runtime smoke tests ────────────────────────────────────────
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

void test_runtime_fuse_round_trip() {
    // Build a fused callable via the fixy::kernel:: alias path, evaluate
    // at runtime — proves the alias resolves through both the F06
    // composability check and the F07 generator.  volatile seed
    // forces runtime evaluation, defeating constexpr folding.
    volatile int const seed = 9;
    auto fused = fk::fuse<
        &kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>();
    EXPECT_TRUE(fused(static_cast<int>(seed)) == 19);  // (9*2)+1 = 19
    EXPECT_TRUE(fused(0) == 1);
    EXPECT_TRUE(fused(-3) == -5);  // (-3*2)+1 = -5

    // Cross-element-type pair — int → double → double.  Use
    // double_bits_eq (defined at TU scope) to satisfy
    // -Werror=float-equal while still proving exact value equality;
    // all three expected values are exactly representable doubles.
    auto fused_d = fk::fuse<
        &kernel_tu_fusion_shapes::p_to_double,
        &kernel_tu_fusion_shapes::p_half>();
    EXPECT_TRUE(double_bits_eq(fused_d(static_cast<int>(seed)), 4.5));  // 9.0 * 0.5
    EXPECT_TRUE(double_bits_eq(fused_d(0),  0.0));
    EXPECT_TRUE(double_bits_eq(fused_d(10), 5.0));
}

void test_runtime_canonical_shape_dispatch() {
    // Volatile-bounded loop confirms dispatch is bit-stable through
    // the alias path — three positive shape kinds + NonCanonical
    // resolved consistently across the cap iterations.
    volatile std::size_t const cap = 16;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into>
            == fk::CanonicalShapeKind::Reduction);
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>
            == fk::CanonicalShapeKind::UnaryTransform);
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>
            == fk::CanonicalShapeKind::BinaryTransform);
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_fusion_shapes::p_double>
            == fk::CanonicalShapeKind::NonCanonical);

        // canonical_shape_name lookup — string_view round-trip across
        // the four most-relevant labels.
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::Reduction)
            == std::string_view{"Reduction"});
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::UnaryTransform)
            == std::string_view{"UnaryTransform"});
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::BinaryTransform)
            == std::string_view{"BinaryTransform"});
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::NonCanonical)
            == std::string_view{"NonCanonical"});
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
    run_test("test_runtime_fuse_round_trip",
             test_runtime_fuse_round_trip);
    run_test("test_runtime_canonical_shape_dispatch",
             test_runtime_canonical_shape_dispatch);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
