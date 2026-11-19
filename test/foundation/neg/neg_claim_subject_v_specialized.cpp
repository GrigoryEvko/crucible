// Graded writes a value without a key only when its grade claims the slot
// and not the bytes.  This file tries to open that door for a lattice that
// claims the bytes: it specializes the trait spelling claim_subject_v.  The
// subject is a function at namespace scope that is not a template, so the
// specialization does not compile, and the gate keeps the door shut.

#include <foundation/algebra/ClaimOrientation.h>

namespace {
struct AboutTheBytes {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
};
}  // namespace

template <>
inline constexpr foundation::algebra::ClaimSubject foundation::algebra::claim_subject_v<AboutTheBytes> =
    foundation::algebra::ClaimSubject::slot;

static_assert(foundation::algebra::GradeIgnoresBytes<AboutTheBytes>);

int main() { return 0; }
