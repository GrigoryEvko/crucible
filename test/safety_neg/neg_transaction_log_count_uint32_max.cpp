// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing TransactionLog<16>::CountCounter with the
// value UINT32_MAX in a constexpr context — the wide-miss fixture
// for the N=16 bound.
//
// Per WRAP-Transaction-5 (#1064), TransactionLog<N>::CountCounter is
// safety::BoundedMonotonic<uint32_t, N=16>.  0xFFFFFFFF is past
// every plausible ring-fill counter value and would aliase to a
// "count" that overshoots the entries[N] array by ~4 billion; the
// gate must reject.
//
// In constexpr context (constant evaluation), a contract violation
// makes the expression non-constant per P1494R5 — using it where a
// constant is required is ill-formed.
//
// Companion fixture: neg_transaction_log_count_above_cap.cpp
//   * That one is the boundary edge (= N + 1, off-by-two).
//   * This one is the wide miss (= UINT32_MAX).  Catches "drop the
//     bound entirely" regression where CountCounter degenerates
//     from `BoundedMonotonic<uint32_t, N>` into a plain
//     `Monotonic<uint32_t>` (or an unwrapped uint32_t typedef);
//     under that drift any count value is silently accepted at
//     construction and the only remaining defense is the existing
//     `if (count_.get() < N) count_.bump();` saturating gate inside
//     begin_tx (works at runtime but loses the type-system gate
//     that fires BEFORE any begin_tx() call ever lands — and
//     crucially, fires on aggregate-init/memcpy/aliasing paths
//     that bypass begin_tx entirely).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.

#include <crucible/Transaction.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the BoundedMonotonic ctor's pre
    // clause `!(T(N) < initial)` to be exercised at compile time.
    // initial == UINT32_MAX → N < UINT32_MAX → predicate(initial)
    // false → contract violation → not a constant expression →
    // ill-formed.
    constexpr crucible::TransactionLog<16>::CountCounter bad{
        uint32_t{UINT32_MAX}};
    (void)bad;
    return 0;
}
