// The compile-time checks of fixy/GradedFacade.h.

#include <fixy/GradedFacade.h>

namespace fixy {

namespace detail::graded_facade_self_test {

using SampleFacade =
    graded_facade<::foundation::algebra::ModalityKind::Absolute,
                  ::foundation::algebra::lattices::QttSemiring::At<::foundation::algebra::lattices::QttGrade::One>,
                  int>;

// The base carries nothing, so inheriting it costs nothing.
static_assert(std::is_empty_v<SampleFacade>);
static_assert(std::is_trivially_default_constructible_v<SampleFacade>);

struct SampleWrapper : SampleFacade {
    graded_type impl_;
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
