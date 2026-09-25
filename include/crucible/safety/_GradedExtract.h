#pragma once

// Projections that read the typedef slots every graded wrapper
// exposes, so a consumer does not reach into the wrapper's members
// itself and repeat the conformance check.
//
// Each projection strips references and top-level cv qualifiers, so a
// call site may name a parameter type directly.  It does not strip
// pointers: a pointer to a wrapper is not a wrapper.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_GradedTrait.h>
#include <crucible/algebra/_Modality.h>

#include <type_traits>

namespace crucible::safety::extract {

template <typename W>
concept IsGradedWrapper = ::crucible::algebra::GradedWrapper<std::remove_cvref_t<W>>;

// The wrapper's own value type, which a container-backed wrapper is
// allowed to decouple from the one its substrate carries.  What a
// consumer wants is the type the author declared, not the storage.
template <typename W>
    requires IsGradedWrapper<W>
using value_type_of_t = typename std::remove_cvref_t<W>::value_type;

template <typename W>
    requires IsGradedWrapper<W>
using lattice_of_t = typename std::remove_cvref_t<W>::lattice_type;

// This reads the grade through the substrate rather than forming the
// lattice element directly, because that form is concept-constrained
// and the wrapper's conformance already discharges the constraint.
template <typename W>
    requires IsGradedWrapper<W>
using grade_of_t = typename std::remove_cvref_t<W>::graded_type::grade_type;

// The wrapper's claim and the substrate's template argument are
// constrained to agree, and reading the wrapper's surface keeps the
// diagnostic chain shorter when they do not.
template <typename W>
    requires IsGradedWrapper<W>
inline constexpr ::crucible::algebra::ModalityKind modality_of_v = std::remove_cvref_t<W>::modality;

// The substrate's view, complementing the wrapper's own above.
template <typename W>
    requires IsGradedWrapper<W>
using graded_type_of_t = typename std::remove_cvref_t<W>::graded_type;

template <typename W>
inline constexpr bool is_graded_wrapper_v = IsGradedWrapper<W>;

// True for the bare substrate type, false for a wrapper around one.
template <typename T>
inline constexpr bool is_graded_specialization_v =
    ::crucible::algebra::is_graded_specialization_v<std::remove_cvref_t<T>>;

// Only the negative side is exercised here, because it needs no
// wrapper.  A synthetic conforming witness would fail the concept's
// forwarder-fidelity clause: the substrate's names come from
// reflection and depend on the translation unit they are read in.
// The positive matrix therefore runs against real wrappers elsewhere.

namespace detail::graded_extract_self_test {

static_assert(!IsGradedWrapper<int>);
static_assert(!IsGradedWrapper<int*>);
static_assert(!IsGradedWrapper<void>);
static_assert(!IsGradedWrapper<int&>);
static_assert(!IsGradedWrapper<int(int)>);

static_assert(!is_graded_wrapper_v<int>);
static_assert(!is_graded_wrapper_v<void>);
static_assert(!is_graded_wrapper_v<int(int)>);

struct Lookalike_missing_surface {
    using value_type = int;
};
static_assert(!IsGradedWrapper<Lookalike_missing_surface>);
static_assert(!is_graded_wrapper_v<Lookalike_missing_surface>);

static_assert(!is_graded_specialization_v<int>);
static_assert(!is_graded_specialization_v<void*>);
static_assert(!is_graded_specialization_v<Lookalike_missing_surface>);

}  // namespace detail::graded_extract_self_test

inline bool graded_extract_smoke_test() noexcept {
    using namespace detail::graded_extract_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !IsGradedWrapper<int>;
        ok = ok && !IsGradedWrapper<void>;
        ok = ok && !IsGradedWrapper<Lookalike_missing_surface>;
        ok = ok && !is_graded_wrapper_v<int>;
        ok = ok && !is_graded_specialization_v<int>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
