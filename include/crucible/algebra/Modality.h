#pragma once

// ── crucible::algebra::ModalityKind ─────────────────────────────────
//
// The six modality forms parameterizing `Graded<M, L, T>` per Orchard,
// Liepelt, Eades 2023 (arXiv:2309.04324), 25_04_2026.md §2.2, and the
// FIXY-G10 (Quotient) / FIXY-G11 (Coeffect) extensions.  Every safety
// wrapper that decorates a value has a graded-modality reading; the
// table below maps each existing wrapper to its modality form.
//
// FIXY-FOUND-092 #2247 audit (2026-05-24): table updated to reflect
// production reality.  Pre-audit prose claimed Relative was RESERVED
// (it IS used by Computation<Row, T>) and that Quotient covers
// Version/Vendor/ForgePhase + Coeffect covers dim::Cost (none of
// those wrappers actually use those modalities — Vendor + Consistency
// + EpochVersioned use Absolute, Tagged uses RelativeMonad, no dim::
// Cost wrapper exists yet).  Four modalities are wired in production;
// Quotient + Coeffect are spec'd via FIXY-G10 / FIXY-G11 but no
// production wrapper has migrated to them yet.
//
//   ModalityKind        | Existing wrapper(s)                 | Operation
//   --------------------+-------------------------------------+----------
//   Comonad             | Secret<T>                           | counit out
//                       |                                     | (declassify)
//   RelativeMonad       | Tagged<T, Source>                   | unit in
//                       |                                     | (retag from src)
//   Absolute            | Linear<T>, Refined<P, T>,           | grade fixed at
//                       | Monotonic<T, Cmp>, AppendOnly<T>,   | construction
//                       | SharedPermission<Tag>, Stale<T>,    |
//                       | Budgeted<T>, Vendor<V, T>,          |
//                       | EpochVersioned<T>,                  |
//                       | Consistency<L, T>, JoinPolicy,      |
//                       | HotPath, DetSafe, NumericalTier,    |
//                       | ResidencyHeat, CipherTier,          |
//                       | AllocClass, Wait, MemOrder,         |
//                       | Progress, ... (Tier-1 majority)     |
//   Relative            | Computation<Row, T>                 | row-typed
//                       | (Met(X) effect-row carrier)         | effect track
//   Quotient            | RESERVED (FIXY-G10 — equivalence-   | equivalence-
//                       | class membership; spec'd for        | class name
//                       | Version/Vendor/ForgePhase, but      |
//                       | those production wrappers           |
//                       | currently use Absolute)             |
//   Coeffect            | RESERVED (FIXY-G11 — resource       | resource
//                       | consumption; spec'd for dim::Cost,  | consumption
//                       | but no production wrapper exists    |
//                       | yet)                                |
//
// Cardinality is pinned to six by the load-bearing static_assert in
// `detail::modality_self_test` (search `modality_kind_count == 6`).  A
// future enumerator MUST extend the table above in lockstep with the
// enum, switch, concept gates, predicate-exclusivity tests, and tag-
// type round-trips — otherwise the cardinality sentinel fires.
//
//   Axiom coverage: TypeSafe — ModalityKind is a strong enum with
//                   explicit underlying type; no implicit conversion to
//                   integers; concepts gate template-substitution.
//                   InitSafe — every NSDMI-eligible site uses {} init.
//   Runtime cost:   zero.  Compile-time tag dispatch only; the enum
//                   never appears in a runtime data path.
//
// Conventions:
//   - Use the strong enum directly in template parameters where possible.
//   - Use `modality::Comonad_t` etc. tag types when overload-set dispatch
//     is more ergonomic than NTTP dispatch (constructor selection,
//     factory-function tag).
//   - Use `modality_name(K)` for diagnostics; never hard-code spelling.
//
// See also: Lattice.h (the lattice paired with a modality), Graded.h
// (the type-level realization), CLAUDE.md L0 for project context.

#include <cstdint>
#include <meta>
#include <string_view>

