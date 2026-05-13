// SPDX-License-Identifier: Apache-2.0
//
// Negative-compile fixture (HS14 mandate) for crucible::
// live_intervals_disjoint_at<MaxLive>, mismatch class #2: LIVE-SET
// COUNT EXCEEDS MaxLive.
//
// CONTRACT-112 ships `live_intervals_disjoint_at<MaxLive>` as the
// production cite for the MemoryPlan invariant.  The MaxLive
// template parameter caps stack scratch — when k live slots
// exceed MaxLive at a given op, the function returns false (it
// cannot safely build the live-set span without overflowing the
// stack-allocated `std::array<Interval, MaxLive>`).
//
// This fixture pins that the helper correctly returns false when
// the caller's MaxLive bound is too tight for the planted
// configuration (3 simultaneously-live internal slots at op 1,
// MaxLive=2).  Catches the misuse class where production code
// hardcodes a too-small MaxLive and silently mis-validates plans
// when fed larger-than-anticipated live sets — without this neg
// fixture, the function would return false (correct), but a
// caller using P2900 `pre()` instead of CRUCIBLE_PRE could in
// principle bypass at consteval and accept a corrupt plan.
//
// Distinct from companion `neg_memory_plan_live_intervals_overlap`
// which catches the OTHER mismatch class (well-formed live-set
// count but byte-overlapping intervals).
//
// In constexpr context, `live_intervals_disjoint_at<2>` returning
// false on a 3-live-slot snapshot → constexpr local is not a
// constant expression → static_assert is ill-formed.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "non-constant condition" / "not a constant expression" /
//   "must be initialized by a constant expression".

#include <crucible/ir001/MerkleDag.h>

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

// Three internal slots, all simultaneously live at op 1, byte
// ranges chosen to be pairwise disjoint (so the predicate would
// PASS if MaxLive were ≥ 3).  We give MaxLive=2 — the function
// must return false because k=3 exceeds the stack scratch.
constexpr auto build_planted_max_overflow() {
    std::array<TensorSlot, 3> s{};
    s[0] = TensorSlot{
        .offset_bytes = 0, .nbytes = 64,
        .birth_op = OpIndex{0}, .death_op = OpIndex{5},
        .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
        .device_idx = 0, .layout = Layout::Strided,
        .is_external = false, .pad = {},
        .slot_id = SlotId{0}, .pad2 = {}};
    s[1] = TensorSlot{
        .offset_bytes = 64, .nbytes = 64,
        .birth_op = OpIndex{0}, .death_op = OpIndex{5},
        .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
        .device_idx = 0, .layout = Layout::Strided,
        .is_external = false, .pad = {},
        .slot_id = SlotId{1}, .pad2 = {}};
    s[2] = TensorSlot{
        .offset_bytes = 128, .nbytes = 64,
        .birth_op = OpIndex{0}, .death_op = OpIndex{5},
        .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
        .device_idx = 0, .layout = Layout::Strided,
        .is_external = false, .pad = {},
        .slot_id = SlotId{2}, .pad2 = {}};
    return s;
}

constexpr auto bad_slots = build_planted_max_overflow();

// MaxLive=2, three live slots → function returns false → constexpr
// local is non-constant → static_assert is ill-formed.
constexpr bool witness = crucible::live_intervals_disjoint_at<2>(
    std::span<const TensorSlot>(bad_slots.data(), bad_slots.size()),
    OpIndex{1});

static_assert(witness, "MaxLive=2 must accommodate live count");

}  // namespace

int main() { return 0; }
