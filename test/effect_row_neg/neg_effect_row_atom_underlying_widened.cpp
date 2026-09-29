// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The effect row lattice maps each atom to one bit of a uint64_t carrier,
// and the underlying type of an atom is uint8_t.
// include/foundation/effects/Effect.h states that for the live Effect enum
// with static_assert(std::is_same_v<std::underlying_type_t<Effect>,
// std::uint8_t>).  A wider underlying type changes the ABI of every row
// and every serialized atom.  That assertion cannot fire from a fixture,
// because the live enum is uint8_t.  This fixture puts the same form of
// assertion on a test enum with a uint16_t underlying type, and the
// assertion refuses it.
//
// The fixture checks the form of the assertion, not the live code.  A change
// that deletes the assertion in Effect.h does not make this fixture compile.
//
// Companion: neg_effect_row_cardinality_over_carrier.cpp puts the other
// carrier assertion (64 atoms at most) on a test universe.
#include <cstddef>
#include <cstdint>
#include <type_traits>

// A test atom enum with a uint16_t underlying type, wider than an atom of
// the effect row lattice.
enum class WidenedAtom : std::uint16_t {
    A = 0,
    B = 1,
};

// The cardinality is inside the carrier, so the underlying type is the one
// reason for the refusal.
struct WidenedUniverse {
    using atom_t = WidenedAtom;
    static constexpr std::size_t cardinality = 2;
    static_assert(std::is_same_v<std::underlying_type_t<atom_t>, std::uint8_t>,
                  "[EffectRowCarrier_Underlying] the underlying type of a test atom is wider than uint8_t");
};

int main() { return 0; }
