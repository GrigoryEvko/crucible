#pragma once

// ── crucible::safety::Cyclic<T, N> ──────────────────────────────────
//
// Free-running modular-counter newtype for power-of-two ring buffers.
// Wraps an unsigned counter whose DEFINING property is "read me modulo
// N": the stored value is a free-running absolute count, and the
// observable state is `counter & (N-1)` — the ring slot.  Carries the
// "this counter is a cyclic ring cursor, not a plain integer" signal
// that a bare `uint32_t head_` discards.
//
// Design intent: every power-of-two ring in Crucible repeats the exact
// same three-line idiom — `slot = head_ & MASK; ...; head_++` for the
// write head, and `(head_ - 1 - i) & MASK` for MRU-first reverse scans.
// The raw counter is deliberately NOT pre-masked: keeping the absolute
// count is what lets a separate fill-counter (`count_`) coexist and
// what makes the `- 1 - i` reverse-offset arithmetic correct.  Cyclic
// promotes that idiom to a type so the masking and the wrap discipline
// live in one audited place instead of being open-coded at every ring.
//
//   - `index()`        the next-write slot,        counter & (N-1).
//   - `index_back(i)`  the i-th most-recent slot,  (counter-1-i) & (N-1).
//   - `advance()`      ++counter (the slot wraps deterministically).
//   - `raw()`          the absolute free-running counter (escape hatch).
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #989  WRAP-RegionCache-4: RegionCache::head_ (CAP=8 ring cursor)
//   #1063 WRAP-Transaction-4: TransactionLog<N>::head_ (entries_ ring)
//
// Both sites today write `head_ & MASK`, `(head_ - 1 - i) & MASK`, and
// `head_++` against a bare `uint32_t head_{0}`; Cyclic<uint32_t, N> is
// the drop-in replacement that carries the masking + wrap invariant in
// the type system.  CyclicBuffer<T, N> (#1063) composes Cyclic with
// FixedArray to fuse the cursor and the storage.
//
// ── Public API ──────────────────────────────────────────────────────
//
//   Construction:
//     Cyclic()                  — NSDMI: counter = 0 (slot 0, next write)
//     explicit Cyclic(T start)  — resume from a known absolute count
//
//   Index views (always in [0, N) by construction — the mask guarantees
//   it regardless of the counter value, so there is no out-of-range
//   case and no precondition, matching FixedArray's mask-bounded reads):
//     index()                   — counter & (N-1): the next-write slot
//     index_back(i)             — (counter-1-i) & (N-1): i-th most recent
//
//   Advancement (mutating):
//     advance()                 — ++counter; the index wraps mod N
//     advance_by(k)             — counter += k; the index wraps mod N
//
//   Raw escape:
//     raw()                     — the absolute free-running counter
//     explicit operator T()     — same, as a conversion (drops the
//                                 cyclic-cursor identity, hence explicit)
//
//   Equality:
//     operator==                — compares the absolute counter
//
// ── Why N must be a power of two ────────────────────────────────────
//
// `counter & (N-1)` equals `counter % N` ONLY when N is a power of two.
// The branchless mask is what makes the slot read a single AND on the
// hot path (vs an integer divide).  `static_assert((N & (N-1)) == 0)`
// rejects non-power-of-two N at instantiation.
//
// ── Why the wrap is DetSafe, not UB ─────────────────────────────────
//
// `advance()` is `++counter` on an UNSIGNED T — well-defined modular
// arithmetic (C++ unsigned overflow is NOT undefined behaviour, unlike
// signed).  When `counter` wraps past its own 2^bits ceiling, the slot
// stays correct because N divides 2^bits (N is a power of two), so the
// low log2(N) bits — the only bits `& (N-1)` reads — are unaffected by
// the high-bit wrap.  Same inputs → same slot sequence on any platform.
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — NSDMI: counter = 0.  No uninitialized read possible.
//   TypeSafe — distinct from bare T; explicit T conversion required to
//              escape.  All arithmetic done in the T domain with an
//              explicit static_cast<T> on the result so integer
//              promotion of narrow T (uint8_t/uint16_t) cannot trip
//              -Werror=conversion or silently change the value.
//   NullSafe — no pointers internally.
//   MemSafe  — no heap, no allocation.  Trivially copyable.
//   BorrowSafe — value type; per-instance, no aliasing surface.
//   ThreadSafe — value type; the foreground-only ring consumers own it
//              single-threaded (see RegionCache's documented discipline).
//              Cross-thread cursors use atomics, not this wrapper.
//   LeakSafe — no resource ownership.
//   DetSafe  — `& (N-1)` is bit-exact across platforms; the unsigned
//              wrap is deterministic modular arithmetic.
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(Cyclic<T, N>) == sizeof(T); alignof matches T.  The single
//   counter is the only member.  index()/advance() are one AND / one
//   increment — identical machine code to the open-coded idiom under
//   -O3.  Trivially_copyable + standard_layout: memcpy-safe.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// A cyclic cursor is a modular-arithmetic structural constraint, not a
// graded modal property.  Z/NZ is a ring, not a lattice with a
// meaningful join/meet for ring-slot semantics — there is no "combine
// two cursors" algebra the way Stale has watermark-join.  Joins
// Saturated, FixedArray, ConstantTime, Pinned, Machine, Bits,
// Borrowed/BorrowedRef as a deliberately-not-graded structural wrapper
// per CLAUDE.md §XVI.
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)
//   safety/Saturated.h   — sibling structural value-with-flag wrapper
//   safety/FixedArray.h  — sibling structural bounded-array wrapper

