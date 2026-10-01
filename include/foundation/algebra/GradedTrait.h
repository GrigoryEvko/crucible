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
//
// Every clause reads something no other translation unit can change:
// the IsGraded concept, a member of W, or a member of W's substrate.
// The concept once read three traits, is_graded_specialization,
// graded_modality and value_type_decoupled, and a trait is a class
// template a foreign translation unit can specialize, so a class that
// pointed graded_type at a fake substrate and specialized the first two
// for it was admitted, and one that specialized the third escaped the
// value_type check.  The first two names stay for the readers that print
// them, derived from the concept and the members, and no gate reads
// them.  The third is gone: DeclaresValueTypeDecoupled reads the member,
// and no reader needs a trait beside it.

#include <foundation/algebra/Graded.h>

#include <concepts>
#include <string_view>
#include <type_traits>

namespace foundation::algebra {

// True when T is the Graded substrate itself.  Derived from IsGraded,
// which is a concept over a reflection query, so a specialization of
// this struct changes nothing the concept below reads.
template <typename T>
struct is_graded_specialization : std::bool_constant<IsGraded<T>> {};

template <typename T>
inline constexpr bool is_graded_specialization_v = IsGraded<T>;

// The modality of a substrate, read off the member the substrate
// publishes.
template <typename T>
    requires IsGraded<T>
struct graded_modality : std::integral_constant<ModalityKind, std::remove_cvref_t<T>::modality> {};

template <typename T>
    requires IsGraded<T>
inline constexpr ModalityKind graded_modality_v = std::remove_cvref_t<T>::modality;

// The concept requires a wrapper's value_type to equal its substrate's.
// A wrapper whose user-facing type is deliberately narrower than the
// type it grades — an element type over a graded container, say —
// declares `static constexpr bool value_type_decoupled = true;` as a
// member and takes responsibility for the gap.  A class body cannot be
// reopened, so only the wrapper's author can make that declaration.

template <typename W>
concept DeclaresValueTypeDecoupled = requires {
    { std::remove_cvref_t<W>::value_type_decoupled } -> std::convertible_to<bool>;
    requires std::remove_cvref_t<W>::value_type_decoupled;
};

template <typename W>
inline constexpr bool is_graded_wrapper_v = false;

template <typename W>
concept GradedWrapper = requires {
    typename W::value_type;
    typename W::lattice_type;
    typename W::graded_type;

    // The substrate must be Graded itself, not a class derived from it
    // and not a lookalike.
    requires IsGraded<typename W::graded_type>;

    requires std::same_as<typename W::lattice_type, typename W::graded_type::lattice_type>;

    requires(DeclaresValueTypeDecoupled<W>
             || std::same_as<typename W::value_type, typename W::graded_type::value_type>);

    requires(W::modality == W::graded_type::modality);

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

namespace detail {

// A Graded specialization that the check file of this header and
// test/foundation/test_algebra_core.cpp name.
using GraderAB = Graded<ModalityKind::Absolute, ::foundation::algebra::detail::TrivialBoolLattice, bool>;

}  // namespace detail

}  // namespace foundation::algebra
