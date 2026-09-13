#pragma once

// The type-level descriptor that binds the OS-effect atom catalog to
// the row substrate.  The Universe is the catalog descriptor and the
// lattice is the value-level algebra over row bitmasks.  Every
// per-category Universe publishes the same surface, so downstream code
// reflects over any catalog through one shape.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRowLattice.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

struct OsUniverse {
    using atom_t = Effect;

    static constexpr std::size_t cardinality = effect_count;

    // The lattice carries a row as a 64-bit mask, one bit per atom, so
    // an atom whose underlying value reaches 64 shifts past the carrier
    // and the row encoding silently overflows.
    static_assert(cardinality <= 64, "[OsUniverse_Overflow] The atom count exceeds the 64-bit bitmask carrier that "
                                     "the row lattice uses.  Either widen the carrier, which touches every row-hash "
                                     "and row-descriptor consumer, or split the catalog into disjoint Universes.");

    // A tripwire, not the truncation defense: bit_position derives its
    // cast width from the underlying type and is safe on its own.
    // Widening the atom encoding signals a catalog large enough to need
    // an audit of every consumer of the row encoding, so it stops here
    // first.
    static_assert(std::is_same_v<std::underlying_type_t<atom_t>, std::uint8_t>,
                  "[OsUniverse_Underlying] The atom underlying type is no longer uint8_t.  bit_position derives its "
                  "cast width from that type and stays truncation-free, but a widening still demands an audit of "
                  "every row-descriptor, row-hash and effect-mask consumer, because a catalog of more than 64 atoms "
                  "overflows the bitmask carrier regardless.  Do the audit, then update this assertion.");

    using lattice = EffectRowLattice;

    // A literal rather than a reflected type name.  The reflected
    // spelling varies with translation-unit context, and this name goes
    // into diagnostics that must read the same everywhere.
    [[nodiscard]] static consteval std::string_view name() noexcept { return "OsUniverse"; }

    [[nodiscard]] static constexpr std::string_view atom_name(atom_t a) noexcept { return effect_name(a); }

    // The bit position is the atom's own underlying value, never its
    // ordinal in the enumeration.  Atoms are therefore append-only: a
    // new atom takes the next free value, and every already-published
    // row encoding and federated cache key keeps its meaning.
    //
    // The cast width tracks the underlying type instead of naming
    // uint8_t, so a later widening of the atom encoding cannot truncate
    // here.
    [[nodiscard]] static constexpr std::size_t bit_position(atom_t a) noexcept {
        return static_cast<std::size_t>(static_cast<std::underlying_type_t<atom_t>>(a));
    }
};

template <typename U>
concept Universe = requires {
    typename U::atom_t;
    typename U::lattice;
    { U::cardinality } -> std::convertible_to<std::size_t>;
    { U::name() } -> std::convertible_to<std::string_view>;
    { U::atom_name(std::declval<typename U::atom_t>()) } -> std::convertible_to<std::string_view>;
};

static_assert(Universe<OsUniverse>, "OsUniverse must satisfy the Universe concept — every per-category "
                                    "Universe descriptor exposes atom_t / cardinality / lattice / "
                                    "name() / atom_name() through this surface.");

static_assert(std::is_same_v<OsUniverse::atom_t, Effect>);
static_assert(std::is_same_v<OsUniverse::lattice, EffectRowLattice>);
static_assert(OsUniverse::cardinality == effect_count);
static_assert(OsUniverse::name() == "OsUniverse");
static_assert(OsUniverse::atom_name(Effect::Alloc) == "Alloc");
static_assert(OsUniverse::atom_name(Effect::IO) == "IO");
static_assert(OsUniverse::atom_name(Effect::Block) == "Block");
static_assert(OsUniverse::atom_name(Effect::Bg) == "Bg");
static_assert(OsUniverse::atom_name(Effect::Init) == "Init");
static_assert(OsUniverse::atom_name(Effect::Test) == "Test");

static_assert(OsUniverse::bit_position(Effect::Alloc) == 0);
static_assert(OsUniverse::bit_position(Effect::IO) == 1);
static_assert(OsUniverse::bit_position(Effect::Block) == 2);
static_assert(OsUniverse::bit_position(Effect::Bg) == 3);
static_assert(OsUniverse::bit_position(Effect::Init) == 4);
static_assert(OsUniverse::bit_position(Effect::Test) == 5);

// The descriptor encoding of a singleton row agrees with this
// Universe's bit position for that atom.
static_assert(row_descriptor_v<Row<Effect::Alloc>>
              == (EffectRowLattice::element_type{1} << OsUniverse::bit_position(Effect::Alloc)));
static_assert(row_descriptor_v<Row<Effect::Bg>>
              == (EffectRowLattice::element_type{1} << OsUniverse::bit_position(Effect::Bg)));

namespace detail::os_universe_self_test {

[[nodiscard]] consteval bool every_atom_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        auto nm = OsUniverse::atom_name([:en:]);
        if (nm.empty() || nm == std::string_view{"<unknown Effect>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_atom_has_name(), "OsUniverse::atom_name must produce a non-empty, non-sentinel "
                                     "name for every Effect atom.  Add the missing arm to the atom-name "
                                     "emitter, or the new atom leaks the '<unknown Effect>' sentinel "
                                     "into Universe-driven diagnostics.");

static_assert(OsUniverse::atom_name(Effect::Alloc) != OsUniverse::atom_name(Effect::IO));
static_assert(OsUniverse::atom_name(Effect::Bg) != OsUniverse::atom_name(Effect::Init));

// While the atom encoding is uint8_t these comparisons are
// tautological.  What they pin is the form of bit_position's cast, not
// its value.  A cast rewritten to name a fixed width diverges from the
// derived one as soon as the encoding widens, and the comparison
// reddens then instead of truncating in silence.

template <Effect E>
[[nodiscard]] consteval std::size_t derived_bit_position_of() noexcept {
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<Effect>>(E));
}

static_assert(OsUniverse::bit_position(Effect::Alloc) == derived_bit_position_of<Effect::Alloc>());
static_assert(OsUniverse::bit_position(Effect::IO) == derived_bit_position_of<Effect::IO>());
static_assert(OsUniverse::bit_position(Effect::Block) == derived_bit_position_of<Effect::Block>());
static_assert(OsUniverse::bit_position(Effect::Bg) == derived_bit_position_of<Effect::Bg>());
static_assert(OsUniverse::bit_position(Effect::Init) == derived_bit_position_of<Effect::Init>());
static_assert(OsUniverse::bit_position(Effect::Test) == derived_bit_position_of<Effect::Test>());

}  // namespace detail::os_universe_self_test

// Drives every accessor with non-constant arguments, where inline-body
// and constant-evaluation regressions surface that the assertions above
// cannot see.
inline void runtime_smoke_test_os_universe() noexcept {
    [[maybe_unused]] auto nm = OsUniverse::name();
    [[maybe_unused]] auto card = OsUniverse::cardinality;

    Effect e_runtime = Effect::Alloc;
    [[maybe_unused]] auto an = OsUniverse::atom_name(e_runtime);

    [[maybe_unused]] std::size_t bp = OsUniverse::bit_position(e_runtime);

    static_assert(Universe<OsUniverse>);

    using L = OsUniverse::lattice;
    [[maybe_unused]] L::element_type b = L::bottom();
    [[maybe_unused]] L::element_type t = L::top();
    [[maybe_unused]] bool ok = L::leq(b, t);
}

}  // namespace crucible::effects
