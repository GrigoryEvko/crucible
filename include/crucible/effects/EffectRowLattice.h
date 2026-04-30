#pragma once

// ── crucible::effects::EffectRowLattice ─────────────────────────────
//
// FOUND-H01.  Bounded distributive lattice adapter making `Row<Es...>`
// a first-class `crucible::algebra::Lattice` instance per the §H series
// migration plan.
//
// ── The lattice ─────────────────────────────────────────────────────
//
// Carrier: `std::uint64_t` bitmask, one bit per `Effect` atom from
// `Capabilities.h`.  With six atoms (Alloc / IO / Block / Bg / Init /
// Test), the bitmask uses bits 0-5; bits 6-63 stay zero.  Adding a new
// Effect atom auto-extends the bitmask via the `effect_count` constant.
//
// Operations:
//
//   bottom()  = 0                              (empty row)
//   top()     = (1 << effect_count) - 1        (universe row)
//   leq(a,b)  = (a & ~b) == 0                  (a ⊆ b — subset bit-test)
//   join(a,b) = a | b                          (set union)
//   meet(a,b) = a & b                          (set intersection)
//
// This is the textbook powerset lattice over the Effect atom set.  It
// is bounded, distributive, and self-dual.  The lattice axioms hold
// trivially because bitwise OR / AND over uint64_t are commutative,
// associative, idempotent, and distributive — exactly the algebraic
// properties Birkhoff's representation theorem promises for any
// powerset lattice.
//
// ── Bridge: type-level Row → runtime descriptor ─────────────────────
//
// `row_descriptor_v<R>` maps a `Row<Es...>` type to its bitmask:
//
//   row_descriptor_v<Row<>>                              ==  0
//   row_descriptor_v<Row<Effect::Alloc>>                 ==  1
//   row_descriptor_v<Row<Effect::IO>>                    ==  2
//   row_descriptor_v<Row<Effect::Alloc, Effect::IO>>     ==  3
//   row_descriptor_v<AllRow /* universe */>              == top()
//
// Order of atoms in the pack does not affect the bitmask — `Row<A,B>`
// and `Row<B,A>` map to the same descriptor, mirroring the semantic
// equivalence the existing `Subrow<R1, R2>` concept already guarantees.
//
// Duplicate atoms in the pack do not affect the bitmask either —
// `Row<A,A>` maps to the same single bit as `Row<A>`.
//
// ── Why this design (not type-level set algebra) ────────────────────
//
// Graded<Modality, Lattice, T>'s `weaken(new_grade) pre (L::leq(...))`
// is evaluated at RUNTIME under the `enforce` contract semantic.  A
// Lattice whose `leq` was consteval-only would fail to instantiate
// against Graded's runtime grade member.  Bitmask lattices fit this
// constraint trivially: every operation is a single bitwise primitive,
// runnable at both compile time and runtime with identical semantics.
//
// The type-level Row<Es...> identity stays intact — `row_descriptor_v`
// is a one-way projection, not a replacement.  Code that needs to
// preserve Effect-pack identity (for diagnostics, reflection, hash
// keys) keeps using Row<Es...>; code that needs to participate in the
// Graded substrate uses the bitmask via this lattice.
//
// ── Forward link to FOUND-H02 / H03 ─────────────────────────────────
//
// H02 introduces `OsUniverse` as the type-level anchor for "the
// Crucible universe of effect atoms" — a typedef / tag carrying
// `using lattice = EffectRowLattice;` and the canonical Effect-enum
// binding.  H03 then ships `Computation<R, T>` as a thin
// `Graded<Modality::Relative, EffectRowLattice, T>` alias whose grade
// is the row's bitmask.  H04 is the migration shim.  H05 verifies
// backward compatibility against test_effects.cpp's existing surface.
//
// This header lands ONLY the lattice + bridge.  No Graded<>
// instantiation here — that's H03's job.

#include <crucible/algebra/Lattice.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

