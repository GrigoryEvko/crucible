// The aliases under test are materialised here in one translation unit
// compiled under the project warning flags, so every embedded static_assert
// in the substrate headers also runs under that flag set.

#include <crucible/fixy/Kernel.h>

#include <crucible/safety/BinaryTransform.h>  // substrate side of identity
#include <crucible/safety/CanonicalShape.h>  // umbrella + dispatch
#include <crucible/safety/Fusion.h>  // composability
#include <crucible/safety/IsReduceInto.h>  // substrate side of identity
#include <crucible/safety/OwnedRegion.h>  // positive shape witnesses
#include <crucible/safety/Reduction.h>
#include <crucible/safety/UnaryTransform.h>  // substrate side of identity
#include <crucible/safety/reduce_into.h>

#include <bit>  // bit-exact FP compare
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>  // canonical shape names
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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

namespace fk = ::crucible::fixy::kernel;
namespace saf = ::crucible::safety;
namespace ext = ::crucible::safety::extract;

// Equality on a double is rejected at both compile time and run time, so the
// comparison goes through the bit pattern. Every value compared below is an
// exactly representable double, and halving such a value stays exact, so bit
// equality is the right test rather than an approximation.
[[nodiscard]] inline constexpr bool double_bits_eq(double a, double b) noexcept {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

struct PlusOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

struct MaxOp {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a > b ? a : b; }
};

struct kernel_tu_input_tag {};
struct kernel_tu_other_tag {};

using OR_int_input = saf::OwnedRegion<int, kernel_tu_input_tag>;
using OR_int_other = saf::OwnedRegion<int, kernel_tu_other_tag>;

// Every type built below is spelled through the alias, never through the
// substrate. That the two spellings name one type is asserted separately.
using RI_int_plus = fk::reduce_into<int, PlusOp>;
using RI_int_max = fk::reduce_into<int, MaxOp>;

}  // namespace

static_assert(std::is_same_v<RI_int_plus, saf::reduce_into<int, PlusOp>>,
              "fixy::kernel::reduce_into<int, PlusOp> must alias safety::reduce_into.");
static_assert(std::is_same_v<RI_int_max, saf::reduce_into<int, MaxOp>>,
              "fixy::kernel::reduce_into<int, MaxOp> must alias safety::reduce_into.");

static_assert(fk::is_reduction_op_v<PlusOp, int> == saf::is_reduction_op_v<PlusOp, int>);
static_assert(fk::is_reduction_op_v<MaxOp, int> == saf::is_reduction_op_v<MaxOp, int>);

static_assert(!std::is_copy_constructible_v<RI_int_plus>,
              "fixy::kernel::reduce_into is not copy-constructible. The accumulator "
              "is linear.");
static_assert(!std::is_copy_assignable_v<RI_int_plus>,
              "fixy::kernel::reduce_into is not copy-assignable. The accumulator is "
              "linear.");
static_assert(std::is_move_constructible_v<RI_int_plus>,
              "fixy::kernel::reduce_into is move-constructible. A dispatcher hands "
              "the accumulator to a worker by move.");
static_assert(std::is_move_assignable_v<RI_int_plus>, "fixy::kernel::reduce_into is move-assignable.");

// The op predicate is the only gate on the class-level requires-clause. If it
// drifts and lets a non-reducer through, the failure surfaces far away, in an
// invocability check inside a parallel reduction.
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

// The canonical reduction signature consumes one region by rvalue reference,
// borrows one accumulator by lvalue reference and returns void. Witnessing it
// needs OwnedRegion, which the header self-test declines to include, so the
// witness lives here instead.
namespace kernel_tu_shapes {

void f_sum_into(OR_int_input&&, RI_int_plus&) noexcept;

// Same region tag, different reducer.
void f_max_into(OR_int_input&&, RI_int_max&) noexcept;

// Any region tag is admitted.
void f_other_tag(OR_int_other&&, RI_int_plus&) noexcept;

// Shapes the concept must reject.
void f_no_param() noexcept;
void f_one_param(OR_int_input&&) noexcept;
void f_three_params(OR_int_input&&, RI_int_plus&, int) noexcept;
void f_two_ints(int, int) noexcept;
int f_returns_int(OR_int_input&&, RI_int_plus&) noexcept;

}  // namespace kernel_tu_shapes

