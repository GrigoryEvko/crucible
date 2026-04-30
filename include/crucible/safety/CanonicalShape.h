#pragma once

// ── crucible::safety::extract::CanonicalShape ───────────────────────
//
// FOUND-D20 of 27_04_2026.md §3.8 + §5.6 + 28_04_2026_effects.md §6.1.
// The closing umbrella over the FOUND-D shape taxonomy: a single
// concept that admits a function iff it matches AT LEAST ONE
// canonical shape (D12-D19), plus its complement NonCanonical
// (the §3.8 catch-all).
//
// ── What this header ships ──────────────────────────────────────────
//
//   CanonicalShape<auto FnPtr>
//                          Concept satisfied iff FnPtr matches at
//                          least one of:
//                            - UnaryTransform     (D12)
//                            - BinaryTransform    (D13)
//                            - Reduction          (D14)
//                            - ProducerEndpoint   (D15)
//                            - ConsumerEndpoint   (D16)
//                            - SwmrWriter         (D17)
//                            - SwmrReader         (D18)
//                            - PipelineStage      (D19)
//
//   NonCanonical<auto FnPtr>
//                          Concept satisfied iff !CanonicalShape.
//                          The §3.8 catch-all: dispatcher refuses
//                          to auto-route NonCanonical functions
//                          and emits a diagnostic via FOUND-E.
//
//   is_canonical_shape_v<auto FnPtr>
//                          Variable-template form for use inside
//                          metaprogram folds.
//
//   CanonicalShapeKind     enum class — one value per canonical
//                          shape, plus NonCanonical.  Used by the
//                          dispatcher's `canonical_shape_kind_v`
//                          query to choose the per-shape lowering
//                          AND by FOUND-E for diagnostic output.
//
//   canonical_shape_kind_v<auto FnPtr>
//                          Compile-time variable evaluating to the
//                          CanonicalShapeKind enum value matching
//                          FnPtr.  Returns NonCanonical when no
//                          shape matches.
//
//   canonical_shape_name(CanonicalShapeKind k)
//                          Constant-time string_view lookup —
//                          human-readable shape name for
//                          diagnostics.  No allocation, no
//                          formatting.
//
// ── Mutual exclusivity guarantee ────────────────────────────────────
//
// 27_04 §5.6 stipulates that the canonical shapes are mutually
// exclusive: a function satisfies AT MOST ONE shape predicate.  The
// eight per-shape concepts in D12-D19 enforce this structurally via
// per-param wrapper-detection clauses + per-return-type clauses +
// slot-order constraints.  This header verifies the property in the
// sentinel TU: for every shape's worked example, exactly one shape
// predicate is true (and `canonical_shape_kind_v` matches).
//
// ── Resolution order ────────────────────────────────────────────────
//
// `canonical_shape_kind_v` evaluates the shape predicates in this
// fixed order:
//
//   1. UnaryTransform   (most specific arity-1 case)
//   2. BinaryTransform  (most specific arity-2 same-shape case)
//   3. Reduction        (arity-2 OwnedRegion + reduce_into)
//   4. ProducerEndpoint (arity-2 with Producer in slot 0)
//   5. ConsumerEndpoint (arity-2 with Consumer in slot 0)
//   6. SwmrWriter       (arity-2 with SwmrWriter in slot 0)
//   7. SwmrReader       (arity-1 with SwmrReader return)
//   8. PipelineStage    (arity-2 Consumer+Producer)
//   9. NonCanonical     (fallback)
//
// Because the shapes are mutually exclusive, the order matters only
// for diagnostic-message stability — the matched kind is the same
// regardless of which clause hit first.  Reordering the clauses
// must not change the test outcomes.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval shape recognition.
//   TypeSafe — disjunction of eight mutually-exclusive predicates;
//              non-canonical signatures fall through to
//              CanonicalShapeKind::NonCanonical.
//   DetSafe — same FnPtr → same recognition result.

#include <crucible/safety/BinaryTransform.h>
#include <crucible/safety/ConsumerEndpoint.h>
#include <crucible/safety/PipelineStage.h>
#include <crucible/safety/ProducerEndpoint.h>
#include <crucible/safety/Reduction.h>
#include <crucible/safety/SwmrReader.h>
#include <crucible/safety/SwmrWriter.h>
#include <crucible/safety/UnaryTransform.h>

#include <cstdint>
#include <string_view>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── Umbrella concept + complement ──────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <auto FnPtr>
concept CanonicalShape =
       UnaryTransform<FnPtr>
    || BinaryTransform<FnPtr>
    || Reduction<FnPtr>
    || ProducerEndpoint<FnPtr>
    || ConsumerEndpoint<FnPtr>
    || SwmrWriter<FnPtr>
    || SwmrReader<FnPtr>
    || PipelineStage<FnPtr>;

template <auto FnPtr>
concept NonCanonical = !CanonicalShape<FnPtr>;