// ── Carrier-width invariant (FOUND-H01-AUDIT-2) ─────────────────────
//
// EffectRowLattice's carrier is std::uint64_t.  Bit position i is
// derived from the underlying value of the i-th Effect enumerator;
// `EL{1} << effect_count` is undefined behavior the moment effect_count
// reaches 64.  The append-only Universe discipline (28_04_2026_effects
// §8.5.3, FOUND-I04) keeps the bit positions of existing atoms stable;
// this guard preempts the carrier-width regression at the moment a 64th
// atom would be added — at which point the lattice MUST migrate to a
// wider carrier (uint128 / std::bitset<N>) before the new atom lands.
static_assert(effect_count <= 64,
    "EffectRowLattice carrier is uint64_t — extend to a wider carrier "
    "(uint128 / std::bitset<effect_count>) before adding a 65th Effect "
    "atom.  See 28_04_2026_effects.md §8.5.3 (append-only Universe).");

// ── EffectRowLattice ───────────────────────────────────────────────

struct EffectRowLattice {
    using element_type = std::uint64_t;

    [[nodiscard]] static constexpr element_type bottom() noexcept {
        return 0;
    }

    // Top is the universe — every Effect atom present.  Adding a new
    // atom to Capabilities.h auto-bumps `effect_count` (it's reflection-
    // derived); the top mask therefore extends without manual update.
    [[nodiscard]] static constexpr element_type top() noexcept {
        return (effect_count == 0)
            ? element_type{0}
            : ((element_type{1} << effect_count) - element_type{1});
    }

    // Subset bit-test: a ⊆ b iff every bit in a is also in b iff
    // `a & ~b` clears every bit in a.
    [[nodiscard]] static constexpr bool leq(
        element_type a, element_type b) noexcept
    {
        return (a & ~b) == 0;
    }

    [[nodiscard]] static constexpr element_type join(
        element_type a, element_type b) noexcept
    {
        return a | b;
    }

    [[nodiscard]] static constexpr element_type meet(
        element_type a, element_type b) noexcept
    {
        return a & b;
    }

    [[nodiscard]] static consteval std::string_view name() noexcept {
        return "EffectRow";
    }

    // ── At<Effect... Atoms> singleton sub-lattice (FOUND-H01-AUDIT-1) ──
    //
    // Per 28_04_2026_effects.md §5.2: the Graded substrate's `Modality::
    // Relative` slot expects a *type-level grade* — the row pinned in
    // the type, NOT carried as a runtime field.  H03's `Computation<R,
    // T>` alias resolves through `EffectRowLattice::At<Es...>` so the
    // grade-slot collapses to zero bytes via Graded's `[[no_unique_
    // address]] grade_type grade_;` member, mirroring `ConfLattice::At
    // <Conf>`, `ToleranceLattice::At<Tolerance>`, `QttSemiring::At<Qtt
    // Grade>`.
    //
    // The sub-lattice is the singleton `{value}` over the parent's
    // bitmask carrier:
    //
    //   At<Atoms...>::value          = OR of bit(Atoms)
    //   At<Atoms...>::element_type   = empty struct (zero-byte carrier)
    //   At<>::bottom() == top()      = the singleton's element
    //   leq(any, any)                = true       (singleton trivially)
    //   join/meet                    = the singleton's element
    //
    // Lattice axioms hold trivially because every operation returns the
    // single inhabitant.  Birkhoff (Lattice.h:269) guarantees a bounded
    // sub-lattice of a distributive lattice is itself distributive.
    template <Effect... Atoms>
    struct At {
        // The bitmask encoding the pinned set of atoms.  Computed via
        // fold-or; empty pack collapses to 0.  Bit position i comes
        // from the underlying value of Effect::i — stable across
        // append-only Universe extensions per 28_04 §8.5.3.
        static constexpr std::uint64_t value =
            ((std::uint64_t{1} << static_cast<std::uint8_t>(Atoms)) | ...
             | std::uint64_t{0});

        // Singleton element — empty struct so Graded's
        // [[no_unique_address]] grade_type grade_ collapses to zero.
        struct element_type {
            constexpr element_type() noexcept = default;
            [[nodiscard]] constexpr bool operator==(
                element_type) const noexcept { return true; }
        };
        static_assert(std::is_empty_v<element_type>,
            "At<Atoms...>::element_type must be empty so Graded's "
            "[[no_unique_address]] grade_ slot collapses to 0 bytes.");

