#pragma once

// ── crucible::safety::CyclicBuffer<T, N> ────────────────────────────
//
// Bounded ring buffer that keeps the last N elements, FIFO-evicting the
// oldest on overflow, MRU-accessible.  Composes three already-audited
// safety wrappers — it is NOT a fresh primitive but the canonical
// COMPOSITION of:
//
//   - FixedArray<T, N>              the N inline ring slots
//   - Cyclic<std::size_t, N>        the free-running write cursor
//   - BoundedMonotonic<size_t, N>   the saturating fill counter (0..N)
//
// Design intent: every fixed-capacity "remember the last N events" ring
// in Crucible repeats the same triple — storage array + masked write
// head + saturating fill count — plus the same two bugs when hand-coded
// (the "circular index after first wrap" off-by-one, and deriving fill
// from a wrapping cursor).  CyclicBuffer promotes that triple to one
// type so the composition is audited once.  It mirrors the proven
// TransactionLog<N> shape (entries_ + head_ + count_) exactly.
//
// ── Why a SEPARATE fill counter (not min(cursor.raw(), N)) ──────────
//
// The cursor is free-running and wraps at 2^bits.  After it laps,
// `raw < N` becomes true again — so deriving size() from the cursor
// would under-report a full ring once the counter exceeds its width.
// A saturating BoundedMonotonic count is the fix TransactionLog already
// uses; CyclicBuffer inherits that discipline rather than re-derive it.
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #1063 WRAP-Transaction-4: TransactionLog<N>::entries_ ring
//   #989  WRAP-RegionCache-4: RegionCache slot ring (SoA variant)
//
// TransactionLog is the textbook single-array consumer (claim a slot,
// mutate in place, advance).  The same MRU-first reverse scan
// (`entries_[(head_ - 1 - i) & MASK]`) appears in QuarantinePolicy and
// ConnectionPool event rings — the exact sites that carried the
// "event_at returns wrong event after first wrap" bug CyclicBuffer's
// recent() prevents by construction.
//
// ── Public API ──────────────────────────────────────────────────────
//
//   Construction:
//     CyclicBuffer()             — NSDMI: empty, all slots default-init
//
//   Queries:
//     size()                     — fill count, 0..N
//     empty() / full()           — fill == 0 / fill == N
//     capacity                   — N (compile-time constant)
//
//   Mutation:
//     claim()                    — [[nodiscard]] T& at the next-write
//                                  slot; advances cursor + bumps fill.
//                                  Mirrors TransactionLog::begin_tx —
//                                  caller mutates the slot in place.
//     push(const T&) / push(T&&) — claim() then assign; convenience for
//                                  the assign-into-slot pattern.
//
//   MRU access:
//     recent(i)                  — i-th most-recent element; recent(0)
//                                  is the last claimed.  Total over all
//                                  i (the mask keeps every index in
//                                  [0, N)); meaningful for i < size().
//
//   Escape:
//     cursor()                   — the underlying Cyclic cursor (const)
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — every member has an NSDMI (storage_{} zero-inits slots,
//              cursor_{} = 0, count_{0}).  CyclicBuffer's defaulted
//              default ctor relies on these — BoundedMonotonic has no
//              default ctor, but the count_{0} NSDMI supplies it.
//   TypeSafe — composes strong wrappers; size_type is std::size_t to
//              match FixedArray's index type.
//   NullSafe — no pointers; FixedArray storage is inline.
//   MemSafe  — no heap.  claim()/recent() return references INTO the
//              inline FixedArray — valid for the buffer's lifetime,
//              invalidated for a given slot only after N more claims
//              wrap onto it (documented ring semantics).
//   BorrowSafe — value type; single-thread owner (matches the
//              TransactionLog / RegionCache "not thread-safe"
//              foreground-only discipline).
//   ThreadSafe — value type; cross-thread rings use atomics, not this.
//   LeakSafe — no resource ownership.
//   DetSafe  — cursor wrap + masking are deterministic modular
//              arithmetic; same push sequence → same slot layout.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(CyclicBuffer<T, N>) == sizeof(FixedArray<T, N>)
//     + sizeof(Cyclic<size_t,N>) + sizeof(BoundedMonotonic<size_t,N>)
//     + alignment padding == N*sizeof(T) + 2*sizeof(size_t) (when T's
//   alignment ≤ size_t's and N*sizeof(T) is size_t-aligned).  claim()
//   is one mask + one increment + one guarded bump — the open-coded
//   ring idiom, with the masking + wrap invariant carried in the type.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// A ring buffer is a COMPOSITION of structural wrappers, not a graded
// modal value — there is no lattice over "ring states."  Joins Cyclic,
// FixedArray, Saturated as a deliberately-not-graded structural wrapper
// per CLAUDE.md §XVI.
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)
//   safety/Cyclic.h      — the write cursor it composes
//   safety/FixedArray.h  — the slot storage it composes
//   safety/Mutation.h    — BoundedMonotonic, the fill counter it composes

#include <crucible/Platform.h>
#include <crucible/safety/Cyclic.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/Mutation.h>