#include <crucible/Platform.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── Cyclic<T, N> ──────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <std::unsigned_integral T, T N>
    requires (N > T{0} && (N & (N - T{1})) == T{0})
class [[nodiscard]] Cyclic {
public:
    using value_type = T;

    // Ring capacity and the AND-mask that maps the absolute counter to
    // a slot.  capacity is a power of two (enforced by the requires
    // clause); mask == capacity - 1.
    static constexpr T capacity = N;
    static constexpr T mask     = N - T{1};

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::Cyclic";
    }

private:
    // ── NSDMI: free-running absolute counter, starts at slot 0 ──
    T counter_ = T{0};

public:
    // ── Construction ────────────────────────────────────────────────

    constexpr Cyclic() noexcept = default;

    // Resume from a known absolute count — used when a ring's cursor is
    // reconstructed (e.g. recovered from a checkpoint).  Explicit: a
    // bare integer is not silently a cyclic cursor.
    constexpr explicit Cyclic(T start) noexcept : counter_{start} {}

    // Defaulted copy/move/dtor — Cyclic is a trivially-copyable value.
    constexpr Cyclic(Cyclic const&)            = default;
    constexpr Cyclic(Cyclic&&)                 = default;
    constexpr Cyclic& operator=(Cyclic const&) = default;
    constexpr Cyclic& operator=(Cyclic&&)      = default;
    ~Cyclic()                                  = default;

    // ── Index views ─────────────────────────────────────────────────
    //
    // Both are total: `& mask` guarantees a result in [0, N) for ANY
    // counter value, so there is no out-of-range case to guard.  The
    // static_cast<T> defends against integer promotion of narrow T
    // (uint8_t/uint16_t promote to int under arithmetic); the low
    // log2(N) bits — the only bits the mask reads — are identical
    // whether computed in int or T domain, so the cast is value-exact.

    // The next-write slot: where the producer writes before advancing.
    [[nodiscard]] constexpr T index() const noexcept {
        return static_cast<T>(counter_ & mask);
    }

    // The i-th most-recent slot.  index_back(0) is the slot the producer
    // last advanced PAST (the most recent fully-written entry); larger i
    // walks backwards through history.  Meaningful for i < N (looking
    // back N or more aliases a still-live slot); callers bound i with
    // their own fill counter, mirroring the ring consumers' `i < count_`
    // loop guard.  No precondition clause — the mask makes every result
    // a valid slot, so this is a logic bound, not a safety bound (same
    // discipline as FixedArray::operator[]).
    [[nodiscard]] constexpr T index_back(T i) const noexcept {
        return static_cast<T>((counter_ - T{1} - i) & mask);
    }

    // ── Advancement ─────────────────────────────────────────────────

    // Advance one step.  ++ on unsigned T is well-defined modular
    // arithmetic; the observable index wraps mod N deterministically.
    constexpr void advance() noexcept { ++counter_; }

    // Advance by k steps.  Explicit static_cast<T> keeps narrow-T
    // compound arithmetic out of -Werror=conversion's way.
    constexpr void advance_by(T k) noexcept {
        counter_ = static_cast<T>(counter_ + k);
    }

    // ── Raw escape ──────────────────────────────────────────────────

    // The absolute free-running counter (total advances since
    // construction, modulo 2^bits).  The escape hatch for the rare
    // caller that needs the count itself, not the slot.
    [[nodiscard]] constexpr T raw() const noexcept { return counter_; }

    // Explicit T conversion — drops the cyclic-cursor identity.  Marked
    // explicit so a Cyclic cannot silently decay to a plain integer.
    [[nodiscard]] constexpr explicit operator T() const noexcept {
        return counter_;
    }

    // ── Equality (compares the absolute counter) ────────────────────
    [[nodiscard]] friend constexpr bool operator==(
        Cyclic const& a, Cyclic const& b) noexcept = default;
};

