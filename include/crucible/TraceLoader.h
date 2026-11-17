#pragma once

// The on-disk trace format, little-endian throughout.  The loader reads each
// field from its bytes, at its offset in its record.
//
//   Header, 16 bytes:
//     char[4]  magic, "CRTR"
//     uint32   version
//     uint32   op count
//     uint32   metadata-record count
//
//   Op records, 88 bytes each, at the offsets of TraceOpRecord:
//     uint64 schema hash, uint64 shape hash,
//     uint64 scope hash, uint64 callsite hash,
//     int64  scalar values, five of them,
//     uint16 input count, uint16 output count, uint16 scalar count,
//     uint8  gradient flag, uint8 packed operation flags,
//     uint16 nesting depth (OpDepth),
//     uint16 tags of the scalar values (ScalarType2), two bits for each,
//     four zero bytes
//
//   Metadata records, 176 bytes each, at the offsets of the tensor metadata
//   struct.  Each record goes through decode_tensor_meta, the checked door of
//   that struct.
//
//   A schema-name table follows if the file has trailing data:
//     uint32   name count
//     Then, per name:
//       uint64   schema hash
//       uint16   name length, terminator excluded
//       char     the name itself, not terminated in the file

#include <cstddef>
#include <cstdint>
#include <utility>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/Refined.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>

namespace crucible {

// Every count in the header arrives as raw bytes, so any value at all is
// reachable on a corrupt or version-skewed file. These ceilings sit far above
// what a real trace reaches and far below the value that would have the loader
// allocate gigabytes before discovering the file is truncated. Both are file
// scope so the runtime check and the typed gate read the same constant.
inline constexpr uint32_t MAX_OPS = 1u << 22;
inline constexpr uint32_t MAX_METAS = 1u << 24;

// The typed gates of the three header counts.  The loader takes each count
// from its header field only through the mint of its gate.  Zero is admitted:
// an empty trace is well-formed.
inline constexpr auto kTraceNumOpsBound = ::fixy::bounded_above<MAX_OPS>;
inline constexpr auto kTraceNumMetasBound = ::fixy::bounded_above<MAX_METAS>;
inline constexpr auto kTraceNumNamesBound = ::fixy::bounded_above<SCHEMA_TABLE_CAP>;

// A name length is stored on disk as a 16-bit value, so any of them can
// arrive. The lower bound is one, because a zero-length name names nothing.
// The upper bound is what the loader's fixed buffer holds once room is left
// for the terminator it writes.
inline constexpr uint16_t SCHEMA_NAME_LEN_MIN = 1;
inline constexpr uint16_t SCHEMA_NAME_LEN_MAX = 256;
inline constexpr auto kSchemaNameLenRange = ::fixy::in_range<SCHEMA_NAME_LEN_MIN, SCHEMA_NAME_LEN_MAX>;

struct TraceOpRecord {
    SchemaHash schema_hash;
    ShapeHash shape_hash;
    ScopeHash scope_hash;
    CallsiteHash callsite_hash;
    ::fixy::FixedArray<int64_t, 5> scalar_values{};
    uint16_t num_inputs = 0;
    uint16_t num_outputs = 0;
    uint16_t num_scalars = 0;
    uint8_t grad_enabled = 0;
    // The name is a misnomer this byte is stuck with: it carries the whole
    // packed set of operation flags, not one of them.
    uint8_t inference_mode = 0;
    OpDepth depth{};
    // The tag of each scalar value, two bits for each, slot 0 in the low bits.
    uint16_t scalar_types = 0;
    // The writer writes zero here, and the loader reads nothing.
    ::fixy::FixedArray<uint8_t, 4> pad{};
};

struct LoadedTrace {
    // Parallel arrays, indexed by the same operation position.
    ::fixy::HeapArray<TraceRing::Entry> entries;
    ::fixy::HeapArray<MetaIndex> meta_starts;
    ::fixy::HeapArray<ScopeHash> scope_hashes;
    ::fixy::HeapArray<CallsiteHash> callsite_hashes;
    ::fixy::HeapArray<OpDepth> depths;

    // Every operation's metadata concatenated. The array above holds each
    // operation's offset into this one.
    ::fixy::HeapArray<TensorMeta> metas;

