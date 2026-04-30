#pragma once

// ── crucible::effects::OsUniverse ───────────────────────────────────
//
// FOUND-H02.  The type-level descriptor binding the OS-effect catalog
// (Capabilities.h's `Effect` enum) to the Met(X) row substrate.  Per
// 28_04_2026_effects.md §5.2 + §14.3, every per-category lattice has a
// matching Universe — a typedef carrying:
//
//   - `atom_t`      : the underlying atom type (Effect)
//   - `cardinality` : the number of atoms (effect_count)
//   - `lattice`     : the value-level lattice instance (EffectRowLattice)
//   - `name()`      : a consteval string for diagnostics
//   - `atom_name()` : a consteval string for individual atoms
//
// The Universe is the type-level *catalog descriptor*; the Lattice is
// the value-level *algebra*.  Together they let downstream code:
//
//   1. Reflect over the atom catalog (cardinality + atom_name)
//   2. Compute on row bitmasks (lattice ops)
//   3. Round-trip type-level rows through the substrate (At<Atoms...>)
//   4. Federate cache keys via row_hash composition (FOUND-I-series)
//
// ── Scope of this header ────────────────────────────────────────────
//
// This header lands ONLY the `OsUniverse` descriptor.  It does NOT:
//
//   - Define the lattice (lives in EffectRowLattice.h)
//   - Define `Computation<R, T>` as a Graded alias (FOUND-H03)
//   - Provide the Computation façade methods (FOUND-H04)
//   - Touch the legacy fx::* tree (deleted in FOUND-B07 / METX-5)
//
// ── Forward link to FOUND-H03 ────────────────────────────────────────
//
// H03 ships `Computation<R, T>` as:
//
//   template <typename R, typename T>
//   using Computation = ::crucible::algebra::Graded<
//       ::crucible::algebra::ModalityKind::Relative,
//       EffectRowLattice::At< /* atoms unpacked from R */ >,
//       T>;
//
// The `At<Atoms...>` slot is a type-level singleton sub-lattice
// (EBO-collapsed grade carrier) so the alias preserves Computation's
// zero-runtime-cost claim.  See EffectRowLattice.h's At<> doc-block.
//
// ── Per-Universe atom_name discipline (FOUND-H01-AUDIT-4) ───────────
//
// Each per-category Universe ships its own consteval atom-name
// emitter via the same shape: `static consteval std::string_view
// atom_name(atom_t a) noexcept`.  OsUniverse forwards to the existing
// `effect_name(Effect)` consteval emitter from Capabilities.h; future
// Universes (DetSafeUniverse, HotPathUniverse, ...) follow the same
// contract.  The emitter is the diagnostic surface every reflection-
// driven error message (FOUND-E18) reads.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRowLattice.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

// ── OsUniverse — the OS-effect catalog descriptor ───────────────────
//
// The OS Universe is the Met(X) atom catalog used by the foreground/
// background context machinery (Capabilities.h's cap::Alloc / cap::IO
// / cap::Block + the Bg / Init / Test contexts).  Every Computation
// in production today targets this Universe; per-axis Universes
// (DetSafe, HotPath, ...) will land alongside their own lattices in
// the FOUND-G series of wrappers.

struct OsUniverse {
    // The atom enum.  Underlying value type is uint8_t per
    // Capabilities.h:41-48; bit positions in the parent lattice's
    // bitmask carrier are derived from these underlying values.
    using atom_t = Effect;

    // Number of atoms in the catalog.  Reflection-derived from the
    // Effect enum at Capabilities.h:54-55 — adding a new atom auto-
    // bumps this constant.  The carrier-width invariant in
    // EffectRowLattice.h asserts cardinality <= 64.
    static constexpr std::size_t cardinality = effect_count;

