#pragma once

// ── crucible::safety::extract::is_swmr_writer_v / is_swmr_reader_v ──
//
// FOUND-D07/D08 of 27_04_2026.md §5.5 + 28_04_2026_effects.md §6.1.
// The dispatcher-side reading-surface predicates that recognize the
// SWMR (single-writer-multiple-reader) endpoints of a Permissioned*
// snapshot — the WriterHandle / ReaderHandle nested classes inside
// `concurrent::PermissionedSnapshot<T, UserTag>` and any future
// snapshot-style channel.
//
// Spec form (27_04 §3.6):
//
//     void publisher(SwmrWriterHandle<T, UserTag>&&, T value);
//     T    reader   (SwmrReaderHandle<T, UserTag>&&);
//
// Lowering: writer → AtomicSnapshot::publish; reader →
// AtomicSnapshot::load.  Permission tree: one Writer<UserTag>
// (linear), N Reader<UserTag> (fractional via SharedPermissionPool).
//
// ── What this header ships ──────────────────────────────────────────
//
//   is_swmr_writer_v<T>      Variable template: true iff T (after
//                            cv-ref stripping) exposes the SWMR
//                            writer endpoint shape — a non-overloaded
//                            `publish(payload const&)` method
//                            returning `void`, AND no `load`.
//
//   IsSwmrWriter<T>          Concept form for `requires`-clauses.
//
//   swmr_writer_value_t<T>   Alias to the payload-type the handle
//                            accepts on `publish`, recovered from
//                            the type of `&T::publish`.  Constrained
//                            on is_swmr_writer_v; ill-formed otherwise.
//
//   is_swmr_reader_v<T>      Variable template: true iff T (after
//                            cv-ref stripping) exposes the SWMR
//                            reader endpoint shape — a non-overloaded
//                            const `load() const` method returning the
//                            payload type by value, AND no `publish`.
//
//   IsSwmrReader<T>          Concept form for `requires`-clauses.
//
//   swmr_reader_value_t<T>   Alias to the payload-type returned by
//                            `load()`, recovered from the type of
//                            `&T::load`.  Constrained on
//                            is_swmr_reader_v; ill-formed otherwise.
//
// ── Detection strategy: structural duck-typing ──────────────────────
//
// Same approach as FOUND-D05/D06: take the address of the member
// function and decompose the member-function-pointer type via
// partial specialisation to recover (Class, Payload).  Mutual
// exclusion is enforced symmetrically — a writer cannot have load,
// a reader cannot have publish.
//
// PermissionedSnapshot's WriterHandle exposes:
//
//     void publish(T const& value) noexcept;
//     std::uint64_t version() const noexcept;
//
// And ReaderHandle exposes:
//
//     T load() const noexcept;
//     std::optional<T> try_load() const noexcept;
//     std::uint64_t version() const noexcept;
//
// Detection ignores `version()` (telemetry-only; not load-bearing for
// shape recognition) and the optional `try_load` (refinement, not the
// canonical SWMR shape — the canonical reader shape is the blocking
// `load()` per AtomicSnapshot's seqlock retry contract).
//
// ── Why publish returns void, load returns by value ─────────────────
//
// AtomicSnapshot's API contract: publish is wait-free and always
// succeeds (no failure mode at the AtomicSnapshot layer); load is
// blocking-with-retry and always returns the latest committed value.
// A publish returning bool would signal a different shape (capacity-
// limited, like a ring's try_push), and would route differently
// through the dispatcher.  A load returning optional<T> would signal
// a non-blocking try-load (which exists on ReaderHandle as
// `try_load`), again a different shape.
//
// ── Axiom coverage ──────────────────────────────────────────────────
//
//   InitSafe / NullSafe / MemSafe / BorrowSafe / ThreadSafe / LeakSafe
//     — N/A; pure consteval predicate.
//   TypeSafe — mutually-exclusive predicates; no implicit conversion.
//   DetSafe — same T → same predicate value.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::safety::extract {

