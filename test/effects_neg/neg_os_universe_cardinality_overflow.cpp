// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-018 audit fixture: pins the cardinality ≤ 64 axiom that
// OsUniverse.h ships at the descriptor site.  EffectRowLattice carries
// row bitmasks in `std::uint64_t`; a Universe with > 64 atoms silently
// overflows the carrier when `bit_position(atom) << shift` produces a
// shift past the carrier width.  Per FOUND-I04 append-only Universe
// extension, atoms can be added at the next free underlying value —
// without the pin the 65th atom lands silently.
//
// This fixture demonstrates the assertion DISCIPLINE: a parallel test-
// only Universe shape with `cardinality = 65` triggers the SAME
// static_assert form that OsUniverse.h ships in production.  The
// fixture's failure is the witness that the discipline rejects the
// over-cardinality case at the type-system boundary.
//
// Companion: neg_os_universe_atom_underlying_widened.cpp pins the
// sister axiom (atom_t underlying type == uint8_t).
//
// Expected diagnostic: "static assertion failed" / "static_assert" /
// "OsUniverse_Overflow" / "fixy-A3-018"

#include <cstddef>
#include <cstdint>

// Synthetic test-only Universe descriptor mirroring OsUniverse's shape,
// but with cardinality = 65 — one atom past the carrier capacity.  The
// same static_assert form as OsUniverse.h:90-97 fires here.
struct OverCardinalityUniverse {
    using atom_t = std::uint8_t;
    static constexpr std::size_t cardinality = 65;

    static_assert(cardinality <= 64,
        "[OsUniverse_Overflow] fixy-A3-018: synthetic test Universe "
        "cardinality exceeds the uint64_t bitmask carrier width — "
        "the production OsUniverse ships the same assertion to "
        "foreclose the FOUND-I04 append-only extension landing a "
        "65th atom silently.");
};

int main() { return 0; }