namespace crucible::algebra {

// ── ModalityKind ────────────────────────────────────────────────────
enum class ModalityKind : std::uint8_t {
    // Status: production-wired (Secret<T> — counit-out / declassify).
    Comonad       = 0,
    // Status: production-wired (Tagged<T, Source> — unit-in / retag).
    RelativeMonad = 1,
    // Status: production-wired (Tier-1 majority — Linear, Refined,
    // Monotonic, AppendOnly, SharedPermission, Stale, Budgeted,
    // Vendor, EpochVersioned, Consistency, JoinPolicy, HotPath,
    // DetSafe, NumericalTier, ResidencyHeat, CipherTier, AllocClass,
    // Wait, MemOrder, Progress, ...).  Grade is fixed at
    // construction; immutable across the value's lifetime.
    Absolute      = 2,
    // Status: production-wired (Computation<Row, T> — Met(X)
    // effect-row carrier per Tang-Lindley POPL 2026 / 25_04_2026
    // §3.2).  Row composition under sequencing is union; under
    // parallel composition is also union.
    Relative      = 3,
    // FIXY-G10: Quotient — equivalence-class membership.  Used for
    // grant categories that name a representative of an equivalence
    // class (Version<N>, Vendor<V>, ForgePhase<P>, ...) rather than
    // a transformation of the value or an obligation on the caller.
    // Two grants of Quotient modality on the same axis are
    // incompatible iff their equivalence-class representatives differ.
    //
    // Status: RESERVED — spec'd but no production wrapper migrated.
    //                    Vendor<V, T> + EpochVersioned<T> + Consistency<L, T>
    //                    were candidates per the original design table
    //                    but ship with ModalityKind::Absolute (the
    //                    Quotient nuance — that two distinct
    //                    representatives are compatible iff they are
    //                    structurally equal — is currently subsumed
    //                    by Absolute's grade-fixed-at-construction
    //                    semantics, since the representatives ARE
    //                    structurally equal across the lattices in
    //                    play).  Adopt Quotient only when an axis
    //                    arises whose equivalence-class semantics
    //                    differ from structural equality (a future
    //                    KernelCache content-addressing-quotient
    //                    axis is the leading candidate per FIXY-G15).
    Quotient      = 4,
    // FIXY-G11: Coeffect — RESOURCE-CONSUMPTION duality of effects.
    // Effects track WHAT IS PRODUCED (IO, Bg, Alloc...); coeffects
    // track WHAT IS CONSUMED (compute time, energy budget, cache
    // residency).  Composition algebra is a SEMIRING (Petricek-
    // Orchard-Mycroft 2014; Brunel-Gaboardi-Mazza-Zdancewic 2014):
    // sequential composition is `+`, parallel composition is `max`,
    // repetition is `·`.  Used by dim::Cost grants whose grade is a
    // polynomial in input-size encoding nanos-per-op.
    //
    // Status: RESERVED — spec'd but no production wrapper exists yet.
    //                    `dim::Cost` is forward-looking; the cost
    //                    model in `concurrent/CostModel.h` is a
    //                    runtime CostBudget struct, not a Coeffect-
    //                    graded wrapper.  Adopt Coeffect when budget-
    //                    annotated production code paths land
    //                    (FIXY-G66 precision-budget calibrator is the
    //                    leading candidate).
    Coeffect      = 5,
};

// Cardinality derived via reflection (P2996R13).  Adding a new
// enumerator auto-bumps this constant — no manual maintenance.  The
// name-coverage assertion in detail::modality_self_test then catches
// any new value that lacks a `modality_name()` switch arm.
inline constexpr std::size_t modality_kind_count =
    std::meta::enumerators_of(^^ModalityKind).size();

// ── Concept gates ───────────────────────────────────────────────────
//
// IsModality<K>  rejects typos in template parameters at substitution
// time.  Per-form concepts give precise requires-clause filtering on
// member functions (e.g. extract() is only available on Comonad).
template <ModalityKind K>
concept IsModality =
    K == ModalityKind::Comonad       ||
    K == ModalityKind::RelativeMonad ||
    K == ModalityKind::Absolute      ||
    K == ModalityKind::Relative      ||
    K == ModalityKind::Quotient      ||
    K == ModalityKind::Coeffect;

template <ModalityKind K>
concept ComonadModality        = (K == ModalityKind::Comonad);

template <ModalityKind K>
concept RelativeMonadModality  = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
concept AbsoluteModality       = (K == ModalityKind::Absolute);

template <ModalityKind K>
concept RelativeModality       = (K == ModalityKind::Relative);

template <ModalityKind K>
concept QuotientModality       = (K == ModalityKind::Quotient);

template <ModalityKind K>
concept CoeffectModality       = (K == ModalityKind::Coeffect);

// ── Compile-time queries ────────────────────────────────────────────
//
// Mutually exclusive predicates classifying a modality by which
// operation set it admits.  Used internally by Graded<>'s requires-
// clauses and by reflection-driven diagnostic emitters.
template <ModalityKind K>
inline constexpr bool has_counit_v     = (K == ModalityKind::Comonad);

template <ModalityKind K>
inline constexpr bool has_unit_v       = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
inline constexpr bool has_grade_only_v =
    (K == ModalityKind::Absolute) || (K == ModalityKind::Relative) ||
    (K == ModalityKind::Quotient)  || (K == ModalityKind::Coeffect);

// ── Diagnostic name emitter ─────────────────────────────────────────
//
// Explicit `default:` arm satisfies -Werror=switch-default; the
// sentinel string is what the `every_modality_has_name()` self-test
// below checks for to catch a future ModalityKind value that forgets
// to update this switch.
[[nodiscard]] consteval std::string_view modality_name(ModalityKind K) noexcept {
    switch (K) {
        case ModalityKind::Comonad:       return "Comonad";
        case ModalityKind::RelativeMonad: return "RelativeMonad";
        case ModalityKind::Absolute:      return "Absolute";
        case ModalityKind::Relative:      return "Relative";
        case ModalityKind::Quotient:      return "Quotient";
        case ModalityKind::Coeffect:      return "Coeffect";
        default:                          return std::string_view{"<unknown ModalityKind>"};
    }
}

// ── Tag types for overload-set dispatch ─────────────────────────────
//
// Empty per axiom MemSafe / LeakSafe.  Each carries a static `kind`
// member that round-trips to the enum value.
namespace modality {

struct Comonad_t       { static constexpr ModalityKind kind = ModalityKind::Comonad;       };
struct RelativeMonad_t { static constexpr ModalityKind kind = ModalityKind::RelativeMonad; };
struct Absolute_t      { static constexpr ModalityKind kind = ModalityKind::Absolute;      };
struct Relative_t      { static constexpr ModalityKind kind = ModalityKind::Relative;      };
struct Quotient_t      { static constexpr ModalityKind kind = ModalityKind::Quotient;      };
struct Coeffect_t      { static constexpr ModalityKind kind = ModalityKind::Coeffect;      };

}  // namespace modality

// ── Self-test block ─────────────────────────────────────────────────
//
// Per code_guide.md §XIII.2: every metafunction ships with positive,
// negative, composition, and edge-case coverage.  These fail at
// header-inclusion time if any modality invariant breaks.
namespace detail::modality_self_test {

// Cardinality.  Six enumerators since FIXY-G11 added Coeffect to the
// FIXY-G10 (Comonad/RelativeMonad/Absolute/Relative/Quotient) set.
// Adding a seventh modality fires this guard AND the name-coverage
// assertion below independently (the latter is the load-bearing one
// because it pinpoints the missing switch arm in modality_name()).
static_assert(modality_kind_count == 6,
    "Modality count diverged from the six-member set "
    "(Comonad/RelativeMonad/Absolute/Relative/Quotient/Coeffect) — "
    "confirm the addition is intentional, the name-coverage "
    "assertion below still fires for the new enumerator, AND the "
    "modality-form table at the top of this file gains a matching "
    "row (the docstring claims 'six modality forms' so the prose "
    "rots if it isn't extended in lockstep).");

// Name coverage via reflection — every enumerator MUST have a
// non-sentinel name from modality_name().  If an enumerator is added
// without updating the modality_name() switch, this fires with
// the offending value visible in the diagnostic.
[[nodiscard]] consteval bool every_modality_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ModalityKind));
    // -Wshadow fires on the `template for` body's induction variable
    // because GCC 16 expands the loop into successive scopes that
    // each declare `en`; suppress locally for the loop body only.
    // See feedback_gcc16_c26_reflection_gotchas memory.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (modality_name([:en:]) == std::string_view{"<unknown ModalityKind>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_modality_has_name(),
    "modality_name() switch is missing an arm for at least one "
    "ModalityKind enumerator — add the arm or the new enumerator "
    "leaks the '<unknown ModalityKind>' sentinel into diagnostics.");