// ═════════════════════════════════════════════════════════════════════
// ── detail: structural shape detectors ─────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail {

// ── publish signature decomp ──────────────────────────────────────
//
// Canonical:
//   void(C::*)(P const&)
//   void(C::*)(P const&) noexcept
template <typename M>
struct publish_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct publish_signature_decomp<void (C::*)(P const&)> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct publish_signature_decomp<void (C::*)(P const&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct publish_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct publish_shape<T, std::void_t<decltype(&T::publish)>> {
    using mptr_t = decltype(&T::publish);
    using decomp = publish_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

// ── load signature decomp (const member function returning P) ─────
//
// Canonical:
//   P(C::*)() const
//   P(C::*)() const noexcept
template <typename M>
struct load_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct load_signature_decomp<P (C::*)() const> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct load_signature_decomp<P (C::*)() const noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

// Reject load signatures that return optional<T> or void — those
// are different shapes (try_load / void-returning-getter) and not
// the canonical SWMR-reader load.

template <typename T, typename = void>
struct load_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct load_shape<T, std::void_t<decltype(&T::load)>> {
    using mptr_t = decltype(&T::load);
    using decomp = load_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches
        && !std::is_void_v<typename decomp::payload>;
    using payload = typename decomp::payload;
};

// ── Negative-presence helpers ────────────────────────────────────
template <typename T, typename = void>
struct has_publish : std::false_type {};

template <typename T>
struct has_publish<T, std::void_t<decltype(&T::publish)>>
    : std::true_type
{};

template <typename T, typename = void>
struct has_load : std::false_type {};

template <typename T>
struct has_load<T, std::void_t<decltype(&T::load)>>
    : std::true_type
{};

}  // namespace detail

// ═════════════════════════════════════════════════════════════════════
// ── Public surface ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
inline constexpr bool is_swmr_writer_v =
    detail::publish_shape<std::remove_cvref_t<T>>::matches
 && !detail::has_load<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSwmrWriter = is_swmr_writer_v<T>;

template <typename T>
    requires is_swmr_writer_v<T>
using swmr_writer_value_t =
    typename detail::publish_shape<std::remove_cvref_t<T>>::payload;

template <typename T>
inline constexpr bool is_swmr_reader_v =
    detail::load_shape<std::remove_cvref_t<T>>::matches
 && !detail::has_publish<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSwmrReader = is_swmr_reader_v<T>;

template <typename T>
    requires is_swmr_reader_v<T>
using swmr_reader_value_t =
    typename detail::load_shape<std::remove_cvref_t<T>>::payload;

// ═════════════════════════════════════════════════════════════════════
// ── Self-test block ────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::is_swmr_handle_self_test {

// Synthetic SWMR writer — exposes publish, no load.
struct synthetic_writer {
    void publish(int const&) noexcept {}
};

// Synthetic SWMR reader — exposes load, no publish.
struct synthetic_reader {
    [[nodiscard]] int load() const noexcept { return 0; }
};

// Hybrid — has both; rejected on both predicates.
struct synthetic_swmr_hybrid {
    void publish(int const&) noexcept {}
    [[nodiscard]] int load() const noexcept { return 0; }
};

// publish returning bool — rejected (must be void).
struct synthetic_bool_publish {
    [[nodiscard]] bool publish(int const&) noexcept { return true; }
};

// publish by-value — rejected (must be const&).
struct synthetic_by_value_publish {
    void publish(int) noexcept {}
};

// load not const-qualified — rejected.
struct synthetic_non_const_load {
    [[nodiscard]] int load() noexcept { return 0; }
};

// load returning void — rejected (must return payload).
struct synthetic_void_load {
    void load() const noexcept {}
};

// Producer/consumer handles must NOT match either SWMR predicate.
struct synthetic_producer_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_consumer_handle {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

// Different payload type — payload extraction must propagate.
struct synthetic_double_writer {
    void publish(double const&) noexcept {}
};

struct synthetic_double_reader {
    [[nodiscard]] double load() const noexcept { return 0.0; }
};

// ── is_swmr_writer_v positives / negatives ────────────────────────

static_assert(is_swmr_writer_v<synthetic_writer>);
static_assert(IsSwmrWriter<synthetic_writer>);
static_assert(is_swmr_writer_v<synthetic_double_writer>);
static_assert(is_swmr_writer_v<synthetic_writer&>);
static_assert(is_swmr_writer_v<synthetic_writer&&>);
static_assert(is_swmr_writer_v<synthetic_writer const&>);

static_assert(!is_swmr_writer_v<int>);
static_assert(!is_swmr_writer_v<void>);
static_assert(!is_swmr_writer_v<synthetic_reader>);
static_assert(!is_swmr_writer_v<synthetic_swmr_hybrid>);
static_assert(!is_swmr_writer_v<synthetic_bool_publish>);
static_assert(!is_swmr_writer_v<synthetic_by_value_publish>);
static_assert(!is_swmr_writer_v<synthetic_producer_handle>);
static_assert(!is_swmr_writer_v<synthetic_consumer_handle>);
static_assert(!is_swmr_writer_v<synthetic_writer*>);

// ── is_swmr_reader_v positives / negatives ────────────────────────

static_assert(is_swmr_reader_v<synthetic_reader>);
static_assert(IsSwmrReader<synthetic_reader>);
static_assert(is_swmr_reader_v<synthetic_double_reader>);
static_assert(is_swmr_reader_v<synthetic_reader&>);
static_assert(is_swmr_reader_v<synthetic_reader&&>);
static_assert(is_swmr_reader_v<synthetic_reader const&>);

static_assert(!is_swmr_reader_v<int>);
static_assert(!is_swmr_reader_v<void>);
static_assert(!is_swmr_reader_v<synthetic_writer>);
static_assert(!is_swmr_reader_v<synthetic_swmr_hybrid>);
static_assert(!is_swmr_reader_v<synthetic_non_const_load>);
static_assert(!is_swmr_reader_v<synthetic_void_load>);
static_assert(!is_swmr_reader_v<synthetic_producer_handle>);
static_assert(!is_swmr_reader_v<synthetic_consumer_handle>);
static_assert(!is_swmr_reader_v<synthetic_reader*>);

// ── Payload extraction ────────────────────────────────────────────

static_assert(std::is_same_v<
    swmr_writer_value_t<synthetic_writer>, int>);
static_assert(std::is_same_v<
    swmr_writer_value_t<synthetic_double_writer>, double>);
static_assert(std::is_same_v<
    swmr_writer_value_t<synthetic_writer const&>, int>);

static_assert(std::is_same_v<
    swmr_reader_value_t<synthetic_reader>, int>);
static_assert(std::is_same_v<
    swmr_reader_value_t<synthetic_double_reader>, double>);
static_assert(std::is_same_v<
    swmr_reader_value_t<synthetic_reader const&>, int>);

}  // namespace detail::is_swmr_handle_self_test

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

inline bool is_swmr_handle_smoke_test() noexcept {
    using namespace detail::is_swmr_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_swmr_writer_v<synthetic_writer>;
        ok = ok && IsSwmrWriter<synthetic_writer&&>;
        ok = ok && is_swmr_reader_v<synthetic_reader>;
        ok = ok && IsSwmrReader<synthetic_reader&&>;
        ok = ok && !is_swmr_writer_v<synthetic_reader>;
        ok = ok && !is_swmr_reader_v<synthetic_writer>;
        ok = ok && !is_swmr_writer_v<synthetic_swmr_hybrid>;
        ok = ok && !is_swmr_reader_v<synthetic_swmr_hybrid>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
