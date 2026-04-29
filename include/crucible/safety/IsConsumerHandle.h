#pragma once

// ── crucible::safety::extract::is_consumer_handle_v ─────────────────
//
// FOUND-D06 of 27_04_2026.md §5.5 + 28_04_2026_effects.md §6.1.  The
// dispatcher-side reading-surface predicate that recognizes the
// consumer half of a Permissioned* channel — the handle types nested
// inside `concurrent::PermissionedSpscChannel`, `PermissionedMpscChannel`,
// `PermissionedMpmcChannel`, `PermissionedShardedGrid`,
// `PermissionedShardedCalendarGrid`, `PermissionedCalendarGrid`, and
// any future channel that exposes a typed consumer endpoint.  Mirrors
// `is_producer_handle_v` (FOUND-D05) at the symmetric pole.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_consumer_handle_v<T>     Variable template: true iff T (after
//                               cv-ref stripping) exposes the consumer
//                               endpoint shape (a non-overloaded
//                               `try_pop()` method returning
//                               `std::optional<P>` for some payload
//                               type P, AND no `try_push`).
//
//   IsConsumerHandle<T>         Concept form for `requires`-clauses.
//
//   consumer_handle_value_t<T>  Alias to the payload-type wrapped by
//                               try_pop's return type, recovered by
//                               decomposing the type of `&T::try_pop`.
//                               Constrained on is_consumer_handle_v;
//                               ill-formed otherwise.
//
// ── Detection strategy: structural duck-typing ──────────────────────
//
// Mirror of FOUND-D05's strategy — taking the address of the member
// function gives a member-function-pointer type that decomposes into
// (Class, Payload).  The canonical Permissioned* consumer endpoint
// declares ONE try_pop method returning `std::optional<P>`:
//
//   1. `&T::try_pop` is a well-formed expression (try_pop exists AND
//      is non-overloaded).
//
//   2. The member-function-pointer type, after stripping noexcept
//      qualifiers, is `std::optional<P>(T::*)()` for some payload
//      type P.
//
//   3. `&T::try_push` is NOT a well-formed expression (mutual
//      exclusion with producer handles).
//
// The handle's payload type is recovered from try_pop's return
// optional<P>, NOT from a typedef on the handle.
//
// ── Why std::optional<P> in the return signature ────────────────────
//
// Every Permissioned* consumer endpoint (PermissionedSpscChannel /
// PermissionedMpscChannel / PermissionedMpmcChannel / Calendar/Grid)
// returns std::optional<P> from try_pop — the SpscRing /
// MpscRing / MpmcRing's "ring is empty → nullopt" contract is the
// canonical signal.  A try_pop returning bool would be a different
// shape (out-parameter consumer); this would route differently
// through the dispatcher and is rejected here.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — three orthogonal `requires`-clauses (signature shape +
//              negative method) form the conjunction; no implicit
//              conversion path.
//   DetSafe — same T → same predicate value, deterministically.

#include <cstddef>
#include <optional>
#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: structural shape detectors ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// ── Decompose a member-function-pointer type into (Class, Payload).
//    Specialisations for the canonical try_pop shape:
//      std::optional<P>(C::*)()
//      std::optional<P>(C::*)() noexcept
template <typename M>
struct consumer_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct consumer_signature_decomp<std::optional<P> (C::*)()> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct consumer_signature_decomp<std::optional<P> (C::*)() noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

