// Vigil::dispatch_op() and related hot-path benchmark suite.
//
// Measures the full per-op pipeline:
//   • Sub-component baselines — TraceRing / MetaLog / PoolAllocator primitives
//   • Isolated ReplayEngine + CrucibleContext (no Vigil wrapper)
//   • record_op (MetaLog + TraceRing) on its own
//   • dispatch_op in every mode: RECORDING, COMPILED steady, COMPILED + output,
//     compiled match-only, full 8-op iteration including auto-reset
//   • Divergence cold-path (end-to-end: fresh setup + divergent dispatch)
//   • RegionCache::find_alternate
//
// Target latencies (x86-64, GCC 16 -O3):
//   COMPILED path:  ~2-5 ns/op
//   RECORDING path: ~15 ns/op
//   Alignment:      ~20 ns/op (cold, amortized across K=5 ops)
//   Divergence:     ~50-200 ns (pure dispatch); end-to-end dominated by
//                   setup_compiled_vigil which is milliseconds.

#include "bench_harness.h"
#include "../test/trace_rig.h"
#include "../vessel/torch/vessel_api.h"

#include <foundation/effects/Effect.h>
#include <crucible/Vigil.h>

#include <bit>
#include <cstdint>

using namespace crucible;

namespace {

// The views of the replay chain are minted on the thread that holds a
// Vigil's producer claim.  The benches that drive the chain without a
// Vigil take that context from the test door.
constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

// Certifies an Entry this file built field by field, so it can reach
// record_op and dispatch_op, which take the second trust tag. It runs no
// check, because a literal written here is whatever this file wrote. An
// adapter filling an Entry from a foreign runtime runs the checks in
// vessel_api_typed.h and crosses the retag edge; this is file-local so
// there is nothing shorter for an adapter to reach for.
//
// The three helpers of an op are inline in each timed body.  A body with a
// call to a helper times the call too, and the choice of the inliner changes
// from one build of this file to the next.
[[nodiscard]] CRUCIBLE_INLINE TraceRing::ValidatedEntry certify_synthetic_entry(TraceRing::SealedEntry const& entry
                                                                                CRUCIBLE_LIFETIMEBOUND) noexcept {
    return mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>();
}
TraceRing::ValidatedEntry certify_synthetic_entry(TraceRing::SealedEntry const&&) =
    delete("the tag names the sealed entry, and a temporary sealed entry ends before the tag");

// Certifies an entry this file sealed, and pairs it with its sealed records,
// so it can reach record_op and dispatch_op.  The process ends when the count
// of the records disagrees with the entry.  The op holds a Ref of the sealed
// entry and a View of the sealed list.
template <std::size_t N>
[[nodiscard]] CRUCIBLE_INLINE ValidatedOp certify_synthetic_op(TraceRing::SealedEntry const& entry
                                                               CRUCIBLE_LIFETIMEBOUND,
                                                               SealedRecords<N> const& records
                                                               CRUCIBLE_LIFETIMEBOUND) noexcept {
    return crucible::mint_validated_op(certify_synthetic_entry(entry), records)
        .expect("the count of the records agrees with the entry");
}
template <std::size_t N>
ValidatedOp certify_synthetic_op(TraceRing::SealedEntry const&&, SealedRecords<N> const&) =
    delete("the op keeps a Ref of the sealed entry, and a temporary sealed entry ends before the op");
template <std::size_t N>
ValidatedOp certify_synthetic_op(TraceRing::SealedEntry const&, SealedRecords<N> const&&) =
    delete("the op keeps a View of the sealed list, and a temporary list ends before the op");

// The same for an entry with no records.
[[nodiscard]] CRUCIBLE_INLINE ValidatedOp certify_synthetic_op(TraceRing::SealedEntry const& entry
                                                               CRUCIBLE_LIFETIMEBOUND) noexcept {
    return crucible::mint_validated_op(certify_synthetic_entry(entry))
        .expect("the entry of an op with no records counts no tensor");
}
ValidatedOp certify_synthetic_op(TraceRing::SealedEntry const&&) =
    delete("the op keeps a Ref of the sealed entry, and a temporary sealed entry ends before the op");

// ── Constants ──────────────────────────────────────────────────────

constexpr uint32_t NUM_OPS = 8;
constexpr uint32_t K = Vigil::ALIGNMENT_K;  // 5

// A record case appends one op in each body.  The bodies of a case stay below
// the room of the ring, so each body appends and no body times the refusal of
// a full ring.  Eight bodies share one timed region, because one append costs
// less than the timer.
constexpr size_t RECORD_WARMUP = 1000;
constexpr size_t RECORD_SAMPLES = 7000;
constexpr size_t RECORD_BATCH = 8;
constexpr size_t RECORD_BODIES = RECORD_WARMUP + RECORD_SAMPLES * RECORD_BATCH;
static_assert(RECORD_BODIES < TraceRing::CAPACITY,
              "a record case must end before the ring fills, or it times the refusal of a full ring");

constexpr ::fixy::FixedArray<SchemaHash, NUM_OPS> SCHEMA{
    SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102}, SchemaHash{0x103},
    SchemaHash{0x104}, SchemaHash{0x105}, SchemaHash{0x106}, SchemaHash{0x107},
};
constexpr ::fixy::FixedArray<ShapeHash, NUM_OPS> SHAPE{
    ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
    ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207},
};

// The position of an op in the cycle of the NUM_OPS ops.
using OpSlot = ::fixy::Index<NUM_OPS>;

