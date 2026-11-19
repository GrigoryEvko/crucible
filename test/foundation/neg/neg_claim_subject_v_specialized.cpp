// Graded writes a value without a key only when its grade claims the slot
// and not the bytes.  The subject was once the variable template
// claim_subject_v, and the gate read it.  This file tries to open that
// door for a lattice that claims the bytes: it specializes that variable.
// The subject comes from concepts and alias templates, and no variable
// gives it, so the specialization does not compile.

#include <foundation/Quarantine.h>
#include <foundation/algebra/ClaimOrientation.h>

namespace forged_claim {
struct AboutTheBytes {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};
}  // namespace forged_claim

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr foundation::algebra::ClaimSubject foundation::algebra::claim_subject_v<forged_claim::AboutTheBytes> =
    foundation::algebra::ClaimSubject::slot;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