    // The value-level lattice instance — bounded distributive lattice
    // over `std::uint64_t` bitmasks, satisfying Lattice +
    // BoundedLattice + the Birkhoff distributivity witness.  See
    // EffectRowLattice.h for the algebra; this typedef pins the
    // identity so downstream code can write `OsUniverse::lattice` and
    // get back the canonical lattice for the OS Universe.
    using lattice = EffectRowLattice;

    // Diagnostic name — consumed by FOUND-E18 row-mismatch error
    // formatter.  Must be stable across compiler versions / TU-context
    // (the consteval surface; not the reflection-driven
    // display_string_of which has documented TU-fragility per
    // algebra/Graded.h:156-186).
    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "OsUniverse";
    }

    // Per-atom name emitter (FOUND-H01-AUDIT-4).  Forwards to the
    // canonical `effect_name(Effect)` from Capabilities.h:58-68.  The
    // forwarding is intentional: the catalog and its names live in
    // one place (Capabilities.h), the Universe descriptor is a thin
    // bridge.  Adding a new atom to Effect requires updating
    // effect_name's switch; the OsUniverse::atom_name picks up the
    // change automatically through this forwarder.
    //
    // constexpr (not consteval) per the runtime smoke-test discipline
    // (feedback_algebra_runtime_smoke_test_discipline) so the smoke
    // test can drive atom_name with a non-constant argument.  Still
    // constant-evaluated when called from consteval contexts (e.g.,
    // detail::os_universe_self_test::every_atom_has_name below).
    [[nodiscard]] static constexpr std::string_view
    atom_name(atom_t a) noexcept {
        return effect_name(a);
    }

    // Bit position of an atom in the parent lattice's bitmask carrier.
    // Stable across append-only Universe extensions (28_04 §8.5.3,
    // FOUND-I04) — the bit position derives from the atom's
    // underlying enumerator value, NOT from enumeration order.
    [[nodiscard]] static constexpr std::size_t
    bit_position(atom_t a) noexcept {
        return static_cast<std::size_t>(static_cast<std::uint8_t>(a));
    }
};

// ── Universe concept gate ───────────────────────────────────────────
//
// Concept-overloaded specialization (FOUND-D-series) consumes Universe
// types via a uniform shape.  The `Universe` concept enforces the
// minimum surface every Universe descriptor must publish — atom_t,
// cardinality, lattice, name, atom_name.  Future per-axis Universes
// must satisfy this concept; the dispatcher reads the surface
// uniformly via `is_universe_v<U>` (planned in the FOUND-D extension).

template <typename U>
concept Universe = requires {
    typename U::atom_t;
    typename U::lattice;
    { U::cardinality } -> std::convertible_to<std::size_t>;
    { U::name() }      -> std::convertible_to<std::string_view>;
    { U::atom_name(std::declval<typename U::atom_t>()) }
        -> std::convertible_to<std::string_view>;
};

// ── Concept-conformance assertions ──────────────────────────────────

static_assert(Universe<OsUniverse>,
    "OsUniverse must satisfy the Universe concept — every per-category "
    "Universe descriptor exposes atom_t / cardinality / lattice / "
    "name() / atom_name() through this surface.");

static_assert(std::is_same_v<OsUniverse::atom_t, Effect>);
static_assert(std::is_same_v<OsUniverse::lattice, EffectRowLattice>);
static_assert(OsUniverse::cardinality == effect_count);
static_assert(OsUniverse::name() == "OsUniverse");
static_assert(OsUniverse::atom_name(Effect::Alloc) == "Alloc");
static_assert(OsUniverse::atom_name(Effect::IO)    == "IO");
static_assert(OsUniverse::atom_name(Effect::Block) == "Block");
static_assert(OsUniverse::atom_name(Effect::Bg)    == "Bg");
static_assert(OsUniverse::atom_name(Effect::Init)  == "Init");
static_assert(OsUniverse::atom_name(Effect::Test)  == "Test");

