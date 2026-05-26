// ═══════════════════════════════════════════════════════════════════
// prop_memory_plan_no_overlap.cpp — liveness-correctness fuzzer for
// BackgroundThread::compute_memory_plan (L3 Memory).
//
// CLAUDE.md L3 states the load-bearing invariant of the static
// memory planner: "Two slots whose live ranges intersect MUST NOT"
// share byte ranges, while two dead-and-reused slots MAY.  The
// existing unit test (test/test_memory_plan.cpp) exercises ONE
// hand-built liveness configuration (4 slots).  That proves the
// happy path but cannot find the planner bug class that only shows
// up under irregular, randomly-interleaved birth/death intervals:
// a first-fit free-list reuse decision that hands a still-live slot
// an offset overlapping another still-live slot.
//
// This fuzzer drives the planner with thousands of random slot sets
// and asserts four invariants on the resulting MemoryPlan:
//
//   (A) Structural: plan non-null, num_slots / num_external faithful
//       to the input, pool_bytes within the PoolAllocator ceiling.
//   (B) No-overlap: at EVERY op boundary t in [0, max_death], the
//       simultaneously-live internal slots have pairwise-disjoint
//       byte intervals — checked through the SAME canonical oracle
//       (live_intervals_disjoint_at) that production CONTRACT-112
//       uses, so the fuzzer's lens matches the contract's lens.
//   (C) Containment: every internal slot's [offset, offset+nbytes)
//       lies within [0, pool_bytes); external slots keep offset 0
//       (the planner does not place them in the pool).
//   (D) Determinism (DetSafe): the same slot set planned twice on
//       two fresh BackgroundThread arenas yields byte-identical
//       offsets and pool_bytes.  Same input → same plan → same
//       addresses, the property the whole replay/reincarnation
//       story rests on.
//   (E) Alignment: every internal slot's offset is a multiple of
//       the planner's 256-byte ALIGNMENT.  A compiled kernel writes
//       to pool_base + offset; a misaligned offset faults on ISAs
//       that require aligned vector stores and silently perf-cliffs
//       the rest.  The planner reserves round_up(nbytes, 256) per
//       slot and only ever places offsets at pool_end (a running
//       multiple of 256) or at inherited/split free-block offsets
//       (aligned by induction), so the invariant must hold for every
//       internal slot regardless of the random liveness shape.
//
// A fresh BackgroundThread is constructed per iteration: its default
// ctor spawns NO pipeline thread (start() does that), so the only
// state is its arena, which is reclaimed when the local dies —
// bounding memory across the run.  test_memory_plan.cpp proves the
// construct-and-call-without-start() shape is sound.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/BackgroundThread.h>
#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>
#include <crucible/Types.h>
#include <crucible/effects/Capabilities.h>

#include <array>
#include <cstdint>
#include <span>

