// Cipher store/load round-trip — filesystem-backed persistence path.
//
// store() hits disk idempotently (one write per content_hash, skipped
// thereafter). load() opens + reads + deserializes. Bench measures
// cold-write, warm-write (dedup skip), unified-log record_event, and
// cold-read paths at two region sizes (16 and 256 ops).
//
// Each sample creates a fresh Arena where required; the Cipher opens
// a tmp dir that's cleaned on exit. Run is registered with BPF — the
// file-I/O surface makes the `disk_r / disk_w / pg_miss` counters
// informative even when paths are cache-hot.

#include <cstdint>
#include <utility>

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <fixy/Ctx.h>
#include <fixy/os/Fs.h>

#include "bench_harness.h"

// Opening the store and its open view need a context whose row admits IO
// and Block.  The context has the background source, as the store of a
// product has.  A context of the test source would time the write of the
// write fault of a test of fixy/os/Fs.h in place of the product write.
[[nodiscard]] inline ::fixy::BgLoadCtx store_ctx() { return ::fixy::BgLoadCtx{::foundation::effects::testing::bg()}; }

using namespace crucible;

static const auto BG = ::foundation::effects::testing::bg();
static constexpr auto A = BG.alloc;

// The block size of each arena of the bench.
static constexpr ::fixy::Quantity<::fixy::unit::Byte, size_t> kArenaBlock{size_t{1} << 18};

// The ops, their arrays and the region are in dag_arena, and one frame on it
// builds them.
static DagRef<RegionNode> synth_region(::fixy::BumpArena<DagArena>& dag_arena, uint32_t num_ops, uint64_t salt) {
    TensorMeta meta{};
    meta.set_shape(vector_tensor_shape(tensor_dim(64)));
    meta.dtype = ScalarType::Float;
    return ::foundation::core::with_arena_frame(dag_arena, [num_ops, salt, &meta](auto& frame) noexcept {
        auto ops_run = make_op_run(A, frame, num_ops);
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();
        for (uint32_t i = 0; i < num_ops; i++) {
            auto& te = *ops.get(i).expect("i is below num_ops");
            te.schema_hash = SchemaHash{salt ^ (0xAAAAULL + i)};
            te.shape_hash = ShapeHash{salt ^ (0xBBBBULL + i)};
            const auto input_metas_run = frame.array(A, 1, meta);
            const auto producers_run = frame.array(A, 1, OpIndex{});
            const auto input_slots_run = frame.array(A, 1, SlotId{});
            const auto op = hold_op(ops_run, i);
            TraceEntry::set_inputs(frame, op, input_metas_run, producers_run, input_slots_run);
            const auto output_metas_run = frame.array(A, 1, meta);
            const auto output_slots_run = frame.array(A, 1, SlotId{});
            TraceEntry::set_outputs(frame, op, output_metas_run, output_slots_run);
        }
        return make_region(A, frame, ops_run).weaken();
    });
}

