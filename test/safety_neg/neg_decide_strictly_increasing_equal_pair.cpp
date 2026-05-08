// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::decide::
// strictly_increasing, mismatch class #1: EQUAL CONSECUTIVE PAIR
// (the strict-vs-weak distinguishing class).
//
// Pins that the predicate correctly rejects a weakly-increasing
// sequence with a duplicate consecutive pair.  This is the bug
// class where a counter "stalls" on the same value across two
// reads — a known failure mode in event-sourced systems where a
// duplicate record can be persisted without advancing the sequence
// number.  std::is_sorted (uses `<=`) silently passes; this
// procedure catches it.
//
// Distinct from the companion fixture (middle-regression):
//   * equal_pair    — fires on the `!(x[i-1] < x[i])` branch when
//     x[i-1] == x[i].  Catches the "stalled counter" bug class
//     that std::is_sorted / pre(begin < end) endpoint shortcuts
//     allow through.  This is THE class that distinguishes
//     strictly_increasing from weakly_increasing.
//   * middle_regress — fires on the same branch but for x[i-1] > x[i]
//     ("monotonically going backward") — a different bug class
//     (memory corruption, race-condition reordering, or a
//     deserializer mis-ordering events).
//
// Equal-pair is placed at index 1-2 (NOT the endpoints) to pin the
// "endpoint-only manual check" anti-pattern: pre(front() < back())
// would silently pass {1, 5, 5, 9} because 1 < 9.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "__builtin_trap" / "call to non-constexpr function".

#include <crucible/safety/Decide.h>
#include <crucible/safety/Pre.h>

#include <cstdint>
#include <span>

namespace {

[[nodiscard]] constexpr bool gate(std::span<const uint64_t> ids) noexcept {
    CRUCIBLE_PRE(crucible::decide::strictly_increasing(ids));
    return true;
}

// Stalled-counter sequence: index 1->2 violates strict less-than
// because step_id[1] == step_id[2].  Endpoints (1, 9) satisfy
// `front < back`; std::is_sorted accepts it; only strictly_increasing
// rejects.  CRUCIBLE_PRE's __builtin_trap fires at consteval.
constexpr uint64_t stalled_step_ids[] = {1ull, 5ull, 5ull, 9ull};
constexpr auto witness = gate(stalled_step_ids);

}  // namespace

int main() { return 0; }