    uint32_t num_ops = 0;
    uint32_t num_metas = 0;
};

// The reason that the loader refuses a trace file.
enum class TraceLoadError : uint8_t {
    // The file does not open.
    OpenFailed,
    // The size of the file does not read.
    SizeUnknown,
    // The file is shorter than its header.
    HeaderTruncated,
    // The file does not start with the magic CRTR.
    WrongMagic,
    // The version is not TRACE_FORMAT_VERSION.
    WrongVersion,
    // A count of the header is past its limit.
    CountPastLimit,
    // The records of the header counts do not fit in the file.
    CountPastFile,
    // A read ends before the bytes that the size of the file gives.
    ReadTruncated,
    // A read of the file gives an error of the kernel.
    ReadFailed,
    // A metadata record breaks a claim of its type.
    MetaCorrupt,
    // The metadata of an op runs past the metadata count.
    MetaRunPastCount,
};

// The loader reads each field from the bytes of the file with no byte swap.
// The little-endian rule of the platform floor in foundation/Platform.h makes
// that read correct.

inline constexpr uint64_t TRACE_HEADER_BYTES = 16;

// The version of the format.  Version 2 has metadata records of 176 bytes,
// with a storage size of 64 bits.  Version 3 has op records of 88 bytes, with
// the nesting depth of each op.  Version 4 has the tag of each scalar value.
// Version 5 has the guard word of each op in the field of the shape hash: the
// depth, the counts, the tags, the strides, the element types and the scalars
// fold into it (compute_shape_hash of vessel/torch/entry_slots.h).  Version 6
// has a guard word that folds the rank, the layout, the negative and conjugate
// bits and the storage offset of each input (pack_tensor_word).  A word of an
// earlier version compares with no live op.  The loader refuses each other
// version.
inline constexpr uint32_t TRACE_FORMAT_VERSION = 6;

// The length of each metadata record.  A count whose records would not fit
// in the bytes that remain is refused before any array is sized from it.
inline constexpr uint64_t TRACE_META_BYTES = sizeof(TensorMeta);

// The magic at the start of each trace file.
inline constexpr ::fixy::FixedArray<std::byte, 4> TRACE_MAGIC{std::byte{'C'}, std::byte{'R'}, std::byte{'T'},
                                                              std::byte{'R'}};

// The gate of a load.  The load opens a file and reads it, and each of the
// two can wait on the file system, so the context owns IO and Block.  The
// load puts the arrays of the trace on the heap, so the context owns Alloc.
template <class Ctx>
concept CtxFitsTraceLoad = ::foundation::effects::IsExecCtx<Ctx>
                        && ::fixy::fs::CtxFitsFileMint<Ctx, ::fixy::fs::read_only> && ::fixy::fs::CtxAdmitsFs<Ctx>
                        && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Alloc>;

namespace detail {

// The count of records that one read fills.  The buffer of a batch is on the
// stack, so the reads of a load need no allocation.
inline constexpr uint32_t kTraceRecordsPerRead = 64;

// The bytes of the inline scalar values of an op record.
inline constexpr std::size_t kTraceScalarBlockBytes = sizeof(int64_t) * decltype(TraceOpRecord::scalar_values)::size();

// The Bytes bytes at the offset Offset of a record.  The bound of the
// template proves that they lie inside the record.
template <std::size_t Offset, std::size_t Bytes, std::size_t RecordBytes>
    requires(Offset <= RecordBytes && Bytes <= RecordBytes - Offset)
[[nodiscard]] inline ::fixy::View<std::byte const, Bytes>
record_block_(::fixy::View<std::byte const, RecordBytes> record) noexcept {
    return record.template window<Bytes>(Offset).expect("the bound of the template puts the block inside its record");
}

// The field of the type Field at the offset Offset of a record.  The copy of
// the bytes is the value, because the bytes and the platform are
// little-endian.
template <class Field, std::size_t Offset, std::size_t RecordBytes>
    requires(Offset <= RecordBytes && sizeof(Field) <= RecordBytes - Offset)
[[nodiscard]] inline Field read_record_field_(::fixy::View<std::byte const, RecordBytes> record) noexcept {
    ::fixy::FixedArray<std::byte, sizeof(Field)> image{};
    ::fixy::copy(image.view(), record_block_<Offset, sizeof(Field)>(record));
    return ::fixy::bit_cast<Field>(image);
}

// Reads the bytes of buffer from the file.  A read that ends early gives
// ReadTruncated, and an error of the kernel gives ReadFailed.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, TraceLoadError>
read_exact_(Ctx const& io, ::fixy::fs::OwnedFd const& file, ::fixy::View<std::byte> buffer) noexcept {
    using Read = ::fixy::Result<::fixy::Unit, TraceLoadError>;
    const std::size_t wanted = buffer.size();
    return ::fixy::fs::read_full(io, file, buffer)
        .match(
            [wanted](std::size_t count) noexcept -> Read {
                if (count != wanted) return ::fixy::err(TraceLoadError::ReadTruncated);
                return ::fixy::Unit{};
            },
            [](::foundation::os::Errno error) noexcept -> Read {
                ::fixy::report(::fixy::Sink::Err, "load_trace: a read of the trace file failed (error {})\n",
                               ::foundation::os::error_number(error));
                return ::fixy::err(TraceLoadError::ReadFailed);
            });
}

// Reads count records of RecordBytes bytes each, and gives each record with
// its index to on_record.  One read fills one batch of kTraceRecordsPerRead
// records.  The first error of a read or of on_record ends the walk, and the
// walk gives that error.  Complexity: O(count) bytes, and one read for each
// batch.
template <std::size_t RecordBytes, CtxFitsTraceLoad Ctx, class OnRecord>
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, TraceLoadError>
read_records_(Ctx const& io, ::fixy::fs::OwnedFd const& file, uint32_t count, OnRecord&& on_record) noexcept {
    ::fixy::FixedArray<std::byte, RecordBytes * kTraceRecordsPerRead> batch{};
    uint32_t index = 0;
    while (index < count) {
        const std::size_t batch_bytes = std::size_t{::fixy::min(count - index, kTraceRecordsPerRead)} * RecordBytes;
        const ::fixy::View<std::byte> filled =
            batch.view().window(0, batch_bytes).expect("a batch of at most kTraceRecordsPerRead records fits");
        CRUCIBLE_TRY(_, read_exact_(io, file, filled));
        for (::fixy::View<std::byte, RecordBytes> record : filled.template chunks<RecordBytes>()) {
            CRUCIBLE_TRY(_, on_record(index, ::fixy::View<std::byte const, RecordBytes>{record}));
            ++index;
        }
    }
    return ::fixy::Unit{};
}

// Fills the ring entry, the metadata start, the scope hash and the callsite
// hash of op i of trace from its record, and moves meta_cursor past the
// tensors of the op.  An op whose metadata runs past the metadata count gives
// MetaRunPastCount.
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, TraceLoadError>
decode_trace_op_(::fixy::View<std::byte const, sizeof(TraceOpRecord)> op_record, uint32_t i, LoadedTrace& trace,
                 uint32_t& meta_cursor) noexcept {
    TraceRing::Entry& entry = *trace.entries.get(i).expect("an op index is below the op count");
    entry.schema_hash = SchemaHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, schema_hash)>(op_record)};
    entry.shape_hash = ShapeHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, shape_hash)>(op_record)};
    const auto num_inputs = read_record_field_<uint16_t, offsetof(TraceOpRecord, num_inputs)>(op_record);
    const auto num_outputs = read_record_field_<uint16_t, offsetof(TraceOpRecord, num_outputs)>(op_record);
    const auto num_scalars = read_record_field_<uint16_t, offsetof(TraceOpRecord, num_scalars)>(op_record);
    const auto grad_enabled = read_record_field_<uint8_t, offsetof(TraceOpRecord, grad_enabled)>(op_record);
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;
    entry.num_scalar_args = num_scalars;
    // The whole packed set of flags, despite the name of the field.
    entry.op_flags = read_record_field_<uint8_t, offsetof(TraceOpRecord, inference_mode)>(op_record);
    if (grad_enabled != 0) {
        entry.op_flags = static_cast<uint8_t>(entry.op_flags | op_flag::GRAD_ENABLED);
    } else {
        entry.op_flags = static_cast<uint8_t>(entry.op_flags & static_cast<uint8_t>(~op_flag::GRAD_ENABLED));
    }
    // After the flags, because the tag of slot 4 is in the two high bits of
    // op_flags.
    entry.set_packed_scalar_types(read_record_field_<uint16_t, offsetof(TraceOpRecord, scalar_types)>(op_record));
    uint16_t lane = 0;
    const auto scalar_block = record_block_<offsetof(TraceOpRecord, scalar_values), kTraceScalarBlockBytes>(op_record);
    using ScalarLane = ::fixy::Index<decltype(entry.scalar_values)::size()>;
    for (::fixy::View<std::byte const, sizeof(int64_t)> scalar : scalar_block.chunks<sizeof(int64_t)>()) {
        if (lane == num_scalars) break;
        entry.scalar_values.at(ScalarLane::check(lane).expect("the scalar block holds one chunk for each lane")) =
            read_record_field_<int64_t, 0>(scalar);
        ++lane;
    }

    // Widened before adding: two 16-bit counts sum past what 16 bits hold,
    // and the wrap would turn an operation claiming tens of thousands of
    // tensors into one claiming none.
    const uint32_t total_tensors = uint32_t{num_inputs} + uint32_t{num_outputs};
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
        if (total_tensors > trace.num_metas - meta_cursor) [[unlikely]] {
            ::fixy::report(::fixy::Sink::Err,
                           "load_trace: the metadata of op {} ({} tensors at the cursor {}) "
                           "runs past num_metas={}\n",
                           i, total_tensors, meta_cursor, trace.num_metas);
            return ::fixy::err(TraceLoadError::MetaRunPastCount);
        }
        *trace.meta_starts.get(i).expect("an op index is below the op count") = MetaIndex{meta_cursor};
        meta_cursor += total_tensors;
    } else {
        *trace.meta_starts.get(i).expect("an op index is below the op count") = MetaIndex::none();
    }

    *trace.scope_hashes.get(i).expect("an op index is below the op count") =
        ScopeHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, scope_hash)>(op_record)};
    *trace.callsite_hashes.get(i).expect("an op index is below the op count") =
        CallsiteHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, callsite_hash)>(op_record)};
    *trace.depths.get(i).expect("an op index is below the op count") =
        OpDepth{read_record_field_<uint16_t, offsetof(TraceOpRecord, depth)>(op_record)};
    return ::fixy::Unit{};
}

