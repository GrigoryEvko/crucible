// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::
// live_intervals_disjoint_at<MaxLive>, mismatch class #1: LIVE-SET
// BYTE OVERLAP.
//
// CONTRACT-112 ships `live_intervals_disjoint_at<MaxLive>` as the
// production cite for the MemoryPlan invariant: at every op
// boundary `t`, the simultaneously-live TensorSlots must have
// pairwise-disjoint byte ranges `[offset_bytes, offset_bytes +
// nbytes)`.  This fixture pins that the helper rejects two
// internal slots whose live ranges intersect AND whose byte
// ranges overlap — the canonical sweep-line offset-assignment
// bug where the planner forgets to bump `next_offset` past the
// previous live slot's nbytes.
//
// In production this bug manifests as: a plan where two slots
// `[birth=0, death=5]` (offset=0, nbytes=128) and `[birth=2,
// death=7]` (offset=64, nbytes=128) are simultaneously live at
// ops 2..5, with byte ranges `[0, 128)` and `[64, 192)` that
// overlap on `[64, 128)`.  Reads from slot B at op 3 see writes
// from slot A's earlier compute trampling slot B's bytes.
//
// Distinct from companion `neg_memory_plan_live_intervals_max_overflow`
// which catches the OTHER mismatch class (live-set count exceeds
// the caller's compile-time MaxLive bound — predicate cannot
// safely build the live span and returns false).
//
// In constexpr context (constant evaluation), `live_intervals_disjoint_at`
// returning false → the surrounding constexpr local is non-
// constant, hence the static_assert is ill-formed.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "must be initialized by a constant expression".

#include <crucible/MerkleDag.h>

#include <array>
#include <cstdint>
#include <span>

namespace {

using crucible::TensorSlot;
using crucible::OpIndex;
using crucible::SlotId;
using crucible::ScalarType;
using crucible::DeviceType;
using crucible::Layout;

// Two internal slots, simultaneously live at op 3, byte-overlapping
// on `[64, 128)`.  Slot A: offset 0, 128 bytes, live [0, 5].  Slot
// B: offset 64, 128 bytes, live [2, 7].  At op 3 BOTH are live AND
// their byte intervals `[0, 128)` and `[64, 192)` share `[64, 128)`
// — the planner's invariant is broken at op 3.
constexpr auto build_planted_overlap() {
    std::array<TensorSlot, 2> s{};
    s[0] = TensorSlot{
        .offset_bytes = 0, .nbytes = 128,
        .birth_op = OpIndex{0}, .death_op = OpIndex{5},
        .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
        .device_idx = 0, .layout = Layout::Strided,
        .is_external = false, .pad = {},
        .slot_id = SlotId{0}, .pad2 = {}};
    s[1] = TensorSlot{
        .offset_bytes = 64, .nbytes = 128,
        .birth_op = OpIndex{2}, .death_op = OpIndex{7},
        .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
        .device_idx = 0, .layout = Layout::Strided,
        .is_external = false, .pad = {},
        .slot_id = SlotId{1}, .pad2 = {}};
    return s;
}

constexpr auto bad_slots = build_planted_overlap();

// Predicate returns false at op 3 → constexpr local is not a
// constant expression → static_assert below is ill-formed.
constexpr bool witness = crucible::live_intervals_disjoint_at<2>(
    std::span<const TensorSlot>(bad_slots.data(), bad_slots.size()),
    OpIndex{3});

static_assert(witness, "MemoryPlan invariant must hold at op 3");

}  // namespace

int main() { return 0; }