// The sealed entries of the bodies of the record rows.  A timed body of such a
// row seals an entry with a new schema hash for each op, so the stream has no
// period.  The build writes each field in the storage of the seal, as the
// product path does, and copies no entry.

// The entry of op 1 of the cycle: one input and one output.
[[nodiscard]] CRUCIBLE_INLINE TraceRing::SealedEntry seal_body_entry(SchemaHash schema_hash) noexcept {
    return TraceRing::SealedEntry{::fixy::build_sealed, [schema_hash]() noexcept {
                                      TraceRing::Entry built{};
                                      built.schema_hash = schema_hash;
                                      built.shape_hash = SHAPE.at(::fixy::index<1>());
                                      built.num_inputs = 1;
                                      built.num_outputs = 1;
                                      return built;
                                  }};
}

// The entry of an op with no tensor, as a profiler hook records it.
[[nodiscard]] CRUCIBLE_INLINE TraceRing::SealedEntry seal_hook_entry(SchemaHash schema_hash) noexcept {
    return TraceRing::SealedEntry{::fixy::build_sealed, [schema_hash]() noexcept {
                                      TraceRing::Entry built{};
                                      built.schema_hash = schema_hash;
                                      built.shape_hash = ShapeHash{0xAAA};
                                      return built;
                                  }};
}

// ── Helpers ────────────────────────────────────────────────────────

// The position after slot in its cycle of Count positions.  A remainder is
// below its divisor, so the optimizer removes the check of the index.
template <std::size_t Count>
[[nodiscard]] ::fixy::Index<Count> next_in_cycle(::fixy::Index<Count> slot) noexcept {
    return ::fixy::Index<Count>::check((slot.raw() + 1) % Count).expect("a remainder is below its divisor");
}

// The iteration and the position of an op: the two parts of its synthetic
// data address.
enum class IterationNumber : std::uint32_t {
};
enum class OpPosition : std::uint32_t {
};

// The synthetic data address of the op at a position of an iteration.  No
// tensor lives there, and the bench reads only the bits of the address.
[[nodiscard]] SyntheticAddress fake_address(IterationNumber iter, OpPosition op) noexcept {
    const std::uint32_t low =
        (static_cast<std::uint32_t>(iter) + 1) * 0x100000 + (static_cast<std::uint32_t>(op) + 1) * 0x1000;
    return SyntheticAddress{low};
}

TensorMeta make_meta(SyntheticAddress address) noexcept {
    TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = tensor_dim(1024);
    m.strides[0] = tensor_dim(1);
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.device_idx = 0;
    m.layout = Layout::Strided;
    m.data_ptr = mint_synthetic_data_ptr(address);
    return m;
}

// The entry of an op and its records, each sealed when the op is made
// (TraceRing::SealedEntry, and SealedRecords of crucible/ValidatedOp.h).  A
// COMPILED body mints the op from the sealed entry and records, so it copies
// no byte.  A RECORDING body seals a new entry for each op
// (seal_body_entry), as the product path does.  The product path of the
// vessel mints the op from the one writer of the run of a frame, and these
// bodies mint it from a sealed list, so they are not the product path.  An
// op does not move, so the ops of a cycle live in a heap array (Ops), and a
// body of a cycle reads the ops through a View of a fixed extent (Cycle).
struct OpData {
    TraceRing::SealedEntry entry;
    SealedRecords<2> records;
};

// The tensor that an op after the first one reads.
enum class InputSource : std::uint8_t {
    PreviousOutput,  // the output of the op before it
    Parameter,  // a tensor that no op writes, at one address in each iteration
};

// The iteration whose addresses the parameters have.  No iteration of the
// bench has this number, so no op writes a parameter.
constexpr IterationNumber kParameterIteration{200};

OpData make_op(uint32_t iter, uint32_t op_idx, InputSource source = InputSource::PreviousOutput) noexcept {
    const OpSlot slot = OpSlot::check(op_idx).expect("the position of an op is below NUM_OPS");
    ::fixy::FixedList<TensorMeta, 2> records{};
    if (op_idx > 0)
        (void)records
            .append(make_meta(source == InputSource::Parameter
                                  ? fake_address(kParameterIteration, OpPosition{op_idx})
                                  : fake_address(IterationNumber{iter}, OpPosition{op_idx - 1})))
            .expect("the list holds the two records of an op");
    (void)records.append(make_meta(fake_address(IterationNumber{iter}, OpPosition{op_idx})))
        .expect("the list holds the two records of an op");
    return OpData{.entry = TraceRing::SealedEntry{::fixy::build_sealed,
                                                  [&slot, op_idx]() noexcept {
                                                      TraceRing::Entry built{};
                                                      built.schema_hash = SCHEMA.at(slot);
                                                      built.shape_hash = SHAPE.at(slot);
                                                      built.num_inputs = (op_idx == 0) ? 0 : 1;
                                                      built.num_outputs = 1;
                                                      return built;
                                                  }},
                  .records = SealedRecords<2>{std::move(records)}};
}

// The ops of one iteration, in the order of the cycle.
using Ops = ::fixy::HeapArray<OpData>;

// The ops of the iteration 10, which the COMPILED rows dispatch after the
// setup of setup_compiled_vigil.  Each op is built in its place.
[[nodiscard]] Ops make_compiled_ops(InputSource source = InputSource::PreviousOutput) noexcept {
    return ::fixy::mint_heap_array_with<OpData>(
        ::foundation::effects::testing::test().alloc, NUM_OPS,
        [source](std::size_t position) noexcept { return make_op(10, static_cast<uint32_t>(position), source); });
}

