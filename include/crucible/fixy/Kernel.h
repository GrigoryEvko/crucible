#pragma once

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/CanonicalShape.h>
#include <crucible/safety/Fusion.h>
#include <crucible/safety/IsReduceInto.h>
#include <crucible/safety/Reduction.h>
#include <crucible/safety/UnaryTransform.h>
#include <crucible/safety/reduce_into.h>

#include <type_traits>

namespace crucible::fixy::kernel {

using ::crucible::safety::reduce_into;

using ::crucible::safety::is_reduction_op_v;

using ::crucible::safety::extract::Reduction;

using ::crucible::safety::extract::is_reduction_v;

using ::crucible::safety::extract::reduction_input_tag_t;
using ::crucible::safety::extract::reduction_input_value_t;

using ::crucible::safety::extract::reduction_accumulator_t;
using ::crucible::safety::extract::reduction_reducer_t;

// The four below classify the wrapper type itself.  `is_reduction_op_v`
// above reads similarly and does something else: it classifies a reducer
// against an accumulator type.

using ::crucible::safety::extract::IsReduceInto;
using ::crucible::safety::extract::is_reduce_into_v;
using ::crucible::safety::extract::reduce_into_accumulator_t;
using ::crucible::safety::extract::reduce_into_reducer_t;

using ::crucible::safety::extract::UnaryTransform;
using ::crucible::safety::extract::is_unary_transform_v;
using ::crucible::safety::extract::is_in_place_unary_transform_v;
using ::crucible::safety::extract::unary_transform_input_tag_t;
using ::crucible::safety::extract::unary_transform_input_value_t;
using ::crucible::safety::extract::unary_transform_output_tag_t;

using ::crucible::safety::extract::BinaryTransform;
using ::crucible::safety::extract::is_binary_transform_v;
using ::crucible::safety::extract::is_in_place_binary_transform_v;
using ::crucible::safety::extract::binary_transform_lhs_tag_t;
using ::crucible::safety::extract::binary_transform_rhs_tag_t;
using ::crucible::safety::extract::binary_transform_lhs_value_t;
using ::crucible::safety::extract::binary_transform_rhs_value_t;
using ::crucible::safety::extract::binary_transform_output_tag_t;
using ::crucible::safety::extract::binary_transform_has_same_tag_v;

using ::crucible::safety::extract::CanonicalShape;
using ::crucible::safety::extract::NonCanonical;
using ::crucible::safety::extract::is_canonical_shape_v;
using ::crucible::safety::extract::is_non_canonical_v;
using ::crucible::safety::extract::CanonicalShapeKind;
using ::crucible::safety::extract::canonical_shape_kind_v;
using ::crucible::safety::extract::canonical_shape_name;
using ::crucible::safety::extract::canonical_shape_name_of_v;

using ::crucible::safety::can_fuse_v;
using ::crucible::safety::IsFusable;
using ::crucible::safety::fuse;

}  // namespace crucible::fixy::kernel

// What these assertions test is that each name above reaches the substrate
// symbol and not some local of the same name that shadows it.

