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
//     Current readers accept the historical 144B and 160B writer layouts.
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
#include <crucible/fixy/Wrap.h>   // FIXY-U-096r: Refined / bounded_above / in_range via the fixy umbrella
#include <crucible/fixy/Handle.h>       // FIXY-V-032-audit: route OwnedFile via fixy::handle::

namespace crucible {

// ── Header count caps (file-format invariant) ────────────────────────
//
// .crtrace headers carry three uint32_t counts read from disk:
//   * num_ops    — number of TraceOpRecord entries (each 80 B)
//   * num_metas  — number of TensorMeta entries (each 168 B)
//   * num_names  — number of (schema_hash, name) pairs in the optional
//                  trailing table
//
// Real traces top out at ~10^5 ops; we cap each count well above any
// plausible-trace value but well below the adversarial wire value that
// would request a multi-GB allocation before discovering truncation.
//
//   MAX_OPS         = 1 << 22  (4 M ops      ≈ 320 MB)
//   MAX_METAS       = 1 << 24  (16 M metas   ≈ 2.7 GB)
//   SCHEMA_TABLE_CAP   from SchemaTable.h    (currently 512)
//
// `num_ops`, `num_metas`, and `num_names` arrive as raw bytes from disk
// — every uint32_t value is reachable on a corrupted or version-skewed
// file.  Per WRAP-TraceLoader-2 (#1050), the runtime if-checks at the
// header / name-table boundaries each pair with a typed Refined ctor
// downstream that pins the bound at the type system.  Both layers fire
// before any std::vector allocation or for-loop with the count as
// upper bound; either alone catches the bug; together the structural
// guarantee is doubled and the type-level proof rides into every
// future call site.  The 6 HS14 negative-compile fixtures
// (test/safety_neg/neg_trace_num_*) wedge each bound into place.
//
// Sized small (uint32_t headroom for hard caps).  `inline constexpr`
// promotes them to file-scope so the Refined alias and the runtime
// guard read the same constants.
inline constexpr uint32_t MAX_OPS   = 1u << 22;   // 4 M ops
inline constexpr uint32_t MAX_METAS = 1u << 24;   // 16 M metas

// Validated count carriers.  `bounded_above<MAX>` admits `v ≤ MAX`
// (zero is admissible — empty traces are well-formed).  Regime-1 EBO
// collapse keeps each wrapper zero-cost: sizeof == sizeof(uint32_t).
using ValidTraceNumOps = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<MAX_OPS>, uint32_t>;

using ValidTraceNumMetas = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<MAX_METAS>, uint32_t>;

using ValidTraceNumNames = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::bounded_above<SCHEMA_TABLE_CAP>, uint32_t>;

// Widening factories.  `gnu::const` documents that the result depends
// only on the argument and has no side effects; the optimizer can CSE /
// DCE the call freely under -O3.
[[nodiscard, gnu::const]] inline constexpr
uint32_t make_trace_num_ops(ValidTraceNumOps raw) noexcept {
    return raw.value();
}

[[nodiscard, gnu::const]] inline constexpr
uint32_t make_trace_num_metas(ValidTraceNumMetas raw) noexcept {
    return raw.value();
}

[[nodiscard, gnu::const]] inline constexpr
uint32_t make_trace_num_names(ValidTraceNumNames raw) noexcept {
    return raw.value();
}

// ── Schema-name length bounds (file-format invariant) ────────────────
//
// The .crtrace optional schema-name table stores each name's length as
// a uint16_t.  By format definition the length is in [1, 256] — names
// must be non-empty (a zero-length name pairs with no schema_hash) and
// fit in the loader's fixed 257-byte stack buffer (256 chars + the null
// terminator written by load_trace).
//
// `name_len` arrives at the loader as raw bytes from disk — every value
// in [0, UINT16_MAX] is reachable on a corrupted or version-skewed
// file.  The defense-in-depth gate is documented in the WRAP-TraceLoader-3
// (#1051) block below; ValidSchemaNameLen pins the bound at the type
// system and the 2 HS14 negative-compile fixtures
// (test/safety_neg/neg_schema_name_len_*) wedge the bound into place.
inline constexpr uint16_t SCHEMA_NAME_LEN_MIN = 1;
inline constexpr uint16_t SCHEMA_NAME_LEN_MAX = 256;

// Validated schema-name length carrier.  Per WRAP-TraceLoader-3 (#1051),
// ValidSchemaNameLen is fixy::wrap::Refined<fixy::wrap::in_range<MIN,MAX>, uint16_t>
// — the typed gate at every uint16_t → schema-name-length widening
// site (currently only TraceLoader::load_trace, but future readers
// of the .crtrace name table inherit the same gate by construction).
//
// The bound `[SCHEMA_NAME_LEN_MIN, SCHEMA_NAME_LEN_MAX]` admits the
// closed interval [1, 256]; values outside that interval cause the
// constructor's pre clause to fail in constexpr context (P1494R5
// non-constant expression → ill-formed) and to terminate via the
// project contract handler at runtime.  Defense-in-depth: existing
// explicit `if (name_len < MIN || name_len > MAX) break;` runtime
// guard + checked Refined ctor fired BEFORE the fread/null-terminator
// writes consume the value.  Either layer alone catches the bug;
// together the structural guarantee is doubled and the type-level
// proof rides into every future call site.
using ValidSchemaNameLen = ::crucible::fixy::wrap::Refined<
    ::crucible::fixy::wrap::in_range<SCHEMA_NAME_LEN_MIN, SCHEMA_NAME_LEN_MAX>,
    uint16_t>;

// Widening factory for ValidSchemaNameLen → uint16_t in production
// hot-path code.  `gnu::const` documents that the result depends only
// on the argument and has no side effects; the optimizer can CSE / DCE
// the call freely under -O3.
[[nodiscard, gnu::const]] inline constexpr
uint16_t make_schema_name_len(ValidSchemaNameLen raw) noexcept {
    return raw.value();
}

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
  // FIXY-V-032: OwnedFile RAII — every early-return below closes the
  // FILE* via the dtor; the 9 std::fclose() calls the previous version
  // sprinkled across error paths are now structurally unreachable as
  // a LeakSafe-axiom-violation (no path can leak the handle).
  ::crucible::fixy::handle::OwnedFile trace_file{std::fopen(path, "rb")};
  if (!trace_file.is_open()) {
    std::fprintf(stderr, "load_trace: cannot open %s\n", path);
    return nullptr;
  }

