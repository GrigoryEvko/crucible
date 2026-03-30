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
//   Meta records (num_metas × 168B):
//     Raw TensorMeta structs (sizes, strides, data_ptr, ndim, dtype, etc.)
//     Auto-detects legacy 144B and 160B formats for backward compat.
//
//   Schema name table (optional, present if file has trailing data):
//     uint32   num_names
//     For each name:
//       uint64   schema_hash
//       uint16   name_len (excluding null terminator)
//       char[name_len]  name (NOT null-terminated in file)
//
// Usage:
//   auto trace = crucible::load_trace("vit_b.crtrace");
//   if (!trace) { /* error */ }
//   // Feed to BackgroundThread::build_trace() via its public vectors.
//   // Schema names are auto-registered in the global SchemaTable.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
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
  uint8_t inference_mode = 0;     // 1B — bit 0: inference_mode, bit 1: is_mutable
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

// .crtrace is written and read as raw struct bytes — only correct on
// little-endian hosts. x86_64 and aarch64 (in LE mode) are fine.
static_assert(std::endian::native == std::endian::little,
              ".crtrace format requires little-endian host");

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

  // Read meta records. Auto-detect 144B (legacy) vs 160B (current) format
  // by checking remaining file size after op records.
  const long meta_start_pos = std::ftell(f);
  std::fseek(f, 0, SEEK_END);
  const long file_size = std::ftell(f);
  std::fseek(f, meta_start_pos, SEEK_SET);

  const long remaining = file_size - meta_start_pos;
  // Detect meta record size: 144B (legacy v1), 160B (v2), 168B (current).
  const long meta_bytes_144 = static_cast<long>(num_metas) * 144;
  const long meta_bytes_160 = static_cast<long>(num_metas) * 160;
  const long meta_bytes_168 = static_cast<long>(num_metas) * 168;

  uint32_t meta_record_size = 168;  // default: current
  if (num_metas > 0) {
    if (remaining >= meta_bytes_168)      meta_record_size = 168;
    else if (remaining >= meta_bytes_160) meta_record_size = 160;
    else if (remaining >= meta_bytes_144) meta_record_size = 144;
  }
  const bool legacy_144 = (meta_record_size == 144);
  const bool legacy_160 = (meta_record_size == 160);

  std::vector<TensorMeta> metas(num_metas);
  if (num_metas > 0) {
    if (legacy_144 || legacy_160) {
      // Legacy format: read each meta at its original size, zero-init rest.
      for (uint32_t i = 0; i < num_metas; i++) {
        if (std::fread(&metas[i], meta_record_size, 1, f) != 1) {
          std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
          std::fclose(f);
          return nullptr;
        }
        // Extended fields beyond meta_record_size stay zero from TensorMeta{}.
      }
    } else {
      // Current 168B format: direct bulk read.
      if (std::fread(metas.data(), sizeof(TensorMeta), num_metas, f) != num_metas) {
        std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
        std::fclose(f);
        return nullptr;
      }
    }
  }

  // Read optional schema name table (trailing data after metas).
  uint32_t num_names = 0;
  if (std::fread(&num_names, 4, 1, f) == 1 && num_names > 0 &&
      num_names <= SCHEMA_TABLE_CAP) {
    for (uint32_t i = 0; i < num_names; i++) {
      uint64_t sh = 0;
      uint16_t name_len = 0;
      if (std::fread(&sh, 8, 1, f) != 1) break;
      if (std::fread(&name_len, 2, 1, f) != 1) break;
      if (name_len == 0 || name_len > 256) break;
      char buf[257]{};
      if (std::fread(buf, 1, name_len, f) != name_len) break;
      buf[name_len] = '\0';
      register_schema_name(SchemaHash{sh}, buf);
    }
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
    e.inference_mode = (r.inference_mode & 1) != 0;
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
