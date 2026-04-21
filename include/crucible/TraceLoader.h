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
//     uint8  grad_enabled, uint8 op_flags (bit-packed, see op_flag:: in TraceRing.h)
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
  SchemaHash   schema_hash;         // 8B — default-ctor 0; same layout as uint64_t
  ShapeHash    shape_hash;          // 8B
  ScopeHash    scope_hash;          // 8B
  CallsiteHash callsite_hash;       // 8B
  int64_t  scalar_values[5]{};     // 40B
  uint16_t num_inputs     = 0;     // 2B
  uint16_t num_outputs    = 0;     // 2B
  uint16_t num_scalars    = 0;     // 2B
  uint8_t  grad_enabled   = 0;     // 1B
  uint8_t  inference_mode = 0;     // 1B — misleading name (kept for struct layout);
                                    //      carries all op_flag bits, see op_flag:: in TraceRing.h
};

// Strong-hash newtypes are sizeof()-identical to their raw uint64_t;
// the on-disk record remains bit-compatible with pre-typed writers.
static_assert(sizeof(TraceOpRecord) == 80, "TraceOpRecord must be 80 bytes");
static_assert(std::is_trivially_copyable_v<TraceOpRecord>);
static_assert(std::is_standard_layout_v<TraceOpRecord>);

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

  // Hard caps: real traces top out at 10^5 ops; reject adversarial
  // headers that would allocate >8 GB of records before discovering
  // truncation.
  static constexpr uint32_t MAX_OPS   = 1u << 22;   // 4 M ops
  static constexpr uint32_t MAX_METAS = 1u << 24;   // 16 M metas
  if (num_ops > MAX_OPS || num_metas > MAX_METAS) {
    std::fprintf(stderr, "load_trace: header counts exceed cap in %s "
                         "(num_ops=%u num_metas=%u)\n",
                 path, num_ops, num_metas);
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

    // Sanitize: validate each meta's untrusted fields AT the read boundary.
    // Source is disk bytes (source::External); downstream code assumes
    // ndim ≤ 8 (TensorMeta::sizes/strides capacity).  Without this check,
    // an adversarial trace with ndim=255 would drive compute_storage_nbytes
    // to read past the end of the sizes[] array.
    //
    // data_ptr is zeroed at write time (see Serialize.h:81 "data_ptr → always
    // 0 on disk") but we re-zero here so any downstream address-use path
    // fails loudly rather than treating a disk byte pattern as a pointer.
    for (uint32_t i = 0; i < num_metas; i++) {
      if (metas[i].ndim > 8) [[unlikely]] {
        std::fprintf(stderr,
            "load_trace: meta[%u].ndim=%u exceeds max 8 in %s — corrupt trace\n",
            i, metas[i].ndim, path);
        std::fclose(f);
        return nullptr;
      }
      metas[i].data_ptr = nullptr;
    }
  }

  // Read optional schema name table (trailing data after metas).
  // Name length bound: matches register_schema_name's FFI contract
  // (schema_name length ≤ 256).  Any wire value outside [1, 256] breaks
  // the loop — detection at the parse boundary, never downstream.
  static constexpr uint16_t kMinSchemaNameLen = 1;
  static constexpr uint16_t kMaxSchemaNameLen = 256;
  uint32_t num_names = 0;
  if (std::fread(&num_names, 4, 1, f) == 1 && num_names > 0 &&
      num_names <= SCHEMA_TABLE_CAP) {
    for (uint32_t i = 0; i < num_names; i++) {
      uint64_t sh = 0;
      uint16_t name_len = 0;
      if (std::fread(&sh, 8, 1, f) != 1) break;
      if (std::fread(&name_len, 2, 1, f) != 1) break;
      if (name_len < kMinSchemaNameLen || name_len > kMaxSchemaNameLen) break;
      // Propagate the validated bound to the optimizer so the fread
      // and the terminating null write both compile without bounds
      // repredicating (buf has fixed 257-byte storage; name_len's
      // range is fully resolved by the guard above).
      [[assume(name_len >= kMinSchemaNameLen && name_len <= kMaxSchemaNameLen)]];
      char buf[257]{};
      if (std::fread(buf, 1, name_len, f) != name_len) break;
      buf[name_len] = '\0';
      // Bytes from disk, but length-validated above (1..256) and explicitly
      // null-terminated.  Retag from External (file source) → Sanitized so
      // the schema table can accept them.
      register_schema_name(
          SchemaHash{sh},
          SchemaTable::SanitizedName{static_cast<const char*>(buf)});
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

    e.schema_hash = r.schema_hash;
    e.shape_hash  = r.shape_hash;
    e.num_inputs = r.num_inputs;
    e.num_outputs = r.num_outputs;
    e.num_scalar_args = r.num_scalars;
    e.set_grad_enabled(r.grad_enabled != 0);
    e.op_flags = r.inference_mode;  // on-disk byte carries all op_flag bits
    uint16_t n = r.num_scalars < 5 ? r.num_scalars : 5;
    for (uint16_t s = 0; s < n; s++)
      e.scalar_values[s] = r.scalar_values[s];

    const auto total_tensors =
        static_cast<uint16_t>(r.num_inputs + r.num_outputs);
    if (total_tensors > 0) {
      trace->meta_starts[i] = MetaIndex{meta_cursor};
      meta_cursor += total_tensors;
    } else {
      trace->meta_starts[i] = MetaIndex::none();
    }

    trace->scope_hashes[i]    = r.scope_hash;
    trace->callsite_hashes[i] = r.callsite_hash;
  }

  return trace;
}

} // namespace crucible