        [[nodiscard]] static constexpr element_type bottom() noexcept {
            return {};
        }
        [[nodiscard]] static constexpr element_type top() noexcept {
            return {};
        }
        [[nodiscard]] static constexpr bool leq(
            element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(
            element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(
            element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            return "EffectRow::At";
        }

        // Bridge to the parent lattice's bitmask carrier.  Consumers
        // wanting the runtime bitmask (cache-key generation, runtime
        // diagnostics) read `At<...>::bits()` to escape the type-level
        // singleton encoding back to the parent lattice's element_type.
        [[nodiscard]] static constexpr std::uint64_t bits() noexcept {
            return value;
        }
    };
};

// ── Bridge: Row<Es...> → bitmask ────────────────────────────────────
//
// Default specialization handles non-Row types by returning 0 — they
// trivially satisfy `leq(0, anything) == true` against any descriptor.
// This is correct for the "no row attached" case; it's never the
// answer to a meaningful query because non-Row types should not be
// queried in the first place.
//
// The Row<Es...> specialization folds the Effect pack into a bitmask
// via fold-or.  Empty pack = 0 = bottom; full pack = top.

template <typename R>
inline constexpr EffectRowLattice::element_type row_descriptor_v = 0;

template <Effect... Es>
inline constexpr EffectRowLattice::element_type row_descriptor_v<Row<Es...>> =
    ((EffectRowLattice::element_type{1} <<
      static_cast<std::uint8_t>(Es)) | ...
     | EffectRowLattice::element_type{0});  // identity for empty pack

// ── Concept conformance + lattice-axiom witnesses ──────────────────

static_assert(::crucible::algebra::Lattice<EffectRowLattice>);
static_assert(::crucible::algebra::BoundedBelowLattice<EffectRowLattice>);
static_assert(::crucible::algebra::BoundedAboveLattice<EffectRowLattice>);
static_assert(::crucible::algebra::BoundedLattice<EffectRowLattice>);

// At<Atoms...> singleton sub-lattice (FOUND-H01-AUDIT-1) — every pinned
// pack must satisfy the same Lattice / BoundedLattice concept chain so
// downstream Graded<Modality::Relative, EffectRowLattice::At<Es...>, T>
// instantiations type-check uniformly.  Spot-witness across the empty
// pack, single atom, multi-atom, and the universe.
static_assert(::crucible::algebra::Lattice<EffectRowLattice::At<>>);
static_assert(::crucible::algebra::BoundedLattice<EffectRowLattice::At<>>);
static_assert(::crucible::algebra::Lattice<
    EffectRowLattice::At<Effect::Alloc>>);
static_assert(::crucible::algebra::BoundedLattice<
    EffectRowLattice::At<Effect::Alloc>>);
static_assert(::crucible::algebra::Lattice<
    EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(::crucible::algebra::BoundedLattice<
    EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);

// At<>::element_type is empty — the Graded grade slot collapses to 0
// bytes, mirroring ConfLattice::At, ToleranceLattice::At, QttSemiring::
// At.  Without this property the type-level grade pinning would force
// a runtime carrier into every Computation<R, T> instance.
static_assert(std::is_empty_v<EffectRowLattice::At<>::element_type>);
static_assert(std::is_empty_v<
    EffectRowLattice::At<Effect::Alloc>::element_type>);
static_assert(std::is_empty_v<
    EffectRowLattice::At<Effect::Alloc, Effect::IO>::element_type>);

// At<...>::bits() is a stable bridge to the parent's bitmask carrier;
// the encoding agrees with row_descriptor_v at every spot witness.
static_assert(EffectRowLattice::At<>::bits() == 0);
static_assert(EffectRowLattice::At<Effect::Alloc>::bits() ==
    (std::uint64_t{1} << static_cast<std::uint8_t>(Effect::Alloc)));
static_assert(EffectRowLattice::At<Effect::IO>::bits() ==
    (std::uint64_t{1} << static_cast<std::uint8_t>(Effect::IO)));
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits() ==
    EffectRowLattice::join(
        EffectRowLattice::At<Effect::Alloc>::bits(),
        EffectRowLattice::At<Effect::IO>::bits()));

// At<>::bits() agrees with row_descriptor_v<Row<Atoms...>> — the two
// type-level descriptors collapse to the same bitmask, proving the
// At<> ↔ Row<> bridge needed by H03's Computation alias.
static_assert(EffectRowLattice::At<>::bits()
              == row_descriptor_v<Row<>>);
static_assert(EffectRowLattice::At<Effect::Alloc>::bits()
              == row_descriptor_v<Row<Effect::Alloc>>);
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == row_descriptor_v<Row<Effect::Alloc, Effect::IO>>);
// Order independence — At<A,B>::bits() == At<B,A>::bits() (set-fold).
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == EffectRowLattice::At<Effect::IO, Effect::Alloc>::bits());