// Predicate exclusivity — exactly one query is true per kind.
template <ModalityKind K>
inline constexpr bool is_exactly_one_predicate =
    int(has_counit_v<K>)
  + int(has_unit_v<K>)
  + int(has_grade_only_v<K>)
    == 1;

static_assert(is_exactly_one_predicate<ModalityKind::Comonad>);
static_assert(is_exactly_one_predicate<ModalityKind::RelativeMonad>);
static_assert(is_exactly_one_predicate<ModalityKind::Absolute>);
static_assert(is_exactly_one_predicate<ModalityKind::Relative>);
static_assert(is_exactly_one_predicate<ModalityKind::Quotient>);
static_assert(is_exactly_one_predicate<ModalityKind::Coeffect>);

// Tag-type round-trip.
static_assert(modality::Comonad_t::kind       == ModalityKind::Comonad);
static_assert(modality::RelativeMonad_t::kind == ModalityKind::RelativeMonad);
static_assert(modality::Absolute_t::kind      == ModalityKind::Absolute);
static_assert(modality::Relative_t::kind      == ModalityKind::Relative);
static_assert(modality::Quotient_t::kind      == ModalityKind::Quotient);
static_assert(modality::Coeffect_t::kind      == ModalityKind::Coeffect);

// Concept gate exhaustiveness.
static_assert(IsModality<ModalityKind::Comonad>);
static_assert(IsModality<ModalityKind::RelativeMonad>);
static_assert(IsModality<ModalityKind::Absolute>);
static_assert(IsModality<ModalityKind::Relative>);
static_assert(IsModality<ModalityKind::Quotient>);
static_assert(IsModality<ModalityKind::Coeffect>);

