// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Serialize-5 #1014, mismatch class #1 of 2:
// CONSTRUCTING `Refined<is_zero, uint64_t>` WITH A NON-ZERO VALUE
// (BOUNDARY EDGE) MUST FIRE THE PREDICATE CONTRACT AT CONSTEVAL.
//
// `safety::is_zero` is the dual of `safety::non_zero` (Refined.h
// alongside `positive` / `non_negative` / `power_of_two`).  It exists
// to pin "must-be-zero" sentinel invariants at the type level — the
// load-bearing use case is Serialize.h's `write_meta()` where
// `zero_ptr` and `zero_grad_fn_hash` MUST emit zeroed bytes on disk
// (data_ptr is a runtime address; grad_fn_hash is a Family-B
// autograd identity — neither is meaningful after reload and
// neither may enter persistent bytes lest DetSafe bit-stable replay
// silently break).
//
// In constexpr context, a contract violation on the Refined ctor's
// `pre(is_zero(v))` clause makes the expression non-constant per
// P1494R5 — using it where a constant is required is ill-formed.
// The boundary edge fixture (v == 1) catches the most common
// regression class: any future refactor that swaps the `0` literal
// for a positive expression (e.g. `m.data_ptr` cast to uint64_t)
// would compile-fire at the construction site instead of silently
// writing a non-zero pointer to disk.
//
// Companion fixture: neg_serialize_is_zero_uint64_max.cpp
//   * This one is the boundary edge (= 1).  Catches drift where the
//     predicate is accidentally relaxed to `>= 0` or `<= 0` (which
//     for uint64_t both still admit zero AND every other value).
//   * That one is the upper-bound wide miss (= UINT64_MAX).  Catches
//     "drop the predicate entirely" regression where Refined becomes
//     a no-op wrapper.
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.

#include <crucible/safety/Refined.h>

#include <cstdint>

int main() {
    // Constant evaluation forces the Refined ctor's pre clause
    // (`is_zero(v)`) to be exercised at compile time.  v == 1 →
    // predicate(v) == false → contract violation → not a constant
    // expression → ill-formed.
    constexpr crucible::safety::Refined<crucible::safety::is_zero,
                                        std::uint64_t> bad{
        std::uint64_t{1}};
    (void)bad;
    return 0;
}