namespace {

// Bounds chosen so each iteration stays cheap under ASan while still
// exercising deep interleaving: up to 64 slots over a 48-op timeline.
// Internal-slot count <= 64 ≪ MAX_FREE (4096) free-list cap, and
// num_ops <= 49 keeps the planner's counting-sort scratch tiny.
inline constexpr uint32_t kMaxSlots = 64;
inline constexpr uint32_t kMaxOp = 48;
inline constexpr uint64_t kMaxSlotBytes = 4096;

// Mirrors compute_memory_plan's private `static constexpr uint32_t
// ALIGNMENT = 256` (BackgroundThread.h).  The planner does not expose
// it, so the fuzzer pins the coupling here; if the planner's alignment
// ever changes, invariant (E) reds until this constant is updated.
inline constexpr uint64_t kPlanAlignment = 256;

struct SlotSpec {
    uint32_t birth = 0;       // <= death, both in [0, kMaxOp)
    uint32_t death = 0;
    uint64_t nbytes = 0;      // [1, kMaxSlotBytes]
    bool is_external = false; // ~25% of slots
    uint8_t pad[3]{};
};

struct SlotSet {
    std::array<SlotSpec, kMaxSlots> specs{};
    uint32_t count = 0;       // [1, kMaxSlots]
    uint8_t pad[4]{};
};

// Materialize a SlotSet into a fresh TensorSlot array and return the
// maximum death_op (the upper bound of the no-overlap sweep).
[[nodiscard]] uint32_t materialize(
    const SlotSet& set, crucible::TensorSlot* out) noexcept
{
    using crucible::TensorSlot;
    using crucible::OpIndex;
    using crucible::SlotId;
    using crucible::ScalarType;
    using crucible::DeviceType;
    using crucible::Layout;

    uint32_t max_death = 0;
    for (uint32_t i = 0; i < set.count; ++i) {
        const SlotSpec& spec = set.specs[i];
        out[i] = TensorSlot{
            .offset_bytes = 0, .nbytes = spec.nbytes,
            .birth_op = OpIndex{spec.birth}, .death_op = OpIndex{spec.death},
            .dtype = ScalarType::Float, .device_type = DeviceType::CPU,
            .device_idx = 0, .layout = Layout::Strided,
            .is_external = spec.is_external, .pad = {},
            .slot_id = SlotId{i}, .pad2 = {}};
        if (spec.death > max_death) max_death = spec.death;
    }
    return max_death;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    using crucible::TensorSlot;
    using crucible::OpIndex;
    using crucible::BackgroundThread;
    using crucible::PoolAllocator;
    using crucible::live_intervals_disjoint_at;

    Config cfg = parse_args(argc, argv);

    return run("memory_plan_no_overlap", cfg,
        // ── Generator: random slot set with bounded interleaving ──
        [](Rng& rng) noexcept -> SlotSet {
            SlotSet set{};
            set.count = 1u + rng.next_below(kMaxSlots);  // [1, kMaxSlots]
            for (uint32_t i = 0; i < set.count; ++i) {
                const uint32_t a = rng.next_below(kMaxOp);
                const uint32_t b = rng.next_below(kMaxOp);
                set.specs[i].birth = a < b ? a : b;
                set.specs[i].death = a < b ? b : a;
                set.specs[i].nbytes =
                    1ull + rng.next_below(static_cast<uint32_t>(kMaxSlotBytes));
                set.specs[i].is_external = (rng.next_below(4) == 0);
            }
            return set;
        },
        // ── Property: plan the set, check (A)-(D) ──
        [](const SlotSet& set) noexcept -> bool {
            const uint32_t num_slots = set.count;

            TensorSlot slots1[kMaxSlots]{};
            const uint32_t max_death = materialize(set, slots1);

            auto test = crucible::effects::testing::test();
            BackgroundThread bt1;
            auto* plan1 = bt1.compute_memory_plan(test.alloc, slots1, num_slots);

            // (A) Structural fidelity.
            if (plan1 == nullptr) return false;
            if (plan1->num_slots != num_slots) return false;

            uint32_t want_external = 0;
            for (uint32_t i = 0; i < num_slots; ++i)
                if (set.specs[i].is_external) ++want_external;
            if (plan1->num_external != want_external) return false;

            if (plan1->pool_bytes > PoolAllocator::kMaxPoolBytes) return false;

            // (B) No-overlap at every op boundary, via the canonical
            //     production oracle (MaxLive=kMaxSlots caps the live set,
            //     which can never exceed num_slots <= kMaxSlots).
            const std::span<const TensorSlot> span1{slots1, num_slots};
            for (uint32_t t = 0; t <= max_death; ++t) {
                if (!live_intervals_disjoint_at<kMaxSlots>(span1, OpIndex{t}))
                    return false;
            }

            // (C) Containment: internal slots fit the pool; external
            //     slots are not placed (offset stays 0).
            // (E) Alignment: internal slot offsets are 256-aligned.
            for (uint32_t i = 0; i < num_slots; ++i) {
                if (slots1[i].is_external) {
                    if (slots1[i].offset_bytes != 0) return false;
                } else {
                    // offset + nbytes can't wrap: both < 2^20 by gen bounds.
                    if (slots1[i].offset_bytes + slots1[i].nbytes >
                        plan1->pool_bytes)
                        return false;
                    if (slots1[i].offset_bytes % kPlanAlignment != 0)
                        return false;
                }
            }

            // (D) Determinism: replan the same set on a fresh arena;
            //     offsets and pool_bytes must be byte-identical.
            TensorSlot slots2[kMaxSlots]{};
            (void)materialize(set, slots2);
            BackgroundThread bt2;
            auto* plan2 = bt2.compute_memory_plan(test.alloc, slots2, num_slots);
            if (plan2 == nullptr) return false;
            if (plan2->pool_bytes != plan1->pool_bytes) return false;
            for (uint32_t i = 0; i < num_slots; ++i) {
                if (slots2[i].offset_bytes != slots1[i].offset_bytes)
                    return false;
            }

            return true;
        });
}