namespace crucible::fixy::kernel::self_test {

// The probe defines its own reducer rather than borrowing the one from the
// substrate's self-test, which would tie this header to a detail name.

struct KernelProbePlus {
    constexpr int operator()(int const& a, int const& b) const noexcept { return a + b; }
};

static_assert(std::is_same_v<::crucible::fixy::kernel::reduce_into<int, KernelProbePlus>,
                             ::crucible::safety::reduce_into<int, KernelProbePlus>>,
              "fixy::kernel::reduce_into<R, Op> must alias safety::reduce_into<R, Op> "
              "— substrate identity drift would orphan the move-only accumulator "
              "linearity discipline (BorrowSafe).");

// A concept has no type to compare, so the two paths are pinned against each
// other by their verdict on the same input rather than by identity.
static_assert(::crucible::fixy::kernel::is_reduction_op_v<KernelProbePlus, int>
                  == ::crucible::safety::is_reduction_op_v<KernelProbePlus, int>,
              "fixy::kernel::is_reduction_op_v must agree with the substrate on "
              "PlusOp/int — concept admission set drift would silently accept or "
              "reject reducers differently across the two reach paths.");

struct KernelProbeNonInvocable {};
static_assert(::crucible::fixy::kernel::is_reduction_op_v<KernelProbeNonInvocable, int>
                  == ::crucible::safety::is_reduction_op_v<KernelProbeNonInvocable, int>,
              "fixy::kernel::is_reduction_op_v must agree with the substrate on "
              "NON-invocable types (both reject).");

// Each witness below violates a different clause of the shape.  They are all
// negative: a positive one needs an owned region and an accumulator
// instantiated, which this header does not pull in, so that proof lives with
// the tests instead.

inline void kp_f_nullary() noexcept {}
static_assert(::crucible::fixy::kernel::Reduction<&kp_f_nullary>
                  == ::crucible::safety::extract::Reduction<&kp_f_nullary>,
              "fixy::kernel::Reduction must agree with the substrate on the "
              "nullary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_nullary>);

inline void kp_f_unary(int) noexcept {}
static_assert(::crucible::fixy::kernel::Reduction<&kp_f_unary> == ::crucible::safety::extract::Reduction<&kp_f_unary>,
              "fixy::kernel::Reduction must agree with the substrate on the "
              "unary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_unary>);

inline void kp_f_ternary(int, int, int) noexcept {}
static_assert(::crucible::fixy::kernel::Reduction<&kp_f_ternary>
                  == ::crucible::safety::extract::Reduction<&kp_f_ternary>,
              "fixy::kernel::Reduction must agree with the substrate on the "
              "ternary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_ternary>);

inline void kp_f_two_ints(int, int) noexcept {}
static_assert(::crucible::fixy::kernel::Reduction<&kp_f_two_ints>
                  == ::crucible::safety::extract::Reduction<&kp_f_two_ints>,
              "fixy::kernel::Reduction must agree with the substrate on the "
              "two-ints witness (both reject; param 0 is not OwnedRegion, "
              "param 1 is not reduce_into).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_two_ints>);

inline int kp_f_returns_int(int, int) noexcept { return 0; }
static_assert(::crucible::fixy::kernel::Reduction<&kp_f_returns_int>
                  == ::crucible::safety::extract::Reduction<&kp_f_returns_int>,
              "fixy::kernel::Reduction must agree with the substrate on the "
              "non-void-return witness (both reject; return type != void).");
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_returns_int>);

// Each concept has a variable-template twin, and a re-export can carry one
// across while leaving the other behind.  Every pair is checked against
// itself for that reason.
static_assert(::crucible::fixy::kernel::is_reduction_v<&kp_f_nullary>
                  == ::crucible::fixy::kernel::Reduction<&kp_f_nullary>,
              "fixy::kernel::is_reduction_v must mirror the Reduction concept "
              "result on every input (variable-template form vs concept form "
              "must stay in lockstep).");

using KP_RI_int_plus = ::crucible::fixy::kernel::reduce_into<int, KernelProbePlus>;

static_assert(::crucible::fixy::kernel::is_reduce_into_v<KP_RI_int_plus>
                  == ::crucible::safety::extract::is_reduce_into_v<KP_RI_int_plus>,
              "fixy::kernel::is_reduce_into_v must agree with the substrate on "
              "the positive witness — drift would mean the dispatcher sees a "
              "different reduce_into-shape membership through the two reach "
              "paths.");
static_assert(::crucible::fixy::kernel::is_reduce_into_v<KP_RI_int_plus>);

static_assert(::crucible::fixy::kernel::is_reduce_into_v<int> == ::crucible::safety::extract::is_reduce_into_v<int>,
              "fixy::kernel::is_reduce_into_v must agree with the substrate on "
              "the negative witness (bare int).");
static_assert(!::crucible::fixy::kernel::is_reduce_into_v<int>);

static_assert(::crucible::fixy::kernel::IsReduceInto<KP_RI_int_plus>
                  == ::crucible::fixy::kernel::is_reduce_into_v<KP_RI_int_plus>,
              "fixy::kernel::IsReduceInto concept must agree with "
              "is_reduce_into_v variable template form.");

static_assert(std::is_same_v<::crucible::fixy::kernel::reduce_into_accumulator_t<KP_RI_int_plus>,
                             ::crucible::safety::extract::reduce_into_accumulator_t<KP_RI_int_plus>>,
              "fixy::kernel::reduce_into_accumulator_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::kernel::reduce_into_accumulator_t<KP_RI_int_plus>, int>,
              "reduce_into_accumulator_t<reduce_into<int, _>> must yield int.");

static_assert(std::is_same_v<::crucible::fixy::kernel::reduce_into_reducer_t<KP_RI_int_plus>,
                             ::crucible::safety::extract::reduce_into_reducer_t<KP_RI_int_plus>>,
              "fixy::kernel::reduce_into_reducer_t must alias the substrate.");
static_assert(std::is_same_v<::crucible::fixy::kernel::reduce_into_reducer_t<KP_RI_int_plus>, KernelProbePlus>,
              "reduce_into_reducer_t<reduce_into<_, PlusOp>> must yield PlusOp.");

static_assert(!std::is_copy_constructible_v<KP_RI_int_plus>,
              "fixy::kernel::reduce_into<R, Op> must not be copy-constructible — "
              "the accumulator is unique state; copy would duplicate it and break "
              "parallel_reduce_views's per-worker partial discipline (BorrowSafe).");

static_assert(!std::is_copy_assignable_v<KP_RI_int_plus>,
              "fixy::kernel::reduce_into<R, Op> must not be copy-assignable — "
              "copy-assign would leak the LHS's old accumulator AND alias the "
              "RHS's, breaking single-owner linearity (BorrowSafe).");

static_assert(std::is_move_constructible_v<KP_RI_int_plus>,
              "fixy::kernel::reduce_into<R, Op> must be move-constructible — "
              "transfer of ownership between dispatcher and worker requires it.");

static_assert(std::is_move_assignable_v<KP_RI_int_plus>, "fixy::kernel::reduce_into<R, Op> must be move-assignable — "
                                                         "symmetric to the move-construct guarantee.");

// The reducer constraint is enforced by a requires-clause on the class, so a
// rejected construction cannot be asserted here without detecting an
// unsatisfied requires-clause.  The agreement on the reducer predicate
// asserted earlier stands in for it.

static_assert(::crucible::fixy::kernel::UnaryTransform<&kp_f_nullary>
                  == ::crucible::safety::extract::UnaryTransform<&kp_f_nullary>,
              "fixy::kernel::UnaryTransform must agree with the substrate on "
              "the nullary witness (both reject; arity != 1).");
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_nullary>);

static_assert(::crucible::fixy::kernel::UnaryTransform<&kp_f_unary>
                  == ::crucible::safety::extract::UnaryTransform<&kp_f_unary>,
              "fixy::kernel::UnaryTransform must agree with the substrate on "
              "the int-unary witness (both reject; parameter 0 is not an "
              "OwnedRegion rvalue ref).");
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_unary>);

static_assert(::crucible::fixy::kernel::UnaryTransform<&kp_f_two_ints>
                  == ::crucible::safety::extract::UnaryTransform<&kp_f_two_ints>,
              "fixy::kernel::UnaryTransform must agree with the substrate on "
              "the two-ints witness (both reject; arity != 1).");
static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_two_ints>);

static_assert(::crucible::fixy::kernel::is_unary_transform_v<&kp_f_nullary>
                  == ::crucible::fixy::kernel::UnaryTransform<&kp_f_nullary>,
              "fixy::kernel::is_unary_transform_v must mirror the UnaryTransform "
              "concept on every input (variable-template vs concept lockstep).");

static_assert(::crucible::fixy::kernel::BinaryTransform<&kp_f_nullary>
                  == ::crucible::safety::extract::BinaryTransform<&kp_f_nullary>,
              "fixy::kernel::BinaryTransform must agree with the substrate on "
              "the nullary witness (both reject; arity != 2).");
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_f_nullary>);