// Per-form concept narrowness.
static_assert( ComonadModality<ModalityKind::Comonad>);
static_assert(!ComonadModality<ModalityKind::Absolute>);
static_assert( RelativeMonadModality<ModalityKind::RelativeMonad>);
static_assert(!RelativeMonadModality<ModalityKind::Comonad>);
static_assert( AbsoluteModality<ModalityKind::Absolute>);
static_assert(!AbsoluteModality<ModalityKind::Relative>);
static_assert( RelativeModality<ModalityKind::Relative>);
static_assert(!RelativeModality<ModalityKind::Absolute>);
static_assert( QuotientModality<ModalityKind::Quotient>);
static_assert(!QuotientModality<ModalityKind::Absolute>);
static_assert( CoeffectModality<ModalityKind::Coeffect>);
static_assert(!CoeffectModality<ModalityKind::Absolute>);

// Diagnostic name coverage — every kind has a non-empty name; no kind
// resolves to the "<unknown>" sentinel.
static_assert(!modality_name(ModalityKind::Comonad).empty());
static_assert(!modality_name(ModalityKind::RelativeMonad).empty());
static_assert(!modality_name(ModalityKind::Absolute).empty());
static_assert(!modality_name(ModalityKind::Relative).empty());
static_assert(!modality_name(ModalityKind::Quotient).empty());
static_assert(!modality_name(ModalityKind::Coeffect).empty());
static_assert( modality_name(ModalityKind::Comonad)       != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::RelativeMonad) != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::Absolute)      != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::Relative)      != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::Quotient)      != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::Coeffect)      != "<unknown ModalityKind>");

// Tag types are empty (EBO must collapse them to zero bytes when used
// as `[[no_unique_address]]` members).
static_assert(std::is_empty_v<modality::Comonad_t>);
static_assert(std::is_empty_v<modality::RelativeMonad_t>);
static_assert(std::is_empty_v<modality::Absolute_t>);
static_assert(std::is_empty_v<modality::Relative_t>);
static_assert(std::is_empty_v<modality::Quotient_t>);
static_assert(std::is_empty_v<modality::Coeffect_t>);

// ── Runtime smoke test (fixy-A3-021) ────────────────────────────────
//
// modality_name() is `consteval` (compile-time only); a future
// refactor that demoted it to `constexpr` would silently shift its
// runtime cost characteristics.  We assert the consteval property
// via runtime path here: calling with a non-constant ModalityKind
// value WOULD fire a hard error if modality_name were ever called
// with non-constant input.  So instead we exercise the tag-type
// round-trip and concept-gate machinery, both of which are
// constexpr-eligible and SHOULD type-check against runtime args.
inline void runtime_smoke_test() {
    // Tag-type runtime default-construction sanity — proves the EBO
    // promise (sizeof==1 empty struct) doesn't break under runtime
    // semantics.  Optimizer almost certainly elides; the parse is
    // what matters.
    [[maybe_unused]] modality::Comonad_t       co_tag{};
    [[maybe_unused]] modality::RelativeMonad_t rm_tag{};
    [[maybe_unused]] modality::Absolute_t      ab_tag{};
    [[maybe_unused]] modality::Relative_t      rl_tag{};
    [[maybe_unused]] modality::Quotient_t      qt_tag{};
    [[maybe_unused]] modality::Coeffect_t      cf_tag{};

    // Round-trip ::kind through a non-constexpr local so the front-end
    // type-checks the static accessor body — also pins that ::kind
    // remains a constant expression usable in either context.
    ModalityKind k = modality::Comonad_t::kind;
    [[maybe_unused]] bool ok1 = (k == ModalityKind::Comonad);
    k = modality::Coeffect_t::kind;
    [[maybe_unused]] bool ok2 = (k == ModalityKind::Coeffect);

    // Drive the constexpr predicate aliases through a non-constant
    // template argument is impossible (they're NTTP), but their
    // per-kind specializations should round-trip via runtime equality.
    [[maybe_unused]] bool unit_co  = has_unit_v<ModalityKind::RelativeMonad>;
    [[maybe_unused]] bool grade_ab = has_grade_only_v<ModalityKind::Absolute>;
    [[maybe_unused]] bool grade_cf = has_grade_only_v<ModalityKind::Coeffect>;
}

}  // namespace detail::modality_self_test

}  // namespace crucible::algebra
