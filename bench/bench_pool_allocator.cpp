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

// The pool is minted live on the thread that holds a Vigil's producer
// claim.  These benches run the pool without a Vigil, so they take that
// context from the test door.
constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

// N internal slots, each 256-byte aligned, offsets = i * slot_size.
MemoryPlan make_uniform_plan(::fixy::View<TensorSlot> slots, uint64_t slot_size) {
    const uint32_t n = static_cast<uint32_t>(slots.size());
    uint32_t i = 0;
    for (TensorSlot& slot : slots) {
        slot.offset_bytes = static_cast<uint64_t>(i) * slot_size;
        slot.nbytes = slot_size;
        slot.slot_id = SlotId{i};
        slot.birth_op = OpIndex{0};
        slot.death_op = OpIndex{n};
        slot.dtype = ScalarType::Float;
        slot.device_type = DeviceType::CPU;
        slot.device_idx = 0;
        slot.layout = Layout::Strided;
        slot.is_external = false;
        i++;
    }
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_external = 0;
    plan.pool_bytes = static_cast<uint64_t>(n) * slot_size;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

MemoryPlan make_mixed_plan(::fixy::View<TensorSlot> slots, uint32_t n_internal, uint32_t n_external,
                           uint64_t slot_size) {
    const uint32_t total = n_internal + n_external;
    uint32_t i = 0;
    for (TensorSlot& slot : slots) {
        const bool ext = (i >= n_internal);
        slot.offset_bytes = ext ? 0 : (static_cast<uint64_t>(i) * slot_size);
        slot.nbytes = slot_size;
        slot.slot_id = SlotId{i};
        slot.birth_op = OpIndex{0};
        slot.death_op = OpIndex{total};
        slot.dtype = ScalarType::Float;
        slot.device_type = DeviceType::CPU;
        slot.device_idx = 0;
        slot.layout = Layout::Strided;
        slot.is_external = ext;
        i++;
    }
    MemoryPlan plan{};
    plan.slots = slots;
    plan.num_external = n_external;
    plan.pool_bytes = static_cast<uint64_t>(n_internal) * slot_size;
    plan.device_type = DeviceType::CPU;
    plan.device_idx = 0;
    return plan;
}

// A count of slots of a plan.
using SlotCount = ::fixy::Quantity<::fixy::unit::Element, uint32_t>;

// The position of a draw in a stream of Philox.
using DrawIndex = ::fixy::Id<struct DrawIndexTag, uint64_t>;

// The slot of a draw: the first word of the Philox block of the draw with the
// key Seed, modulo the count of the slots.
template <uint64_t Seed>
[[nodiscard]] uint32_t random_slot(DrawIndex draw, SlotCount num_slots) noexcept {
    const ::fixy::Philox4x32::Counter block = ::fixy::Philox4x32::generate(draw.raw(), Seed);
    return block.at(::fixy::index<0>()) % num_slots.raw();
}

// Fills each index with a random slot below num_slots, from the Philox
// stream of the key 42.
template <std::size_t Count>
void fill_random_indices(::fixy::FixedArray<uint32_t, Count>& indices, SlotCount num_slots) noexcept {
    uint64_t draw = 0;
    for (uint32_t& index : indices) {
        index = random_slot<42>(DrawIndex{draw}, num_slots);
        ++draw;
    }
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();
    auto test = ::foundation::effects::testing::test();

    bench::print("=== pool_allocator ===\n");
    bench::print("  sizeof(PoolAllocator)               = {}\n", sizeof(PoolAllocator));
    bench::print("  sizeof(TensorSlot)                  = {}\n", sizeof(TensorSlot));
    bench::print("  sizeof(MemoryPlan)                  = {}\n", sizeof(MemoryPlan));
    bench::print("  sizeof(SlotId)                      = {}\n\n", sizeof(SlotId));

    // The 18 rows below, three of them in each of the four loops.
    ::fixy::FixedList<bench::Report, 18> reports{};
    // The slot counts of the rows of each loop.
    constexpr ::fixy::FixedArray<uint32_t, 3> kSlotCounts{10u, 100u, 1000u};

    // ── slot_ptr sequential access ────────────────────────────────────
    bench::add_report(reports, [&] {
        ::foundation::core::FixedArray<TensorSlot, 1> slots{};
        MemoryPlan plan = make_uniform_plan(slots.view(), 256);
        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, plan);
        auto pv = pool.mint_initialized_view(kVigilForeground);
        const SlotId s0{0};
        auto r = bench::run("slot_ptr(1 slot, same)", [&] { bench::do_not_optimize(pool.slot_ptr(s0, pv)); });
        pool.destroy();
        return r;
    }());

    for (uint32_t N : kSlotCounts) {
        bench::add_report(reports, [&, N] {
            ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
            MemoryPlan plan = make_uniform_plan(slots.view(), 256);
            PoolAllocator pool;
            pool.init(::foundation::effects::testing::test().alloc, plan);
            auto pv = pool.mint_initialized_view(kVigilForeground);
            auto r = bench::run(bench::format_name("slot_ptr({} slots, sweep)", N), [&, N] {
                for (uint32_t i = 0; i < N; i++) {
                    bench::do_not_optimize(pool.slot_ptr(SlotId{i}, pv));
                }
            });
            pool.destroy();
            return r;
        }());
    }

    // ── slot_ptr random access ────────────────────────────────────────
    constexpr uint32_t ACCESS_COUNT = 10000;

    for (uint32_t N : kSlotCounts) {
        bench::add_report(reports, [&, N] {
            ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
            MemoryPlan plan = make_uniform_plan(slots.view(), 256);
            PoolAllocator pool;
            pool.init(::foundation::effects::testing::test().alloc, plan);
            auto pv = pool.mint_initialized_view(kVigilForeground);

            ::fixy::FixedArray<uint32_t, ACCESS_COUNT> indices{};
            fill_random_indices(indices, SlotCount{N});
            uint32_t idx = 0;

            auto r = bench::run(bench::format_name("slot_ptr({} slots, random)", N), [&] {
                const ::fixy::Index<ACCESS_COUNT> at =
                    ::fixy::Index<ACCESS_COUNT>::check(idx % ACCESS_COUNT).expect("a remainder is below the count");
                bench::do_not_optimize(pool.slot_ptr(SlotId{indices.at(at)}, pv));
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
    bench::add_report(reports, [&] {
        constexpr uint32_t N = 100;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
        MemoryPlan plan = make_uniform_plan(slots.view(), 256);
        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, plan);
        auto pv = pool.mint_initialized_view(kVigilForeground);
        auto r = bench::run("slot_ptr(100, direct ref)", [&] {
            for (uint32_t i = 0; i < N; i++)
                bench::do_not_optimize(pool.slot_ptr(SlotId{i}, pv));
        });
        pool.destroy();
        return r;
    }());

    bench::add_report(reports, [&] {
        constexpr uint32_t N = 100;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
        MemoryPlan plan = make_uniform_plan(slots.view(), 256);
        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, plan);
        auto pv = pool.mint_initialized_view(kVigilForeground);
        const auto pool_ref = ::fixy::to(static_cast<const PoolAllocator&>(pool));
        bench::do_not_optimize(pool_ref);
        auto r = bench::run("slot_ptr(100, via pointer)", [&] {
            for (uint32_t i = 0; i < N; i++)
                bench::do_not_optimize(pool_ref->slot_ptr(SlotId{i}, pv));
        });
        pool.destroy();
        return r;
    }());

    bench::add_report(reports, [&] {
        constexpr uint32_t N = 100;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
        MemoryPlan plan = make_uniform_plan(slots.view(), 256);
        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, plan);
        auto pv = pool.mint_initialized_view(kVigilForeground);
        const ::fixy::View<const ::fixy::Option<::fixy::RunAddress>> table = pool.table(pv);
        bench::do_not_optimize(table);
        auto r = bench::run("table (100, a View)", [&] {
            for (const ::fixy::Option<::fixy::RunAddress>& address : table)
                bench::do_not_optimize(address);
        });
        pool.destroy();
        return r;
    }());

    // ── init cold path ────────────────────────────────────────────────
    for (uint32_t N : kSlotCounts) {
        bench::add_report(reports, [&, N] {
            ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
            MemoryPlan plan = make_uniform_plan(slots.view(), 256);
            return bench::run(bench::format_name("init({} slots)", N), [&] {
                PoolAllocator pool;
                pool.init(::foundation::effects::testing::test().alloc, plan);
                bench::do_not_optimize(pool.pool_base());
                pool.destroy();
            });
        }());
    }

    // ── destroy/no-op and init+destroy pair ───────────────────────────
    bench::add_report(reports, bench::run("destroy() (no-op, never init'd)", [&] {
                          PoolAllocator pool;
                          pool.destroy();
                          bench::do_not_optimize(pool);
                      }));

    bench::add_report(reports, [&] {
        constexpr uint32_t N = 100;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
        MemoryPlan plan = make_uniform_plan(slots.view(), 256);
        return bench::run("init+destroy (100 slots, pair)", [&] {
            PoolAllocator pool;
            pool.init(::foundation::effects::testing::test().alloc, plan);
            bench::do_not_optimize(pool.pool_base());
            pool.destroy();
        });
    }());

    // ── detach+reinit (region switch) ─────────────────────────────────
    bench::add_report(reports, [&] {
        constexpr uint32_t N = 100;
        ::fixy::HeapArray<TensorSlot> slots_a = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
        ::fixy::HeapArray<TensorSlot> slots_b = ::fixy::mint_heap_array(test.alloc, N, TensorSlot{});
        MemoryPlan plan_a = make_uniform_plan(slots_a.view(), 256);
        MemoryPlan plan_b = make_uniform_plan(slots_b.view(), 512);
        return bench::run("detach+init(100 slots)", [&] {
            PoolAllocator pool;
            pool.init(::foundation::effects::testing::test().alloc, plan_a);
            {
                auto pv = pool.mint_initialized_view(kVigilForeground);
                auto old = pool.detach(pv);
                bench::do_not_optimize(old.size());
                pool.init(::foundation::effects::testing::test().alloc, plan_b);
            }
            // The detached bytes go back to the heap at the end of the scope.
            bench::do_not_optimize(pool.pool_base());
            pool.destroy();
        });
    }());

    // ── register_external ─────────────────────────────────────────────
    bench::add_report(reports, [&] {
        constexpr uint32_t N_INT = 80;
        constexpr uint32_t N_EXT = 20;
        constexpr uint32_t TOTAL = N_INT + N_EXT;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, TOTAL, TensorSlot{});
        MemoryPlan plan = make_mixed_plan(slots.view(), N_INT, N_EXT, 256);

        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, plan);
        auto pv = pool.mint_initialized_view(kVigilForeground);

        // The external buffers: one run of bytes, 256 bytes for each slot.
        struct alignas(256) ExternalBuffers {
            ::fixy::FixedArray<unsigned char, size_t{N_EXT} * 256> bytes{};
        };
        static ExternalBuffers ext_bufs{};

        auto r = bench::run("register_external(20 slots)", [&] {
            for (uint32_t i = 0; i < N_EXT; i++) {
                const ::fixy::RunAddress address = ::fixy::mint_run_address(ext_bufs.bytes.view(), size_t{i} * 256)
                                                       .expect("the run holds 256 bytes for each external slot");
                pool.register_external(SlotId{N_INT + i}, crucible::TensorData{address, crucible::kHostDevice}, pv);
            }
        });
        pool.destroy();
        return r;
    }());

    // ── Realistic replay pattern ──────────────────────────────────────
    bench::add_report(reports, [&] {
        constexpr uint32_t NUM_OPS = 200;
        constexpr uint32_t NUM_SLOTS = 100;
        ::fixy::HeapArray<TensorSlot> slots = ::fixy::mint_heap_array(test.alloc, NUM_SLOTS, TensorSlot{});
        MemoryPlan plan = make_uniform_plan(slots.view(), 256);

        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, plan);
        auto pv = pool.mint_initialized_view(kVigilForeground);

        struct OpSlots {
            SlotId in0{};
            SlotId in1{};
            SlotId out{};
        };
        // The three slots of each op, from the Philox stream of the key 123.
        ::fixy::FixedArray<OpSlots, NUM_OPS> op_slots{};
        uint64_t draw = 0;
        for (OpSlots& op : op_slots) {
            op.in0 = SlotId{random_slot<123>(DrawIndex{draw}, SlotCount{NUM_SLOTS})};
            op.in1 = SlotId{random_slot<123>(DrawIndex{draw + 1}, SlotCount{NUM_SLOTS})};
            op.out = SlotId{random_slot<123>(DrawIndex{draw + 2}, SlotCount{NUM_SLOTS})};
            draw += 3;
        }

        auto r = bench::run("replay(200 ops, 3 slots each)", [&] {
            for (OpSlots const& op : op_slots) {
                bench::do_not_optimize(pool.slot_ptr(op.in0, pv));
                bench::do_not_optimize(pool.slot_ptr(op.in1, pv));
                bench::do_not_optimize(pool.slot_ptr(op.out, pv));
            }
        });
        pool.destroy();
        return r;
    }());

    bench::emit_reports_text(reports.view());

    // Via-pointer vs. direct-ref (inside the one block that has both)
    // should be statistically indistinguishable: same assembly after
    // inlining. The raw-table form skips one indirection and may shave
    // a cycle per lookup.
    bench::print("\n=== compare — via-pointer overhead ===\n  ");
    // reports[7] = "slot_ptr(100, direct ref)", reports[8] = "slot_ptr(100, via pointer)"
    bench::compare(*reports.get(7).expect("the row of slot_ptr(100, direct ref)"),
                   *reports.get(8).expect("the row of slot_ptr(100, via pointer)"))
        .print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