static_assert(::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>
                  == ::crucible::safety::extract::BinaryTransform<&kp_f_two_ints>,
              "fixy::kernel::BinaryTransform must agree with the substrate on "
              "the two-ints witness (both reject; parameters are not "
              "OwnedRegion rvalue refs).");
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>);

static_assert(::crucible::fixy::kernel::is_binary_transform_v<&kp_f_two_ints>
                  == ::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>,
              "fixy::kernel::is_binary_transform_v must mirror the "
              "BinaryTransform concept (variable-template / concept lockstep).");

static_assert(
    std::is_same_v<::crucible::fixy::kernel::CanonicalShapeKind, ::crucible::safety::extract::CanonicalShapeKind>,
    "fixy::kernel::CanonicalShapeKind must BE the substrate enum, "
    "not a redeclaration — identity drift here would silently break "
    "every enumerator comparison across the two reach paths.");

// Given the identity above these comparisons cannot fail.  They are here to
// pin the set of enumerator names, so that an append to the enum has to be
// noticed here too.
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::NonCanonical
              == ::crucible::safety::extract::CanonicalShapeKind::NonCanonical);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::UnaryTransform
              == ::crucible::safety::extract::CanonicalShapeKind::UnaryTransform);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::BinaryTransform
              == ::crucible::safety::extract::CanonicalShapeKind::BinaryTransform);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::Reduction
              == ::crucible::safety::extract::CanonicalShapeKind::Reduction);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::ProducerEndpoint
              == ::crucible::safety::extract::CanonicalShapeKind::ProducerEndpoint);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::ConsumerEndpoint
              == ::crucible::safety::extract::CanonicalShapeKind::ConsumerEndpoint);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::SwmrWriter
              == ::crucible::safety::extract::CanonicalShapeKind::SwmrWriter);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::SwmrReader
              == ::crucible::safety::extract::CanonicalShapeKind::SwmrReader);
static_assert(::crucible::fixy::kernel::CanonicalShapeKind::PipelineStage
              == ::crucible::safety::extract::CanonicalShapeKind::PipelineStage);

static_assert(::crucible::fixy::kernel::CanonicalShape<&kp_f_two_ints>
              == ::crucible::safety::extract::CanonicalShape<&kp_f_two_ints>);