// The ops of a cycle as a View of the fixed extent NUM_OPS.  A body holds the
// View by value, so it reads an op at an OpSlot with one load and no check.
using Cycle = ::fixy::View<OpData const, NUM_OPS>;

[[nodiscard]] Cycle cycle_of(Ops const& ops CRUCIBLE_LIFETIMEBOUND) noexcept {
    return ops.view().window<NUM_OPS>(0).expect("the array holds NUM_OPS ops");
}
Cycle cycle_of(Ops const&&) = delete("the View points into the array, and a temporary array ends before the View");

void feed_record(Vigil& vigil, uint32_t iter, InputSource source) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto d = make_op(iter, i, source);
        (void)vigil.record_op(certify_synthetic_op(d.entry, d.records));
    }
}

void feed_trigger(Vigil& vigil, uint32_t iter, InputSource source) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto d = make_op(iter, i, source);
        (void)vigil.record_op(certify_synthetic_op(d.entry, d.records));
    }
}

void wait_region_published(Vigil& vigil) {
    while (!vigil.has_pending_region())
        CRUCIBLE_SPIN_PAUSE;
}

// Aligns the published region with K ops, then dispatches the rest of the
// iteration in COMPILED mode.  Returns the number of results that are not
// the expected ones.  A timed body adds the count up, and a check after the
// measurement reads it.
[[nodiscard]] uint32_t align_and_activate(Vigil& vigil, uint32_t iter, InputSource source) {
    uint32_t unexpected_results = 0;
    for (uint32_t i = 0; i < K; i++) {
        auto d = make_op(iter, i, source);
        const auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
        unexpected_results += static_cast<uint32_t>(r.action != DispatchResult::Action::RECORD);
    }
    unexpected_results += static_cast<uint32_t>(!vigil.context().is_compiled());

    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto d = make_op(iter, i, source);
        const auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
        unexpected_results += static_cast<uint32_t>(r.action != DispatchResult::Action::COMPILED);
    }
    return unexpected_results;
}

// Build a Vigil in COMPILED mode ready for benchmarking. On return the
// engine is at position 0 and the next dispatch_op is the first MATCH.
// Returns the count of align_and_activate.
[[nodiscard]] uint32_t setup_compiled_vigil(Vigil& vigil, InputSource source = InputSource::PreviousOutput) {
    feed_record(vigil, 0, source);
    feed_record(vigil, 1, source);
    feed_trigger(vigil, 2, source);
    const ::fixy::TestRunnerCtx waiter{::foundation::effects::testing::test()};
    vigil.flush(waiter).expect("the pipeline of the bench runs");
    wait_region_published(vigil);
    return align_and_activate(vigil, 3, source);
}

// The count of the external slots of the plan that the Vigil replays.
[[nodiscard]] uint32_t count_external_slots(Vigil const& vigil) {
    const DagRef<RegionNode const> region = vigil.context().active_region().expect("the Vigil replays a region");
    return region->plan.expect("an active region has a plan")->num_external;
}

// The benches below measure a plan for one CPU device with no distribution,
// which is what MemoryPlan and TensorSlot give by default.  The arena gives
// raw storage, so a plan that the setup does not construct holds the bytes
// that the arena held before.  Each slot is a copy of the fill value of its
// array.
void check_single_cpu_plan(const MemoryPlan& plan) {
    CRUCIBLE_BENCH_CHECK(plan.device_type == DeviceType::CPU && plan.device_idx == -1);
    CRUCIBLE_BENCH_CHECK(plan.rank == -1 && plan.world_size == 0 && plan.device_capability == 0);
}

void check_cpu_slot(const TensorSlot& slot) {
    CRUCIBLE_BENCH_CHECK(slot.device_type == DeviceType::CPU && slot.device_idx == -1);
    CRUCIBLE_BENCH_CHECK(slot.layout == Layout::Strided);
}

// ── Reusable BenchRegion for isolated engine/context/region-cache runs ──
//
// Build a full RegionNode with valid TensorMetas, slot IDs, and a
// MemoryPlan. compute_content_hash reads input_metas/output_metas, so
// we allocate those too — even though the pointers we write through
// fake_ptr don't point at real storage.
//
// The ops, their arrays, the region and the slots are in dag_arena.  The
// plan is in arena.  The region has one op for each schema hash, and the
// shape hashes have the same count.
struct BenchRegion {
    ::foundation::effects::Test test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<DagArena> dag_arena =
        ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16});
    DagRef<RegionNode> region;

    BenchRegion(::fixy::View<const SchemaHash> schemas, ::fixy::View<const ShapeHash> shapes)
        : region{make_ops_region_(schemas, shapes)} {
        check_single_cpu_plan(*region->plan.expect("the region of the bench has a plan"));
    }

