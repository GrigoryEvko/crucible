// A specialization of graded_modality for a fake substrate.  An
// unconstrained primary would admit it.  The primary is constrained to
// Graded, so the specialization for a type that is not Graded is not
// merely inert, it does not compile.  This fixture stands on that.
//
// The second required diagnostic is the compiler's note naming the
// constraint and the type, `IsGraded<T>' [with T = FakeSubstrate]`,
// which the source never spells.

#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/QttSemiring.h>

#include <string_view>
#include <type_traits>

struct FakeSubstrate {
    using value_type = int;
    using lattice_type = ::foundation::algebra::lattices::QttSemiring::At<::foundation::algebra::lattices::QttGrade::One>;
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    static consteval std::string_view value_type_name() noexcept { return "int"; }
    static consteval std::string_view lattice_name() noexcept { return "QttSemiring::At<1>"; }
};

namespace foundation::algebra {
template <>
struct graded_modality<::FakeSubstrate> : std::integral_constant<ModalityKind, ModalityKind::Absolute> {};
}  // namespace foundation::algebra

int main() { return 0; }
