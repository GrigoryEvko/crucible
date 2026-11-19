// Graded writes a value without a key only when its grade claims the slot
// and not the bytes.  This file tries to open that door for a lattice that
// claims the bytes: it writes an explicit specialization of the concept
// that the gate reads.  A concept cannot have a specialization, so the
// declaration does not compile, and the gate keeps the door shut.

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
constexpr bool foundation::algebra::detail::StatesSlot<AboutTheBytes> = true;

int main() { return 0; }