// ═════════════════════════════════════════════════════════════════════
// ── Layout invariants ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// sizeof preserved at exactly sizeof(T) — the single counter is the
// only member.  Production-relevant instantiations:
static_assert(sizeof(Cyclic<uint32_t, 8>)  == sizeof(uint32_t));   // RegionCache CAP=8
static_assert(sizeof(Cyclic<uint32_t, 16>) == sizeof(uint32_t));   // TransactionLog N=16
static_assert(sizeof(Cyclic<uint64_t, 64>) == sizeof(uint64_t));
static_assert(sizeof(Cyclic<uint8_t, 4>)   == sizeof(uint8_t));    // narrow-T promotion witness

// alignof matches T (the only member determines alignment).
static_assert(alignof(Cyclic<uint32_t, 8>)  == alignof(uint32_t));
static_assert(alignof(Cyclic<uint64_t, 64>) == alignof(uint64_t));

// Trivially copyable / standard layout / trivially destructible — no
// user-defined ctor body, only NSDMI + defaulted operations.  memcpy-
// and serialization-safe.
static_assert(std::is_trivially_copyable_v<Cyclic<uint32_t, 8>>);
static_assert(std::is_trivially_destructible_v<Cyclic<uint32_t, 8>>);
static_assert(std::is_standard_layout_v<Cyclic<uint32_t, 8>>);