// ── Bridge: Row<Es...> → At<Es...> (FOUND-H03 prerequisite) ────────
//
// Type-level lifting from `Row<Es...>` into the matching singleton
// `At<Es...>` sub-lattice.  H03's `ComputationGraded<R, T>` alias
// resolves to `Graded<Modality::Relative, effect_row_to_at_t<R>, T>`
// — the helper performs the `Row → At` pattern-match so the alias
// stays a pure typedef with no per-call lookup cost.
//
// No primary template definition: the *only* admitted argument shape
// is `Row<Es...>`.  Passing a non-Row type to `effect_row_to_at_t`
// produces a hard error ("incomplete type") at the point of use,
// surfacing the misuse at the alias declaration rather than silently
// succeeding with a wrong grade.  This is the same discipline applied
// to GradedTrait's `graded_modality` (algebra/GradedTrait.h:159).

namespace detail {
template <typename R> struct effect_row_to_at;  // primary undefined on purpose

template <Effect... Es>
struct effect_row_to_at<Row<Es...>> {
    using type = EffectRowLattice::template At<Es...>;
};
}  // namespace detail

template <typename R>
using effect_row_to_at_t = typename detail::effect_row_to_at<R>::type;

// ── effect_row_to_at_t agreement witnesses ──────────────────────────
//
// Spot-check the helper against direct `At<Es...>` instantiations
// across the empty pack, single atom, and multi-atom cases.  Order-
// independence + bit-position stability already proven at the At<>
// level; here we just verify the pattern-match.
static_assert(std::is_same_v<
    effect_row_to_at_t<Row<>>,
    EffectRowLattice::At<>>);
static_assert(std::is_same_v<
    effect_row_to_at_t<Row<Effect::Alloc>>,
    EffectRowLattice::At<Effect::Alloc>>);
static_assert(std::is_same_v<
    effect_row_to_at_t<Row<Effect::Bg>>,
    EffectRowLattice::At<Effect::Bg>>);
static_assert(std::is_same_v<
    effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>,
    EffectRowLattice::At<Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<
    effect_row_to_at_t<Row<
        Effect::Alloc, Effect::IO, Effect::Block,
        Effect::Bg,    Effect::Init, Effect::Test>>,
    EffectRowLattice::At<
        Effect::Alloc, Effect::IO, Effect::Block,
        Effect::Bg,    Effect::Init, Effect::Test>>);

// Helper output's bits() agrees with row_descriptor_v on the input —
// the round-trip Row → At → bits matches the direct Row → bits bridge.
static_assert(effect_row_to_at_t<Row<>>::bits()
              == row_descriptor_v<Row<>>);
static_assert(effect_row_to_at_t<Row<Effect::Alloc>>::bits()
              == row_descriptor_v<Row<Effect::Alloc>>);
static_assert(effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>::bits()
              == row_descriptor_v<Row<Effect::Alloc, Effect::IO>>);

// ── Self-test block ────────────────────────────────────────────────