private:
    // The region of one op for each pair of hashes, with a plan of one slot
    // for each op.  Op i reads the output of op i - 1.
    [[nodiscard]] DagRef<RegionNode> make_ops_region_(::fixy::View<const SchemaHash> schemas,
                                                      ::fixy::View<const ShapeHash> shapes) {
        CRUCIBLE_BENCH_CHECK(schemas.size() == shapes.size());
        // One frame on dag_arena builds the ops, their arrays, the plan, its
        // slots and the region.
        return ::foundation::core::with_arena_frame(
            dag_arena, [alloc = test.alloc, schemas, shapes](auto& frame) noexcept {
                const auto n_ops = static_cast<uint32_t>(schemas.size());
                auto ops_run = make_op_run(alloc, frame, n_ops);
                // A plain View of an arena run: the View carries no brand of its arena.
                const ::fixy::View<TraceEntry> ops = ops_run.view();
                for (uint32_t i = 0; i < n_ops; i++) {
                    TraceEntry& op = *ops.get(i).expect("i is below n_ops");
                    op.schema_hash = *schemas.get(i).expect("i is below the count of the schema hashes");
                    op.shape_hash = *shapes.get(i).expect("i is below the count of the shape hashes");
                    TensorMeta meta{};
                    meta.ndim = 1;
                    meta.sizes[0] = tensor_dim(1024);
                    meta.strides[0] = tensor_dim(1);
                    meta.dtype = ScalarType::Float;
                    const auto holder = hold_op(ops_run, i);
                    if (i > 0) {
                        const auto input_metas_run = frame.array(alloc, 1, meta);
                        const auto producers_run = frame.array(alloc, 1, OpIndex{});
                        const auto input_slots_run = frame.array(alloc, 1, SlotId{i - 1});
                        TraceEntry::set_inputs(frame, holder, input_metas_run, producers_run, input_slots_run);
                    }
                    const auto output_metas_run = frame.array(alloc, 1, meta);
                    const auto output_slots_run = frame.array(alloc, 1, SlotId{i});
                    TraceEntry::set_outputs(frame, holder, output_metas_run, output_slots_run);
                }
                const auto made = make_region(alloc, frame, ops_run);

                auto slots_run = frame.template array<TensorSlot>(alloc, n_ops, TensorSlot{});
                uint32_t position = 0;
                for (TensorSlot& slot : slots_run.view()) {
                    slot.offset_bytes = position * 4096;
                    slot.nbytes = 4096;
                    slot.slot_id = SlotId{position};
                    slot.birth_op = OpIndex{position};
                    slot.death_op = OpIndex{position < n_ops - 1 ? position + 1 : position};
                    slot.dtype = ScalarType::Float;
                    slot.is_external = false;
                    check_cpu_slot(slot);
                    position++;
                }
                const auto plan = frame.template make<MemoryPlan>(alloc);
                frame.store(plan, &MemoryPlan::slots, slots_run);
                plan->num_external = 0;
                plan->pool_bytes = n_ops * 4096;
                frame.store(made, &RegionNode::plan, plan);
                return made.weaken();
            });
    }
};

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();
    const bench::JsonTail json = bench::env_json();

    bench::print("=== dispatch ===\n");
    bench::print("  sizeof(Vigil)            : {} B\n", sizeof(Vigil));
    bench::print("  sizeof(CrucibleContext)  : {} B\n", sizeof(CrucibleContext));
    bench::print("  sizeof(ReplayEngine)     : {} B\n", sizeof(ReplayEngine));
    bench::print("  sizeof(TraceRing::Entry) : {} B\n", sizeof(TraceRing::Entry));
    bench::print("  sizeof(TensorMeta)       : {} B\n\n", sizeof(TensorMeta));

    // The 20 rows of this function.
    ::fixy::FixedList<bench::Report, 20> reports{};

    // ── Sub-component baselines ────────────────────────────────────
    bench::print("--- sub-component baselines ---\n");

    bench::add_report(reports, [&] {
        // A TraceRing is about 5.3 MB, and the stack is 8 MB.  A second
        // large local in a sibling IIFE, or the frame of main when GCC inlines
        // each IIFE into it at -O3, goes past the guard page of the stack.
        // So the ring lives on the heap, as it does in Vigil.  The timed body
        // reads the ring through a reference that the Box gives one time, so
        // no test of the Box is in the timed loop.
        ::fixy::Box<crucible::test::OpenRing> ring_box =
            ::fixy::mint_box<crucible::test::OpenRing>(::foundation::effects::testing::test().alloc);
        crucible::test::OpenRing& ring = *ring_box;
        TraceRing::Entry entry{};
        entry.schema_hash = SchemaHash{0x100};
        entry.shape_hash = ShapeHash{0x200};
        entry.num_inputs = 1;
        entry.num_outputs = 1;
        auto r = bench::run("TraceRing::try_append (raw, reset-on-full)", [&] {
            if (!ring.try_append(entry)) [[unlikely]]
                ring.reset();
            bench::do_not_optimize(ring);
        });
        return r;
    }());

    bench::add_report(reports, [&] {
        crucible::test::OpenLog meta_log{};
        ::fixy::FixedArray<TensorMeta, 2> metas{
            make_meta(fake_address(IterationNumber{0}, OpPosition{0})),
            make_meta(fake_address(IterationNumber{0}, OpPosition{1})),
        };
        return bench::run("MetaLog::try_append (2 metas)", [&] {
            bench::do_not_optimize(meta_log);
            bench::do_not_optimize(meta_log.try_append(metas.view()));
        });
    }());

    bench::add_report(reports, [&] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> arena =
            ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16});
        constexpr uint32_t NSLOTS = 16;
        // One frame on the arena builds the slots and the plan.
        const DagRef<MemoryPlan const> plan =
            ::foundation::core::with_arena_frame(arena, [alloc = test.alloc](auto& frame) noexcept {
                auto slots_run = frame.template array<TensorSlot>(alloc, NSLOTS, TensorSlot{});
                uint32_t i = 0;
                for (TensorSlot& slot : slots_run.view()) {
                    slot.offset_bytes = i * 256;
                    slot.nbytes = 256;
                    slot.slot_id = SlotId{i};
                    slot.birth_op = OpIndex{0};
                    slot.death_op = OpIndex{0};
                    slot.dtype = ScalarType::Float;
                    slot.is_external = false;
                    check_cpu_slot(slot);
                    i++;
                }
                const auto made = frame.template make<MemoryPlan>(alloc);
                frame.store(made, &MemoryPlan::slots, slots_run);
                made->num_external = 0;
                made->pool_bytes = NSLOTS * 256;
                return DagRef<MemoryPlan const>{made.weaken()};
            });
        check_single_cpu_plan(*plan);

        PoolAllocator pool;
        pool.init(::foundation::effects::testing::test().alloc, *plan);

        uint32_t slot_idx = 0;
        auto pv = pool.mint_initialized_view(kVigilForeground);
        auto r = bench::run("PoolAllocator::slot_ptr (cyclic, N=16)", [&] {
            bench::do_not_optimize(pool.slot_ptr(SlotId{slot_idx}, pv));
            slot_idx = (slot_idx + 1) & (NSLOTS - 1);
        });
        pool.destroy();
        return r;
    }());

    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil) == 0);
        return bench::run("Vigil::is_compiled() [true]", [&] {
            bench::do_not_optimize(vigil);
            bench::do_not_optimize(vigil.context().is_compiled());
        });
    }());

    // ── Isolated engine / context ──────────────────────────────────
    bench::print("\n--- isolated engine/context ---\n");

    bench::add_report(reports, [&] {
        BenchRegion br{SCHEMA.view(), SHAPE.view()};
        CrucibleContext engine_owner{br.test.alloc};
        auto context_key = crucible::mint_context_key(br.test);
        (void)engine_owner.activate(context_key, *br.region);
        ReplayEngine& engine = engine_owner.engine(br.test);
        auto av = engine.mint_active_view(kVigilForeground);

        // At the end of the cycle the engine starts the region again.
        OpSlot slot = ::fixy::index<0>();
        auto r = bench::run("ReplayEngine::advance (cyclic, 8 ops)", [&] {
            bench::do_not_optimize(engine);
            auto s = engine.advance(SCHEMA.at(slot), SHAPE.at(slot), av);
            bench::do_not_optimize(s);
            ::fixy::Option<OpSlot> next = OpSlot::check(slot.raw() + 1);
            if (next.is_none()) engine.reset(av);
            slot = std::move(next).value_or(OpSlot{::fixy::index<0>()});
        });
        return r;
    }());

    bench::add_report(reports, [&] {
        BenchRegion br{SCHEMA.view(), SHAPE.view()};
        CrucibleContext ctx{::foundation::effects::testing::test().alloc};
        auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
        (void)ctx.activate(context_key, *br.region);

        // Each op opens one compiled scope, as a dispatch of the Vigil does.  The
        // body gives the step to the sink, as the row of the engine does, and the
        // row sinks only the bool of the answer.  A sink of the answer itself
        // writes an Option of the step to memory for each op, and the row then
        // measures that copy and not the scope.
        OpSlot slot = ::fixy::index<0>();
        auto r = bench::run("CrucibleContext::advance (cyclic, 8 ops)", [&] {
            bench::do_not_optimize(ctx);
            const bool is_replayed = ctx.with_compiled(context_key,
                                                       [schema = SCHEMA.at(slot), shape = SHAPE.at(slot)](
                                                           crucible::CompiledScope& scope) noexcept {
                                                           auto step = scope.advance(schema, shape);
                                                           bench::do_not_optimize(step);
                                                       })
                                         .is_some();
            bench::do_not_optimize(is_replayed);
            slot = next_in_cycle(slot);
        });
        ctx.deactivate(context_key);
        return r;
    }());

    bench::add_report(reports, [&] {
        constexpr uint32_t BIG = 32;
        using BigSlot = ::fixy::Index<BIG>;
        ::fixy::FixedArray<SchemaHash, BIG> big_sch{};
        ::fixy::FixedArray<ShapeHash, BIG> big_shp{};
        for (BigSlot const i : ::fixy::IndexRange<BIG>{}) {
            big_sch.set(i, SchemaHash{0x1000 + i.raw()});
            big_shp.set(i, ShapeHash{0x2000 + i.raw()});
        }
        BenchRegion br{big_sch.view(), big_shp.view()};
        CrucibleContext ctx{::foundation::effects::testing::test().alloc};
        auto context_key = crucible::mint_context_key(::foundation::effects::testing::test());
        (void)ctx.activate(context_key, *br.region);

        // Each op opens one compiled scope, and the body sinks the step, as in
        // the row of 8 ops.
        BigSlot slot = ::fixy::index<0>();
        auto r = bench::run("CrucibleContext::advance (cyclic, 32 ops)", [&] {
            bench::do_not_optimize(ctx);
            const bool is_replayed = ctx.with_compiled(context_key,
                                                       [schema = big_sch.at(slot), shape = big_shp.at(slot)](
                                                           crucible::CompiledScope& scope) noexcept {
                                                           auto step = scope.advance(schema, shape);
                                                           bench::do_not_optimize(step);
                                                       })
                                         .is_some();
            bench::do_not_optimize(is_replayed);
            slot = next_in_cycle(slot);
        });
        ctx.deactivate(context_key);
        return r;
    }());

    // ── record_op path ─────────────────────────────────────────────
    bench::print("\n--- record_op path ---\n");

    // Each record case runs RECORD_BODIES bodies.  The two record_op cases
    // count the refusals of record_op, and a refusal ends the bench, because
    // the case then timed a full ring and not the append that its name says.
    // Each body gives the op a schema hash that no other body gives, so the
    // detector finds no iteration and the Vigil stays in the RECORD mode.
    // The seal of a copy of the entry with the hash is the one cost that this
    // adds.
    bench::add_report(reports, [&] {
        Vigil vigil;
        uint64_t refused = 0;
        uint64_t tick = 0;
        auto report = bench::Run("Vigil::record_op (0 metas, profiler hook)")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(bench::env_core())
                          .measure([&] {
                              const TraceRing::SealedEntry entry = seal_hook_entry(SchemaHash{0x1000000u + tick++});
                              const bool took = vigil.record_op(certify_synthetic_op(entry));
                              refused += static_cast<uint64_t>(!took);
                              bench::do_not_optimize(took);
                          });
        CRUCIBLE_BENCH_CHECK(refused == 0);
        return report;
    }());

    bench::add_report(reports, [&] {
        Vigil vigil;
        auto d = make_op(0, 1);
        uint64_t refused = 0;
        uint64_t tick = 0;
        auto report = bench::Run("Vigil::record_op (2 metas)")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(bench::env_core())
                          .measure([&] {
                              const TraceRing::SealedEntry entry = seal_body_entry(SchemaHash{0x1000000u + tick++});
                              const bool took = vigil.record_op(certify_synthetic_op(entry, d.records));
                              refused += static_cast<uint64_t>(!took);
                              bench::do_not_optimize(took);
                          });
        CRUCIBLE_BENCH_CHECK(refused == 0);
        return report;
    }());

    // ── dispatch_op RECORDING ──────────────────────────────────────
    bench::print("\n--- dispatch_op RECORDING ---\n");

    // The answer of dispatch_op does not tell a refusal of the ring.  The run
    // stays below the room of the ring, so no body meets a full ring.  The
    // count of RECORD answers shows that the Vigil stayed in the RECORD mode.
    bench::add_report(reports, [&] {
        Vigil vigil;
        auto d = make_op(0, 1);
        uint64_t recorded = 0;
        uint64_t tick = 0;
        auto report = bench::Run("dispatch_op [RECORDING, no pending]")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(bench::env_core())
                          .measure([&] {
                              const TraceRing::SealedEntry entry = seal_body_entry(SchemaHash{0x1000000u + tick++});
                              const auto r = vigil.dispatch_op(certify_synthetic_op(entry, d.records));
                              recorded += static_cast<uint64_t>(r.is_record());
                              bench::do_not_optimize(r);
                          });
        CRUCIBLE_BENCH_CHECK(recorded == tick);
        return report;
    }());

    // The C entry of crucible_mode.py: the checks of the arrays of the
    // caller, the fold of seven scalars, two of them past the five inline
    // slots, the decode of the two records and the dispatch.  Each body gives
    // the op a schema hash that no other body gives, so the Vigil stays in the
    // RECORD mode, and the run stays below the room of the ring.
    bench::add_report(reports, [&] {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the bench calls the C entries of the vessel with the arrays of a caller")
        CrucibleHandle handle = 0;
        CRUCIBLE_BENCH_CHECK(crucible_create(0, &handle) == CRUCIBLE_HANDLE_LIVE);
        ::fixy::FixedArray<CrucibleMeta, 2> records{};
        for (::fixy::Index<2> position : ::fixy::FixedArray<CrucibleMeta, 2>::indices()) {
            CrucibleMeta record{};
            record.sizes[0] = 4;
            record.strides[0] = 1;
            record.ndim = 1;
            record.dtype = static_cast<int8_t>(ScalarType::Float);
            record.device_idx = -1;
            record.flags = 0x02;
            record.storage_nbytes = 16;
            record.data_ptr = std::bit_cast<void*>(uintptr_t{0x10000} * (position.raw() + 1));
            records.set(position, record);
        }
        // lr, beta1, beta2, weight_decay and eps of a fused Adam step as data
        // doubles, then amsgrad and maximize as structural bools.
        const ::fixy::FixedArray<int64_t, 7> values{std::bit_cast<int64_t>(0.001),
                                                    std::bit_cast<int64_t>(0.9),
                                                    std::bit_cast<int64_t>(0.999),
                                                    std::bit_cast<int64_t>(0.0),
                                                    std::bit_cast<int64_t>(1e-8),
                                                    0,
                                                    1};
        const ::fixy::FixedArray<uint8_t, 7> tags{1, 1, 1, 1, 1, 2, 2};
        const ::fixy::FixedArray<uint8_t, 7> roles{1, 1, 1, 1, 1, 0, 0};
        const CrucibleMeta* const metas = &records.at(::fixy::index<0>());
        const int64_t* const value_array = &values.at(::fixy::index<0>());
        const uint8_t* const tag_array = &tags.at(::fixy::index<0>());
        const uint8_t* const role_array = &roles.at(::fixy::index<0>());
        uint64_t recorded = 0;
        uint64_t tick = 0;
        auto report = bench::Run("crucible_dispatch_op_ex [RECORDING, 2 records, 7 scalars]")
                          .warmup(RECORD_WARMUP)
                          .samples(RECORD_SAMPLES)
                          .batch(RECORD_BATCH)
                          .core(bench::env_core())
                          .measure([&] {
                              const CrucibleDispatchResult r =
                                  crucible_dispatch_op_ex(handle, 0x1000000u + tick++, 1, 1, metas, 2, value_array,
                                                          tag_array, role_array, 7, op_flag::GRAD_ENABLED);
                              recorded += static_cast<uint64_t>(r.action == CRUCIBLE_ACTION_RECORD);
                              bench::do_not_optimize(r);
                          });
        CRUCIBLE_BENCH_CHECK(recorded == tick);
        CRUCIBLE_BENCH_CHECK(crucible_destroy(handle) == CRUCIBLE_HANDLE_LIVE);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        return report;
    }());

    // ── dispatch_op COMPILED ───────────────────────────────────────
    bench::print("\n--- dispatch_op COMPILED ---\n");

    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil) == 0);
        Ops ops = make_compiled_ops();
        const Cycle cycle = cycle_of(ops);

        OpSlot slot = ::fixy::index<0>();
        return bench::run("dispatch_op [COMPILED, cyclic]", [&, cycle] {
            const OpData& d = cycle.at(slot);
            auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
            bench::do_not_optimize(r);
            slot = next_in_cycle(slot);
        });
    }());

    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil) == 0);
        Ops ops = make_compiled_ops();
        const Cycle cycle = cycle_of(ops);

        // The claim is checked once here, as the adapter holds it for a whole
        // window, so the loop measures the pointer read and not the claim.
        const VigilFgCtx fg = vigil.mint_producer_context();
        OpSlot slot = ::fixy::index<0>();
        return bench::run("dispatch_op [COMPILED + output_ptr]", [&, cycle] {
            const OpData& d = cycle.at(slot);
            auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
            if (r.action == DispatchResult::Action::COMPILED)
                bench::do_not_optimize(vigil.output_ptr(fg, crucible::OutputPosition{0}));
            bench::do_not_optimize(r);
            slot = next_in_cycle(slot);
        });
    }());

    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil) == 0);
        Ops ops = make_compiled_ops();
        const Cycle cycle = cycle_of(ops);

        // The position of the body runs over the ops before the last op.  At
        // the end of that run the body dispatches the last op too.
        using LeadSlot = ::fixy::Index<NUM_OPS - 1>;
        LeadSlot slot = ::fixy::index<0>();
        return bench::run("dispatch_op [COMPILED, MATCH only]", [&, cycle] {
            const OpData& d = cycle.at(slot);
            auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
            bench::do_not_optimize(r);
            ::fixy::Option<LeadSlot> next = LeadSlot::check(slot.raw() + 1);
            if (next.is_none()) {
                const OpData& last = cycle.at(::fixy::index<NUM_OPS - 1>());
                auto rl = vigil.dispatch_op(certify_synthetic_op(last.entry, last.records));
                bench::do_not_optimize(rl);
            }
            slot = std::move(next).value_or(LeadSlot{::fixy::index<0>()});
        });
    }());

    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil) == 0);
        Ops ops = make_compiled_ops();
        const Cycle cycle = cycle_of(ops);

        return bench::run("dispatch_op [full 8-op iteration]", [&, cycle] {
            for (const OpData& d : cycle) {
                auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
                bench::do_not_optimize(r);
            }
        });
    }());

    // Each body dispatches the op after the matched position of the last
    // result, so each body waits for that position.  The row times the path
    // of the position from the guard to the result.
    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil) == 0);
        Ops ops = make_compiled_ops();
        const Cycle cycle = cycle_of(ops);

        OpSlot next = ::fixy::index<0>();
        return bench::run("dispatch_op [COMPILED, chained position]", [&, cycle] {
            const OpData& d = cycle.at(next);
            const DispatchResult r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
            next = OpSlot::check((r.op_index.raw() + 1) % NUM_OPS).expect("the remainder is below NUM_OPS");
        });
    }());

    // The replay of a model binds the parameters of each op to the storage of
    // the live op.  In the two rows that follow, each op after the first one
    // reads one parameter, so the replay binds one external slot for it.  The
    // region has one external slot for each parameter.
    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil, InputSource::Parameter) == 0);
        CRUCIBLE_BENCH_CHECK(count_external_slots(vigil) == NUM_OPS - 1);
        Ops ops = make_compiled_ops(InputSource::Parameter);
        const Cycle cycle = cycle_of(ops);

        OpSlot slot = ::fixy::index<0>();
        return bench::run("dispatch_op [COMPILED, external reads]", [&, cycle] {
            const OpData& d = cycle.at(slot);
            auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
            bench::do_not_optimize(r);
            slot = next_in_cycle(slot);
        });
    }());

    bench::add_report(reports, [&] {
        Vigil vigil;
        CRUCIBLE_BENCH_CHECK(setup_compiled_vigil(vigil, InputSource::Parameter) == 0);
        CRUCIBLE_BENCH_CHECK(count_external_slots(vigil) == NUM_OPS - 1);
        Ops ops = make_compiled_ops(InputSource::Parameter);
        const Cycle cycle = cycle_of(ops);

        return bench::run("dispatch_op [8-op iteration, external reads]", [&, cycle] {
            for (const OpData& d : cycle) {
                auto r = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
                bench::do_not_optimize(r);
            }
        });
    }());

    // ── Cold path ──────────────────────────────────────────────────
    bench::print("\n--- cold paths ---\n");

    // Divergence is single-shot per sample: each call destroys compiled
    // state. The harness auto-batch won't help; a fresh compiled Vigil
    // is built inside the body every sample. The Report thus measures
    // *end-to-end* cost (setup + advance + divergence), not divergence
    // alone — label makes that explicit.
    //
    // The body counts the results that are not the expected ones, and the
    // check after the measurement reads the count, so no check runs in the
    // timed body.
    bench::add_report(reports, [&] {
        bench::Run r{"divergence [end-to-end: setup + dispatch]"};
        if (const int c = bench::env_core(); c >= 0) (void)r.core(c);
        uint32_t unexpected_results = 0;
        auto report = r.samples(100).warmup(2).batch(1).measure([&] {
            Vigil vigil;
            unexpected_results += setup_compiled_vigil(vigil);
            for (uint32_t i = 0; i < 3; i++) {
                auto d = make_op(10, i);
                const auto rr = vigil.dispatch_op(certify_synthetic_op(d.entry, d.records));
                unexpected_results += static_cast<uint32_t>(rr.action != DispatchResult::Action::COMPILED);
            }
            const TraceRing::SealedEntry bad{::fixy::build_sealed, []() noexcept {
                                                 TraceRing::Entry built{};
                                                 built.schema_hash = SchemaHash{0xBAD};
                                                 built.shape_hash = SHAPE.at(::fixy::index<3>());
                                                 built.num_inputs = 1;
                                                 built.num_outputs = 1;
                                                 return built;
                                             }};
            ::fixy::FixedList<TensorMeta, 2> bad_list{};
            (void)bad_list.append(make_meta(fake_address(IterationNumber{10}, OpPosition{2})))
                .expect("the list holds the two records of an op");
            (void)bad_list.append(make_meta(fake_address(IterationNumber{10}, OpPosition{3})))
                .expect("the list holds the two records of an op");
            const SealedRecords<2> bad_metas{std::move(bad_list)};
            auto rr = vigil.dispatch_op(certify_synthetic_op(bad, bad_metas));
            bench::do_not_optimize(rr);
            unexpected_results += static_cast<uint32_t>(rr.action != DispatchResult::Action::RECORD);
            unexpected_results += static_cast<uint32_t>(rr.status != ReplayStatus::DIVERGED);
        });
        CRUCIBLE_BENCH_CHECK(unexpected_results == 0);
        return report;
    }());

    bench::add_report(reports, [&] {
        auto test = ::foundation::effects::testing::test();
        ::fixy::BumpArena<DagArena> arena =
            ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 16});
        constexpr uint32_t NR = 4;
        ::fixy::FixedList<DagRef<RegionNode>, NR> regions{};
        for (uint32_t r = 0; r < NR; r++) {
            // One frame on the arena builds the ops, the region and its plan.
            const DagRef<RegionNode> region =
                ::foundation::core::with_arena_frame(arena, [alloc = test.alloc, r](auto& frame) noexcept {
                    auto ops_run = make_op_run(alloc, frame, NUM_OPS);
                    // A plain View of an arena run: the View carries no brand of its arena.
                    const ::fixy::View<TraceEntry> ops = ops_run.view();
                    for (OpSlot const i : ::fixy::IndexRange<NUM_OPS>{}) {
                        TraceEntry& op = *ops.get(i.raw()).expect("i is below NUM_OPS");
                        op.schema_hash = SCHEMA.at(i);
                        op.shape_hash = ShapeHash{SHAPE.at(i).raw() + r * 0x100};
                    }
                    auto const made = make_region(alloc, frame, ops_run);
                    // A plan of no slot: a new plan holds the empty run.
                    auto const plan = frame.template make<MemoryPlan>(alloc);
                    plan->pool_bytes = 0;
                    check_single_cpu_plan(*plan);
                    frame.store(made, &RegionNode::plan, plan);
                    return made.weaken();
                });
            static_cast<void>(regions.append(region).expect("the list holds NR regions"));
        }
        RegionCache cache;
        for (DagRef<RegionNode> const region : regions)
            cache.insert(*region);

        SchemaHash tgt_schema = SCHEMA.at(::fixy::index<3>());
        ShapeHash tgt_shape{SHAPE.at(::fixy::index<3>()).raw() + 2 * 0x100};
        RegionNode const& first_region = **regions.get(0).expect("the list holds the first region");
        const crucible::ConstRegionLink excluded = crucible::ConstRegionLink::some(::fixy::to(first_region));
        return bench::run("RegionCache::find_alternate (4 entries)", [&] {
            auto found = cache.find_alternate(3, tgt_schema, tgt_shape, excluded);
            bench::do_not_optimize(found);
        });
    }());

    bench::emit_reports_text(reports.view());

    // ── compare: dispatch_op RECORDING vs record_op (overhead) ─────
    //
    // dispatch_op [RECORDING] does everything record_op does, plus:
    //   - ctx_.is_compiled() check (false)
    //   - pending_region_.load(acquire) + pending_activation_ branch
    //   - return DispatchResult instead of bool
    // The Δ is the dispatch wrapper's overhead on the RECORDING path.
    const size_t ri_rec = 8;  // Vigil::record_op (2 metas)
    const size_t ri_dis = 9;  // dispatch_op [RECORDING, no pending]
    bench::print("\n=== compare — dispatch_op vs record_op (RECORDING) ===\n  ");
    bench::compare(*reports.get(ri_rec).expect("the row of Vigil::record_op"),
                   *reports.get(ri_dis).expect("the row of dispatch_op in RECORDING mode"))
        .print_text();

    // COMPILED cyclic vs full-iteration (per-op amortized):
    // the full-iteration Report divides its total by 8 (roughly).
    bench::print("\n=== compare — COMPILED cyclic vs full-iteration ===\n  ");
    bench::compare(*reports.get(10).expect("the row of the COMPILED cyclic dispatch"),
                   *reports.get(13).expect("the row of the full iteration"))
        .print_text();

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