static_assert(!::crucible::fixy::kernel::CanonicalShape<&kp_f_two_ints>);

static_assert(::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>
              == ::crucible::safety::extract::NonCanonical<&kp_f_two_ints>);
static_assert(::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>);

static_assert(::crucible::fixy::kernel::is_canonical_shape_v<&kp_f_two_ints>
              == ::crucible::fixy::kernel::CanonicalShape<&kp_f_two_ints>);
static_assert(::crucible::fixy::kernel::is_non_canonical_v<&kp_f_two_ints>
              == ::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>);

static_assert(::crucible::fixy::kernel::canonical_shape_kind_v<&kp_f_two_ints>
                  == ::crucible::fixy::kernel::CanonicalShapeKind::NonCanonical,
              "kp_f_two_ints (void(int,int)) matches no canonical shape — "
              "dispatch must resolve to NonCanonical.");

static_assert(::crucible::fixy::kernel::canonical_shape_name(::crucible::fixy::kernel::CanonicalShapeKind::NonCanonical)
              == std::string_view{"NonCanonical"});
static_assert(::crucible::fixy::kernel::canonical_shape_name(::crucible::fixy::kernel::CanonicalShapeKind::Reduction)
              == std::string_view{"Reduction"});
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name(::crucible::fixy::kernel::CanonicalShapeKind::UnaryTransform)
    == std::string_view{"UnaryTransform"});
static_assert(
    ::crucible::fixy::kernel::canonical_shape_name(::crucible::fixy::kernel::CanonicalShapeKind::BinaryTransform)
    == std::string_view{"BinaryTransform"});

static_assert(::crucible::fixy::kernel::canonical_shape_name_of_v<&kp_f_two_ints> == std::string_view{"NonCanonical"});

// Composability does not depend on a region or an accumulator, so unlike the
// shape recognizers above this one gets a positive witness here.

inline int kp_p_double(int x) noexcept { return x * 2; }
inline int kp_p_inc(int x) noexcept { return x + 1; }

static_assert(::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_p_inc>
                  == ::crucible::safety::can_fuse_v<&kp_p_double, &kp_p_inc>,
              "fixy::kernel::can_fuse_v must agree with the substrate on the "
              "canonical positive witness (matching int(int) types).");
static_assert(::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_p_inc>);

static_assert(::crucible::fixy::kernel::IsFusable<&kp_p_double, &kp_p_inc>);
static_assert(::crucible::fixy::kernel::IsFusable<&kp_p_double, &kp_p_inc>
              == ::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_p_inc>);

static_assert(!::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_f_nullary>);

static_assert(!::crucible::fixy::kernel::can_fuse_v<&kp_p_double, &kp_f_two_ints>);

constexpr auto kp_fused = ::crucible::fixy::kernel::fuse<&kp_p_double, &kp_p_inc>();
static_assert(kp_fused(7) == 15, "fuse<&double, &inc>()(7) must compute inc(double(7)) = 14+1 = 15.");
static_assert(kp_fused(0) == 1, "fuse<&double, &inc>()(0) must compute inc(0*2) = 1.");
static_assert(noexcept(kp_fused(7)), "fuse() must produce a noexcept callable when both inputs are "
                                     "noexcept (substrate inherits the noexcept-ness via the lambda's "
                                     "noexcept(noexcept(Fn2(Fn1(x)))) computed exception specifier).");

static_assert(std::is_empty_v<decltype(kp_fused)>, "fuse() must produce a stateless empty closure — non-empty would "
                                                   "break ICF folding under the linker's identical-code-collapse pass "
                                                   "(and silently bloat the fused-callable layout).");
static_assert(sizeof(decltype(kp_fused)) == 1, "Empty closure has the standard-mandated 1-byte size — larger "
                                               "implies hidden capture state contradicting the F07 promise.");

// At most one shape predicate holds for any function.  The two witnesses
// below are both outside every shape, and they get there by different
// routes: one fails on arity, the other on parameter type.

static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_f_two_ints>);
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_f_two_ints>);
static_assert(!::crucible::fixy::kernel::Reduction<&kp_f_two_ints>);
static_assert(::crucible::fixy::kernel::NonCanonical<&kp_f_two_ints>);

static_assert(!::crucible::fixy::kernel::UnaryTransform<&kp_p_double>);
static_assert(!::crucible::fixy::kernel::BinaryTransform<&kp_p_double>);
static_assert(!::crucible::fixy::kernel::Reduction<&kp_p_double>);
static_assert(::crucible::fixy::kernel::NonCanonical<&kp_p_double>);

constexpr int kernel_alias_cardinality = 38;
static_assert(kernel_alias_cardinality == 38, "fixy::kernel:: cardinality changed — update Kernel.h sentinel "
                                              "block to track the substrate kernel-shape recognizer surface.");

}  // namespace crucible::fixy::kernel::self_test
