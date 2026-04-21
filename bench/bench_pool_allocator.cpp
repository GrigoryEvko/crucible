// PoolAllocator hot and cold paths.
//
// Hot:  slot_ptr(SlotId)   — target: 1 cycle (8-byte load) ≈ 0.3 ns
//       register_external  — per-external-slot setup
// Cold: init / destroy / detach+reinit — amortized at region-change time
//
// Scenarios cover:
//   sequential slot_ptr sweeps at 1/10/100/1000 slots
//   random-access slot_ptr at 10/100/1000 slots
//   via-pointer vs direct-reference vs raw-table lookups (matters for
//     ReplayEngine which holds const PoolAllocator*)
//   init / destroy / detach+init cold paths at 10/100/1000 slots
//   register_external with 20 externals
//   realistic replay pattern: 200 ops × (2 inputs + 1 output) slot_ptr
//     calls per op, typical of a compiled trace walk

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>

#include "bench_harness.h"

using crucible::DeviceType;
using crucible::Layout;
using crucible::MemoryPlan;
using crucible::OpIndex;
using crucible::PoolAllocator;
using crucible::ScalarType;
using crucible::SlotId;
using crucible::TensorSlot;

namespace {

// N internal slots, each 256-byte aligned, offsets = i * slot_size.
MemoryPlan make_uniform_plan(TensorSlot* slots, uint32_t n, uint64_t slot_size) {
    for (uint32_t i = 0; i < n; i++) {
        slots[i].offset_bytes = static_cast<uint64_t>(i) * slot_size;
        slots[i].nbytes       = slot_size;
        slots[i].slot_id      = SlotId{i};
        slots[i].birth_op     = OpIndex{0};
        slots[i].death_op     = OpIndex{n};
        slots[i].dtype        = ScalarType::Float;
        slots[i].device_type  = DeviceType::CPU;
        slots[i].device_idx   = 0;
        slots[i].layout       = Layout::Strided;
        slots[i].is_external  = false;
        std::memset(slots[i].pad, 0, sizeof(slots[i].pad));
    }
    MemoryPlan plan{};
    plan.slots        = slots;
    plan.num_slots    = n;
    plan.num_external = 0;
    plan.pool_bytes   = static_cast<uint64_t>(n) * slot_size;
    plan.device_type  = DeviceType::CPU;
    plan.device_idx   = 0;
    return plan;
}

MemoryPlan make_mixed_plan(TensorSlot* slots, uint32_t n_internal,
                           uint32_t n_external, uint64_t slot_size) {
    const uint32_t total = n_internal + n_external;
    for (uint32_t i = 0; i < total; i++) {
        const bool ext = (i >= n_internal);
        slots[i].offset_bytes = ext ? 0 : (static_cast<uint64_t>(i) * slot_size);
        slots[i].nbytes       = slot_size;
        slots[i].slot_id      = SlotId{i};
        slots[i].birth_op     = OpIndex{0};
        slots[i].death_op     = OpIndex{total};
        slots[i].dtype        = ScalarType::Float;
        slots[i].device_type  = DeviceType::CPU;
        slots[i].device_idx   = 0;
        slots[i].layout       = Layout::Strided;
        slots[i].is_external  = ext;
        std::memset(slots[i].pad, 0, sizeof(slots[i].pad));
    }
    MemoryPlan plan{};
    plan.slots        = slots;
    plan.num_slots    = total;
    plan.num_external = n_external;
    plan.pool_bytes   = static_cast<uint64_t>(n_internal) * slot_size;
    plan.device_type  = DeviceType::CPU;
    plan.device_idx   = 0;
    return plan;
}

void fill_random_indices(uint32_t* indices, uint32_t count, uint32_t num_slots) {
    std::mt19937 rng(42);
    for (uint32_t i = 0; i < count; i++) {
        indices[i] = rng() % num_slots;
    }
}

} // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bool json = bench::env_json();

    std::printf("=== pool_allocator ===\n");
    std::printf("  sizeof(PoolAllocator)               = %zu\n", sizeof(PoolAllocator));
    std::printf("  sizeof(PoolAllocator::DetachedPool) = %zu\n", sizeof(PoolAllocator::DetachedPool));
    std::printf("  sizeof(TensorSlot)                  = %zu\n", sizeof(TensorSlot));
    std::printf("  sizeof(MemoryPlan)                  = %zu\n", sizeof(MemoryPlan));
    std::printf("  sizeof(SlotId)                      = %zu\n\n", sizeof(SlotId));

    std::vector<bench::Report> reports;
    reports.reserve(30);

    // ── slot_ptr sequential access ────────────────────────────────────
    reports.push_back([&]{
        TensorSlot slots[1]{};
        MemoryPlan plan = make_uniform_plan(slots, 1, 256);
        PoolAllocator pool;
        pool.init(&plan);
        auto pv = pool.mint_initialized_view();
        const SlotId s0{0};
        auto r = bench::run("slot_ptr(1 slot, same)", [&]{
            bench::do_not_optimize(pool.slot_ptr(s0, pv));
        });
        pool.destroy();
        return r;
    }());

    for (uint32_t N : {10u, 100u, 1000u}) {
        reports.push_back([&, N]{
            std::vector<TensorSlot> slots(N);
            MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
            PoolAllocator pool;
            pool.init(&plan);
            auto pv = pool.mint_initialized_view();
            char label[64];
            std::snprintf(label, sizeof(label), "slot_ptr(%u slots, sweep)", N);
            auto r = bench::run(label, [&, N]{
                for (uint32_t i = 0; i < N; i++) {
                    bench::do_not_optimize(pool.slot_ptr(SlotId{i}, pv));
                }
            });
            pool.destroy();
            return r;
        }());
    }

    // ── slot_ptr random access ────────────────────────────────────────
    constexpr uint32_t ACCESS_COUNT = 10'000;

    for (uint32_t N : {10u, 100u, 1000u}) {
        reports.push_back([&, N]{
            std::vector<TensorSlot> slots(N);
            MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
            PoolAllocator pool;
            pool.init(&plan);
            auto pv = pool.mint_initialized_view();

            std::vector<uint32_t> indices(ACCESS_COUNT);
            fill_random_indices(indices.data(), ACCESS_COUNT, N);
            uint32_t idx = 0;

            char label[64];
            std::snprintf(label, sizeof(label), "slot_ptr(%u slots, random)", N);
            auto r = bench::run(label, [&]{
                bench::do_not_optimize(
                    pool.slot_ptr(SlotId{indices[idx % ACCESS_COUNT]}, pv));
                idx++;
            });
            pool.destroy();
            return r;
        }());
    }

    // ── via-pointer vs direct-ref vs raw-table ────────────────────────
    //
    // Three siblings that look alike on paper but exercise different
    // indirection forms. Each gets its own fresh PoolAllocator in its
    // own IIFE scope so the measurements don't interact.
    reports.push_back([&]{
        constexpr uint32_t N = 100;
        std::vector<TensorSlot> slots(N);
        MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
        PoolAllocator pool;
        pool.init(&plan);
        auto pv = pool.mint_initialized_view();
        auto r = bench::run("slot_ptr(100, direct ref)", [&]{
            for (uint32_t i = 0; i < N; i++)
                bench::do_not_optimize(pool.slot_ptr(SlotId{i}, pv));
        });
        pool.destroy();
        return r;
    }());

    reports.push_back([&]{
        constexpr uint32_t N = 100;
        std::vector<TensorSlot> slots(N);
        MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
        PoolAllocator pool;
        pool.init(&plan);
        auto pv = pool.mint_initialized_view();
        const PoolAllocator* pool_ptr = &pool;
        bench::do_not_optimize(pool_ptr);
        auto r = bench::run("slot_ptr(100, via pointer)", [&]{
            for (uint32_t i = 0; i < N; i++)
                bench::do_not_optimize(pool_ptr->slot_ptr(SlotId{i}, pv));
        });
        pool.destroy();
        return r;
    }());

    reports.push_back([&]{
        constexpr uint32_t N = 100;
        std::vector<TensorSlot> slots(N);
        MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
        PoolAllocator pool;
        pool.init(&plan);
        void* const* tbl = pool.table();
        bench::do_not_optimize(tbl);
        auto r = bench::run("table[sid] (100, captured raw)", [&]{
            for (uint32_t i = 0; i < N; i++)
                bench::do_not_optimize(tbl[i]);
        });
        pool.destroy();
        return r;
    }());

    // ── init cold path ────────────────────────────────────────────────
    for (uint32_t N : {10u, 100u, 1000u}) {
        reports.push_back([&, N]{
            std::vector<TensorSlot> slots(N);
            MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
            char label[64];
            std::snprintf(label, sizeof(label), "init(%u slots)", N);
            return bench::run(label, [&]{
                PoolAllocator pool;
                pool.init(&plan);
                bench::do_not_optimize(pool.pool_base());
                pool.destroy();
            });
        }());
    }

    // ── destroy/no-op and init+destroy pair ───────────────────────────
    reports.push_back(bench::run("destroy() (no-op, never init'd)", [&]{
        PoolAllocator pool;
        pool.destroy();
        bench::do_not_optimize(&pool);
    }));

    reports.push_back([&]{
        constexpr uint32_t N = 100;
        std::vector<TensorSlot> slots(N);
        MemoryPlan plan = make_uniform_plan(slots.data(), N, 256);
        return bench::run("init+destroy (100 slots, pair)", [&]{
            PoolAllocator pool;
            pool.init(&plan);
            bench::do_not_optimize(pool.pool_base());
            pool.destroy();
        });
    }());

    // ── detach+reinit (region switch) ─────────────────────────────────
    reports.push_back([&]{
        constexpr uint32_t N = 100;
        std::vector<TensorSlot> slots_a(N);
        std::vector<TensorSlot> slots_b(N);
        MemoryPlan plan_a = make_uniform_plan(slots_a.data(), N, 256);
        MemoryPlan plan_b = make_uniform_plan(slots_b.data(), N, 512);
        return bench::run("detach+init(100 slots)", [&]{
            PoolAllocator pool;
            pool.init(&plan_a);
            {
                auto pv = pool.mint_initialized_view();
                auto old = pool.detach(pv);
                bench::do_not_optimize(old.base);
                pool.init(&plan_b);
            }
            // DetachedPool destructs here, freeing old allocation.
            bench::do_not_optimize(pool.pool_base());
            pool.destroy();
        });
    }());

    // ── register_external ─────────────────────────────────────────────
    reports.push_back([&]{
        constexpr uint32_t N_INT = 80;
        constexpr uint32_t N_EXT = 20;
        constexpr uint32_t TOTAL = N_INT + N_EXT;
        std::vector<TensorSlot> slots(TOTAL);
        MemoryPlan plan = make_mixed_plan(slots.data(), N_INT, N_EXT, 256);

        PoolAllocator pool;
        pool.init(&plan);
        auto pv = pool.mint_initialized_view();

        alignas(256) static char ext_bufs[N_EXT][256];

        auto r = bench::run("register_external(20 slots)", [&]{
            for (uint32_t i = 0; i < N_EXT; i++) {
                pool.register_external(SlotId{N_INT + i},
                    crucible::safety::NonNull<void*>{ext_bufs[i]}, pv);
            }
        });
        pool.destroy();
        return r;
    }());

    // ── Realistic replay pattern ──────────────────────────────────────
    reports.push_back([&]{
        constexpr uint32_t NUM_OPS   = 200;
        constexpr uint32_t NUM_SLOTS = 100;
        std::vector<TensorSlot> slots(NUM_SLOTS);
        MemoryPlan plan = make_uniform_plan(slots.data(), NUM_SLOTS, 256);

        PoolAllocator pool;
        pool.init(&plan);
        auto pv = pool.mint_initialized_view();

        struct OpSlots { SlotId in0, in1, out; };
        std::vector<OpSlots> op_slots(NUM_OPS);
        std::mt19937 rng(123);
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            op_slots[i].in0 = SlotId{static_cast<uint32_t>(rng() % NUM_SLOTS)};
            op_slots[i].in1 = SlotId{static_cast<uint32_t>(rng() % NUM_SLOTS)};
            op_slots[i].out = SlotId{static_cast<uint32_t>(rng() % NUM_SLOTS)};
        }

        auto r = bench::run("replay(200 ops, 3 slots each)", [&]{
            for (uint32_t i = 0; i < NUM_OPS; i++) {
                bench::do_not_optimize(pool.slot_ptr(op_slots[i].in0, pv));
                bench::do_not_optimize(pool.slot_ptr(op_slots[i].in1, pv));
                bench::do_not_optimize(pool.slot_ptr(op_slots[i].out, pv));
            }
        });
        pool.destroy();
        return r;
    }());

    bench::emit_reports_text(reports);

    // Via-pointer vs. direct-ref (inside the one block that has both)
    // should be statistically indistinguishable: same assembly after
    // inlining. The raw-table form skips one indirection and may shave
    // a cycle per lookup.
    std::printf("\n=== compare — via-pointer overhead ===\n  ");
    // reports[7] = "slot_ptr(100, direct ref)", reports[8] = "slot_ptr(100, via pointer)"
    bench::compare(reports[7], reports[8]).print_text(stdout);

    bench::emit_reports_json(reports, json);
    return 0;
}