// The report of a name of a trace that the schema table refuses, because the
// table holds a different name of its hash.  The load keeps the names before
// it, and the table of names of the trace stops there.
[[gnu::cold, gnu::noinline]] inline void report_name_collision_(uint64_t schema_hash, ::fixy::TextView name) noexcept {
    ::fixy::report(::fixy::Sink::Err,
                   "load_trace: the trace gives the schema hash {} the name {}, and the schema table holds a "
                   "different name of that hash, so the names of the trace stop there\n",
                   schema_hash, name);
}

// The report of a name of a trace that the schema table refuses, because the
// table holds SCHEMA_TABLE_CAP names and the hash is not one of them.  The
// load keeps the names before it, and the table of names of the trace stops
// there.
[[gnu::cold, gnu::noinline]] inline void report_name_table_full_(uint64_t schema_hash, ::fixy::TextView name) noexcept {
    ::fixy::report(::fixy::Sink::Err,
                   "load_trace: the schema table holds {} names, so it refuses the name {} of the schema hash {}, "
                   "and the names of the trace stop there\n",
                   SCHEMA_TABLE_CAP, name, schema_hash);
}

}  // namespace detail

// Builds one metadata record from its bytes through decode_tensor_meta, the
// checked door of TensorMeta.  A record whose bytes break a claim of its type
// is refused.  Each field is read as the integer of its width, so the bool
// is read as a byte.  The stored address is not read: it has no meaning in
// this process, so the data pointer keeps its null value and a later use of
// it fails loudly.
[[nodiscard]] inline ::fixy::Option<TensorMeta>
decode_trace_meta(::fixy::View<std::byte const, sizeof(TensorMeta)> raw) noexcept {
    using detail::read_record_field_;
    using Lanes = ::fixy::FixedArray<int64_t, kMaxTensorNDim>;
    UncheckedTensorMeta fields{};
    fields.sizes = read_record_field_<Lanes, offsetof(TensorMeta, sizes)>(raw);
    fields.strides = read_record_field_<Lanes, offsetof(TensorMeta, strides)>(raw);
    fields.ndim = read_record_field_<uint8_t, offsetof(TensorMeta, ndim)>(raw);
    fields.dtype = read_record_field_<int8_t, offsetof(TensorMeta, dtype)>(raw);
    fields.device_type = read_record_field_<int8_t, offsetof(TensorMeta, device_type)>(raw);
    fields.device_idx = read_record_field_<int8_t, offsetof(TensorMeta, device_idx)>(raw);
    fields.layout = read_record_field_<int8_t, offsetof(TensorMeta, layout)>(raw);
    fields.requires_grad = read_record_field_<uint8_t, offsetof(TensorMeta, requires_grad)>(raw);
    fields.flags = read_record_field_<uint8_t, offsetof(TensorMeta, flags)>(raw);
    fields.output_nr = read_record_field_<uint8_t, offsetof(TensorMeta, output_nr)>(raw);
    fields.storage_offset = read_record_field_<int64_t, offsetof(TensorMeta, storage_offset)>(raw);
    fields.version = read_record_field_<uint32_t, offsetof(TensorMeta, version)>(raw);
    fields.storage_nbytes = read_record_field_<uint64_t, offsetof(TensorMeta, storage_nbytes)>(raw);
    fields.grad_fn_hash = read_record_field_<uint64_t, offsetof(TensorMeta, grad_fn_hash)>(raw);
    return decode_tensor_meta(fields, ExternalDataPtr{});
}