static_assert(fk::Reduction<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::is_reduction_v<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_max_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_other_tag>);

// Each pairing below checks that the alias and the substrate recognise a
// witness the same way. Drift would mean a caller reaching the predicate by
// one spelling sees a different answer from a caller reaching it by the
// other.
static_assert(fk::Reduction<&kernel_tu_shapes::f_sum_into> == ext::Reduction<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_max_into> == ext::Reduction<&kernel_tu_shapes::f_max_into>);

static_assert(!fk::Reduction<&kernel_tu_shapes::f_no_param>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_one_param>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_three_params>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_two_ints>);
static_assert(!fk::Reduction<&kernel_tu_shapes::f_returns_int>);

static_assert(fk::Reduction<&kernel_tu_shapes::f_no_param> == ext::Reduction<&kernel_tu_shapes::f_no_param>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_returns_int> == ext::Reduction<&kernel_tu_shapes::f_returns_int>);

// Each extractor is exercised across two signatures, which catches an alias
// that re-exported the concept but dropped one of the extractors.
static_assert(std::is_same_v<fk::reduction_input_tag_t<&kernel_tu_shapes::f_sum_into>, kernel_tu_input_tag>);
static_assert(std::is_same_v<fk::reduction_input_tag_t<&kernel_tu_shapes::f_other_tag>, kernel_tu_other_tag>);
static_assert(std::is_same_v<fk::reduction_input_tag_t<&kernel_tu_shapes::f_sum_into>,
                             ext::reduction_input_tag_t<&kernel_tu_shapes::f_sum_into>>);

static_assert(std::is_same_v<fk::reduction_input_value_t<&kernel_tu_shapes::f_sum_into>, int>);
static_assert(std::is_same_v<fk::reduction_input_value_t<&kernel_tu_shapes::f_sum_into>,
                             ext::reduction_input_value_t<&kernel_tu_shapes::f_sum_into>>);

static_assert(std::is_same_v<fk::reduction_accumulator_t<&kernel_tu_shapes::f_sum_into>, int>);
static_assert(std::is_same_v<fk::reduction_accumulator_t<&kernel_tu_shapes::f_max_into>, int>);
static_assert(std::is_same_v<fk::reduction_accumulator_t<&kernel_tu_shapes::f_sum_into>,
                             ext::reduction_accumulator_t<&kernel_tu_shapes::f_sum_into>>);

// The two signatures share an accumulator type and differ only in the
// reducer, so the extraction has to distinguish them on the reducer alone.
static_assert(std::is_same_v<fk::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>, PlusOp>);
static_assert(std::is_same_v<fk::reduction_reducer_t<&kernel_tu_shapes::f_max_into>, MaxOp>);
static_assert(!std::is_same_v<fk::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>,
                              fk::reduction_reducer_t<&kernel_tu_shapes::f_max_into>>);
static_assert(std::is_same_v<fk::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>,
                             ext::reduction_reducer_t<&kernel_tu_shapes::f_sum_into>>);

// These predicates classify the wrapper type, not the accumulated value type,
// so a bare int is a negative witness.
static_assert(fk::is_reduce_into_v<RI_int_plus>);
static_assert(fk::is_reduce_into_v<RI_int_max>);
static_assert(!fk::is_reduce_into_v<int>);
static_assert(!fk::is_reduce_into_v<PlusOp>);

static_assert(fk::is_reduce_into_v<RI_int_plus> == ext::is_reduce_into_v<RI_int_plus>);
static_assert(fk::is_reduce_into_v<int> == ext::is_reduce_into_v<int>);

// References and const qualification are stripped before classification.
static_assert(fk::is_reduce_into_v<RI_int_plus&>);
static_assert(fk::is_reduce_into_v<RI_int_plus&&>);
static_assert(fk::is_reduce_into_v<RI_int_plus const&>);

