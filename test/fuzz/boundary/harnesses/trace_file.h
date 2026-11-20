#pragma once

// A .crtrace file.  The loader either refuses the file or returns a trace
// whose descriptors are in range and whose per-operation metadata runs lie
// inside the metadata array.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include <crucible/TraceLoader.h>
#include <fixy/Ctx.h>
#include <fixy/os/Path.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

// One operation with one input and one output, two metadata records, and a
// name table that gives one hash the name aten::x and then aten::y.  The
// first name registers, and the second name reaches the refusal of a
// collision in the schema table.  The seed has the version that the loader
// reads, so the fuzzer starts from a trace that loads.  The header, the
// record and the metadata come from the encoders of the writer.
[[nodiscard]] inline SeedList seeds_trace_file() noexcept {
    SeedWriter seed{};
    const auto header = encode_trace_header(1, 2);
    seed.append_bytes(header.view());
    TraceOpRecord record{};
    record.schema_hash = SchemaHash{0xABCD};
    record.num_inputs = 1;
    record.num_outputs = 1;
    record.depth = OpDepth{1};
    const auto op = encode_trace_op(record);
    seed.append_bytes(op.view());
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = tensor_dim(4);
    meta.strides[0] = tensor_dim(1);
    meta.dtype = ScalarType::Float;
    const auto meta_bytes = encode_trace_meta(meta);
    seed.append_bytes(meta_bytes.view());
    seed.append_bytes(meta_bytes.view());
    seed.append_raw(std::uint32_t{2});
    seed.append_raw(std::uint64_t{0xABCD});
    seed.append_raw(std::uint16_t{7});
    seed.append_text("aten::x");
    seed.append_raw(std::uint64_t{0xABCD});
    seed.append_raw(std::uint16_t{7});
    seed.append_text("aten::y");
    SeedList seeds = empty_seed_list();
    add_seed(seeds, seed.finish());
    return seeds;
}

inline void run_trace_file(::fixy::View<std::byte const> bytes) noexcept {
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    const ::fixy::View<std::byte const> image =
        bytes.size() > kMaxInputBytes ? bytes.window(0, kMaxInputBytes).expect("the cap is inside the input") : bytes;

    // The scratch path is absolute and holds no ".." component.
    ::foundation::os::PathText<::fixy::kPathBytes> input{};
    if (input.append(scratch_dir().path()).is_err() || input.append(::fixy::TextView{"/input.crtrace"}).is_err())
        return;
    auto sanitized_path =
        ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(input.c_text().view()));
    if (sanitized_path.is_err()) return;
    const ::fixy::Path<::fixy::tags::source::Sanitized> path =
        std::move(sanitized_path).expect("the scratch path passes the check");
    // The descriptor closes in the write, before the load.
    if (!write_scratch_file("input.crtrace", image)) return;
    // The load registers the names of the trace into the global table, so a
    // second name of one hash reaches the refusal of a collision.  The table
    // starts empty at each run, so no name of an earlier input collides,
    // and the names of one trace fit, because the loader bounds their count
    // at the capacity of the table.
    constexpr VigilFgCtx kForeground = ::foundation::effects::testing::foreground<Vigil>();
    global_schema_table().clear(::foundation::effects::testing::test());
    auto loaded = load_trace(kHarnessCtx, kForeground, path);
    if (loaded.is_err()) return;
    const LoadedTrace trace = std::move(loaded).expect("the load holds a trace after the check of its error");

    CRUCIBLE_FUZZ_CLAIM("trace_file", global_schema_table().count() <= SCHEMA_TABLE_CAP);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.metas.size() == trace.num_metas);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.entries.size() == trace.num_ops);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.depths.size() == trace.num_ops);
    for (const TensorMeta& meta : trace.metas)
        claim_meta_in_range(as_harness("trace_file"), meta);
    for (const auto [entry, meta_start] : trace.entries.view()
                                              .walk()
                                              .try_zip(trace.meta_starts.view().walk())
                                              .expect("the arrays of a trace are parallel")) {
        const std::uint32_t tensors = std::uint32_t{entry->num_inputs} + std::uint32_t{entry->num_outputs};
        if (tensors == 0) continue;
        const MetaIndex start = *meta_start;
        CRUCIBLE_FUZZ_CLAIM("trace_file", start.is_valid());
        CRUCIBLE_FUZZ_CLAIM("trace_file", std::uint64_t{start.raw()} + tensors <= trace.num_metas);
    }
}

}  // namespace crucible::fuzz::boundary
