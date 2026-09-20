#pragma once

// The six members every graded wrapper publishes, written once.
//
// A wrapper over foundation/algebra/Graded.h has to answer six
// questions the same way every time: what it wraps, what lattice grades
// it, which modality that grading is, what the substrate instantiation
// is, and the two names the diagnostic surface reads off that
// instantiation.  Each of the seven wrappers in fixy wrote all six by
// hand, and the two name forwarders were the same one-line body seven
// times over.
//
// The repetition was not free.  GradedWrapper in
// foundation/algebra/GradedTrait.h compares each forwarder's string
// against the substrate's precisely because a hand-written forwarder
// can return something the wrapper does not have.  One base answers
// from graded_type in every case, so that class of drift has nowhere
// left to start.
//
// The base is empty, so a wrapper that inherits it keeps its size: the
// grade lives in the wrapper's own graded_type member, not here.  It is
// not a CRTP base, because nothing here needs the derived type — every
// member is static or a typedef.
//
// A wrapper whose value_type is not the substrate's says so by
// declaring its own, which hides the base's, and by declaring the
// member `static constexpr bool value_type_decoupled = true;`, which
// is what GradedWrapper in GradedTrait.h reads to admit the mismatch.
// AppendOnly is that case: it wraps a container and grades the
// container, while its value_type is the element.

#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <string_view>
#include <type_traits>

namespace fixy {

template <::foundation::algebra::ModalityKind M, typename Lattice, typename T>
struct graded_facade {
    using value_type = T;
    using lattice_type = Lattice;
    static constexpr ::foundation::algebra::ModalityKind modality = M;
    using graded_type = ::foundation::algebra::Graded<M, Lattice, T>;

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

namespace detail::graded_facade_self_test {

using SampleFacade =
    graded_facade<::foundation::algebra::ModalityKind::Absolute,
                  ::foundation::algebra::lattices::QttSemiring::At<::foundation::algebra::lattices::QttGrade::One>,
                  int>;

// The base carries nothing, so inheriting it costs nothing.
static_assert(std::is_empty_v<SampleFacade>);
static_assert(std::is_trivially_default_constructible_v<SampleFacade>);

struct SampleWrapper : SampleFacade {
    graded_type impl_{};
};
static_assert(sizeof(SampleWrapper) == sizeof(SampleFacade::graded_type),
              "graded_facade must collapse into its deriver; the grade lives in the wrapper's own member");

// The six members arrive through the base, and the two names agree
// with the substrate, which is what GradedWrapper checks.
static_assert(std::is_same_v<SampleWrapper::value_type, int>);
static_assert(SampleWrapper::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(SampleWrapper::value_type_name() == SampleFacade::graded_type::value_type_name());
static_assert(SampleWrapper::lattice_name() == SampleFacade::graded_type::lattice_name());

// A deriver that wraps one type and grades another declares its own
// value_type, which hides the base's.  That is the AppendOnly shape.
struct DecoupledWrapper : SampleFacade {
    using value_type = char;
};
static_assert(std::is_same_v<DecoupledWrapper::value_type, char>);
static_assert(std::is_same_v<DecoupledWrapper::graded_type::value_type, int>);

}  // namespace detail::graded_facade_self_test

}  // namespace fixy