// THE WRITER
//   The encoders below put each field of a record at the offset that the
//   loader reads it from, as the integer of its width.  So the writer of a
//   trace and the loader agree field by field.

namespace detail {

// Puts the bytes of value at the offset Offset of a record.  The inverse of
// read_record_field_: the bytes and the platform are little-endian.
template <std::size_t Offset, class Field, std::size_t RecordBytes>
    requires(Offset <= RecordBytes && sizeof(Field) <= RecordBytes - Offset)
inline void write_record_field_(::fixy::View<std::byte, RecordBytes> record, Field const& value) noexcept {
    const auto image = ::fixy::bit_cast<::fixy::FixedArray<std::byte, sizeof(Field)>>(value);
    ::fixy::copy(record.template window<sizeof(Field)>(Offset).expect(
                     "the bound of the template puts the field inside its record"),
                 image.view());
}

// The lanes of a block of sizes or strides, as the array that a record holds.
[[nodiscard]] inline ::fixy::FixedArray<int64_t, kMaxTensorNDim> record_lanes_(TensorDimArray const& block) noexcept {
    ::fixy::FixedArray<int64_t, kMaxTensorNDim> lanes{};
    ::fixy::copy(lanes.view(), block.lanes());
    return lanes;
}

}  // namespace detail

// The header of a trace of num_ops ops and num_metas metadata records.
[[nodiscard]] inline ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES>
encode_trace_header(uint32_t num_ops, uint32_t num_metas) noexcept {
    using detail::write_record_field_;
    ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header{};
    const ::fixy::View<std::byte, TRACE_HEADER_BYTES> bytes = header.view();
    write_record_field_<0>(bytes, TRACE_MAGIC);
    write_record_field_<4>(bytes, TRACE_FORMAT_VERSION);
    write_record_field_<8>(bytes, num_ops);
    write_record_field_<12>(bytes, num_metas);
    return header;
}

