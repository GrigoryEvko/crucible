// The instruction tier of a binding is read against a floor through the
// concept HasHwTierAtOrAbove.  This file tries to put a timestamp read below
// the floor of NonDeterministicTsc, so that a hot binding that reads the
// counter passes V201: it writes an explicit specialization for the name of
// the concept.  A concept is no class template and has no specialization,
// so the file does not compile.

#include <fixy/Collision.h>

namespace hw = fixy::atom::hw;

template <>
struct fixy::collision::detail::HasHwTierAtOrAbove<hw::non_deterministic_tsc, hw::HwInstruction::NonDeterministicTsc> {
    static constexpr bool value = false;
};

int main() { return 0; }