namespace detail::effect_row_lattice_self_test {

using L  = EffectRowLattice;
using EL = L::element_type;

// Witness elements covering the full lattice surface:
//   0           = bottom (empty row)
//   1           = {Alloc}      (only bit 0)
//   2           = {IO}         (only bit 1)
//   3           = {Alloc, IO}  (bits 0+1)
//   L::top()    = full universe (bits 0..5)
//   intermediate witnesses test the partial-order machinery.

inline constexpr EL e_bot   = L::bottom();
inline constexpr EL e_alloc = EL{1} << static_cast<std::uint8_t>(Effect::Alloc);
inline constexpr EL e_io    = EL{1} << static_cast<std::uint8_t>(Effect::IO);
inline constexpr EL e_block = EL{1} << static_cast<std::uint8_t>(Effect::Block);
inline constexpr EL e_bg    = EL{1} << static_cast<std::uint8_t>(Effect::Bg);
inline constexpr EL e_init  = EL{1} << static_cast<std::uint8_t>(Effect::Init);
inline constexpr EL e_test  = EL{1} << static_cast<std::uint8_t>(Effect::Test);
inline constexpr EL e_top   = L::top();

// Sanity: bottom is below top; top is above bottom.
static_assert(L::leq(e_bot, e_top));
static_assert(!L::leq(e_top, e_bot));

// Each single-atom descriptor sits strictly between bottom and top.
static_assert(L::leq(e_bot, e_alloc));
static_assert(L::leq(e_alloc, e_top));
static_assert(!L::leq(e_alloc, e_io));    // disjoint single atoms
static_assert(!L::leq(e_io,    e_alloc));

// Join = set union: {Alloc} ∪ {IO} = {Alloc, IO}.
static_assert(L::join(e_alloc, e_io) == (e_alloc | e_io));
static_assert(L::join(e_alloc, e_bot) == e_alloc);
static_assert(L::join(e_alloc, e_top) == e_top);

// Meet = set intersection: {Alloc, IO} ∩ {IO, Block} = {IO}.
static_assert(L::meet(e_alloc | e_io, e_io | e_block) == e_io);
static_assert(L::meet(e_alloc, e_bot) == e_bot);
static_assert(L::meet(e_alloc, e_top) == e_alloc);

// Top contains every named atom.
static_assert((e_top & e_alloc) == e_alloc);
static_assert((e_top & e_io)    == e_io);
static_assert((e_top & e_block) == e_block);
static_assert((e_top & e_bg)    == e_bg);
static_assert((e_top & e_init)  == e_init);
static_assert((e_top & e_test)  == e_test);

// Top has exactly `effect_count` bits set; nothing beyond.
static_assert(L::top() == ((EL{1} << effect_count) - EL{1}));

// ── Lattice-axiom witnesses (verify_bounded_lattice_axioms_at) ─────
//
// Three-witness invocations across representative element triples
// prove the bounded-lattice rollup at each cell.  The triples cover:
//   - all-bottom (degenerate; reflexive laws fire)
//   - bottom + single + double (chain witnesses)
//   - three pairwise-disjoint singletons (true 3D distributive case)
//   - includes top (top-identity laws fire)

static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(
    e_bot, e_bot, e_bot));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(
    e_bot, e_alloc, e_alloc | e_io));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(
    e_alloc, e_io, e_block));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(
    e_alloc | e_io, e_io | e_block, e_block | e_bg));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(
    e_bot, e_alloc, e_top));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(
    e_top, e_top, e_top));

// ── Distributivity (Birkhoff) ──────────────────────────────────────
//
// Powerset lattices are distributive — the meet and join distribute
// over each other.  Verify at the same triples.

static_assert(::crucible::algebra::verify_distributive_lattice<L>(
    e_alloc, e_io, e_block));
static_assert(::crucible::algebra::verify_distributive_lattice<L>(
    e_alloc | e_io, e_io | e_block, e_block | e_bg));
static_assert(::crucible::algebra::verify_distributive_lattice<L>(
    e_bot, e_alloc, e_top));

// ── row_descriptor_v bridge — type-level Row → bitmask ─────────────

static_assert(row_descriptor_v<Row<>>                                   == e_bot);
static_assert(row_descriptor_v<Row<Effect::Alloc>>                      == e_alloc);
static_assert(row_descriptor_v<Row<Effect::IO>>                         == e_io);
static_assert(row_descriptor_v<Row<Effect::Block>>                      == e_block);
static_assert(row_descriptor_v<Row<Effect::Bg>>                         == e_bg);
static_assert(row_descriptor_v<Row<Effect::Init>>                       == e_init);
static_assert(row_descriptor_v<Row<Effect::Test>>                       == e_test);

// Multi-atom rows: descriptor is the OR of single-atom descriptors.
static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>
              == (e_alloc | e_io));
