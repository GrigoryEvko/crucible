#pragma once

// ── crucible::algebra::ModalityKind ─────────────────────────────────
//
// The four modality forms parameterizing `Graded<M, L, T>` per Orchard,
// Liepelt, Eades 2023 (arXiv:2309.04324) and 25_04_2026.md §2.2.  Every
// safety wrapper that decorates a value has a graded-modality reading;
// the table below maps each existing wrapper to its modality form.
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
//                       | Budgeted<budget, T>                 |
//   Relative            | RESERVED                            | cross-region
//                       |                                     | flow
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
    Comonad       = 0,
    RelativeMonad = 1,
    Absolute      = 2,
    Relative      = 3,
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
    K == ModalityKind::Relative;

template <ModalityKind K>
concept ComonadModality        = (K == ModalityKind::Comonad);

template <ModalityKind K>
concept RelativeMonadModality  = (K == ModalityKind::RelativeMonad);

template <ModalityKind K>
concept AbsoluteModality       = (K == ModalityKind::Absolute);

template <ModalityKind K>
concept RelativeModality       = (K == ModalityKind::Relative);

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
    (K == ModalityKind::Absolute) || (K == ModalityKind::Relative);

// ── Diagnostic name emitter ─────────────────────────────────────────
[[nodiscard]] consteval std::string_view modality_name(ModalityKind K) noexcept {
    switch (K) {
        case ModalityKind::Comonad:       return "Comonad";
        case ModalityKind::RelativeMonad: return "RelativeMonad";
        case ModalityKind::Absolute:      return "Absolute";
        case ModalityKind::Relative:      return "Relative";
    }
    // Unreachable for IsModality<K>; keep an explicit sentinel for the
    // case where a future ModalityKind value forgets to update this
    // switch — the static_assert in detail::modality_self_test catches
    // the missing arm at compile time.
    return std::string_view{"<unknown ModalityKind>"};
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

}  // namespace modality

// ── Self-test block ─────────────────────────────────────────────────
//
// Per code_guide.md §XIII.2: every metafunction ships with positive,
// negative, composition, and edge-case coverage.  These fail at
// header-inclusion time if any modality invariant breaks.
namespace detail::modality_self_test {

// Cardinality.  Held at four for the original Comonad / RelativeMonad
// / Absolute / Relative quartet — if a future revision adds a fifth
// modality, this guard fires AND the name-coverage assertion below
// independently fires (the latter is the load-bearing one because it
// pinpoints the missing switch arm in modality_name()).
static_assert(modality_kind_count == 4,
    "Modality count diverged from the original quartet — confirm the "
    "addition is intentional and the name-coverage assertion below "
    "still fires for the new enumerator.");

// Name coverage via reflection — every enumerator MUST have a
// non-sentinel name from modality_name().  If an enumerator is added
// without updating the modality_name() switch, this fires with
// the offending value visible in the diagnostic.
[[nodiscard]] consteval bool every_modality_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^ModalityKind));
    template for (constexpr auto en : enumerators) {
        if (modality_name([:en:]) == std::string_view{"<unknown ModalityKind>"}) {
            return false;
        }
    }
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

// Tag-type round-trip.
static_assert(modality::Comonad_t::kind       == ModalityKind::Comonad);
static_assert(modality::RelativeMonad_t::kind == ModalityKind::RelativeMonad);
static_assert(modality::Absolute_t::kind      == ModalityKind::Absolute);
static_assert(modality::Relative_t::kind      == ModalityKind::Relative);

// Concept gate exhaustiveness.
static_assert(IsModality<ModalityKind::Comonad>);
static_assert(IsModality<ModalityKind::RelativeMonad>);
static_assert(IsModality<ModalityKind::Absolute>);
static_assert(IsModality<ModalityKind::Relative>);

// Per-form concept narrowness.
static_assert( ComonadModality<ModalityKind::Comonad>);
static_assert(!ComonadModality<ModalityKind::Absolute>);
static_assert( RelativeMonadModality<ModalityKind::RelativeMonad>);
static_assert(!RelativeMonadModality<ModalityKind::Comonad>);
static_assert( AbsoluteModality<ModalityKind::Absolute>);
static_assert(!AbsoluteModality<ModalityKind::Relative>);
static_assert( RelativeModality<ModalityKind::Relative>);
static_assert(!RelativeModality<ModalityKind::Absolute>);

// Diagnostic name coverage — every kind has a non-empty name; no kind
// resolves to the "<unknown>" sentinel.
static_assert(!modality_name(ModalityKind::Comonad).empty());
static_assert(!modality_name(ModalityKind::RelativeMonad).empty());
static_assert(!modality_name(ModalityKind::Absolute).empty());
static_assert(!modality_name(ModalityKind::Relative).empty());
static_assert( modality_name(ModalityKind::Comonad)       != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::RelativeMonad) != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::Absolute)      != "<unknown ModalityKind>");
static_assert( modality_name(ModalityKind::Relative)      != "<unknown ModalityKind>");

// Tag types are empty (EBO must collapse them to zero bytes when used
// as `[[no_unique_address]]` members).
static_assert(std::is_empty_v<modality::Comonad_t>);
static_assert(std::is_empty_v<modality::RelativeMonad_t>);
static_assert(std::is_empty_v<modality::Absolute_t>);
static_assert(std::is_empty_v<modality::Relative_t>);

}  // namespace detail::modality_self_test

}  // namespace crucible::algebra
