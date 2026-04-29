#pragma once

// ── crucible::safety::extract::is_producer_handle_v ─────────────────
//
// FOUND-D05 of 27_04_2026.md §5.5 + 28_04_2026_effects.md §6.1.  The
// dispatcher-side reading-surface predicate that recognizes the
// producer half of a Permissioned* channel — the handle types nested
// inside `concurrent::PermissionedSpscChannel`, `PermissionedMpscChannel`,
// `PermissionedMpmcChannel`, `PermissionedShardedGrid`,
// `PermissionedShardedCalendarGrid`, `PermissionedCalendarGrid`, and
// any future channel that exposes a typed producer endpoint.
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_producer_handle_v<T>     Variable template: true iff T (after
//                               cv-ref stripping) exposes the producer
//                               endpoint shape (a non-overloaded
//                               `try_push(payload const&)` method
//                               returning `bool`, AND no `try_pop`).
//
//   IsProducerHandle<T>         Concept form for `requires`-clauses.
//
//   producer_handle_value_t<T>  Alias to the payload-type the handle
//                               accepts on `try_push`, recovered by
//                               decomposing the type of `&T::try_push`.
//                               Constrained on is_producer_handle_v;
//                               ill-formed otherwise.
//
// ── Detection strategy: structural duck-typing ──────────────────────
//
// The producer handles in `concurrent/Permissioned*.h` are NESTED
// classes inside their channel templates (PermissionedSpscChannel<
// T, Cap, Tag>::ProducerHandle, etc.).  Naming-based detection
// (partial specialisation on `Channel::ProducerHandle`) would force
// the dispatcher to enumerate every channel template — fragile and
// not extension-safe.  Instead we detect the *shape*:
//
//   1. `&T::try_push` is a well-formed expression (try_push exists
//      AND is non-overloaded — overload sets cannot have their
//      address taken).  This is intentional; the canonical
//      Permissioned* producer endpoint exposes ONE try_push method.
//
//   2. The member-function-pointer type, after stripping cv/ref/
//      noexcept qualifiers, is `bool(T::*)(Payload const&)` for some
//      Payload type.
//
//   3. `&T::try_pop` is NOT a well-formed expression (mutual
//      exclusion — a hybrid handle that exposes both is structurally
//      ambiguous and rejected to keep the dispatcher's §3.4
//      recogniser unambiguous).
//
// The handle's payload type is recovered from try_push's parameter
// type, NOT from a typedef on the handle (the production handles do
// not always re-export their channel's `value_type`).
//
// ── Why &T::try_push (member-function-pointer) ──────────────────────
//
// Naive `requires { t.try_push(some_value); }` SFINAE forces us to
// guess the payload type — there is no way to "ask" SFINAE for the
// declared type without already knowing it.  Taking the address of
// the member function gives us a member-function-pointer type that
// can be decomposed via partial specialisation to recover the
// signature.  The trade-off: T::try_push must NOT be overloaded —
// overload-set address-of is ill-formed, which we want, since the
// canonical Permissioned* producer endpoint declares ONE try_push.
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
#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: structural shape detectors ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// ── Decompose a member-function-pointer type into (Class, Payload).
//    Specialisations for the canonical try_push shape:
//      bool(C::*)(P const&)
//      bool(C::*)(P const&) noexcept
//    Both forms are matched.  cv-ref-qualified member functions are
//    NOT matched — they are not part of the canonical hot-path
//    Permissioned* shape (try_push is always callable on a non-const
//    handle).
template <typename M>
struct producer_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct producer_signature_decomp<bool (C::*)(P const&)> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct producer_signature_decomp<bool (C::*)(P const&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

// ── try_push presence + signature shape ───────────────────────────
template <typename T, typename = void>
struct try_push_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct try_push_shape<T, std::void_t<decltype(&T::try_push)>> {
    using mptr_t = decltype(&T::try_push);
    using decomp = producer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

// ── try_pop presence (negative requirement) ───────────────────────
template <typename T, typename = void>
struct has_try_pop : std::false_type {};

template <typename T>
struct has_try_pop<T, std::void_t<decltype(&T::try_pop)>>
    : std::true_type
{};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_producer_handle_v =
    detail::try_push_shape<std::remove_cvref_t<T>>::matches
 && !detail::has_try_pop<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsProducerHandle = is_producer_handle_v<T>;

template <typename T>
    requires is_producer_handle_v<T>
using producer_handle_value_t =
    typename detail::try_push_shape<std::remove_cvref_t<T>>::payload;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Synthetic producer / consumer / hybrid handles let the trait be
// validated without instantiating a Permissioned* channel (which
// would require pulling in concurrent/ headers).  The sentinel TU
// (test/test_is_producer_handle.cpp) cross-checks the trait against
// REAL PermissionedSpscChannel::ProducerHandle / ConsumerHandle.

namespace detail::is_producer_handle_self_test {

// Synthetic producer-handle witness — exposes try_push, no try_pop.
// No value_type typedef (mirroring the production handles, which
// don't always re-export their channel's value_type).
struct synthetic_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// Synthetic consumer-handle witness — exposes try_pop, no try_push.
struct synthetic_consumer {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Synthetic hybrid — exposes BOTH; rejected as ambiguous.
struct synthetic_hybrid {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Synthetic shape with try_push returning void — rejected (must be
// bool, signalling "can the channel accept this push right now").
struct synthetic_void_push {
    void try_push(int const&) noexcept {}
};

// Synthetic shape with overloaded try_push — &T::try_push is
// ill-formed (cannot take address of overload set), so the trait
// rejects.  The canonical Permissioned* producer endpoint declares
// ONE try_push method; overload sets are not part of the shape.
struct synthetic_overloaded_push {
    [[nodiscard]] bool try_push(int const&)   noexcept { return true; }
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};

// Synthetic shape with try_push taking a non-const reference —
// rejected because the canonical shape consumes by const&.
struct synthetic_non_const_ref_push {
    [[nodiscard]] bool try_push(int&) noexcept { return true; }
};

// Synthetic shape with try_push taking by value — rejected.
struct synthetic_by_value_push {
    [[nodiscard]] bool try_push(int) noexcept { return true; }
};

// Different payload type — payload extraction must propagate.
struct synthetic_double_producer {
    [[nodiscard]] bool try_push(double const&) noexcept { return true; }
};

// ── Positive cases ────────────────────────────────────────────────

static_assert(is_producer_handle_v<synthetic_producer>);
static_assert(IsProducerHandle<synthetic_producer>);
static_assert(is_producer_handle_v<synthetic_double_producer>);

// Cv-ref stripping — every reference category resolves identically.
static_assert(is_producer_handle_v<synthetic_producer&>);
static_assert(is_producer_handle_v<synthetic_producer&&>);
static_assert(is_producer_handle_v<synthetic_producer const&>);

// ── Negative cases ────────────────────────────────────────────────

static_assert(!is_producer_handle_v<int>);
static_assert(!is_producer_handle_v<int*>);
static_assert(!is_producer_handle_v<void>);
static_assert(!is_producer_handle_v<synthetic_consumer>);
static_assert(!is_producer_handle_v<synthetic_hybrid>);
static_assert(!is_producer_handle_v<synthetic_void_push>);
static_assert(!is_producer_handle_v<synthetic_overloaded_push>);
static_assert(!is_producer_handle_v<synthetic_non_const_ref_push>);
static_assert(!is_producer_handle_v<synthetic_by_value_push>);

// Pointer-to-handle is NOT a handle.
static_assert(!is_producer_handle_v<synthetic_producer*>);

// ── Payload extraction ────────────────────────────────────────────

static_assert(std::is_same_v<
    producer_handle_value_t<synthetic_producer>, int>);
static_assert(std::is_same_v<
    producer_handle_value_t<synthetic_double_producer>, double>);

// Cv-ref stripping on the alias.
static_assert(std::is_same_v<
    producer_handle_value_t<synthetic_producer&>, int>);
static_assert(std::is_same_v<
    producer_handle_value_t<synthetic_producer const&>, int>);

}  // namespace detail::is_producer_handle_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool is_producer_handle_smoke_test() noexcept {
    using namespace detail::is_producer_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_producer_handle_v<synthetic_producer>;
        ok = ok && IsProducerHandle<synthetic_producer&&>;
        ok = ok && !is_producer_handle_v<int>;
        ok = ok && !is_producer_handle_v<synthetic_consumer>;
        ok = ok && !is_producer_handle_v<synthetic_hybrid>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
