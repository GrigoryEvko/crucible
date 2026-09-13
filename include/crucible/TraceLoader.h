#pragma once

// The on-disk trace format, little-endian throughout and read as raw struct
// bytes.
//
//   Header, 16 bytes:
//     char[4]  magic, "CRTR"
//     uint32   version
//     uint32   op count
//     uint32   metadata-record count
//
//   Op records, 80 bytes each:
//     uint64 schema hash, uint64 shape hash,
//     uint64 scope hash, uint64 callsite hash,
//     int64  scalar values, five of them,
//     uint16 input count, uint16 output count, uint16 scalar count,
//     uint8  gradient flag, uint8 packed operation flags
//
//   Metadata records, 168 bytes each: tensor metadata structs, verbatim.
//   Two shorter historical record sizes are also accepted, 144 and 160.
//
//   A schema-name table follows if the file has trailing data:
//     uint32   name count
//     Then, per name:
//       uint64   schema hash
//       uint16   name length, terminator excluded
//       char     the name itself, not terminated in the file

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TraceRing.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/fixy/Handle.h>

namespace crucible {

// Every count in the header arrives as raw bytes, so any value at all is
// reachable on a corrupt or version-skewed file. These ceilings sit far above
// what a real trace reaches and far below the value that would have the loader
// allocate gigabytes before discovering the file is truncated. Both are file
// scope so the runtime check and the typed gate read the same constant.
inline constexpr uint32_t MAX_OPS = 1u << 22;
inline constexpr uint32_t MAX_METAS = 1u << 24;

// Zero is admitted: an empty trace is well-formed.
using ValidTraceNumOps = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<MAX_OPS>, uint32_t>;

using ValidTraceNumMetas = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<MAX_METAS>, uint32_t>;

using ValidTraceNumNames =
    ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<SCHEMA_TABLE_CAP>, uint32_t>;

[[nodiscard, gnu::const]] inline constexpr uint32_t make_trace_num_ops(ValidTraceNumOps raw) noexcept {
    return raw.value();
}

[[nodiscard, gnu::const]] inline constexpr uint32_t make_trace_num_metas(ValidTraceNumMetas raw) noexcept {
    return raw.value();
}

[[nodiscard, gnu::const]] inline constexpr uint32_t make_trace_num_names(ValidTraceNumNames raw) noexcept {
    return raw.value();
}

// A name length is stored on disk as a 16-bit value, so any of them can
// arrive. The lower bound is one, because a zero-length name names nothing.
// The upper bound is what the loader's fixed buffer holds once room is left
// for the terminator it writes.
inline constexpr uint16_t SCHEMA_NAME_LEN_MIN = 1;
inline constexpr uint16_t SCHEMA_NAME_LEN_MAX = 256;

using ValidSchemaNameLen =
    ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::in_range<SCHEMA_NAME_LEN_MIN, SCHEMA_NAME_LEN_MAX>,
                                    uint16_t>;

[[nodiscard, gnu::const]] inline constexpr uint16_t make_schema_name_len(ValidSchemaNameLen raw) noexcept {
    return raw.value();
}

struct TraceOpRecord {
    SchemaHash schema_hash;
    ShapeHash shape_hash;
    ScopeHash scope_hash;
    CallsiteHash callsite_hash;
    int64_t scalar_values[5]{};
    uint16_t num_inputs = 0;
    uint16_t num_outputs = 0;
    uint16_t num_scalars = 0;
    uint8_t grad_enabled = 0;
    // The name is a misnomer this byte is stuck with: it carries the whole
    // packed set of operation flags, not one of them.
    uint8_t inference_mode = 0;
};

// The strong hash types are the size of the integers they wrap, so the record
// stays byte-compatible with a file written before they were introduced.
static_assert(sizeof(TraceOpRecord) == 80, "TraceOpRecord must be 80 bytes");
static_assert(std::is_trivially_copyable_v<TraceOpRecord>);
static_assert(std::is_standard_layout_v<TraceOpRecord>);

struct LoadedTrace {
    // Parallel arrays, indexed by the same operation position.
    std::vector<TraceRing::Entry> entries;
    std::vector<MetaIndex> meta_starts;
    std::vector<ScopeHash> scope_hashes;
    std::vector<CallsiteHash> callsite_hashes;

    // Every operation's metadata concatenated. The array above holds each
    // operation's offset into this one.
    std::vector<TensorMeta> metas;

