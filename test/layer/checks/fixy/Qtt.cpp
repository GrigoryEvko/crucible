// The compile-time checks of fixy/Qtt.h.

#include <fixy/Qtt.h>

#include <foundation/Lifetime.h>
#include <foundation/Linear.h>
#include <foundation/core/Index.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/SharedPermission.h>

#include <memory>

namespace fixy {

// Both builds are pinned, and each pin is written so the other build
// cannot satisfy it by accident.  fixy/Qtt.h keeps the pins of the
// tracked build, because only a translation unit that sets
// CRUCIBLE_QTT_TRACK_CONSUME can read them: each unit of a test build.
//
// Off: the grade is empty, the tracker is empty, and the wrapper is the
// value.  This is the shape every hot path was measured against.
static_assert(qtt_consume_tracked || sizeof(Linear<int>) == sizeof(int));
static_assert(qtt_consume_tracked || sizeof(Linear<void*>) == sizeof(void*));
static_assert(qtt_consume_tracked || sizeof(Linear<long long>) == sizeof(long long));
static_assert(qtt_consume_tracked || sizeof(Affine<int>) == sizeof(int));
static_assert(qtt_consume_tracked || sizeof(Affine<void*>) == sizeof(void*));
static_assert(qtt_consume_tracked || sizeof(Affine<long long>) == sizeof(long long));
static_assert(qtt_consume_tracked || std::is_trivially_destructible_v<Linear<int>>);
static_assert(qtt_consume_tracked || std::is_trivially_move_constructible_v<Linear<int>>);

// The consume state of fixy/Qtt.h: a T that declares a niche holds the
// state in its own bytes in the tracked build, so the wrapper has the size of
// T in each build.  Its destructor reads the state only in the tracked build.
static_assert(sizeof(Linear<::foundation::core::Index<8>>) == sizeof(::foundation::core::Index<8>));
static_assert(sizeof(Affine<::foundation::core::Index<8>>) == sizeof(::foundation::core::Index<8>));
static_assert(qtt_consume_tracked == !std::is_trivially_destructible_v<Linear<::foundation::core::Index<8>>>);
static_assert(qtt_consume_tracked == !std::is_trivially_move_constructible_v<Linear<::foundation::core::Index<8>>>);

static_assert(Linear<int>::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(Affine<int>::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(std::is_same_v<Linear<int>::lattice_type, ::foundation::algebra::lattices::qtt::LinearGrade>);
static_assert(std::is_same_v<Affine<int>::lattice_type, ::foundation::algebra::lattices::qtt::AffineGrade>);

// Qtt carries a non-type parameter, which is the case the reflection
// form in foundation/reflect/Instance.h exists for.  The cv-ref strip
// and the non-template rejection are pinned beside it.
static_assert(::foundation::reflect::IsInstanceOf<Linear<int>, ^^Qtt>);
static_assert(::foundation::reflect::IsInstanceOf<Affine<int>, ^^Qtt>);
static_assert(::foundation::reflect::IsInstanceOf<Linear<int> const&, ^^Qtt>);
static_assert(::foundation::reflect::IsInstanceOf<Linear<int>&&, ^^Qtt>);
static_assert(!::foundation::reflect::IsInstanceOf<int, ^^Qtt>);
static_assert(!::foundation::reflect::IsInstanceOf<void, ^^Qtt>);
static_assert(!::foundation::reflect::IsInstanceOf<std::unique_ptr<int>, ^^Qtt>);
static_assert(!::foundation::reflect::IsInstanceOf<Linear<int>, ^^std::unique_ptr>);

// The grade is readable off the wrapper, which is what tells the
// exactly-once half of the family from the at-most-once half.
static_assert(Linear<int>::usage_grade == ::foundation::algebra::lattices::QttGrade::One);
static_assert(Affine<int>::usage_grade == ::foundation::algebra::lattices::QttGrade::AtMostOne);

namespace detail::qtt_witness {

// The recognisers read the linear marker, which needs a complete class.
struct tag {
    using permission_row = ::foundation::effects::Row<>;
};

using ExclusiveToken = ::foundation::permissions::Permission<tag>;
using SharedToken = ::foundation::permissions::SharedPermission<tag>;

// A class that can move and holds a token by value is linear through it.
struct HoldsToken {
    ExclusiveToken token;
};

}  // namespace detail::qtt_witness

// Both recognisers, pinned in both directions.  A gate that loses its
// arms still reads as a gate and admits everything, until something
// asserts that the arms still answer.  Each line names types the
// recogniser must accept and types it must refuse, and an empty
// recogniser fails the first, a universal one the second.  A share is not
// linear, because a copy of a share proves nothing more than the share.
static_assert(::foundation::contracts::predicate_accepts<
              is_already_linear, Linear<int>, Linear<int> const&, Linear<int>&&, Linear<void*>,
              detail::qtt_witness::ExclusiveToken, detail::qtt_witness::HoldsToken const&>());
static_assert(::foundation::contracts::predicate_refuses<is_already_linear, int, void*, Affine<int>,
                                                         std::unique_ptr<int>, detail::qtt_witness::SharedToken>());

static_assert(::foundation::contracts::predicate_accepts<
              is_already_consume_disciplined, Linear<int>, Affine<int>, Affine<int> const&, Affine<void*>&&,
              detail::qtt_witness::ExclusiveToken, detail::qtt_witness::HoldsToken>());
static_assert(::foundation::contracts::predicate_refuses<is_already_consume_disciplined, int, void*,
                                                         std::unique_ptr<int>, detail::qtt_witness::SharedToken>());

// The shape of the marker: Linear<T> is linear and Affine<T> is not, and
// neither is trivially copyable, so std::bit_cast builds neither from
// bytes.  The move assignment is a call, and the move constructor stays
// trivial in the untracked build.
static_assert(::foundation::linear::is_marked_linear(^^Linear<int>));
static_assert(!::foundation::linear::is_linear_v<Affine<int>>);
static_assert(!std::is_trivially_copyable_v<Linear<int>>);
static_assert(!std::is_trivially_copyable_v<Affine<int>>);
static_assert(!std::is_trivially_move_assignable_v<Linear<int>>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Linear<int>>);

// The two rejections the gates exist for, stated as the facts they
// rest on.  The refusals themselves are compile errors, so they are
// proven by the fixtures test/fixy/neg/neg_qtt_*.cpp rather than here.
static_assert(is_already_linear_v<Linear<int>>, "Linear<Linear<T>> must be refused: the inner wrapper already "
                                                "carries the exactly-once obligation.");
static_assert(is_already_consume_disciplined_v<Linear<int>>,
              "Affine<Linear<T>> must be refused: wrapping an exactly-once obligation in an at-most-once one "
              "downgrades a required consume to an optional one.");

// A live Linear owes a consume at its end, so it marks no end of its own.
// The generic drop door and end_unrun_body then refuse it and each body that
// holds it, in each build.  An Affine owes no consume.
namespace detail::qtt_end_witness {
using LinearBody = decltype([held = mint_linear<int>(1)]() mutable noexcept { return std::move(held).consume(); });
inline constexpr auto drop_probe =
    [](auto&& value) -> decltype(::foundation::linear::drop(static_cast<decltype(value)&&>(value))) {};
inline constexpr auto unrun_probe =
    [](auto&& body) -> decltype(::foundation::linear::end_unrun_body(
                        static_cast<decltype(body)&&>(body), ::foundation::linear::UnrunReason::refusal_test)) {};
}  // namespace detail::qtt_end_witness

static_assert(::foundation::linear::owes_consume_at_end(^^Linear<int>));
static_assert(!::foundation::linear::marks_own_end(^^Linear<int>));
static_assert(!::foundation::linear::DroppableLinear<Linear<int>>);
static_assert(!::foundation::linear::owes_consume_at_end(^^Affine<int>));
static_assert(!std::is_invocable_v<decltype(detail::qtt_end_witness::drop_probe), Linear<int>&&>);
static_assert(!::foundation::linear::UnrunBody<detail::qtt_end_witness::LinearBody>);
static_assert(
    !std::is_invocable_v<decltype(detail::qtt_end_witness::unrun_probe), detail::qtt_end_witness::LinearBody&&>);

}  // namespace fixy