// The store serializes the producer index and the slot ids of each op, and
// the bench does not set them.  Each one keeps the none() of its default
// constructor, so the stored bytes do not depend on the arena.
static void check_unset_ids(const RegionNode& region) {
    for (const TraceEntry& op_record : region.ops.view()) {
        for (const OpIndex producer : op_record.input_trace_indices())
            CRUCIBLE_BENCH_CHECK(!producer.is_valid());
        for (const SlotId slot_id : op_record.input_slot_ids())
            CRUCIBLE_BENCH_CHECK(!slot_id.is_valid());
        for (const SlotId slot_id : op_record.output_slot_ids())
            CRUCIBLE_BENCH_CHECK(!slot_id.is_valid());
    }
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    // Isolated tmp dir per run.  Its owner removes the tree at its end.
    using ::fixy::fs::TemporaryDirectory;
    TemporaryDirectory scratch = ::fixy::fs::mint_temporary_directory(store_ctx())
                                     .match(
                                         [](::fixy::Linear<TemporaryDirectory> made) noexcept -> TemporaryDirectory {
                                             return std::move(made).consume();
                                         },
                                         [](::foundation::os::Errno error) noexcept -> TemporaryDirectory {
                                             ::fixy::report(::fixy::Sink::Err,
                                                            "bench_cipher: the temporary directory does not open "
                                                            "(error {})\n",
                                                            ::foundation::os::error_number(error));
                                             return TemporaryDirectory{};
                                         });
    if (!scratch.is_open()) return 1;
    // A copy of the path, which the last line prints after the end of the
    // owner.
    ::foundation::core::GrowText dir_copy = ::foundation::core::mint_grow_text(Cipher::MAX_ROOT_PATH_BYTES);
    for (char const character : scratch.path().view()) {
        dir_copy.append(A, character).expect("the text holds each character of the directory");
    }
    ::foundation::core::HeapText const dir_text = ::foundation::core::freeze(std::move(dir_copy));
    const ::fixy::TextView dir = dir_text.view();
    bench::print("=== cipher ===\n  tmpdir: {}\n\n", dir);

    // 6 Runs × 2 region sizes = 12 Reports. Each (num_ops) scope holds
    // its Cipher + warm-region fixture across the per-size lambdas.
    ::fixy::FixedList<bench::Report, 12> reports{};
    std::size_t idx_256_cold = 0;
    std::size_t idx_256_warm = 0;
    std::size_t idx_256_ca_warm = 0;
    std::size_t idx_256_record_event = 0;

    ::fixy::FixedArray<uint32_t, 2> const op_counts{16u, 256u};
    for (uint32_t const num_ops : op_counts.view()) {
        // Cipher + warm_region live in THIS scope, shared across the
        // three benches for this num_ops.  Distinct 'salt' per size
        // avoids collisions in the dedup hash space.
        const auto ctx = store_ctx();
        // The text of the root copies the characters of the directory.
        ::foundation::core::GrowText root = ::foundation::core::mint_grow_text(Cipher::MAX_ROOT_PATH_BYTES);
        for (char const character : dir) {
            root.append(A, character).expect("the text holds each character of the directory");
        }
        ::foundation::core::HeapText const root_path = ::foundation::core::freeze(std::move(root));
        auto cipher = Cipher::open(ctx, A, ::fixy::mint_tagged<::fixy::tags::source::External>(root_path.view()))
                          .expect("the bench opens its store");
        auto open_view = cipher.mint_open_view(ctx);

        ::fixy::BumpArena<DagArena> warm_dag_arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
        const RegionNode& warm_region = *synth_region(warm_dag_arena, num_ops, 0xC0FFEE + num_ops);
        check_unset_ids(warm_region);
        const auto warm_payload = Cipher::content_addressed(::fixy::to(warm_region));
        (void)cipher.store(ctx, open_view, warm_payload);  // prime the warm path
        const ContentHash warm_h = warm_region.content_hash;

        // Cold write: distinct content_hash per iteration, so every
        // sample hits the filesystem. Auto-batch is fine: each body
        // allocates its own arena + region, so state doesn't grow
        // unboundedly across batches.
        const bench::RunName label_cold = bench::format_name("store cold  ({} ops, distinct)", num_ops);
        const bench::RunName label_warm = bench::format_name("store warm  ({} ops, dedup hit)", num_ops);
        const bench::RunName label_ca_warm = bench::format_name("store CA    ({} ops, dedup hit)", num_ops);
        const bench::RunName label_read = bench::format_name("load  cold  ({} ops)", num_ops);
        const bench::RunName label_ca_read = bench::format_name("load  CA    ({} ops, cache hit)", num_ops);
        const bench::RunName label_record_event = bench::format_name("record event ({} ops, unified log)", num_ops);

        const std::size_t base_idx = reports.size();
        uint64_t record_step = static_cast<uint64_t>(num_ops) << 32;

        bench::add_report(reports, [&] {
            uint64_t salt = static_cast<uint64_t>(num_ops) << 32;
            return bench::run(label_cold, [&] {
                ::fixy::BumpArena<DagArena> region_dag_arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
                auto h = cipher.store(ctx, open_view,
                                      Cipher::content_addressed(synth_region(region_dag_arena, num_ops, ++salt)));
                bench::do_not_optimize(h);
            });
        }());

        bench::add_report(reports, bench::run(label_warm, [&] {
                              auto h = cipher.store(ctx, open_view, warm_payload);
                              bench::do_not_optimize(h);
                          }));

        bench::add_report(reports, bench::run(label_ca_warm, [&] {
                              auto h = cipher.store(ctx, open_view, warm_payload);
                              bench::do_not_optimize(h);
                          }));

        bench::add_report(reports, bench::run(label_read, [&] {
                              ::fixy::BumpArena<DagArena> read_arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
                              auto r = cipher.load_content_addressed(ctx, open_view, A, warm_h, read_arena);
                              const bool is_loaded = r.is_ok();
                              bench::do_not_optimize(is_loaded);
                          }));

        bench::add_report(reports, bench::run(label_ca_read, [&] {
                              ::fixy::BumpArena<DagArena> read_arena = ::fixy::mint_bump_arena<DagArena>(kArenaBlock);
                              auto r = cipher.load_content_addressed(ctx, open_view, A, warm_h, read_arena);
                              const bool is_cache_hit =
                                  r.match([](auto const& loaded) noexcept { return loaded.cache_hit(); },
                                          [](LoadError) noexcept { return false; });
                              bench::do_not_optimize(r.is_ok());
                              bench::do_not_optimize(is_cache_hit);
                          }));

        bench::add_report(reports, bench::run(label_record_event, [&] {
                              const uint64_t step = ++record_step;
                              const ContentHash event_hash{warm_h.raw() ^ step};
                              const bool recorded = cipher.record_event(ctx, open_view, event_hash, step).is_ok();
                              bench::do_not_optimize(recorded);
                          }));

        if (num_ops == 256u) {
            idx_256_cold = base_idx;
            idx_256_warm = base_idx + 1;
            idx_256_ca_warm = base_idx + 2;
            idx_256_record_event = base_idx + 5;
        }
    }

    bench::emit_reports_text(reports.view());

    // warm vs. cold store at 256 ops — the dedup-skip path should be
    // orders of magnitude faster; filesystem::exists + compare vs. a
    // full write.
    const bench::Report& cold_256 = *reports.get(idx_256_cold).expect("the cold store row of 256 ops");
    const bench::Report& warm_256 = *reports.get(idx_256_warm).expect("the warm store row of 256 ops");
    bench::print("\n=== compare — store dedup win (256 ops) ===\n  ");
    bench::compare(cold_256, warm_256).print_text();

    bench::print("\n=== compare — ContentAddressed wrapper overhead (256 ops) ===\n  ");
    bench::compare(warm_256, *reports.get(idx_256_ca_warm).expect("the CA store row of 256 ops")).print_text();

    bench::print("\n=== compare — unified log write vs warm store (256 ops) ===\n  ");
    bench::compare(warm_256, *reports.get(idx_256_record_event).expect("the record row of 256 ops")).print_text();

    // The end of the owner removes the tree of the directory.
    { TemporaryDirectory const ended = std::move(scratch); }
    bench::print("\n{}: cleaned\n", dir);

    bench::emit_reports_json(reports.view(), json);
    return 0;
}