    uint32_t num_ops = 0;
    uint32_t num_metas = 0;
};

// The file is written and read as raw struct bytes.
static_assert(std::endian::native == std::endian::little, ".crtrace format requires little-endian host");

[[nodiscard]] inline std::unique_ptr<LoadedTrace> load_trace(const char* path) {
    ::crucible::fixy::handle::OwnedFile trace_file{std::fopen(path, "rb")};
    if (!trace_file.is_open()) {
        std::fprintf(stderr, "load_trace: cannot open %s\n", path);
        return nullptr;
    }

    char magic[4]{};
    uint32_t version = 0, num_ops = 0, num_metas = 0;
    if (std::fread(magic, 1, 4, trace_file.get()) != 4 || std::fread(&version, 4, 1, trace_file.get()) != 1
        || std::fread(&num_ops, 4, 1, trace_file.get()) != 1 || std::fread(&num_metas, 4, 1, trace_file.get()) != 1) {
        std::fprintf(stderr, "load_trace: truncated header in %s\n", path);
        return nullptr;
    }

    if (std::memcmp(magic, "CRTR", 4) != 0) {
        std::fprintf(stderr, "load_trace: bad magic in %s\n", path);
        return nullptr;
    }
    if (version != 1) {
        std::fprintf(stderr, "load_trace: unsupported version %u in %s\n", version, path);
        return nullptr;
    }

    if (num_ops > MAX_OPS || num_metas > MAX_METAS) {
        std::fprintf(stderr,
                     "load_trace: header counts exceed cap in %s "
                     "(num_ops=%u num_metas=%u)\n",
                     path, num_ops, num_metas);
        return nullptr;
    }
    // The check above is what rejects an out-of-range value in every build.
    // Passing each count through its type here, and again for the name count
    // below, adds nothing at run time but carries the bound as a witness that
    // downstream code inherits instead of re-deriving.
    num_ops = make_trace_num_ops(ValidTraceNumOps{num_ops});
    num_metas = make_trace_num_metas(ValidTraceNumMetas{num_metas});

    std::vector<TraceOpRecord> records(num_ops);
    if (num_ops > 0 && std::fread(records.data(), sizeof(TraceOpRecord), num_ops, trace_file.get()) != num_ops) {
        std::fprintf(stderr, "load_trace: truncated op records in %s\n", path);
        return nullptr;
    }

    // Which of the three record sizes this file uses is inferred from how many
    // bytes remain after the op records.
    const long meta_start_pos = std::ftell(trace_file.get());
    std::fseek(trace_file.get(), 0, SEEK_END);
    const long file_size = std::ftell(trace_file.get());
    std::fseek(trace_file.get(), meta_start_pos, SEEK_SET);

    const long remaining = file_size - meta_start_pos;
    const long meta_bytes_144 = static_cast<long>(num_metas) * 144;
    const long meta_bytes_160 = static_cast<long>(num_metas) * 160;
    const long meta_bytes_168 = static_cast<long>(num_metas) * 168;

    uint32_t meta_record_size = 168;
    if (num_metas > 0) {
        if (remaining >= meta_bytes_168)
            meta_record_size = 168;
        else if (remaining >= meta_bytes_160)
            meta_record_size = 160;
        else if (remaining >= meta_bytes_144)
            meta_record_size = 144;
    }
    const bool historical_144 = (meta_record_size == 144);
    const bool historical_160 = (meta_record_size == 160);

    std::vector<TensorMeta> metas(num_metas);
    if (num_metas > 0) {
        if (historical_144 || historical_160) {
            // One record at a time at its original size. The fields the older
            // layout does not carry keep their zero-initialised values.
            for (uint32_t i = 0; i < num_metas; i++) {
                if (std::fread(&metas[i], meta_record_size, 1, trace_file.get()) != 1) {
                    std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
                    return nullptr;
                }
            }
        } else {
            if (std::fread(metas.data(), sizeof(TensorMeta), num_metas, trace_file.get()) != num_metas) {
                std::fprintf(stderr, "load_trace: truncated meta records in %s\n", path);
                return nullptr;
            }
        }

        // The bulk read above copies disk bytes straight into the struct, so
        // every field below is untrusted until checked here. Each of the four
        // is checked because something downstream would otherwise act on it.
        for (uint32_t i = 0; i < num_metas; i++) {
            // The rank indexes fixed-width size and stride arrays, so a value
            // past their capacity reads off the end of them.
            if (metas[i].ndim > 8) [[unlikely]] {
                std::fprintf(stderr, "load_trace: meta[%u].ndim=%u exceeds max 8 in %s — corrupt trace\n", i,
                             metas[i].ndim, path);
                return nullptr;
            }
            // Computing a storage size from an unrecognised element type
            // reaches a switch whose default is marked unreachable.
            if (!valid_scalar_type(static_cast<std::int8_t>(metas[i].dtype))) [[unlikely]] {
                std::fprintf(stderr, "load_trace: meta[%u].dtype=%d invalid in %s — corrupt trace\n", i,
                             static_cast<int>(metas[i].dtype), path);
                return nullptr;
            }
            // The device type is folded into the node's content hash, which is
            // its identity and half of a compiled-kernel lookup key, so an
            // unrecognised value would not fail. It would key the wrong entry.
            if (!valid_device_type(static_cast<std::int8_t>(metas[i].device_type))) [[unlikely]] {
                std::fprintf(stderr, "load_trace: meta[%u].device_type=%d invalid in %s — corrupt trace\n", i,
                             static_cast<int>(metas[i].device_type), path);
                return nullptr;
            }
            // This path only copies the layout onward, but another consumer
            // hashes it, and every other boundary refuses an unrecognised one.
            if (!valid_layout(static_cast<std::int8_t>(metas[i].layout))) [[unlikely]] {
                std::fprintf(stderr, "load_trace: meta[%u].layout=%d invalid in %s — corrupt trace\n", i,
                             static_cast<int>(metas[i].layout), path);
                return nullptr;
            }
            // The stored address is written as zero and is meaningless in this
            // process anyway. Re-zeroing it means a later use of it fails
            // loudly rather than treating a disk byte pattern as an address.
            metas[i].data_ptr = external_data_ptr(nullptr);
        }
    }

    // The name table is optional and sits after the metadata.
    uint32_t num_names = 0;
    if (std::fread(&num_names, 4, 1, trace_file.get()) == 1 && num_names > 0 && num_names <= SCHEMA_TABLE_CAP) {
        num_names = make_trace_num_names(ValidTraceNumNames{num_names});
        auto schema_table_view = global_schema_table().mint_mutable_view();
        for (uint32_t i = 0; i < num_names; i++) {
            uint64_t schema_hash_raw = 0;
            uint16_t raw_name_len = 0;
            if (std::fread(&schema_hash_raw, 8, 1, trace_file.get()) != 1) break;
            if (std::fread(&raw_name_len, 2, 1, trace_file.get()) != 1) break;
            // A length outside the bound ends the table here. Whatever names
            // were read stay registered, which is this function's policy for a
            // malformed tail.
            if (raw_name_len < SCHEMA_NAME_LEN_MIN || raw_name_len > SCHEMA_NAME_LEN_MAX) break;
            const uint16_t name_len = make_schema_name_len(ValidSchemaNameLen{raw_name_len});
            char name_buf[257]{};
            if (std::fread(name_buf, 1, name_len, trace_file.get()) != name_len) break;
            name_buf[name_len] = '\0';
            // Bounded and terminated here, which is what lets the bytes cross
            // from untrusted file content into the table.
            register_schema_name(schema_table_view, SchemaHash{schema_hash_raw},
                                 SchemaTable::SanitizedName{static_cast<const char*>(name_buf)});
        }
    }

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

        entry.schema_hash = op_record.schema_hash;
        entry.shape_hash = op_record.shape_hash;
        entry.num_inputs = op_record.num_inputs;
        entry.num_outputs = op_record.num_outputs;
        entry.num_scalar_args = op_record.num_scalars;
        entry.op_flags = op_record.inference_mode;  // the whole packed set, despite the field name
        if (op_record.grad_enabled != 0) {
            entry.op_flags = static_cast<uint8_t>(entry.op_flags | op_flag::GRAD_ENABLED);
        } else {
            entry.op_flags = static_cast<uint8_t>(entry.op_flags & static_cast<uint8_t>(~op_flag::GRAD_ENABLED));
        }
        const uint16_t num_inline_scalars = op_record.num_scalars < 5 ? op_record.num_scalars : 5;
        for (uint16_t j = 0; j < num_inline_scalars; j++)
            entry.scalar_values[j] = op_record.scalar_values[j];

        // Widened before adding: two 16-bit counts sum past what 16 bits hold,
        // and the wrap would turn an operation claiming tens of thousands of
        // tensors into one claiming none.
        const uint32_t total_tensors =
            static_cast<uint32_t>(op_record.num_inputs) + static_cast<uint32_t>(op_record.num_outputs);
        if (total_tensors > 0) {
            // Every consumer reads this operation's metadata as a run starting
            // at the cursor, so a cursor that runs past the end of the array
            // is an out-of-bounds read driven by the file. Rejecting it here
            // means a loaded trace is always self-consistent.
            //
            // Written as a subtraction on the right rather than an addition on
            // the left so the sum cannot overflow. The subtraction cannot
            // underflow either, because the cursor never passes the count, and
            // it is this very check that keeps that true.
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

        trace->scope_hashes[i] = op_record.scope_hash;
        trace->callsite_hashes[i] = op_record.callsite_hash;
    }

    return trace;
}

}  // namespace crucible