static_assert(row_descriptor_v<Row<Effect::Block, Effect::Alloc, Effect::IO>>
              == (e_block | e_alloc | e_io));

// Order independence — the bridge is a set-fold, not a sequence-fold.
static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>
              == row_descriptor_v<Row<Effect::IO, Effect::Alloc>>);

// Duplicate atoms — the bridge ignores duplication.
static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::Alloc>>
              == row_descriptor_v<Row<Effect::Alloc>>);

// AllRow analog: union of every named atom equals top().
static_assert(row_descriptor_v<Row<
    Effect::Alloc, Effect::IO, Effect::Block,
    Effect::Bg,    Effect::Init, Effect::Test>> == e_top);

// Non-Row types map to bottom (default-specialization safety).
static_assert(row_descriptor_v<int>     == 0);
static_assert(row_descriptor_v<double>  == 0);

// ── Bridge ↔ EffectRow.h Subrow concept agreement ──────────────────
//
// The semantic guarantee: `is_subrow_v<R1, R2>` (membership-based) iff
// `EffectRowLattice::leq(row_descriptor_v<R1>, row_descriptor_v<R2>)`
// (bitmask-based).  The two surfaces MUST agree at every Subrow query
// site — otherwise downstream Graded<EffectRowLattice, _, T> code
// would misclassify rows.

static_assert( L::leq(row_descriptor_v<Row<>>,
                      row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<>, Row<Effect::Alloc>>);

static_assert( L::leq(row_descriptor_v<Row<Effect::Alloc>>,
                      row_descriptor_v<Row<Effect::Alloc, Effect::IO>>)
              == is_subrow_v<Row<Effect::Alloc>,
                             Row<Effect::Alloc, Effect::IO>>);

static_assert( L::leq(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>,
                      row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<Effect::Alloc, Effect::IO>,
                             Row<Effect::Alloc>>);

static_assert( L::leq(row_descriptor_v<Row<Effect::IO>>,
                      row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<Effect::IO>, Row<Effect::Alloc>>);

}  // namespace detail::effect_row_lattice_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per the runtime smoke-test discipline (auto-memory feedback): exercise
// every accessor with non-constant arguments so consteval-vs-constexpr
// regressions and inline-body bugs are caught alongside the static_assert
// wall above.

inline void runtime_smoke_test_lattice() noexcept {
    using L  = EffectRowLattice;
    using EL = L::element_type;

    [[maybe_unused]] EL b   = L::bottom();
    [[maybe_unused]] EL t   = L::top();

    [[maybe_unused]] bool ok_le = L::leq(b, t);
    [[maybe_unused]] EL u       = L::join(b, t);
    [[maybe_unused]] EL i       = L::meet(b, t);

    [[maybe_unused]] auto nm = ::crucible::algebra::lattice_name<L>();

    // Bridge with type-level rows — confirms the alias names are
    // visible at the boundary, not just inside the self-test ns.
    [[maybe_unused]] EL d_pure  = row_descriptor_v<Row<>>;
    [[maybe_unused]] EL d_alloc = row_descriptor_v<Row<Effect::Alloc>>;

    // At<> singleton sub-lattice (FOUND-H01-AUDIT-1) — drive the
    // type-level grade-pin surface so its accessors get instantiated
    // alongside the parent lattice's.  Same discipline as the parent
    // smoke test: instantiate every consteval/constexpr accessor with
    // non-constant args + concept-based capability checks.
    using AtAlloc = L::template At<Effect::Alloc>;
    using AtAB    = L::template At<Effect::Alloc, Effect::IO>;

    [[maybe_unused]] auto at_b = AtAlloc::bottom();
    [[maybe_unused]] auto at_t = AtAlloc::top();
    [[maybe_unused]] bool at_le = AtAlloc::leq(at_b, at_t);
    [[maybe_unused]] auto at_join = AtAlloc::join(at_b, at_t);
    [[maybe_unused]] auto at_meet = AtAlloc::meet(at_b, at_t);
    [[maybe_unused]] auto at_nm   = AtAlloc::name();
    [[maybe_unused]] std::uint64_t at_bits = AtAlloc::bits();
    [[maybe_unused]] std::uint64_t ab_bits = AtAB::bits();
}

}  // namespace crucible::effects