#include <cstddef>
#include <cstdlib>       // std::abort (runtime_smoke_test)
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── CyclicBuffer<T, N> ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T, std::size_t N>
    requires (N > 0 && (N & (N - 1)) == 0)
class [[nodiscard]] CyclicBuffer {
public:
    using value_type      = T;
    using size_type       = std::size_t;
    using reference       = T&;
    using const_reference = T const&;

    static constexpr size_type capacity = N;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::CyclicBuffer";
    }

private:
    // ── Composed members, each NSDMI-initialized ──
    FixedArray<T, N>                  storage_{};                  // ring slots
    Cyclic<std::size_t, N>            cursor_{};                   // write head
    BoundedMonotonic<std::size_t, N>  count_{std::size_t{0}};      // fill 0..N

public:
    // ── Construction ────────────────────────────────────────────────

    constexpr CyclicBuffer() = default;

    // Defaulted copy/move/dtor — trivially copyable when T is.
    constexpr CyclicBuffer(CyclicBuffer const&)            = default;
    constexpr CyclicBuffer(CyclicBuffer&&)                 = default;
    constexpr CyclicBuffer& operator=(CyclicBuffer const&) = default;
    constexpr CyclicBuffer& operator=(CyclicBuffer&&)      = default;
    ~CyclicBuffer()                                        = default;

    // ── Queries ─────────────────────────────────────────────────────

    [[nodiscard]] constexpr size_type size()  const noexcept { return count_.get(); }
    [[nodiscard]] constexpr bool      empty() const noexcept { return count_.get() == 0; }
    [[nodiscard]] constexpr bool      full()  const noexcept { return count_.get() == N; }

    // ── Mutation ────────────────────────────────────────────────────

    // Claim the next-write slot: bind the slot at the current cursor
    // index, advance the cursor, then bump the saturating fill count.
    // Returns a reference to the claimed slot for in-place mutation
    // (the TransactionLog::begin_tx pattern — the slot still holds its
    // prior/default value; the caller overwrites).  The reference is
    // valid until N more claims wrap back onto this slot.
    //
    // Order matches the proven idiom: bind-at-current, then advance.
    // advance() touches cursor_ only, so the bound reference into
    // storage_ stays valid across the advance.
    //
    // The `if (count_.get() < N)` guard is LOAD-BEARING, not defensive:
    // BoundedMonotonic::bump() does NOT saturate internally — it carries
    // `CRUCIBLE_PRE(get() < Max)`.  Calling bump() on a full ring would
    // trip that precondition: an abort under `enforce`, and UB under
    // `ignore` (NDEBUG hot path), where the PRE lowers to
    // `[[assume(get() < Max)]]` and a false assumption is undefined
    // behaviour.  Do not remove this guard believing the counter
    // self-saturates — it does not.
    [[nodiscard]] constexpr reference claim() noexcept {
        reference slot = storage_[cursor_.index()];
        cursor_.advance();
        if (count_.get() < N) count_.bump();
        return slot;
    }

    // Push by copy — claim a slot and copy-assign.  Gated on
    // copy-assignability so non-assignable T still gets claim().
    constexpr reference push(T const& value)
        noexcept(std::is_nothrow_copy_assignable_v<T>)
        requires std::is_copy_assignable_v<T>
    {
        reference slot = claim();
        slot = value;
        return slot;
    }

    // Push by move — claim a slot and move-assign.
    constexpr reference push(T&& value)
        noexcept(std::is_nothrow_move_assignable_v<T>)
        requires std::is_move_assignable_v<T>
    {
        reference slot = claim();
        slot = std::move(value);
        return slot;
    }

    // ── MRU access ──────────────────────────────────────────────────
    //
    // recent(0) is the slot the last claim()/push() wrote; larger i
    // walks backwards through history.  Total over all i — the cursor's
    // mask keeps every index in [0, N), so this is never out of bounds;
    // it is meaningful for i < size() (looking back further returns a
    // stale/default slot).  Callers bound i with size(), mirroring the
    // ring consumers' `i < count_` loop guard.  No precondition clause —
    // the mask makes every result a valid slot, so this is a logic
    // bound, not a safety bound (same discipline as Cyclic::index_back
    // and FixedArray::operator[]).
    [[nodiscard]] constexpr reference recent(size_type i) noexcept {
        return storage_[cursor_.index_back(i)];
    }
    [[nodiscard]] constexpr const_reference recent(size_type i) const noexcept {
        return storage_[cursor_.index_back(i)];
    }

    // ── Escape ──────────────────────────────────────────────────────

    // The underlying write cursor (const) — for callers that need the
    // raw modular index or the absolute push count.
    [[nodiscard]] constexpr Cyclic<std::size_t, N> const& cursor() const noexcept {
        return cursor_;
    }
};

// ═════════════════════════════════════════════════════════════════════
// ── Layout invariants ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// No overhead beyond the three composed members + alignment.  For T
// whose alignment ≤ size_t's and whose N*sizeof(T) is size_t-aligned,
// sizeof is exactly the sum (no interior padding before the cursor).
static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>)
    == sizeof(FixedArray<std::uint32_t, 8>)
     + sizeof(Cyclic<std::size_t, 8>)
     + sizeof(BoundedMonotonic<std::size_t, 8>));
