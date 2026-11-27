// Serialize / deserialize round-trip bench for RegionNode (CDAG v7).
//
// Cipher::store round-trips through serialize_region; Cipher::load uses
// deserialize_region. These land on the persistence hot path during
// checkpoint / replay. Target: sub-microsecond for small regions,
// linear in op count for large ones.

#include <cstddef>
#include <cstdint>

#include <crucible/MerkleDag.h>
#include <crucible/Serialize.h>
#include <foundation/effects/Effect.h>

#include "bench_harness.h"

using namespace crucible;

static const auto BG = ::foundation::effects::testing::bg();
static constexpr auto A = BG.alloc;

// Avoids collision with crucible::make_region — this one builds a region
// populated with realistic (schema/shape/scope/callsite, 2-input GEMM)
// op rows.  The ops, their arrays and the region are in dag_arena, and one
// frame on it builds them.
static DagRef<RegionNode> synth_region(::fixy::BumpArena<DagArena>& dag_arena, uint32_t num_ops) {
    // Each input and the output of each op have one metadata.
    TensorMeta meta{};
    const ::fixy::FixedArray<TensorDim, 2> dims{tensor_dim(128), tensor_dim(256)};
    meta.set_shape(contiguous_tensor_shape(TensorSizes{dims.view()}).expect("a contiguous shape of rank 2"));
    meta.dtype = ScalarType::Float;

    return ::foundation::core::with_arena_frame(dag_arena, [num_ops, &meta](auto& frame) noexcept {
        auto ops_run = make_op_run(A, frame, num_ops);
        // A plain View of an arena run: the View carries no brand of its arena.
        const ::fixy::View<TraceEntry> ops = ops_run.view();

        for (uint32_t i = 0; i < num_ops; i++) {
            auto& te = *ops.get(i).expect("i is below num_ops");
            te.schema_hash = SchemaHash{0xAAAA000000000000ULL + i};
            te.shape_hash = ShapeHash{0xBBBB000000000000ULL + i};
            te.scope_hash = ScopeHash{0xCCCC000000000000ULL + i};
            te.callsite_hash = CallsiteHash{0xDDDD000000000000ULL + i};
            te.kernel_id = CKernelId::GEMM_MM;

            auto producers_run = frame.array(A, 2, OpIndex::none());
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<OpIndex> producers = producers_run.view();
            if (i >= 1) *producers.get(0).expect("the op has two inputs") = OpIndex{i - 1};
            auto input_slots_run = frame.array(A, 2, SlotId{i});
            // A plain View of an arena run: the View carries no brand of its arena.
            const ::fixy::View<SlotId> input_slots = input_slots_run.view();
            *input_slots.get(1).expect("the op has two inputs") = SlotId{100 + i};
            const auto input_metas_run = frame.array(A, 2, meta);
            const auto op = hold_op(ops_run, i);
            TraceEntry::set_inputs(frame, op, input_metas_run, producers_run, input_slots_run);
            const auto output_metas_run = frame.array(A, 1, meta);
            const auto output_slots_run = frame.array(A, 1, SlotId{200 + i});
            TraceEntry::set_outputs(frame, op, output_metas_run, output_slots_run);
        }

        return make_region(A, frame, ops_run).weaken();
    });
}

// The serialize of a region through a call that GCC cannot see into.  The
// write of an image is cold code of the product by design (CRUCIBLE_COLD in
// crucible/Serialize.h), so a direct call of serialize_region is a cold call.
// After such a call in main, GCC takes the rows for cold code, and a timed
// body of only such a call is cold code too (the print doors of
// bench/bench_harness.h).  The call of a noipa function can end its block,
// so the rows and the timed body around it stay in hot code.  A reborrow
// lends the run of image to the serialize, and the run comes back to image,
// so each call writes the same run.  The body holds the region through a
// const borrow, so it writes no borrow of its frame into the region.
template <class Brand, std::size_t Extent>
[[gnu::noipa]] static size_t serialize_through_call(DagRef<RegionNode const> region,
                                                    ::fixy::ViewMut<std::byte, Brand, Extent>& image) {
    auto [written, back] = ::fixy::reborrow(std::move(image), [region](auto lent) noexcept {
        return serialize_region(::fixy::to(*region), std::move(lent));
    });
    image = std::move(back);
    return written;
}

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    bench::print("=== serialize ===\n\n");

    // Two rows for each of the three sizes.
    ::fixy::FixedList<bench::Report, 6> reports{};

    constexpr ::fixy::FixedArray<uint32_t, 3> kSizes{16u, 128u, 1024u};
    for (uint32_t const num_ops : kSizes) {
        // Per-size fixture: one arena, one region, one wire buffer sized
        // generously for the worst-case row shape. Lives across both
        // (ser, deser) Runs.
        ::fixy::BumpArena<DagArena> dag_arena =
            ::fixy::mint_bump_arena<DagArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
        // The bodies below read the region through a Ref of const, so a body
        // reaches no part of the region that it can write.
        const DagRef<RegionNode const> region = synth_region(dag_arena, num_ops);

        const size_t wire_cap =
            64 + num_ops * (40 + 2 * sizeof(TensorMeta) + sizeof(TensorMeta) + 3 * 3 * sizeof(uint32_t));
        ::fixy::HeapArray<std::byte> blank = ::fixy::mint_heap_array(A, wire_cap, std::byte{0});

        const bench::RunName label_ser = bench::format_name("serialize_region   ({} ops)", ::fixy::Padded<4>{num_ops});
        const bench::RunName label_des = bench::format_name("deserialize_region ({} ops)", ::fixy::Padded<4>{num_ops});

        // One frame of the buffer holds the row of serialize_region and the
        // write that primes the buffer, outside the timed region.
        auto [serial_len, buf] =
            ::fixy::with_mut(std::move(blank), [&reports, &label_ser, &region](auto wire) noexcept {
                bench::add_report(reports, bench::run(label_ser, [&] {
                                      size_t n = serialize_through_call(region, wire);
                                      bench::do_not_optimize(n);
                                  }));

                // Prime the buffer once — deserialize consumes stable bytes.
                return serialize_through_call(region, wire);
            });
        if (serial_len == 0) {
            ::fixy::fatal("serialize_region wrote no byte for a region of {} ops", num_ops);
        }
        const ::fixy::View<const std::byte> image =
            buf.view().window(0, serial_len).expect("the writer writes inside its buffer");

        bench::add_report(reports, bench::run(label_des, [&, image] {
                              ::fixy::BumpArena<DagArena> read_arena = ::fixy::mint_bump_arena<DagArena>(
                                  ::fixy::Quantity<::fixy::unit::Byte, size_t>{size_t{1} << 20});
                              const ::fixy::Result<LoadedRegionNode, LoadError> loaded_region =
                                  deserialize_region(A, image, read_arena);
                              const bool is_loaded = loaded_region.is_ok();
                              bench::do_not_optimize(is_loaded);
                          }));

        bench::print("  [{} ops] wire = {} bytes ({} B/op)\n", num_ops, serial_len,
                     ::fixy::Fixed<1>{static_cast<double>(serial_len) / num_ops});
    }

    bench::print("\n");
    bench::emit_reports(reports.view(), json);
    return 0;
}