  // Read header (16 bytes).
  char magic[4]{};
  uint32_t version = 0, num_ops = 0, num_metas = 0;
  if (std::fread(magic, 1, 4, trace_file.get()) != 4 ||
      std::fread(&version, 4, 1, trace_file.get()) != 1 ||
      std::fread(&num_ops, 4, 1, trace_file.get()) != 1 ||
      std::fread(&num_metas, 4, 1, trace_file.get()) != 1) {
    std::fprintf(stderr, "load_trace: truncated header in %s\n", path);
    return nullptr;
  }

  if (std::memcmp(magic, "CRTR", 4) != 0) {
    std::fprintf(stderr, "load_trace: bad magic in %s\n", path);
    return nullptr;
  }
  if (version != 1) {
    std::fprintf(stderr, "load_trace: unsupported version %u in %s\n",
                 version, path);
    return nullptr;
  }

  // Hard caps: real traces top out at 10^5 ops; reject adversarial
  // headers that would allocate >8 GB of records before discovering
  // truncation.  MAX_OPS / MAX_METAS are file-scope `inline constexpr`
  // (see top of header) so the runtime guard and the typed gate below
  // read the same constants.
  if (num_ops > MAX_OPS || num_metas > MAX_METAS) {
    std::fprintf(stderr, "load_trace: header counts exceed cap in %s "
                         "(num_ops=%u num_metas=%u)\n",
                 path, num_ops, num_metas);
    return nullptr;
  }
  // Defense-in-depth typed witnesses (#1050 WRAP-TraceLoader-2).  The
  // if-check above already returns nullptr-equivalent for out-of-range
  // values, so the Refined ctor's pre clause holds and never aborts on
  // this path.  The reassignment is structurally a no-op (same value,
  // same type) but makes every downstream `num_ops` / `num_metas` read
  // inherit the bound witness from the gate.  The neg-compile fixtures
  // pin the structural guarantee at the type level: a constexpr
  // ValidTraceNumOps{> MAX_OPS} or ValidTraceNumMetas{> MAX_METAS} is
  // ill-formed regardless of what callers do.
  num_ops   = make_trace_num_ops(ValidTraceNumOps{num_ops});
  num_metas = make_trace_num_metas(ValidTraceNumMetas{num_metas});

  // Read op records.
  std::vector<TraceOpRecord> records(num_ops);
  if (num_ops > 0 &&
      std::fread(records.data(), sizeof(TraceOpRecord), num_ops, trace_file.get()) != num_ops) {
    std::fprintf(stderr, "load_trace: truncated op records in %s\n", path);
    return nullptr;
  }

