// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// factorization_eq, mismatch class #1: WRONG PRODUCT (off-by-factor).
//
// Pins that the predicate correctly rejects a factor list whose
// running product does NOT match the expected total.  The witness
// `{8, 2, 4, 1, 2}` multiplies to 128 — but the production-shape
// total is 64.  An off-by-factor-of-2 bug: the engineer wrote
// "TP=8" instead of "TP=4", and the partition over-divides the
// world size.
//
// In production this manifests as: 5D parallelism config validation
// where TP × DP × PP × EP × CP must equal world_size.  When the
// product exceeds world_size, half the ranks are unaddressable;
// when it falls short, ranks remain unassigned.  Either way the
// collective topology breaks at allocation time.  Without this
// predicate cited at the boundary, the bug surfaces at startup
// in cryptic NCCL/CNTP errors hundreds of stack frames deep.
//
// Anti-pattern targeted: hand-rolled `pre(TP * DP * PP * EP * CP
// == world_size)` written at every site with chained `*` operators.
// This not only drifts across sites but also fails to detect
// overflow during the multiplication — a separate failure mode
// pinned by the companion fixture.
//
// Distinct from the companion fixture (overflow):
//   * wrong_product (this fixture) — fires on `{8, 2, 4, 1, 2}`
//     vs total 64.  Mathematical product (128) != total (64).
//     Catches the "wrote the wrong factors" bug.
//   * overflow                    — fires on `{65536, 65536, 2}`
//     vs total 0.  Running product overflows uint32_t; even though
//     the wrapped value (also 0) coincides with total, predicate
//     REJECTS.  Catches the "didn't validate inputs" bug.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr uint32_t
gate(std::span<const uint32_t> dims, uint32_t world) noexcept {
    CRUCIBLE_PRE(crucible::decide::factorization_eq(dims, world));
    return world;
}

// 5D partition: 8 * 2 * 4 * 1 * 2 = 128.  But world_size is 64.
// The factor 8 should have been 4 — a transcription typo from
// the human-curated config.  factorization_eq's running product
// reaches 128 at the final factor and the equality check rejects.
// CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr uint32_t off_by_factor[] = {8u, 2u, 4u, 1u, 2u};
constexpr auto witness = gate(off_by_factor, 64u);

}  // namespace

int main() { return 0; }