// The bytes of one op record, at the offsets of TraceOpRecord.
[[nodiscard]] inline ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)>
encode_trace_op(TraceOpRecord const& record) noexcept {
    using detail::write_record_field_;
    ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)> image{};
    const ::fixy::View<std::byte, sizeof(TraceOpRecord)> bytes = image.view();
    write_record_field_<offsetof(TraceOpRecord, schema_hash)>(bytes, record.schema_hash.raw());
    write_record_field_<offsetof(TraceOpRecord, shape_hash)>(bytes, record.shape_hash.raw());
    write_record_field_<offsetof(TraceOpRecord, scope_hash)>(bytes, record.scope_hash.raw());
    write_record_field_<offsetof(TraceOpRecord, callsite_hash)>(bytes, record.callsite_hash.raw());
    write_record_field_<offsetof(TraceOpRecord, scalar_values)>(bytes, record.scalar_values);
    write_record_field_<offsetof(TraceOpRecord, num_inputs)>(bytes, record.num_inputs);
    write_record_field_<offsetof(TraceOpRecord, num_outputs)>(bytes, record.num_outputs);
    write_record_field_<offsetof(TraceOpRecord, num_scalars)>(bytes, record.num_scalars);
    write_record_field_<offsetof(TraceOpRecord, grad_enabled)>(bytes, record.grad_enabled);
    write_record_field_<offsetof(TraceOpRecord, inference_mode)>(bytes, record.inference_mode);
    write_record_field_<offsetof(TraceOpRecord, depth)>(bytes, record.depth.raw());
    write_record_field_<offsetof(TraceOpRecord, scalar_types)>(bytes, record.scalar_types);
    return image;
}