  // Read meta records. Detect 144B, 160B, and 168B layouts by checking
  // remaining file size after op records.
  const long meta_start_pos = std::ftell(trace_file.get());
  std::fseek(trace_file.get(), 0, SEEK_END);
  const long file_size = std::ftell(trace_file.get());
  std::fseek(trace_file.get(), meta_start_pos, SEEK_SET);

  const long remaining = file_size - meta_start_pos;
  const long meta_bytes_144 = static_cast<long>(num_metas) * 144;
  const long meta_bytes_160 = static_cast<long>(num_metas) * 160;
  const long meta_bytes_168 = static_cast<long>(num_metas) * 168;

  uint32_t meta_record_size = 168;  // default: current
  if (num_metas > 0) {
    if (remaining >= meta_bytes_168)      meta_record_size = 168;
    else if (remaining >= meta_bytes_160) meta_record_size = 160;
    else if (remaining >= meta_bytes_144) meta_record_size = 144;
  }
  const bool historical_144 = (meta_record_size == 144);
  const bool historical_160 = (meta_record_size == 160);

  std::vector<TensorMeta> metas(num_metas);
  if (num_metas > 0) {
    if (historical_144 || historical_160) {
      // Historical format: read each meta at its original size, zero-init rest.
      for (uint32_t i = 0; i < num_metas; i++) {
        if (std::fread(&metas[i], meta_record_size, 1, trace_file.get()) != 1) {
          std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
          return nullptr;
        }
        // Extended fields beyond meta_record_size stay zero from TensorMeta{}.
      }
    } else {
      // Current 168B format: direct bulk read.
      if (std::fread(metas.data(), sizeof(TensorMeta), num_metas, trace_file.get()) != num_metas) {
        std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
        return nullptr;
      }
    }

    // Sanitize: validate each meta's untrusted fields AT the read boundary.
    // Source is disk bytes (source::External); downstream code assumes
    // ndim ≤ 8 (TensorMeta::sizes/strides capacity).  Without this check,
    // an adversarial trace with ndim=255 would drive compute_storage_nbytes
    // to read past the end of the sizes[] array.
    //
    // data_ptr is zeroed at write time (Serialize.h: WriteOps writes
    // TensorMeta with data_ptr=0; ReadOps treats it as discarded).  We
    // re-zero here so any downstream address-use path fails loudly rather
    // than treating a disk byte pattern as a pointer.
    for (uint32_t i = 0; i < num_metas; i++) {
      if (metas[i].ndim > 8) [[unlikely]] {
        std::fprintf(stderr,
            "load_trace: meta[%u].ndim=%u exceeds max 8 in %s — corrupt trace\n",
            i, metas[i].ndim, path);
        return nullptr;
      }
      metas[i].data_ptr = external_data_ptr(nullptr);
    }
  }

