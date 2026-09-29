// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The effect row lattice carries a row as a uint64_t bitmask, so an effect
// universe holds 64 atoms at most.  include/foundation/effects/Row.h states
// that bound for the live Effect enum with static_assert(effect_count <= 64).
// That assertion cannot fire from a fixture, because the live enum has fewer
// atoms.  This fixture puts the same form of assertion on a test universe
// with 65 atoms, and the assertion refuses it.
//
// The fixture checks the form of the assertion, not the live code.  A change
// that deletes the assertion in Row.h does not make this fixture compile.
//
// Companion: neg_effect_row_atom_underlying_widened.cpp puts the other
// carrier assertion (the underlying type of an atom is uint8_t) on a test
// enum.
#include <cstddef>
#include <cstdint>

// A test universe with 65 atoms, one more than the carrier holds.
struct OverCardinalityUniverse {
    using atom_t = std::uint8_t;
    static constexpr std::size_t cardinality = 65;
    static_assert(cardinality <= 64, "[EffectRowCarrier_Overflow] the test universe has more atoms than the "
                                     "uint64_t bitmask carrier of the effect row lattice holds");
};

int main() { return 0; }