// The bytes of one metadata record, at the offsets of TensorMeta.  The data
// pointer is an address of this process, so the record holds zero there, as
// the image of Serialize.h does, and decode_trace_meta does not read it.  The
// bytes of the pad hold zero too.  So two exports of one computation in two
// runs give the same bytes.
[[nodiscard]] inline ::fixy::FixedArray<std::byte, TRACE_META_BYTES>
encode_trace_meta(TensorMeta const& meta) noexcept {
    using detail::record_lanes_;
    using detail::write_record_field_;
    ::fixy::FixedArray<std::byte, TRACE_META_BYTES> image{};
    const ::fixy::View<std::byte, TRACE_META_BYTES> bytes = image.view();
    write_record_field_<offsetof(TensorMeta, sizes)>(bytes, record_lanes_(meta.sizes));
    write_record_field_<offsetof(TensorMeta, strides)>(bytes, record_lanes_(meta.strides));
    write_record_field_<offsetof(TensorMeta, ndim)>(bytes, meta.ndim);
    write_record_field_<offsetof(TensorMeta, dtype)>(bytes, std::to_underlying(meta.dtype));
    write_record_field_<offsetof(TensorMeta, device_type)>(bytes, std::to_underlying(meta.device_type));
    write_record_field_<offsetof(TensorMeta, device_idx)>(bytes, meta.device_idx);
    write_record_field_<offsetof(TensorMeta, layout)>(bytes, std::to_underlying(meta.layout));
    write_record_field_<offsetof(TensorMeta, requires_grad)>(bytes, static_cast<uint8_t>(meta.requires_grad ? 1 : 0));
    write_record_field_<offsetof(TensorMeta, flags)>(bytes, meta.flags);
    write_record_field_<offsetof(TensorMeta, output_nr)>(bytes, meta.output_nr);
    write_record_field_<offsetof(TensorMeta, storage_offset)>(bytes, meta.storage_offset);
    write_record_field_<offsetof(TensorMeta, version)>(bytes, meta.version);
    write_record_field_<offsetof(TensorMeta, storage_nbytes)>(bytes, meta.storage_nbytes);
    write_record_field_<offsetof(TensorMeta, grad_fn_hash)>(bytes, meta.grad_fn_hash.value());
    return image;
}

