#pragma once

// TraceLoader: load .crtrace binary files for benchmarking build_trace().
//
// File format (all little-endian, designed for zero-overhead C++ loading):
//
//   Header (16B):
//     char[4]  magic    = "CRTR"
//     uint32   version  = 1
//     uint32   num_ops
//     uint32   num_metas
//
//   Op records (num_ops × 80B):
//     uint64 schema_hash, uint64 shape_hash,
//     uint64 scope_hash, uint64 callsite_hash,
//     int64  scalar_values[5],
//     uint16 num_inputs, uint16 num_outputs, uint16 num_scalars,
//     uint8  grad_enabled, uint8 inference_mode
//
//   Meta records (num_metas × 144B):
//     Raw TensorMeta structs (sizes, strides, data_ptr, ndim, dtype, etc.)
//
// Usage:
//   auto trace = crucible::load_trace("vit_b.crtrace");
//   if (!trace) { /* error */ }
//   // Feed to BackgroundThread::build_trace() via its public vectors.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <crucible/MerkleDag.h>
#include <crucible/TraceRing.h>

namespace crucible {

// ── On-disk record layout (80 bytes) ─────────────────────────────────

struct TraceOpRecord {
  uint64_t schema_hash = 0;       // 8B
  uint64_t shape_hash = 0;        // 8B
  uint64_t scope_hash = 0;        // 8B
  uint64_t callsite_hash = 0;     // 8B
  int64_t scalar_values[5]{};     // 40B
  uint16_t num_inputs = 0;        // 2B
  uint16_t num_outputs = 0;       // 2B
  uint16_t num_scalars = 0;       // 2B
  uint8_t grad_enabled = 0;       // 1B
  uint8_t inference_mode = 0;     // 1B
};

static_assert(sizeof(TraceOpRecord) == 80, "TraceOpRecord must be 80 bytes");

// ── Loaded trace data ────────────────────────────────────────────────

struct LoadedTrace {
  // Parallel arrays matching BackgroundThread's vectors.
  std::vector<TraceRing::Entry> entries;
  std::vector<MetaIndex> meta_starts;
  std::vector<ScopeHash> scope_hashes;
  std::vector<CallsiteHash> callsite_hashes;

  // All TensorMetas concatenated (entries index into this).
  std::vector<TensorMeta> metas;

  uint32_t num_ops = 0;
  uint32_t num_metas = 0;
};

// ── Loader ───────────────────────────────────────────────────────────

[[nodiscard]] inline std::unique_ptr<LoadedTrace> load_trace(const char* path) {
  std::FILE* f = std::fopen(path, "rb");
  if (!f) {
    std::fprintf(stderr, "load_trace: cannot open %s\n", path);
    return nullptr;
  }

  // Read header (16 bytes).
  char magic[4]{};
  uint32_t version = 0, num_ops = 0, num_metas = 0;
  if (std::fread(magic, 1, 4, f) != 4 ||
      std::fread(&version, 4, 1, f) != 1 ||
      std::fread(&num_ops, 4, 1, f) != 1 ||
      std::fread(&num_metas, 4, 1, f) != 1) {
    std::fprintf(stderr, "load_trace: truncated header in %s\n", path);
    std::fclose(f);
    return nullptr;
  }

  if (std::memcmp(magic, "CRTR", 4) != 0) {
    std::fprintf(stderr, "load_trace: bad magic in %s\n", path);
    std::fclose(f);
    return nullptr;
  }
  if (version != 1) {
    std::fprintf(stderr, "load_trace: unsupported version %u in %s\n",
                 version, path);
    std::fclose(f);
    return nullptr;
  }

  // Read op records.
  std::vector<TraceOpRecord> records(num_ops);
  if (num_ops > 0 &&
      std::fread(records.data(), sizeof(TraceOpRecord), num_ops, f) != num_ops) {
    std::fprintf(stderr, "load_trace: truncated op records in %s\n", path);
    std::fclose(f);
    return nullptr;
  }

  // Read meta records.
  std::vector<TensorMeta> metas(num_metas);
  if (num_metas > 0 &&
      std::fread(metas.data(), sizeof(TensorMeta), num_metas, f) != num_metas) {
    std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
    std::fclose(f);
    return nullptr;
  }

  std::fclose(f);

  // Convert to BackgroundThread-compatible vectors.
  auto trace = std::make_unique<LoadedTrace>();
  trace->num_ops = num_ops;
  trace->num_metas = num_metas;
  trace->metas = std::move(metas);

  trace->entries.resize(num_ops);
  trace->meta_starts.resize(num_ops);
  trace->scope_hashes.resize(num_ops);
  trace->callsite_hashes.resize(num_ops);

  uint32_t meta_cursor = 0;
  for (uint32_t i = 0; i < num_ops; i++) {
    const auto& r = records[i];
    auto& e = trace->entries[i];

    e.schema_hash = SchemaHash{r.schema_hash};
    e.shape_hash = ShapeHash{r.shape_hash};
    e.num_inputs = r.num_inputs;
    e.num_outputs = r.num_outputs;
    e.num_scalar_args = r.num_scalars;
    e.grad_enabled = r.grad_enabled != 0;
    e.inference_mode = r.inference_mode != 0;
    uint16_t n = r.num_scalars < 5 ? r.num_scalars : 5;
    for (uint16_t s = 0; s < n; s++)
      e.scalar_values[s] = r.scalar_values[s];

    uint16_t total_tensors = r.num_inputs + r.num_outputs;
    if (total_tensors > 0) {
      trace->meta_starts[i] = MetaIndex{meta_cursor};
      meta_cursor += total_tensors;
    } else {
      trace->meta_starts[i] = MetaIndex::none();
    }

    trace->scope_hashes[i] = ScopeHash{r.scope_hash};
    trace->callsite_hashes[i] = CallsiteHash{r.callsite_hash};
  }

  return trace;
}

} // namespace crucible