static_assert(fk::IsReduceInto<RI_int_plus> == fk::is_reduce_into_v<RI_int_plus>);
static_assert(!fk::IsReduceInto<int>);

static_assert(std::is_same_v<fk::reduce_into_accumulator_t<RI_int_plus>, int>);
static_assert(std::is_same_v<fk::reduce_into_reducer_t<RI_int_plus>, PlusOp>);
static_assert(std::is_same_v<fk::reduce_into_accumulator_t<RI_int_max>, int>);
static_assert(std::is_same_v<fk::reduce_into_reducer_t<RI_int_max>, MaxOp>);

static_assert(std::is_same_v<fk::reduce_into_accumulator_t<RI_int_plus>, ext::reduce_into_accumulator_t<RI_int_plus>>);
static_assert(std::is_same_v<fk::reduce_into_reducer_t<RI_int_plus>, ext::reduce_into_reducer_t<RI_int_plus>>);

namespace kernel_tu_unary_shapes {

using OR_in_d = saf::OwnedRegion<double, kernel_tu_input_tag>;
using OR_out_d = saf::OwnedRegion<double, kernel_tu_other_tag>;

void f_unary_in_place(OR_int_input&&) noexcept;

OR_int_other f_unary_out_of_place(OR_int_input&&) noexcept;

// The element type changes across the transform, not just the tag.
OR_out_d f_unary_change_element(OR_in_d&&) noexcept;

void f_unary_arity_two(OR_int_input&&, OR_int_input&&) noexcept;

}  // namespace kernel_tu_unary_shapes

static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::is_unary_transform_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_change_element>);

static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>
              == ext::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>
              == ext::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);

// A void return means the transform works in place. That distinction picks
// the lowering: one buffer for the in-place path, two for the other.
static_assert(fk::is_in_place_unary_transform_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::is_in_place_unary_transform_v<&kernel_tu_unary_shapes::f_unary_out_of_place>);

static_assert(
    std::is_same_v<fk::unary_transform_input_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>, kernel_tu_input_tag>);
static_assert(std::is_same_v<fk::unary_transform_input_value_t<&kernel_tu_unary_shapes::f_unary_in_place>, int>);

// An in-place transform reports a void output tag, since nothing is
// allocated for it.
static_assert(std::is_same_v<fk::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>, void>);

static_assert(std::is_same_v<fk::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_out_of_place>,
                             kernel_tu_other_tag>);

// A non-int element type, so the extractor cannot be reading a fixed type.
static_assert(
    std::is_same_v<fk::unary_transform_input_value_t<&kernel_tu_unary_shapes::f_unary_change_element>, double>);

static_assert(std::is_same_v<fk::unary_transform_input_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>,
                             ext::unary_transform_input_tag_t<&kernel_tu_unary_shapes::f_unary_in_place>>);
static_assert(std::is_same_v<fk::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_out_of_place>,
                             ext::unary_transform_output_tag_t<&kernel_tu_unary_shapes::f_unary_out_of_place>>);

static_assert(!fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_arity_two>);

namespace kernel_tu_binary_shapes {

// In place against the left-hand region.
void f_binary_in_place(OR_int_input&&, OR_int_input&&) noexcept;

OR_int_input f_binary_same_tag(OR_int_input&&, OR_int_input&&) noexcept;

OR_int_other f_binary_different_tags(OR_int_input&&, OR_int_other&&) noexcept;

void f_binary_arity_one(OR_int_input&&) noexcept;

void f_binary_arity_three(OR_int_input&&, OR_int_input&&, OR_int_input&&) noexcept;

}  // namespace kernel_tu_binary_shapes

static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::is_binary_transform_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_same_tag>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);

static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>
              == ext::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>
              == ext::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);

