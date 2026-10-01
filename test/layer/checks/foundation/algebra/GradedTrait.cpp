// The compile-time checks of foundation/algebra/GradedTrait.h.

#include <foundation/algebra/GradedTrait.h>

namespace foundation::algebra {

namespace detail::is_graded_specialization_self_test {

static_assert(is_graded_specialization_v<GraderAB>);

static_assert(is_graded_specialization_v<GraderAB const>);
static_assert(is_graded_specialization_v<GraderAB&>);
static_assert(is_graded_specialization_v<GraderAB const&>);
static_assert(is_graded_specialization_v<GraderAB&&>);
static_assert(is_graded_specialization_v<GraderAB const&&>);

static_assert(IsGraded<GraderAB const&> == is_graded_specialization_v<GraderAB const&>);
static_assert(IsGraded<GraderAB&&> == is_graded_specialization_v<GraderAB&&>);

static_assert(!is_graded_specialization_v<int>);
static_assert(!is_graded_specialization_v<int const&>);
static_assert(!is_graded_specialization_v<void>);
static_assert(!is_graded_specialization_v<::foundation::algebra::detail::TrivialBoolLattice>);

static_assert(IsGraded<int const&> == is_graded_specialization_v<int const&>);
static_assert(IsGraded<void> == is_graded_specialization_v<void>);

static_assert(graded_modality_v<GraderAB> == ModalityKind::Absolute);
static_assert(graded_modality<GraderAB const&>::value == ModalityKind::Absolute);

// The opt-in is a member, and its absence answers no.
struct Undeclared {};
struct DeclaredTrue {
    static constexpr bool value_type_decoupled = true;
};
struct DeclaredFalse {
    static constexpr bool value_type_decoupled = false;
};
static_assert(!DeclaresValueTypeDecoupled<Undeclared>);
static_assert(DeclaresValueTypeDecoupled<DeclaredTrue>);
static_assert(!DeclaresValueTypeDecoupled<DeclaredFalse>);
static_assert(DeclaresValueTypeDecoupled<DeclaredTrue const&>);

}  // namespace detail::is_graded_specialization_self_test

}  // namespace foundation::algebra
