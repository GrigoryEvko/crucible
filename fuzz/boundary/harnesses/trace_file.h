#pragma once

// A .crtrace file.  The loader either refuses the file or returns a trace
// whose descriptors are in range and whose per-operation metadata runs lie
// inside the metadata array.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include <crucible/TraceLoader.h>

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

    const auto path = scratch_dir() / "input.crtrace";
    write_file(path, bytes);
    auto trace = load_trace(path.c_str());
    if (!trace) return;

    CRUCIBLE_FUZZ_CLAIM("trace_file", trace->metas.size() == trace->num_metas);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace->entries.size() == trace->num_ops);
    for (const TensorMeta& meta : trace->metas) claim_meta_in_range("trace_file", meta);
    for (std::uint32_t i = 0; i < trace->num_ops; ++i) {
        const auto& entry = trace->entries[i];
        const std::uint32_t tensors = std::uint32_t{entry.num_inputs} + std::uint32_t{entry.num_outputs};
        if (tensors == 0) continue;
        const MetaIndex start = trace->meta_starts[i];
        CRUCIBLE_FUZZ_CLAIM("trace_file", start.is_valid());
        CRUCIBLE_FUZZ_CLAIM("trace_file", std::uint64_t{start.raw()} + tensors <= trace->num_metas);
    }
}

}  // namespace crucible::fuzz::boundary