static_assert(fk::is_in_place_binary_transform_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(!fk::is_in_place_binary_transform_v<&kernel_tu_binary_shapes::f_binary_same_tag>);
static_assert(!fk::is_in_place_binary_transform_v<&kernel_tu_binary_shapes::f_binary_different_tags>);

static_assert(
    std::is_same_v<fk::binary_transform_lhs_tag_t<&kernel_tu_binary_shapes::f_binary_in_place>, kernel_tu_input_tag>);
static_assert(
    std::is_same_v<fk::binary_transform_rhs_tag_t<&kernel_tu_binary_shapes::f_binary_in_place>, kernel_tu_input_tag>);
static_assert(std::is_same_v<fk::binary_transform_lhs_value_t<&kernel_tu_binary_shapes::f_binary_in_place>, int>);
static_assert(std::is_same_v<fk::binary_transform_rhs_value_t<&kernel_tu_binary_shapes::f_binary_in_place>, int>);

static_assert(std::is_same_v<fk::binary_transform_output_tag_t<&kernel_tu_binary_shapes::f_binary_in_place>, void>);

static_assert(std::is_same_v<fk::binary_transform_lhs_tag_t<&kernel_tu_binary_shapes::f_binary_different_tags>,
                             kernel_tu_input_tag>);
static_assert(std::is_same_v<fk::binary_transform_rhs_tag_t<&kernel_tu_binary_shapes::f_binary_different_tags>,
                             kernel_tu_other_tag>);
static_assert(std::is_same_v<fk::binary_transform_output_tag_t<&kernel_tu_binary_shapes::f_binary_different_tags>,
                             kernel_tu_other_tag>);

// Two same-tag witnesses and one distinct-tag witness fill the predicate's
// truth table.
static_assert(fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_same_tag>);
static_assert(!fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_different_tags>);

static_assert(fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_in_place>
              == ext::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_different_tags>
              == ext::binary_transform_has_same_tag_v<&kernel_tu_binary_shapes::f_binary_different_tags>);

static_assert(!fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_arity_one>);
static_assert(!fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_arity_three>);

// Composability does not involve OwnedRegion, so plain functions are enough
// to witness it. Note that the substrate for these names sits directly in
// crucible::safety rather than in the extract namespace the sections above
// compare against.

namespace kernel_tu_fusion_shapes {

inline int p_double(int x) noexcept { return x * 2; }
inline int p_inc(int x) noexcept { return x + 1; }

// The second pair changes the element type along the way.
inline double p_to_double(int x) noexcept { return static_cast<double>(x); }
inline double p_half(double x) noexcept { return x * 0.5; }

}  // namespace kernel_tu_fusion_shapes

static_assert(fk::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);

static_assert(fk::can_fuse_v<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>);

static_assert(fk::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>
              == saf::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);
static_assert(fk::can_fuse_v<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>
              == saf::can_fuse_v<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>);

static_assert(fk::IsFusable<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);
static_assert(fk::IsFusable<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>
              == fk::can_fuse_v<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>);

constexpr auto fused_double_then_inc = fk::fuse<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>();
static_assert(fused_double_then_inc(7) == 15);  // (7*2)+1
static_assert(fused_double_then_inc(0) == 1);
static_assert(fused_double_then_inc(-3) == -5);  // (-3*2)+1
static_assert(noexcept(fused_double_then_inc(7)));
// The result type comes from the second function of the pair.
static_assert(std::is_same_v<decltype(fused_double_then_inc(0)), int>);

constexpr auto fused_to_double_then_half =
    fk::fuse<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>();
static_assert(double_bits_eq(fused_to_double_then_half(4), 2.0));  // 4.0 * 0.5
static_assert(double_bits_eq(fused_to_double_then_half(10), 5.0));
static_assert(std::is_same_v<decltype(fused_to_double_then_half(0)), double>);

static_assert(fk::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into> == fk::CanonicalShapeKind::Reduction);
static_assert(fk::canonical_shape_name_of_v<&kernel_tu_shapes::f_sum_into> == std::string_view{"Reduction"});

static_assert(fk::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>
              == fk::CanonicalShapeKind::UnaryTransform);
static_assert(fk::canonical_shape_name_of_v<&kernel_tu_unary_shapes::f_unary_in_place>
              == std::string_view{"UnaryTransform"});

static_assert(fk::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>
              == fk::CanonicalShapeKind::BinaryTransform);
static_assert(fk::canonical_shape_name_of_v<&kernel_tu_binary_shapes::f_binary_in_place>
              == std::string_view{"BinaryTransform"});

// The fusion fixtures take an int rather than a region, so they match no
// canonical shape at all.
static_assert(fk::canonical_shape_kind_v<&kernel_tu_fusion_shapes::p_double> == fk::CanonicalShapeKind::NonCanonical);
static_assert(fk::canonical_shape_name_of_v<&kernel_tu_fusion_shapes::p_double> == std::string_view{"NonCanonical"});

static_assert(fk::is_canonical_shape_v<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::is_canonical_shape_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::is_canonical_shape_v<&kernel_tu_binary_shapes::f_binary_in_place>);

static_assert(!fk::is_non_canonical_v<&kernel_tu_shapes::f_sum_into>);
static_assert(!fk::is_non_canonical_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::is_non_canonical_v<&kernel_tu_binary_shapes::f_binary_in_place>);

static_assert(fk::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into>
              == ext::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>
              == ext::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(fk::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>
              == ext::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>);

// The shapes are mutually exclusive: a function satisfies at most one of the
// three predicates. The dispatcher takes at most one branch per function as a
// consequence, which is what the rest of this block pins.
static_assert(!fk::UnaryTransform<&kernel_tu_shapes::f_sum_into>);
static_assert(!fk::BinaryTransform<&kernel_tu_shapes::f_sum_into>);
static_assert(fk::Reduction<&kernel_tu_shapes::f_sum_into>);

static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::BinaryTransform<&kernel_tu_unary_shapes::f_unary_in_place>);
static_assert(!fk::Reduction<&kernel_tu_unary_shapes::f_unary_in_place>);

static_assert(!fk::UnaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_in_place>);
static_assert(!fk::Reduction<&kernel_tu_binary_shapes::f_binary_in_place>);

// The same matrix over the out-of-place variants, so exclusivity does not
// rest on the in-place choice.
static_assert(fk::UnaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);
static_assert(!fk::BinaryTransform<&kernel_tu_unary_shapes::f_unary_out_of_place>);
static_assert(!fk::Reduction<&kernel_tu_unary_shapes::f_unary_out_of_place>);

static_assert(!fk::UnaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);
static_assert(fk::BinaryTransform<&kernel_tu_binary_shapes::f_binary_different_tags>);
static_assert(!fk::Reduction<&kernel_tu_binary_shapes::f_binary_different_tags>);

// A floor rather than an equality, so that adding a re-export does not redden
// this translation unit. The count breaks down as reduce_into 2, the
// reduction shape 6, reduce_into detection 4, UnaryTransform 6,
// BinaryTransform 9, the canonical-shape umbrella 8 and Fusion 3. Adopting a
// new shape recognizer raises the floor.
static_assert(fk::self_test::kernel_alias_cardinality >= 38, "The fixy::kernel surface re-exports at least 38 names.");

namespace {

void test_reduction_smoke_through_alias() {
    // Reaching the substrate's own smoke entry point through the alias shows
    // the include path resolves under the strictest warning flags.
    EXPECT_TRUE(ext::reduction_smoke_test());
}

void test_reduce_into_smoke_through_alias() { EXPECT_TRUE(saf::reduce_into_smoke_test()); }

void test_is_reduce_into_smoke_through_alias() { EXPECT_TRUE(ext::is_reduce_into_smoke_test()); }

void test_runtime_reduce_into_round_trip() {
    // The alias has to resolve to a type whose value semantics work at run
    // time, not only to one that satisfies the type-level assertions above.
    volatile int const seed = 0;
    RI_int_plus acc{static_cast<int>(seed), PlusOp{}};
    acc.combine(7);
    acc.combine(35);
    EXPECT_TRUE(acc.peek() == 42);

    int extracted = std::move(acc).consume();
    EXPECT_TRUE(extracted == 42);

    RI_int_max rmax{0, MaxOp{}};
    rmax.combine(7);
    rmax.combine(99);
    rmax.combine(3);
    EXPECT_TRUE(rmax.peek() == 99);
}

void test_runtime_recognition_consistency() {
    // The loop bound is volatile so the compiler cannot fold the recognition
    // away and leave the loop empty.
    volatile std::size_t const cap = 32;
    bool const baseline_pos = fk::is_reduction_v<&kernel_tu_shapes::f_sum_into>;
    bool const baseline_neg = !fk::is_reduction_v<&kernel_tu_shapes::f_no_param>;
    EXPECT_TRUE(baseline_pos);
    EXPECT_TRUE(baseline_neg);
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(baseline_pos == fk::is_reduction_v<&kernel_tu_shapes::f_sum_into>);
        EXPECT_TRUE(baseline_neg == !fk::is_reduction_v<&kernel_tu_shapes::f_no_param>);
        EXPECT_TRUE(fk::is_reduce_into_v<RI_int_plus>);
        EXPECT_TRUE(!fk::is_reduce_into_v<int>);
    }
}

void test_runtime_fuse_round_trip() {
    // The seed is volatile so the fused call is evaluated at run time rather
    // than folded at compile time.
    volatile int const seed = 9;
    auto fused = fk::fuse<&kernel_tu_fusion_shapes::p_double, &kernel_tu_fusion_shapes::p_inc>();
    EXPECT_TRUE(fused(static_cast<int>(seed)) == 19);  // (9*2)+1 = 19
    EXPECT_TRUE(fused(0) == 1);
    EXPECT_TRUE(fused(-3) == -5);  // (-3*2)+1 = -5

    auto fused_d = fk::fuse<&kernel_tu_fusion_shapes::p_to_double, &kernel_tu_fusion_shapes::p_half>();
    EXPECT_TRUE(double_bits_eq(fused_d(static_cast<int>(seed)), 4.5));  // 9.0 * 0.5
    EXPECT_TRUE(double_bits_eq(fused_d(0), 0.0));
    EXPECT_TRUE(double_bits_eq(fused_d(10), 5.0));
}

void test_runtime_canonical_shape_dispatch() {
    volatile std::size_t const cap = 16;
    for (std::size_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_shapes::f_sum_into> == fk::CanonicalShapeKind::Reduction);
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_unary_shapes::f_unary_in_place>
                    == fk::CanonicalShapeKind::UnaryTransform);
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_binary_shapes::f_binary_in_place>
                    == fk::CanonicalShapeKind::BinaryTransform);
        EXPECT_TRUE(fk::canonical_shape_kind_v<&kernel_tu_fusion_shapes::p_double>
                    == fk::CanonicalShapeKind::NonCanonical);

        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::Reduction) == std::string_view{"Reduction"});
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::UnaryTransform)
                    == std::string_view{"UnaryTransform"});
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::BinaryTransform)
                    == std::string_view{"BinaryTransform"});
        EXPECT_TRUE(fk::canonical_shape_name(fk::CanonicalShapeKind::NonCanonical) == std::string_view{"NonCanonical"});
    }
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_fixy_kernel:\n");
    run_test("test_reduction_smoke_through_alias", test_reduction_smoke_through_alias);
    run_test("test_reduce_into_smoke_through_alias", test_reduce_into_smoke_through_alias);
    run_test("test_is_reduce_into_smoke_through_alias", test_is_reduce_into_smoke_through_alias);
    run_test("test_runtime_reduce_into_round_trip", test_runtime_reduce_into_round_trip);
    run_test("test_runtime_recognition_consistency", test_runtime_recognition_consistency);
    run_test("test_runtime_fuse_round_trip", test_runtime_fuse_round_trip);
    run_test("test_runtime_canonical_shape_dispatch", test_runtime_canonical_shape_dispatch);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