static_assert(sizeof(CyclicBuffer<std::uint64_t, 16>)
    == sizeof(FixedArray<std::uint64_t, 16>)
     + sizeof(Cyclic<std::size_t, 16>)
     + sizeof(BoundedMonotonic<std::size_t, 16>));

// Concrete byte counts for the production-relevant shapes:
//   uint32_t × 8 = 32 storage + 8 cursor + 8 count = 48.
//   uint64_t × 16 = 128 storage + 8 cursor + 8 count = 144.
static_assert(sizeof(CyclicBuffer<std::uint32_t, 8>)  == 48);
static_assert(sizeof(CyclicBuffer<std::uint64_t, 16>) == 144);

// alignof matches the widest member (size_t cursor/count) when T's
// alignment is narrower.
static_assert(alignof(CyclicBuffer<std::uint32_t, 8>) == alignof(std::size_t));

// Trivially copyable / standard layout when T is — memcpy-safe ring.
static_assert(std::is_trivially_copyable_v<CyclicBuffer<std::uint32_t, 8>>);
static_assert(std::is_trivially_destructible_v<CyclicBuffer<std::uint32_t, 8>>);

// Distinct type identity vs the bare FixedArray it wraps.
static_assert(!std::is_same_v<CyclicBuffer<int, 8>, FixedArray<int, 8>>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::cyclic_buffer_self_test {

using CB8 = CyclicBuffer<int, 8>;

// Default ctor is empty.
[[nodiscard]] consteval bool default_is_empty() noexcept {
    CB8 b{};
    return b.empty() && !b.full() && b.size() == 0 && CB8::capacity == 8;
}
static_assert(default_is_empty());

// push grows the fill and recent(0) is the last pushed.
[[nodiscard]] consteval bool push_grows_and_recent_tracks() noexcept {
    CB8 b{};
    b.push(10);
    b.push(20);
    b.push(30);
    return b.size() == 3
        && b.recent(0) == 30     // last pushed
        && b.recent(1) == 20
        && b.recent(2) == 10;    // oldest of the three
}
static_assert(push_grows_and_recent_tracks());

// claim() returns a slot to fill in place; recent(0) reflects it.
[[nodiscard]] consteval bool claim_then_mutate() noexcept {
    CB8 b{};
    int& slot = b.claim();
    slot = 99;
    return b.size() == 1 && b.recent(0) == 99;
}
static_assert(claim_then_mutate());

// Fill saturates at N: push N+ items, size stays N, oldest evicted.
[[nodiscard]] consteval bool saturates_and_evicts() noexcept {
    CB8 b{};
    for (int v = 0; v < 12; ++v) b.push(v);   // push 12 into a ring of 8
    if (b.size() != 8 || !b.full()) return false;
    // Most recent is 11; the 8 retained are 11,10,...,4 (0..3 evicted).
    return b.recent(0) == 11 && b.recent(7) == 4;
}
static_assert(saturates_and_evicts());

// full() / empty() transitions.
[[nodiscard]] consteval bool full_empty_transitions() noexcept {
    CB8 b{};
    if (!b.empty()) return false;
    for (int v = 0; v < 8; ++v) b.push(v);
    return b.full() && b.size() == 8;
}
static_assert(full_empty_transitions());

// cursor() escape exposes the absolute push count.
[[nodiscard]] consteval bool cursor_tracks_absolute_count() noexcept {
    CB8 b{};
    for (int v = 0; v < 5; ++v) b.push(v);
    return b.cursor().raw() == 5 && b.cursor().index() == 5;
}
static_assert(cursor_tracks_absolute_count());

// Wrapper-kind diagnostic.
static_assert(CB8::wrapper_kind() == "structural::CyclicBuffer");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise every
// named op with NON-constant input so a consteval-vs-runtime divergence
// in the composed primitives' arithmetic would surface here.
inline void runtime_smoke_test() {
    CB8 b{};
    if (!b.empty() || b.size() != 0) std::abort();

    // Non-constant pushes.
    volatile int seed = 100;
    for (int k = 0; k < 3; ++k) b.push(static_cast<int>(seed) + k);
    if (b.size() != 3) std::abort();
    if (b.recent(0) != 102 || b.recent(2) != 100) std::abort();

    // claim + in-place mutate.
    int& slot = b.claim();
    slot = 555;
    if (b.size() != 4 || b.recent(0) != 555) std::abort();

    // Saturation + eviction past N with non-constant input.
    CB8 r{};
    for (int v = 0; v < 20; ++v) r.push(static_cast<int>(seed) + v);
    if (r.size() != 8 || !r.full()) std::abort();
    if (r.recent(0) != 119) std::abort();   // 100 + 19
    if (r.recent(7) != 112) std::abort();    // 100 + 12 (oldest retained)

    // cursor escape.
    if (r.cursor().raw() != 20) std::abort();
    if (r.cursor().index() != (20u & 7u)) std::abort();   // 20 & 7 = 4
}

}  // namespace detail::cyclic_buffer_self_test

}  // namespace crucible::safety
