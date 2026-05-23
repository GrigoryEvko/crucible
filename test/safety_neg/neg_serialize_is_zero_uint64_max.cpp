// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Serialize-5 #1014, mismatch class #2 of 2:
// CONSTRUCTING `Refined<is_zero, uint64_t>` WITH `UINT64_MAX`
// (UPPER-BOUND WIDE MISS) MUST FIRE THE PREDICATE CONTRACT AT CONSTEVAL.
//
// Companion to neg_serialize_is_zero_nonzero_construct.cpp.  The
// boundary edge there (v == 1) catches "predicate accidentally
// relaxed to admit any non-negative value" regressions.  THIS
// fixture catches the more disastrous "drop the predicate entirely"
// regression — if Refined<is_zero, ...> were ever silently
// downgraded to a transparent wrapper (e.g. by a future refactor
// that aliases `Refined<...>` to `T` for "compatibility"), the
// UINT64_MAX construction would compile cleanly and a uint64_t
// representation of a uint64_t-typed pointer would silently enter
// the on-disk bytes.  DetSafe bit-stable replay would break,
// process-local addresses would leak through a cipher roundtrip,
// and there'd be no compile-time evidence of the wire-format drift.
//
// Pinning the wide-miss fixture forces every future refactor to
// preserve the strict equality semantics of `is_zero` rather than
// a "close enough" relaxation.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/safety/Refined.h>

#include <climits>
#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`is_zero(v)`) to be exercised at compile time.  v == UINT64_MAX
    // → predicate(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::safety::Refined<crucible::safety::is_zero,
                                        std::uint64_t> bad{
        UINT64_MAX};
    (void)bad;
    return 0;
}