// Loads the trace at path.  When should_register_names is true, the schema
// names of the trace go into the global table through schema_table_view.  An
// empty view means that the table is sealed, and the names then stay out.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace_(Ctx const& io, ::fixy::Path<::fixy::tags::source::Sanitized> const& path,
            ::fixy::Option<SchemaTable::MutableView> const& schema_table_view, bool should_register_names) {
    using detail::read_record_field_;

    // A report names no path, because a report writes no raw text.  Each
    // caller knows the path that it gave.
    using Opened = ::fixy::Result<::fixy::fs::OwnedFd, TraceLoadError>;
    CRUCIBLE_TRY(
        trace_file,
        ::fixy::fs::mint_file<::fixy::fs::read_only>(io, path).match(
            [](::fixy::Linear<::fixy::fs::OwnedFd> opened) noexcept -> Opened { return std::move(opened).consume(); },
            [](::foundation::os::Errno error) noexcept -> Opened {
                ::fixy::report(::fixy::Sink::Err, "load_trace: cannot open the trace file (error {})\n",
                               ::foundation::os::error_number(error));
                return ::fixy::err(TraceLoadError::OpenFailed);
            }));

    // The size of the file bounds every count in it, so it is read first.
    using Sized = ::fixy::Result<uint64_t, TraceLoadError>;
    CRUCIBLE_TRY(file_bytes, ::fixy::fs::file_size(io, trace_file)
                                 .match(
                                     [](int64_t size) noexcept -> Sized {
                                         if (size < 0) return ::fixy::err(TraceLoadError::SizeUnknown);
                                         return static_cast<uint64_t>(size);
                                     },
                                     [](::foundation::os::Errno error) noexcept -> Sized {
                                         ::fixy::report(::fixy::Sink::Err,
                                                        "load_trace: cannot read the size of the trace file "
                                                        "(error {})\n",
                                                        ::foundation::os::error_number(error));
                                         return ::fixy::err(TraceLoadError::SizeUnknown);
                                     }));
    if (file_bytes < TRACE_HEADER_BYTES) {
        ::fixy::report(::fixy::Sink::Err, "load_trace: the header of the trace file is truncated\n");
        return ::fixy::err(TraceLoadError::HeaderTruncated);
    }

    ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header{};
    CRUCIBLE_TRY(_, detail::read_exact_(io, trace_file, header.view()));
    const ::fixy::View<std::byte const, TRACE_HEADER_BYTES> header_bytes{header.view()};

    if (!::fixy::equal(detail::record_block_<0, TRACE_MAGIC.size()>(header_bytes), TRACE_MAGIC.view())) {
        ::fixy::report(::fixy::Sink::Err, "load_trace: the trace file does not start with the magic CRTR\n");
        return ::fixy::err(TraceLoadError::WrongMagic);
    }
    const auto version = read_record_field_<uint32_t, 4>(header_bytes);
    if (version != TRACE_FORMAT_VERSION) {
        ::fixy::report(::fixy::Sink::Err, "load_trace: the trace file has the version {}, and the loader reads {}\n",
                       version, TRACE_FORMAT_VERSION);
        return ::fixy::err(TraceLoadError::WrongVersion);
    }

    const auto raw_num_ops = read_record_field_<uint32_t, 8>(header_bytes);
    const auto raw_num_metas = read_record_field_<uint32_t, 12>(header_bytes);
    if (!kTraceNumOpsBound(raw_num_ops) || !kTraceNumMetasBound(raw_num_metas)) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the header counts are past their limits (num_ops={} num_metas={})\n", raw_num_ops,
                       raw_num_metas);
        return ::fixy::err(TraceLoadError::CountPastLimit);
    }
    // The check above refuses an out-of-range count with a message.  The
    // loader then takes each count only through the mint of its gate, so no
    // later read of a count skips the bound.
    const uint32_t num_ops = ::fixy::mint_refined<kTraceNumOpsBound>(raw_num_ops).value();
    const uint32_t num_metas = ::fixy::mint_refined<kTraceNumMetasBound>(raw_num_metas).value();

    // A count is refused when its records cannot fit in the bytes that
    // remain.  The check comes before any array is sized from a count, so a
    // small file that claims millions of records allocates nothing.  The
    // size check above keeps the subtraction from a wrap.
    const uint64_t after_header = file_bytes - TRACE_HEADER_BYTES;
    const uint64_t op_bytes = uint64_t{num_ops} * sizeof(TraceOpRecord);
    if (op_bytes > after_header || uint64_t{num_metas} * TRACE_META_BYTES > after_header - op_bytes) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the header counts do not fit in the file "
                       "(num_ops={} num_metas={}, {} bytes after the header)\n",
                       num_ops, num_metas, after_header);
        return ::fixy::err(TraceLoadError::CountPastFile);
    }

    LoadedTrace trace{};
    trace.num_ops = num_ops;
    trace.num_metas = num_metas;
    trace.entries = ::fixy::mint_heap_array(io.cap().alloc, num_ops, TraceRing::Entry{});
    trace.meta_starts = ::fixy::mint_heap_array(io.cap().alloc, num_ops, MetaIndex{});
    trace.scope_hashes = ::fixy::mint_heap_array(io.cap().alloc, num_ops, ScopeHash{});
    trace.callsite_hashes = ::fixy::mint_heap_array(io.cap().alloc, num_ops, CallsiteHash{});
    trace.depths = ::fixy::mint_heap_array(io.cap().alloc, num_ops, OpDepth{});
    trace.metas = ::fixy::mint_heap_array(io.cap().alloc, num_metas, TensorMeta{});

    using Step = ::fixy::Result<::fixy::Unit, TraceLoadError>;
    uint32_t meta_cursor = 0;
    CRUCIBLE_TRY(
        _, detail::read_records_<sizeof(TraceOpRecord)>(
               io, trace_file, num_ops,
               [&trace, &meta_cursor](uint32_t i,
                                      ::fixy::View<std::byte const, sizeof(TraceOpRecord)> op_record) noexcept -> Step {
                   return detail::decode_trace_op_(op_record, i, trace, meta_cursor);
               }));

    // Each record is read into a buffer of its full length and decoded from
    // there, so no byte of the file reaches a TensorMeta unchecked.
    CRUCIBLE_TRY(_, detail::read_records_<TRACE_META_BYTES>(
                        io, trace_file, num_metas,
                        [&trace](uint32_t i, ::fixy::View<std::byte const, TRACE_META_BYTES> raw) noexcept -> Step {
                            return decode_trace_meta(raw).match(
                                [&trace, i](TensorMeta meta) noexcept -> Step {
                                    *trace.metas.get(i).expect("a metadata index is below the metadata count") = meta;
                                    return ::fixy::Unit{};
                                },
                                [i]() noexcept -> Step {
                                    ::fixy::report(::fixy::Sink::Err,
                                                   "load_trace: the metadata record {} breaks a claim of its type, "
                                                   "so the trace is corrupt\n",
                                                   i);
                                    return ::fixy::err(TraceLoadError::MetaCorrupt);
                                });
                        }));

    // The name table is optional and sits after the metadata.  A failed read
    // in the table ends the table, as a malformed tail does, and the trace
    // still loads.
    ::fixy::FixedArray<std::byte, sizeof(uint32_t)> count_bytes{};
    const bool has_name_count = detail::read_exact_(io, trace_file, count_bytes.view()).is_ok();
    const uint32_t raw_num_names =
        has_name_count
            ? read_record_field_<uint32_t, 0>(::fixy::View<std::byte const, sizeof(uint32_t)>{count_bytes.view()})
            : 0;
    if (raw_num_names > 0 && kTraceNumNamesBound(raw_num_names)) {
        uint32_t num_names = ::fixy::mint_refined<kTraceNumNamesBound>(raw_num_names).value();
        // Once the table is sealed the background thread reads it with no
        // lock, so a trace loaded after the seal keeps its names out of the
        // table.  The trace itself still loads.
        if (!should_register_names) {
            num_names = 0;
        } else if (schema_table_view.is_none()) {
            ::fixy::report(::fixy::Sink::Err,
                           "load_trace: the schema table is sealed, so the {} names of the trace are not registered\n",
                           num_names);
            num_names = 0;
        }
        for (uint32_t i = 0; i < num_names; i++) {
            ::fixy::FixedArray<std::byte, sizeof(uint64_t) + sizeof(uint16_t)> entry_head{};
            if (detail::read_exact_(io, trace_file, entry_head.view()).is_err()) break;
            const ::fixy::View<std::byte const, sizeof(uint64_t) + sizeof(uint16_t)> entry_head_bytes{
                entry_head.view()};
            const auto schema_hash_raw = read_record_field_<uint64_t, 0>(entry_head_bytes);
            const auto raw_name_len = read_record_field_<uint16_t, sizeof(uint64_t)>(entry_head_bytes);
            // A length outside the bound ends the table here. Whatever names
            // were read stay registered, which is this function's policy for a
            // malformed tail.
            if (!kSchemaNameLenRange(raw_name_len)) break;
            const uint16_t name_len = ::fixy::mint_refined<kSchemaNameLenRange>(raw_name_len).value();
            ::fixy::FixedArray<std::byte, SCHEMA_NAME_LEN_MAX> name_bytes{};
            const ::fixy::View<std::byte> name_read =
                name_bytes.view().window(0, name_len).expect("a name of a bounded length fits in its buffer");
            if (detail::read_exact_(io, trace_file, name_read).is_err()) break;
            ::fixy::FixedText<SCHEMA_NAME_LEN_MAX> name_text{};
            for (const std::byte name_byte : name_read) {
                name_text.push(static_cast<char>(name_byte)).expect("a name of a bounded length fits in its text");
            }
            // Bounded here, which is what lets the bytes cross from untrusted
            // file content into the table.  The table copies the characters
            // before the first zero.  A seal that lands during the load ends
            // the table here too: the table refuses every later write.
            auto name = ::fixy::mint_tagged<::fixy::tags::source::External>(name_text.view())
                            .retag<::fixy::tags::source::Sanitized>();
            // A name that the table refuses ends the table here, as a
            // malformed tail does.  A collision is a name that the file gives
            // a held hash in place of its first name.  A full table holds
            // SCHEMA_TABLE_CAP names and refuses each new hash.  The report
            // names the name of each of the two.
            const SchemaRegistration registration = schema_table_view.match(
                [&](SchemaTable::MutableView const& view) {
                    return register_schema_name(io.cap().alloc, view, SchemaHash{schema_hash_raw}, name);
                },
                [] noexcept { return SchemaRegistration{::fixy::err(SchemaRefusal::sealed)}; });
            if (registration.is_err()) {
                if (is_collision(registration)) detail::report_name_collision_(schema_hash_raw, name_text.view());
                if (is_full(registration)) detail::report_name_table_full_(schema_hash_raw, name_text.view());
                break;
            }
        }
    }

    return trace;
}

// Loads the trace at path.  The open and the reads can wait on the file
// system, so the context must own IO and Block.  The foreground context of
// a Vigil owns neither.  The schema names of the trace stay out of the
// global table, because only a thread that holds a Vigil's producer claim
// writes there.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace(Ctx const& io, ::fixy::Path<::fixy::tags::source::Sanitized> const& path) {
    return load_trace_(io, path, ::fixy::none, false);
}

// Loads the trace at path, and puts its schema names into the global table.
// It takes two contexts, because no one context proves the two claims: io
// owns the file reads, and fg proves that the caller holds a Vigil's
// producer claim, which the table asks for before it takes a name.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace(Ctx const& io, VigilFgCtx const& fg, ::fixy::Path<::fixy::tags::source::Sanitized> const& path) {
    return load_trace_(io, path, global_schema_table().mint_mutable_view(fg), true);
}

}  // namespace crucible