// ── try_pop presence + signature shape ───────────────────────────
template <typename T, typename = void>
struct try_pop_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct try_pop_shape<T, std::void_t<decltype(&T::try_pop)>> {
    using mptr_t = decltype(&T::try_pop);
    using decomp = consumer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

// ── try_push presence (negative requirement) ─────────────────────
template <typename T, typename = void>
struct has_try_push : std::false_type {};

template <typename T>
struct has_try_push<T, std::void_t<decltype(&T::try_push)>>
    : std::true_type
{};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_consumer_handle_v =
    detail::try_pop_shape<std::remove_cvref_t<T>>::matches
 && !detail::has_try_push<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsConsumerHandle = is_consumer_handle_v<T>;

template <typename T>
    requires is_consumer_handle_v<T>
using consumer_handle_value_t =
    typename detail::try_pop_shape<std::remove_cvref_t<T>>::payload;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::is_consumer_handle_self_test {

// Synthetic consumer-handle witness — exposes try_pop returning
// std::optional<int>, no try_push.
struct synthetic_consumer {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

// Synthetic producer-handle witness — exposes try_push, no try_pop.
struct synthetic_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// Synthetic hybrid — exposes BOTH; rejected as ambiguous.
struct synthetic_hybrid {
    [[nodiscard]] std::optional<int> try_pop()       noexcept { return {}; }
    [[nodiscard]] bool try_push(int const&)          noexcept { return true; }
};

// Synthetic shape with try_pop returning bool (out-parameter
// consumer) — rejected (must be optional<P>).
struct synthetic_bool_pop {
    [[nodiscard]] bool try_pop() noexcept { return false; }
};

// Synthetic shape with try_pop returning T (no nullable signal) —
// rejected.
struct synthetic_value_pop {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Synthetic shape with overloaded try_pop — &T::try_pop is ill-formed.
struct synthetic_overloaded_pop {
    [[nodiscard]] std::optional<int>   try_pop() noexcept       { return {}; }
    [[nodiscard]] std::optional<int>   try_pop(int) noexcept    { return {}; }
};

// Different payload type — payload extraction must propagate.
struct synthetic_double_consumer {
    [[nodiscard]] std::optional<double> try_pop() noexcept { return {}; }
};

// ── Positive cases ────────────────────────────────────────────────

static_assert(is_consumer_handle_v<synthetic_consumer>);
static_assert(IsConsumerHandle<synthetic_consumer>);
static_assert(is_consumer_handle_v<synthetic_double_consumer>);

// Cv-ref stripping.
static_assert(is_consumer_handle_v<synthetic_consumer&>);
static_assert(is_consumer_handle_v<synthetic_consumer&&>);
static_assert(is_consumer_handle_v<synthetic_consumer const&>);

// ── Negative cases ────────────────────────────────────────────────

static_assert(!is_consumer_handle_v<int>);
static_assert(!is_consumer_handle_v<int*>);
static_assert(!is_consumer_handle_v<void>);
static_assert(!is_consumer_handle_v<synthetic_producer>);
static_assert(!is_consumer_handle_v<synthetic_hybrid>);
static_assert(!is_consumer_handle_v<synthetic_bool_pop>);
static_assert(!is_consumer_handle_v<synthetic_value_pop>);
static_assert(!is_consumer_handle_v<synthetic_overloaded_pop>);

// Pointer-to-handle is NOT a handle.
static_assert(!is_consumer_handle_v<synthetic_consumer*>);

// ── Payload extraction ────────────────────────────────────────────

static_assert(std::is_same_v<
    consumer_handle_value_t<synthetic_consumer>, int>);
static_assert(std::is_same_v<
    consumer_handle_value_t<synthetic_double_consumer>, double>);

// Cv-ref stripping on the alias.
static_assert(std::is_same_v<
    consumer_handle_value_t<synthetic_consumer&>, int>);
static_assert(std::is_same_v<
    consumer_handle_value_t<synthetic_consumer const&>, int>);

}  // namespace detail::is_consumer_handle_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool is_consumer_handle_smoke_test() noexcept {
    using namespace detail::is_consumer_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_consumer_handle_v<synthetic_consumer>;
        ok = ok && IsConsumerHandle<synthetic_consumer&&>;
        ok = ok && !is_consumer_handle_v<int>;
        ok = ok && !is_consumer_handle_v<synthetic_producer>;
        ok = ok && !is_consumer_handle_v<synthetic_hybrid>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
