// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing TransactionLog<16>::CountCounter with the
// value 17 in a constexpr context — fires
// BoundedMonotonic<uint32_t, 16>'s ctor pre-clause at the boundary
// edge (= N + 1).
//
// Per WRAP-Transaction-5 (#1064), TransactionLog<N>::CountCounter is
// safety::BoundedMonotonic<uint32_t, N>.  The ctor's
// `pre(!(T(Max) < initial))` (≡ initial <= Max) admits values in
// [0, N] and rejects N + 1 and above.  Valid runtime counts occupy
// [0, N] (inclusive — count_ caps at N once the ring is full and
// every subsequent begin_tx wraps in place).  N + 1 is never
// legitimate.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_transaction_log_count_uint32_max.cpp
//   * This one is the boundary edge (= N + 1 = 17, off-by-two).
//     Catches drift where the bound widens from
//     `BoundedMonotonic<..., N>` to `BoundedMonotonic<..., N + K>`
//     for some K >= 1, silently admitting count values past the
//     ring's capacity and breaking the previous() backward-walk
//     invariant (`(head_ - 1 - i) & MASK` only addresses the
//     ring-resident entries when count_ <= N).
//   * That one is the wide miss (= UINT32_MAX).  Catches
//     "BoundedMonotonic regressed to plain Monotonic" or to raw
//     uint32_t — the upper bound disappears entirely and any
//     uint32_t is admitted at construction.  Under that drift the
//     existing `if (count_.get() < N) count_.bump();` saturating
//     gate still works at runtime, but the type-system gate that
//     fires before any begin_tx() call ever lands disappears.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/ir001/Transaction.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the BoundedMonotonic ctor's pre
    // clause `!(T(N) < initial)` to be exercised at compile time.
    // initial == N + 1 (= 17 for default N=16) → N < N + 1 →
    // predicate(initial) false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::TransactionLog<16>::CountCounter bad{
        uint32_t{17}};
    (void)bad;
    return 0;
}
