#pragma once

// A .crtrace file.  The loader either refuses the file or returns a trace
// whose descriptors are in range and whose per-operation metadata runs lie
// inside the metadata array.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include <crucible/TraceLoader.h>
#include <fixy/Ctx.h>

namespace crucible::fuzz::boundary {

// One operation with one input and one output, two metadata records, and a
// name table with one name.
[[nodiscard]] inline Seeds seeds_trace_file() {
    std::vector<std::uint8_t> seed = text_bytes("CRTR");
    append_raw(seed, std::uint32_t{1});
    append_raw(seed, std::uint32_t{1});
    append_raw(seed, std::uint32_t{2});
    TraceOpRecord record{};
    record.schema_hash = SchemaHash{0xABCD};
    record.num_inputs = 1;
    record.num_outputs = 1;
    append_raw(seed, record);
    TensorMeta meta{};
    meta.ndim = 1;
    meta.sizes[0] = tensor_dim(4);
    meta.strides[0] = tensor_dim(1);
    meta.dtype = ScalarType::Float;
    append_raw(seed, meta);
    append_raw(seed, meta);
    append_raw(seed, std::uint32_t{1});
    append_raw(seed, std::uint64_t{0xABCD});
    append_raw(seed, std::uint16_t{7});
    append_bytes(seed, text_bytes("aten::x"));
    return {seed};
}

inline void run_trace_file(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    if (bytes.size() > kMaxInputBytes) bytes = bytes.first(kMaxInputBytes);

    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    // The scratch path is absolute and holds no ".." component.
    ::foundation::os::PathText<::fixy::kPathBytes> input{};
    if (input.append(::fixy::mint_foreign_text(scratch_dir().c_str(), scratch_dir().native().size())).is_err()
        || input.append(::fixy::TextView{"/input.crtrace"}).is_err())
        return;
    auto sanitized_path =
        ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(input.c_text().view()));
    if (sanitized_path.is_err()) return;
    const ::fixy::Path<::fixy::tags::source::Sanitized> path =
        std::move(sanitized_path).expect("the scratch path passes the check");
    {
        // The descriptor closes at the end of this block, before the load.
        const ::fixy::fs::OwnedFd file =
            ::fixy::fs::mint_file<::fixy::atom::fs::mode<::fixy::fs::open_mode::WriteTruncate>>(ctx, path).match(
                [](::fixy::Linear<::fixy::fs::OwnedFd> opened) -> ::fixy::fs::OwnedFd {
                    return std::move(opened).consume();
                },
                [](::foundation::os::Errno) -> ::fixy::fs::OwnedFd { return ::fixy::fs::OwnedFd{}; });
        if (!file.is_open() || ::fixy::fs::write_full(ctx, file, as_bytes_view(bytes)).is_err()) return;
    }
    auto loaded = load_trace(ctx, path);
    if (loaded.is_err()) return;
    const LoadedTrace trace = std::move(loaded).expect("the load holds a trace after the check of its error");

    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.metas.size() == trace.num_metas);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.entries.size() == trace.num_ops);
    for (const TensorMeta& meta : trace.metas)
        claim_meta_in_range("trace_file", meta);
    for (const auto [entry, meta_start] :
         ::fixy::zip(trace.entries.view(), trace.meta_starts.view()).expect("the arrays of a trace are parallel")) {
        const std::uint32_t tensors = std::uint32_t{entry->num_inputs} + std::uint32_t{entry->num_outputs};
        if (tensors == 0) continue;
        const MetaIndex start = *meta_start;
        CRUCIBLE_FUZZ_CLAIM("trace_file", start.is_valid());
        CRUCIBLE_FUZZ_CLAIM("trace_file", std::uint64_t{start.raw()} + tensors <= trace.num_metas);
    }
}

}  // namespace crucible::fuzz::boundary
