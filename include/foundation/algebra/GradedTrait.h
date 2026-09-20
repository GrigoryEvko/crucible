#pragma once

// GradedWrapper is the shape a class must have to count as a wrapper
// over the Graded substrate.  It asserts one thing: that the wrapper's
// diagnostic surface agrees with the substrate it names.
//
// It deliberately says nothing about size.  A wrapper's storage cost
// follows from the storage regime its lattice selects, and those
// regimes disagree with each other, so a blanket size check would
// either reject the honest wrappers or admit dishonest ones.  Each
// wrapper carries its own size witness instead.
//
// It also says nothing about copy, move, swap or construction.  One
// wrapper deletes copy to enforce linearity while the next defaults it,
// and construction surfaces vary by wrapper.  Constraining either would
// only be a way of forbidding legitimate designs.

#include <foundation/algebra/Graded.h>

#include <concepts>
#include <string_view>
#include <type_traits>

namespace foundation::algebra {

// The concept needs this because a wrapper could otherwise point
// graded_type at any type at all, even void, and still satisfy every
// other clause.

template <typename T>
struct is_graded_specialization : std::false_type {};

template <ModalityKind M, typename L, typename T>
struct is_graded_specialization<Graded<M, L, T>> : std::true_type {};

// The cv-ref strip belongs here and not in the trait struct.  IsGraded
// answers the same question and strips, so a caller who reaches for
// either spelling must get the same answer on a reference or a const
// type.  Keeping the struct unstripped leaves its metafunction shape
// usable directly.
template <typename T>
inline constexpr bool is_graded_specialization_v = is_graded_specialization<std::remove_cvref_t<T>>::value;

template <typename T>
struct graded_modality;

template <ModalityKind M, typename L, typename T>
struct graded_modality<Graded<M, L, T>> : std::integral_constant<ModalityKind, M> {};

template <typename T>
inline constexpr ModalityKind graded_modality_v = graded_modality<T>::value;

// The concept requires a wrapper's value_type to equal its substrate's.
// A wrapper whose user-facing type is deliberately narrower than the
// type it grades — an element type over a graded container, say —
// specializes this to true and takes responsibility for the gap.

template <typename W>
struct value_type_decoupled : std::false_type {};

template <typename W>
inline constexpr bool value_type_decoupled_v = value_type_decoupled<W>::value;

template <typename W>
inline constexpr bool is_graded_wrapper_v = false;

template <typename W>
concept GradedWrapper = requires {
    typename W::value_type;
    typename W::lattice_type;
    typename W::graded_type;

    requires is_graded_specialization_v<typename W::graded_type>;

    requires std::same_as<typename W::lattice_type, typename W::graded_type::lattice_type>;

    requires(value_type_decoupled_v<W> || std::same_as<typename W::value_type, typename W::graded_type::value_type>);

    requires(W::modality == graded_modality_v<typename W::graded_type>);

    { W::value_type_name() } noexcept -> std::same_as<std::string_view>;
    { W::lattice_name() } noexcept -> std::same_as<std::string_view>;

    // Existing forwarders are not enough.  A wrapper can define both
    // names with a body that returns something else, and the
    // diagnostics then report a type and a lattice the wrapper does not
    // actually have.  Comparing the strings closes that.
    requires(W::value_type_name() == W::graded_type::value_type_name());
    requires(W::lattice_name() == W::graded_type::lattice_name());
};

template <typename W>
    requires GradedWrapper<W>
inline constexpr bool is_graded_wrapper_v<W> = true;

namespace detail::is_graded_specialization_self_test {

using GraderAB =
    Graded<ModalityKind::Absolute, ::foundation::algebra::detail::lattice_self_test::TrivialBoolLattice, bool>;

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
static_assert(!is_graded_specialization_v<::foundation::algebra::detail::lattice_self_test::TrivialBoolLattice>);

static_assert(IsGraded<int const&> == is_graded_specialization_v<int const&>);
static_assert(IsGraded<void> == is_graded_specialization_v<void>);

}  // namespace detail::is_graded_specialization_self_test

}  // namespace foundation::algebra
