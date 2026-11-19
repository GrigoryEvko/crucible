// A lattice states the subject of its claim with a member claim_subject of
// the type ClaimSubject.  A member of that name with another type is a
// misspelt statement, and it must not fall back to the bytes.  This file
// declares such a member, and the gate stops the build at a call whose
// name gives the fix.

#include <foundation/algebra/ClaimOrientation.h>

namespace {
struct Mistyped {
    using element_type = bool;
    [[nodiscard]] static constexpr bool leq(bool a, bool b) noexcept { return !a || b; }
    [[nodiscard]] static constexpr bool join(bool a, bool b) noexcept { return a || b; }
    [[nodiscard]] static constexpr bool meet(bool a, bool b) noexcept { return a && b; }
    [[maybe_unused]] static constexpr int claim_subject = 1;
};
}  // namespace

static_assert(!foundation::algebra::GradeIgnoresBytes<Mistyped>);

int main() { return 0; }