  // Read optional schema name table (trailing data after metas).
  // Name-length bound: SCHEMA_NAME_LEN_MIN ≤ name_len ≤ SCHEMA_NAME_LEN_MAX
  // (1..256 inclusive). Any wire value outside [1, 256] breaks the
  // loop at the parse boundary, never downstream.  The validated
  // length feeds ValidSchemaNameLen — the type-level witness propagates
  // the bound to the optimizer (the Refined ctor's pre clause is the
  // [[assume]] that used to live here, scoped to the validated
  // variable rather than open-coded at every call site).  See
  // WRAP-TraceLoader-3 (#1051) and the negative-compile fixtures
  // test/safety_neg/neg_schema_name_len_below_min.cpp +
  // test/safety_neg/neg_schema_name_len_above_max.cpp.
  uint32_t num_names = 0;
  if (std::fread(&num_names, 4, 1, trace_file.get()) == 1 && num_names > 0 &&
      num_names <= SCHEMA_TABLE_CAP) {
    // Defense-in-depth typed witness (#1050 WRAP-TraceLoader-2).  The
    // condition on the if above ensures `num_names` lies in (0,
    // SCHEMA_TABLE_CAP], so the Refined ctor's pre clause holds and
    // never aborts.  Reassigning makes the loop bound below inherit
    // the bound witness.
    num_names = make_trace_num_names(ValidTraceNumNames{num_names});
    auto schema_table_view = global_schema_table().mint_mutable_view();
    for (uint32_t i = 0; i < num_names; i++) {
      uint64_t schema_hash_raw = 0;
      uint16_t raw_name_len = 0;
      if (std::fread(&schema_hash_raw, 8, 1, trace_file.get()) != 1) break;
      if (std::fread(&raw_name_len, 2, 1, trace_file.get()) != 1) break;
      if (raw_name_len < SCHEMA_NAME_LEN_MIN ||
          raw_name_len > SCHEMA_NAME_LEN_MAX) break;
      // Validated uint16_t → schema-name-length widening
      // (#1051 WRAP-TraceLoader-3).  The if-break above already
      // returns nullptr-equivalent (loop exit; the partial table is
      // the deserialize-error policy this function uses), and the
      // checked Refined ctor stands as a defense-in-depth re-check —
      // the bound is established by the if-break, so the ctor's pre
      // clause holds and never aborts on this path.  The neg-compile
      // fixtures pin the structural guarantee at the type level: a
      // constexpr ValidSchemaNameLen{0} or {>= 257} is ill-formed
      // regardless of what callers do.
      const uint16_t name_len = make_schema_name_len(
          ValidSchemaNameLen{raw_name_len});
      char name_buf[257]{};
      if (std::fread(name_buf, 1, name_len, trace_file.get()) != name_len) break;
      name_buf[name_len] = '\0';
      // Bytes from disk, but length-validated above (1..256) and explicitly
      // null-terminated.  Retag from External (file source) → Sanitized so
      // the schema table can accept them.
      register_schema_name(
          schema_table_view,
          SchemaHash{schema_hash_raw},
          SchemaTable::SanitizedName{static_cast<const char*>(name_buf)});
    }
  }


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
    const auto& op_record = records[i];
    auto& entry = trace->entries[i];

    entry.schema_hash     = op_record.schema_hash;
    entry.shape_hash      = op_record.shape_hash;
    entry.num_inputs      = op_record.num_inputs;
    entry.num_outputs     = op_record.num_outputs;
    entry.num_scalar_args = op_record.num_scalars;
    entry.op_flags        = op_record.inference_mode;  // on-disk byte carries all op_flag bits
    if (op_record.grad_enabled != 0) {
      entry.op_flags = static_cast<uint8_t>(
          entry.op_flags | op_flag::GRAD_ENABLED);
    } else {
      entry.op_flags = static_cast<uint8_t>(
          entry.op_flags & static_cast<uint8_t>(~op_flag::GRAD_ENABLED));
    }
    const uint16_t num_inline_scalars = op_record.num_scalars < 5 ? op_record.num_scalars : 5;
    for (uint16_t j = 0; j < num_inline_scalars; j++)
      entry.scalar_values[j] = op_record.scalar_values[j];

    // num_inputs + num_outputs in uint32: each is a uint16, so their sum can
    // reach 131070 and would WRAP if computed in uint16 (65535 + 1 -> 0),
    // hiding an op that claims tens of thousands of tensors behind a
    // meta_start of none().
    const uint32_t total_tensors =
        static_cast<uint32_t>(op_record.num_inputs) +
        static_cast<uint32_t>(op_record.num_outputs);
    if (total_tensors > 0) {
      // meta_starts[i] indexes the metas vector (num_metas entries); every
      // consumer (BackgroundThread build path, vis/BlockDetector) reads
      // metas[meta_start + k] for k in [0, num_inputs + num_outputs).  The
      // running cursor must never reach past num_metas, else those reads
      // overrun the vector — a heap OOB read from an untrusted .crtrace.
      // Reject at the load boundary so load_trace always yields a self-
      // consistent LoadedTrace.  `total_tensors > num_metas - meta_cursor`
      // is the overflow-safe form of `meta_cursor + total_tensors >
      // num_metas`: meta_cursor <= num_metas is the loop invariant (held by
      // this very check), so num_metas - meta_cursor never underflows.
      if (total_tensors > num_metas - meta_cursor) [[unlikely]] {
        std::fprintf(stderr,
            "load_trace: op[%u] meta range (%u tensors at cursor %u) "
            "overruns num_metas=%u in %s\n",
            i, total_tensors, meta_cursor, num_metas, path);
        return nullptr;
      }
      trace->meta_starts[i] = MetaIndex{meta_cursor};
      meta_cursor += total_tensors;
    } else {
      trace->meta_starts[i] = MetaIndex::none();
    }

    trace->scope_hashes[i]    = op_record.scope_hash;
    trace->callsite_hashes[i] = op_record.callsite_hash;
  }

  return trace;
}

} // namespace crucible
