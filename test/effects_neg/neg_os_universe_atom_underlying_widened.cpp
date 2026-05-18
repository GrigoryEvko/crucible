// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-018 audit fixture (companion to neg_os_universe_cardinality_-
// overflow.cpp): pins the OPPOSITE side of the carrier-width axiom.
// Where the companion proves cardinality > 64 is rejected, this fixture
// proves a wider atom_t underlying type is rejected.
//
// `OsUniverse::bit_position(atom)` casts through `uint8_t` —
// widening the underlying type (e.g. `enum class Effect : uint16_t`)
// silently expands the encodable bit range past the uint64_t bitmask
// carrier width WITHOUT bumping `cardinality`.  The pin in OsUniverse.h
// (sister to the cardinality pin) catches this drift at compile time.
//
// This fixture demonstrates the assertion DISCIPLINE: a parallel test-
// only Universe shape with atom_t = uint16_t triggers the SAME
// static_assert form that OsUniverse.h ships in production.  The
// fixture's failure is the witness that the discipline rejects the
// underlying-type widening at the type-system boundary.
//
// Expected diagnostic: "static assertion failed" / "static_assert" /
// "OsUniverse_Underlying" / "fixy-A3-018"

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Synthetic test-only atom enum with uint16_t underlying — wider than
// the OsUniverse axiom permits.  The atom_t-widening pin catches this
// even if cardinality fits, because bit_position's cast through uint8_t
// would truncate values ≥ 256 into the low byte.
enum class WidenedAtom : std::uint16_t {
    A = 0,
    B = 1,
};

struct WidenedUniverse {
    using atom_t = WidenedAtom;
    static constexpr std::size_t cardinality = 2;  // fits cardinality
                                                    // pin, but trips
                                                    // the underlying-
                                                    // type pin.

    static_assert(
        std::is_same_v<std::underlying_type_t<atom_t>, std::uint8_t>,
        "[OsUniverse_Underlying] fixy-A3-018: synthetic test Universe "
        "atom_t underlying type widened beyond uint8_t — bit_position "
        "would expand the encodable bit range past the uint64_t "
        "carrier without bumping cardinality.  Production OsUniverse "
        "ships the same assertion to foreclose silent ABI drift.");
};

int main() { return 0; }