// Bit-position bridge agrees with the parent lattice's encoding.
// row_descriptor_v<Row<Effect::Alloc>> sets bit at bit_position(Alloc).
static_assert(OsUniverse::bit_position(Effect::Alloc) == 0);
static_assert(OsUniverse::bit_position(Effect::IO)    == 1);
static_assert(OsUniverse::bit_position(Effect::Block) == 2);
static_assert(OsUniverse::bit_position(Effect::Bg)    == 3);
static_assert(OsUniverse::bit_position(Effect::Init)  == 4);
static_assert(OsUniverse::bit_position(Effect::Test)  == 5);

// Bridge: every atom's bit position matches the parent lattice's
// row_descriptor_v encoding for the singleton row containing only
// that atom.
static_assert(row_descriptor_v<Row<Effect::Alloc>>
              == (EffectRowLattice::element_type{1}
                  << OsUniverse::bit_position(Effect::Alloc)));
static_assert(row_descriptor_v<Row<Effect::Bg>>
              == (EffectRowLattice::element_type{1}
                  << OsUniverse::bit_position(Effect::Bg)));

// ── Self-test block ─────────────────────────────────────────────────

namespace detail::os_universe_self_test {

// Reflection-driven exhaustive coverage: every Effect enumerator must
// have a non-empty atom_name from the Universe.  Mirrors the
// every_effect_has_name pattern in Capabilities.h's self-test, but
// targets the Universe's atom_name surface specifically — catches the
// case where Capabilities.h's effect_name is updated but a future
// Universe overrides atom_name and forgets to mirror the addition.

[[nodiscard]] consteval bool every_atom_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Effect));
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
static_assert(every_atom_has_name(),
    "OsUniverse::atom_name must produce a non-empty, non-sentinel "
    "name for every Effect atom.  Add the missing arm to "
    "effect_name() in Capabilities.h or the new atom leaks the "
    "'<unknown Effect>' sentinel into Universe-driven diagnostics.");

// Pairwise distinctness — the Universe's atom_name surface inherits
// effect_name's distinctness, but the sanity check is cheap.
static_assert(OsUniverse::atom_name(Effect::Alloc)
              != OsUniverse::atom_name(Effect::IO));
static_assert(OsUniverse::atom_name(Effect::Bg)
              != OsUniverse::atom_name(Effect::Init));

// Layout: OsUniverse is a stateless type — sizeof(OsUniverse) is
// implementation-defined for empty structs (1 byte under most ABIs)
// but the Universe is never instantiated; only its static surface is
// consumed.  Document the expectation rather than assert — sizeof(1)
// is a microarch trivia, not a load-bearing invariant.

}  // namespace detail::os_universe_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per the runtime smoke-test discipline (auto-memory feedback): drive
// every consteval/constexpr accessor with non-constant arguments so
// inline-body / consteval-vs-constexpr regressions surface alongside
// the static_assert wall.

inline void runtime_smoke_test_os_universe() noexcept {
    [[maybe_unused]] auto nm = OsUniverse::name();
    [[maybe_unused]] auto card = OsUniverse::cardinality;

    // Drive atom_name via a non-constant argument so the consteval
    // accessor's inline body actually instantiates at the boundary.
    Effect e_runtime = Effect::Alloc;
    [[maybe_unused]] auto an = OsUniverse::atom_name(e_runtime);

    [[maybe_unused]] std::size_t bp =
        OsUniverse::bit_position(e_runtime);

    // Concept-based capability check at the boundary (per
    // feedback_algebra_runtime_smoke_test_discipline) — confirms the
    // Universe concept-gate is satisfied at the boundary, not just
    // at the static_assert wall above.
    static_assert(Universe<OsUniverse>);

    // Surface the lattice typedef so downstream code that goes
    // OsUniverse::lattice gets a reachable instantiation here.
    using L = OsUniverse::lattice;
    [[maybe_unused]] L::element_type b = L::bottom();
    [[maybe_unused]] L::element_type t = L::top();
    [[maybe_unused]] bool ok = L::leq(b, t);
}

}  // namespace crucible::effects