template <auto FnPtr>
inline constexpr bool is_canonical_shape_v = CanonicalShape<FnPtr>;

template <auto FnPtr>
inline constexpr bool is_non_canonical_v = NonCanonical<FnPtr>;

// ═════════════════════════════════════════════════════════════════════
// ── CanonicalShapeKind enum + lookup ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════

enum class CanonicalShapeKind : std::uint8_t {
    NonCanonical = 0,
    UnaryTransform,
    BinaryTransform,
    Reduction,
    ProducerEndpoint,
    ConsumerEndpoint,
    SwmrWriter,
    SwmrReader,
    PipelineStage,
};

namespace detail {

template <auto FnPtr>
consteval CanonicalShapeKind canonical_shape_kind_impl() noexcept {
    if constexpr (UnaryTransform<FnPtr>) {
        return CanonicalShapeKind::UnaryTransform;
    } else if constexpr (BinaryTransform<FnPtr>) {
        return CanonicalShapeKind::BinaryTransform;
    } else if constexpr (Reduction<FnPtr>) {
        return CanonicalShapeKind::Reduction;
    } else if constexpr (ProducerEndpoint<FnPtr>) {
        return CanonicalShapeKind::ProducerEndpoint;
    } else if constexpr (ConsumerEndpoint<FnPtr>) {
        return CanonicalShapeKind::ConsumerEndpoint;
    } else if constexpr (SwmrWriter<FnPtr>) {
        return CanonicalShapeKind::SwmrWriter;
    } else if constexpr (SwmrReader<FnPtr>) {
        return CanonicalShapeKind::SwmrReader;
    } else if constexpr (PipelineStage<FnPtr>) {
        return CanonicalShapeKind::PipelineStage;
    } else {
        return CanonicalShapeKind::NonCanonical;
    }
}

}  // namespace detail

template <auto FnPtr>
inline constexpr CanonicalShapeKind canonical_shape_kind_v =
    detail::canonical_shape_kind_impl<FnPtr>();

// ═════════════════════════════════════════════════════════════════════
// ── Human-readable name lookup ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Constant-time string_view lookup for diagnostic output.  The
// dispatcher uses this to render error messages naming the matched
// shape (or naming "NonCanonical" with the §3.8 fallthrough hint).

[[nodiscard]] constexpr std::string_view canonical_shape_name(
    CanonicalShapeKind k) noexcept
{
    switch (k) {
        case CanonicalShapeKind::UnaryTransform:    return "UnaryTransform";
        case CanonicalShapeKind::BinaryTransform:   return "BinaryTransform";
        case CanonicalShapeKind::Reduction:         return "Reduction";
        case CanonicalShapeKind::ProducerEndpoint:  return "ProducerEndpoint";
        case CanonicalShapeKind::ConsumerEndpoint:  return "ConsumerEndpoint";
        case CanonicalShapeKind::SwmrWriter:        return "SwmrWriter";
        case CanonicalShapeKind::SwmrReader:        return "SwmrReader";
        case CanonicalShapeKind::PipelineStage:     return "PipelineStage";
        case CanonicalShapeKind::NonCanonical:      return "NonCanonical";
        default: return "Unknown";  // unreachable under exhaustive enum
    }
}

template <auto FnPtr>
inline constexpr std::string_view canonical_shape_name_of_v =
    canonical_shape_name(canonical_shape_kind_v<FnPtr>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::canonical_shape_self_test {

// Functions used as witnesses for the per-kind lookup tests.
// Concrete shape-matching witnesses live in the sentinel TU
// (test_canonical_shape.cpp); the header self-test only covers
// negatives + the NonCanonical fallback semantics.

inline void f_two_ints(int, int) noexcept {}
static_assert(!CanonicalShape<&f_two_ints>);
static_assert( NonCanonical<&f_two_ints>);
static_assert(canonical_shape_kind_v<&f_two_ints>
              == CanonicalShapeKind::NonCanonical);
static_assert(canonical_shape_name(CanonicalShapeKind::NonCanonical)
              == std::string_view{"NonCanonical"});

inline void f_three_params(int, int, int) noexcept {}
static_assert(!CanonicalShape<&f_three_params>);
static_assert( NonCanonical<&f_three_params>);

}  // namespace detail::canonical_shape_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool canonical_shape_smoke_test() noexcept {
    using namespace detail::canonical_shape_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !CanonicalShape<&f_two_ints>;
        ok = ok &&  NonCanonical<&f_two_ints>;
        ok = ok && (canonical_shape_kind_v<&f_two_ints>
                    == CanonicalShapeKind::NonCanonical);
        ok = ok && (canonical_shape_name(canonical_shape_kind_v<
                                          &f_three_params>)
                    == std::string_view{"NonCanonical"});
    }
    return ok;
}

}  // namespace crucible::safety::extract