// Distinct type identity vs bare T (load-bearing — prevents a plain
// integer from being mistaken for a ring cursor and vice versa).
static_assert(!std::is_same_v<Cyclic<uint32_t, 8>, uint32_t>);
// No implicit conversion in EITHER direction (ctor is explicit, the T
// conversion operator is explicit) — a ring cursor is never silently a
// plain integer.
static_assert(!std::is_convertible_v<uint32_t, Cyclic<uint32_t, 8>>);
static_assert(!std::is_convertible_v<Cyclic<uint32_t, 8>, uint32_t>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::cyclic_self_test {

using C8  = Cyclic<uint32_t, 8>;   // RegionCache shape
using C16 = Cyclic<uint32_t, 16>;  // TransactionLog shape

// Default ctor starts at counter 0 → slot 0 (NSDMI) — load-bearing.
[[nodiscard]] consteval bool default_at_slot_zero() noexcept {
    C8 c{};
    return c.raw() == 0 && c.index() == 0;
}
static_assert(default_at_slot_zero());

// advance() walks the slot 0,1,2,...,7,0 and wraps at N.
[[nodiscard]] consteval bool advance_walks_and_wraps() noexcept {
    C8 c{};
    for (uint32_t expected = 0; expected < 8; ++expected) {
        if (c.index() != expected) return false;
        c.advance();
    }
    // After 8 advances the slot wrapped back to 0; raw() kept counting.
    return c.index() == 0 && c.raw() == 8;
}
static_assert(advance_walks_and_wraps());

// index_back(i) reads MRU-first history.  After writing slots 0..3
// (4 advances), the most recent written slot is 3, then 2, 1, 0.
[[nodiscard]] consteval bool index_back_is_mru_first() noexcept {
    C8 c{};
    c.advance_by(4);                 // counter = 4, next-write slot = 4
    return c.index()        == 4     // next write goes to slot 4
        && c.index_back(0)  == 3     // most-recent written slot
        && c.index_back(1)  == 2
        && c.index_back(2)  == 1
        && c.index_back(3)  == 0;
}
static_assert(index_back_is_mru_first());

// index_back wraps correctly through 0: from counter=0, looking back
// reaches the high slots (the ring's tail) via unsigned wrap.
[[nodiscard]] consteval bool index_back_wraps_through_zero() noexcept {
    C8 c{};                          // counter = 0
    return c.index_back(0) == 7      // (0 - 1 - 0) & 7 = 7
        && c.index_back(1) == 6;     // (0 - 1 - 1) & 7 = 6
}
static_assert(index_back_wraps_through_zero());

// DetSafe: the counter wraps at 2^bits but the slot stays correct
// because N | 2^bits.  Witness with uint8_t (wraps at 256, N=4): set
// counter to 255, advance → 0; slots 3 → 0 across the wrap.
[[nodiscard]] consteval bool wrap_at_type_ceiling_is_slot_exact() noexcept {
    Cyclic<uint8_t, 4> c{uint8_t{255}};
    if (c.index() != 3) return false;   // 255 & 3 = 3
    c.advance();                         // 255 + 1 = 0 (unsigned wrap)
    return c.raw() == 0 && c.index() == 0;  // 0 & 3 = 0 — slot continued cleanly
}
static_assert(wrap_at_type_ceiling_is_slot_exact());

// Equality compares the absolute counter.
[[nodiscard]] consteval bool equality_compares_counter() noexcept {
    C8 a{3};
    C8 b{3};
    C8 c{4};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_counter());

// Explicit T conversion yields the absolute counter.
[[nodiscard]] consteval bool explicit_t_conversion_is_raw() noexcept {
    C16 c{5};
    c.advance();
    return static_cast<uint32_t>(c) == 6;
}
static_assert(explicit_t_conversion_is_raw());

// Compile-time constants are exposed for the consumer migration.
static_assert(C8::capacity == 8 && C8::mask == 7);
static_assert(C16::capacity == 16 && C16::mask == 15);

// Wrapper-kind diagnostic.
static_assert(C8::wrapper_kind() == "structural::Cyclic");

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline: exercise every
// named op with NON-constant input so consteval-vs-runtime divergence
// (e.g. a promotion bug that only the optimizer's runtime path hits)
// would surface here, not hide behind consteval folding.
inline void runtime_smoke_test() {
    // Non-constant start so the optimizer cannot fold the counter away.
    volatile uint32_t seed = 3;
    C8 c{static_cast<uint32_t>(seed)};

    if (c.raw() != 3) std::abort();
    if (c.index() != 3) std::abort();          // 3 & 7
    if (c.index_back(0) != 2) std::abort();    // (3 - 1) & 7
    if (c.index_back(3) != 7) std::abort();    // (3 - 1 - 3) & 7 = -1 & 7 = 7

    c.advance();                                // counter = 4
    if (c.index() != 4 || c.raw() != 4) std::abort();

    c.advance_by(5);                            // counter = 9
    if (c.raw() != 9 || c.index() != 1) std::abort();  // 9 & 7 = 1

    if (static_cast<uint32_t>(c) != 9) std::abort();

    // Wrap-at-ceiling on narrow T with non-constant input.
    volatile uint8_t hi = 255;
    Cyclic<uint8_t, 4> n{static_cast<uint8_t>(hi)};
    if (n.index() != 3) std::abort();
    n.advance();
    if (n.raw() != 0 || n.index() != 0) std::abort();

    // Equality + default.
    C8 d{};
    if (d.raw() != 0 || d.index() != 0) std::abort();
    C8 e{};
    if (!(d == e)) std::abort();
    e.advance();
    if (d == e) std::abort();
}

}  // namespace detail::cyclic_self_test

}  // namespace crucible::safety
