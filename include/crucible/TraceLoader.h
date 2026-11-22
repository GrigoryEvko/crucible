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
//   A schema-name table follows the metadata records and ends the file:
//     uint32   name count
//     Then, per name:
//       uint64   schema hash
//       uint16   name length, terminator excluded
//       char     the name itself, not terminated in the file
//
// THE LOAD DOOR.  The loader reads the whole file into one buffer, and the
// check of CheckedTraceImage reads that image once: the header, each count
// against the length of the image, and the name table to its end.  A file that
// ends before a record or a name of its counts, a name length outside its
// range, and a byte after the last name are each refused with a TraceLoadError.
// So each cut of a file that the writer wrote is refused, and the load gives a
// whole trace or no trace.  The decode then reads only the checked image, and
// a refused file registers no name.
//
// THE FIRST FORMAT.  The loader reads the version 1 too, the format of the
// traces that the vessel recorded first.  Its op records have 80 bytes: the
// first 80 bytes of an op record of today, with no depth, no tags and no data
// slots.  Each op then has the depth 0, the tag INT on each scalar slot, and
// no data slot.  Its metadata records have 168 bytes: the bytes of today up to
// the version, then the storage size in 32 bits at byte 156, and the hash of
// the gradient function at byte 160.  The shape hash of a version 1 op is not
// the guard word of today, so it compares with no live op.  A trace of the
// version 1 serves an offline pass, such as the phases of bench_phases.

#include <cstddef>
#include <cstdint>
#include <utility>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TensorMeta.h>
#include <crucible/TraceRing.h>
#include <crucible/ValidatedOp.h>
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

// The most names of a name table.  The writer writes the union of the global
// schema table and the late schema table (crucible/SchemaTable.h), each of
// SCHEMA_TABLE_CAP names.  A load registers the names that the global table
// has room for, and the table refuses each name after that.
inline constexpr uint32_t TRACE_NAMES_MAX = 2 * SCHEMA_TABLE_CAP;

// The typed gates of the three counts of a file.  The loader takes each count
// from its field only through the mint of its gate.  Zero is admitted: an
// empty trace is well-formed.
inline constexpr auto kTraceNumOpsBound = ::fixy::bounded_above<MAX_OPS>;
inline constexpr auto kTraceNumMetasBound = ::fixy::bounded_above<MAX_METAS>;
inline constexpr auto kTraceNumNamesBound = ::fixy::bounded_above<TRACE_NAMES_MAX>;

// A name length is stored on disk as a 16-bit value, so any of them can
// arrive.  The lower bound is one, because a zero-length name names nothing.
// The upper bound is SCHEMA_NAME_MAX, the longest name that the schema table
// holds, so each name that the writer writes fits it.  The check file of this
// header holds the two bounds equal.
inline constexpr uint16_t SCHEMA_NAME_LEN_MIN = 1;
inline constexpr uint16_t SCHEMA_NAME_LEN_MAX = 4096;
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
    // The data slots (DataSlots of crucible/Types.h): bit i is set when slot i
    // holds a data scalar.  Only the bits of the slots below the count can be
    // set.
    uint8_t data_slots = 0;
    // The writer writes zero here, and the loader reads nothing.
    ::fixy::FixedArray<uint8_t, 3> pad{};
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
    // The version is not TRACE_FORMAT_VERSION and not TRACE_FORMAT_FIRST_VERSION.
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
    // An op record breaks a claim of an entry (entry_is_well_formed of
    // crucible/ValidatedOp.h): a schema hash of zero, more inputs than
    // kMaxOpInputs or more outputs than kMaxOpOutputs, more scalars than the
    // inline slots, or a data slot past its count of scalars.
    OpCorrupt,
    // The metadata of an op runs past the metadata count.
    MetaRunPastCount,
    // The file ends inside the name table: before its count, or before a name
    // of its count.
    NameTableTruncated,
    // A name of the table has a length outside kSchemaNameLenRange.
    NameLengthPastLimit,
    // A byte follows the last name of the table.
    TrailingBytes,
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
// bits and the storage offset of each input (pack_tensor_word).  Version 7 has
// the data slots of each op, and a guard word that reads the tag of a data
// slot but not its value.  A word of an earlier version compares with no live
// op.  The writer writes this version.  The loader reads it and the first
// version (THE FIRST FORMAT), and it refuses each other version.
inline constexpr uint32_t TRACE_FORMAT_VERSION = 7;

// The length of each metadata record.  A count whose records would not fit
// in the bytes that remain is refused before any array is sized from it.
inline constexpr uint64_t TRACE_META_BYTES = sizeof(TensorMeta);

// The first version of the format, and the lengths of its op records and its
// metadata records (THE FIRST FORMAT).
inline constexpr uint32_t TRACE_FORMAT_FIRST_VERSION = 1;
inline constexpr std::size_t TRACE_FIRST_OP_BYTES = 80;
inline constexpr std::size_t TRACE_FIRST_META_BYTES = 168;

// The magic at the start of each trace file.
inline constexpr ::fixy::FixedArray<std::byte, 4> TRACE_MAGIC{std::byte{'C'}, std::byte{'R'}, std::byte{'T'},
                                                              std::byte{'R'}};

// The gate of a load.  The load opens a file and reads it, and each of the
// two can wait on the file system, so the context owns IO and Block.  The
// load puts the arrays of the trace on the heap, so the context owns Alloc.
// A name registration spins in the write section of the table, so it owns Div.
template <class Ctx>
concept CtxFitsTraceLoad = ::foundation::effects::IsExecCtx<Ctx>
                        && ::fixy::fs::CtxFitsFileMint<Ctx, ::fixy::fs::read_only> && ::fixy::fs::CtxAdmitsFs<Ctx>
                        && ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::Alloc,
                                                               ::foundation::effects::Effect::Div>;

namespace detail {

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
    image.with_mut(
        [record](auto run) noexcept { ::fixy::copy(run.view(), record_block_<Offset, sizeof(Field)>(record)); });
    return ::fixy::bit_cast<Field>(image);
}

// The trace file, open for reading only, and its borrow.
using TraceFile = ::fixy::fs::FileOf<::fixy::fs::read_only>;
using TraceFileRef = ::fixy::fs::FileRef<TraceFile::rights>;

// Reads the bytes of buffer from the file.  A read that ends early gives
// ReadTruncated, and an error of the kernel gives ReadFailed.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, TraceLoadError> read_exact_(Ctx const& io, TraceFileRef file,
                                                                              ::fixy::View<std::byte> buffer) noexcept {
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

// Fills the ring entry, the metadata start, the scope hash and the callsite
// hash of op i of trace from its record, and moves meta_cursor past the
// tensors of the op.  An op that breaks a claim of an entry gives OpCorrupt,
// and an op whose metadata runs past the metadata count gives
// MetaRunPastCount.  A record of the first version is the first
// TRACE_FIRST_OP_BYTES bytes of a record of today, so the op takes the depth
// 0, the tag 0 on each scalar slot and no data slot (THE FIRST FORMAT).
template <std::size_t RecordBytes>
    requires(RecordBytes == sizeof(TraceOpRecord) || RecordBytes == TRACE_FIRST_OP_BYTES)
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, TraceLoadError>
decode_trace_op_(::fixy::View<std::byte const, RecordBytes> op_record, uint32_t i, LoadedTrace& trace,
                 uint32_t& meta_cursor) noexcept {
    constexpr bool is_first_format = RecordBytes == TRACE_FIRST_OP_BYTES;
    // The op fills a local entry, and the end of the decode writes it and the
    // other values of the op into the columns of the trace, each through its
    // element door.
    TraceRing::Entry entry{};
    entry.schema_hash = SchemaHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, schema_hash)>(op_record)};
    entry.shape_hash = ShapeHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, shape_hash)>(op_record)};
    const auto num_inputs = read_record_field_<uint16_t, offsetof(TraceOpRecord, num_inputs)>(op_record);
    const auto num_outputs = read_record_field_<uint16_t, offsetof(TraceOpRecord, num_outputs)>(op_record);
    const auto num_scalars = read_record_field_<uint16_t, offsetof(TraceOpRecord, num_scalars)>(op_record);
    const auto grad_enabled = read_record_field_<uint8_t, offsetof(TraceOpRecord, grad_enabled)>(op_record);
    uint8_t data_bits = 0;
    uint16_t scalar_types = 0;
    OpDepth depth{};
    if constexpr (!is_first_format) {
        data_bits = read_record_field_<uint8_t, offsetof(TraceOpRecord, data_slots)>(op_record);
        scalar_types = read_record_field_<uint16_t, offsetof(TraceOpRecord, scalar_types)>(op_record);
        depth = OpDepth{read_record_field_<uint16_t, offsetof(TraceOpRecord, depth)>(op_record)};
    }
    // The word of the data slots and the count of the scalars must fit the
    // fields of an entry: no bit past the inline slots, and at most
    // kScalarSlots scalars.  So a count of 261 does not narrow to 5.  The
    // validator of an entry checks the other claims below.
    const bool fits_entry = num_scalars <= kScalarSlots
                         && DataSlots::from_bits(data_bits).match(
                             [&entry](DataSlots data) noexcept {
                                 entry.set_data_slots(data);
                                 return true;
                             },
                             [] noexcept { return false; });
    if (!fits_entry) [[unlikely]]
        return ::fixy::err(TraceLoadError::OpCorrupt);
    entry.num_inputs = num_inputs;
    entry.num_outputs = num_outputs;
    // The check above keeps the count at most kScalarSlots.
    entry.num_scalar_args = static_cast<uint8_t>(num_scalars);
    // The whole packed set of flags, despite the name of the field.
    entry.op_flags = read_record_field_<uint8_t, offsetof(TraceOpRecord, inference_mode)>(op_record);
    if (grad_enabled != 0) {
        entry.op_flags = static_cast<uint8_t>(entry.op_flags | op_flag::GRAD_ENABLED);
    } else {
        entry.op_flags = static_cast<uint8_t>(entry.op_flags & static_cast<uint8_t>(~op_flag::GRAD_ENABLED));
    }
    // After the flags, because the tag of slot 4 is in the two high bits of
    // op_flags.
    entry.set_packed_scalar_types(scalar_types);
    uint16_t lane = 0;
    const auto scalar_block = record_block_<offsetof(TraceOpRecord, scalar_values), kTraceScalarBlockBytes>(op_record);
    using ScalarLane = ::fixy::Index<decltype(entry.scalar_values)::size()>;
    for (::fixy::View<std::byte const, sizeof(int64_t)> scalar : scalar_block.template chunks<sizeof(int64_t)>()) {
        if (lane == num_scalars) break;
        const int64_t value = read_record_field_<int64_t, 0>(scalar);
        entry.scalar_values.with_element(
            ScalarLane::check(lane).expect("the scalar block holds one chunk for each lane"),
            [value](int64_t& held) noexcept { held = value; });
        ++lane;
    }
    // The entry passes the one validator of an entry from outside, as an
    // entry of the C entries does.  So the load admits no entry that the
    // trust ladder of the vessel refuses.
    if (!entry_is_well_formed(entry)) [[unlikely]]
        return ::fixy::err(TraceLoadError::OpCorrupt);

    // Widened before adding: two 16-bit counts sum past what 16 bits hold,
    // and the wrap would turn an operation claiming tens of thousands of
    // tensors into one claiming none.
    const uint32_t total_tensors = uint32_t{num_inputs} + uint32_t{num_outputs};
    MetaIndex meta_start = MetaIndex::none();
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
        meta_start = MetaIndex{meta_cursor};
        meta_cursor += total_tensors;
    }

    const auto write_at_op = [i](auto& column, auto const& value) noexcept {
        column.with_element(
            i, [&value](auto& slot) noexcept -> void { slot = value; },
            [] noexcept { ::fixy::fatal("an op index is below the op count"); });
    };
    write_at_op(trace.entries, entry);
    write_at_op(trace.meta_starts, meta_start);
    write_at_op(trace.scope_hashes,
                ScopeHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, scope_hash)>(op_record)});
    write_at_op(trace.callsite_hashes,
                CallsiteHash{read_record_field_<uint64_t, offsetof(TraceOpRecord, callsite_hash)>(op_record)});
    write_at_op(trace.depths, depth);
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

namespace detail {

// The offsets of the fields of a metadata record of the first version (THE
// FIRST FORMAT).  They are fixed numbers, so a change of the layout of
// TensorMeta does not move them.
inline constexpr std::size_t kFirstMetaSizesAt = 0;
inline constexpr std::size_t kFirstMetaStridesAt = 64;
inline constexpr std::size_t kFirstMetaNdimAt = 136;
inline constexpr std::size_t kFirstMetaDtypeAt = 137;
inline constexpr std::size_t kFirstMetaDeviceTypeAt = 138;
inline constexpr std::size_t kFirstMetaDeviceIdxAt = 139;
inline constexpr std::size_t kFirstMetaLayoutAt = 140;
inline constexpr std::size_t kFirstMetaRequiresGradAt = 141;
inline constexpr std::size_t kFirstMetaFlagsAt = 142;
inline constexpr std::size_t kFirstMetaOutputNrAt = 143;
inline constexpr std::size_t kFirstMetaStorageOffsetAt = 144;
inline constexpr std::size_t kFirstMetaVersionAt = 152;
inline constexpr std::size_t kFirstMetaStorageBytesAt = 156;
inline constexpr std::size_t kFirstMetaGradFnHashAt = 160;

// Builds one metadata record of the first version through decode_tensor_meta,
// as decode_trace_meta builds a record of today.  The storage size has 32
// bits there.
[[nodiscard]] inline ::fixy::Option<TensorMeta>
decode_first_trace_meta_(::fixy::View<std::byte const, TRACE_FIRST_META_BYTES> raw) noexcept {
    using Lanes = ::fixy::FixedArray<int64_t, kMaxTensorNDim>;
    UncheckedTensorMeta fields{};
    fields.sizes = read_record_field_<Lanes, kFirstMetaSizesAt>(raw);
    fields.strides = read_record_field_<Lanes, kFirstMetaStridesAt>(raw);
    fields.ndim = read_record_field_<uint8_t, kFirstMetaNdimAt>(raw);
    fields.dtype = read_record_field_<int8_t, kFirstMetaDtypeAt>(raw);
    fields.device_type = read_record_field_<int8_t, kFirstMetaDeviceTypeAt>(raw);
    fields.device_idx = read_record_field_<int8_t, kFirstMetaDeviceIdxAt>(raw);
    fields.layout = read_record_field_<int8_t, kFirstMetaLayoutAt>(raw);
    fields.requires_grad = read_record_field_<uint8_t, kFirstMetaRequiresGradAt>(raw);
    fields.flags = read_record_field_<uint8_t, kFirstMetaFlagsAt>(raw);
    fields.output_nr = read_record_field_<uint8_t, kFirstMetaOutputNrAt>(raw);
    fields.storage_offset = read_record_field_<int64_t, kFirstMetaStorageOffsetAt>(raw);
    fields.version = read_record_field_<uint32_t, kFirstMetaVersionAt>(raw);
    fields.storage_nbytes = read_record_field_<uint32_t, kFirstMetaStorageBytesAt>(raw);
    fields.grad_fn_hash = read_record_field_<uint64_t, kFirstMetaGradFnHashAt>(raw);
    return decode_tensor_meta(fields, ExternalDataPtr{});
}

// One metadata record of the format of MetaBytes: of today, or of the first
// version.
template <std::size_t MetaBytes>
    requires(MetaBytes == TRACE_META_BYTES || MetaBytes == TRACE_FIRST_META_BYTES)
[[nodiscard]] inline ::fixy::Option<TensorMeta>
decode_meta_record_(::fixy::View<std::byte const, MetaBytes> raw) noexcept {
    if constexpr (MetaBytes == TRACE_META_BYTES) {
        return decode_trace_meta(raw);
    } else {
        return decode_first_trace_meta_(raw);
    }
}

// The bytes of the count of a name table, and of the hash and the length that
// start each name.
inline constexpr std::size_t kTraceNameCountBytes = sizeof(uint32_t);
inline constexpr std::size_t kTraceNameHeadBytes = sizeof(uint64_t) + sizeof(uint16_t);

// The most bytes of a name table: the count, and TRACE_NAMES_MAX names of the
// longest length.
inline constexpr uint64_t kTraceNameTableMaxBytes =
    kTraceNameCountBytes + uint64_t{TRACE_NAMES_MAX} * (kTraceNameHeadBytes + SCHEMA_NAME_LEN_MAX);

// One name of a name table: its schema hash and its characters.
struct TraceName {
    SchemaHash hash{};
    ::fixy::View<std::byte const> characters{};
};

// The first name of rest, and rest moves past it.  A rest that ends before
// the head or the characters of the name gives NameTableTruncated, and a
// length outside kSchemaNameLenRange gives NameLengthPastLimit.  Complexity:
// constant.
[[nodiscard]] inline ::fixy::Result<TraceName, TraceLoadError>
take_trace_name_(::fixy::View<std::byte const>& rest) noexcept {
    if (rest.size() < kTraceNameHeadBytes) return ::fixy::err(TraceLoadError::NameTableTruncated);
    const ::fixy::View<std::byte const, kTraceNameHeadBytes> head =
        rest.template window<kTraceNameHeadBytes>(0).expect("the test above keeps the head in rest");
    const auto raw_length = read_record_field_<uint16_t, sizeof(uint64_t)>(head);
    if (!kSchemaNameLenRange(raw_length)) return ::fixy::err(TraceLoadError::NameLengthPastLimit);
    const std::size_t length = ::fixy::mint_refined<kSchemaNameLenRange>(raw_length).value();
    const std::size_t after = kTraceNameHeadBytes + length;
    if (after > rest.size()) return ::fixy::err(TraceLoadError::NameTableTruncated);
    const TraceName name{
        .hash = SchemaHash{read_record_field_<uint64_t, 0>(head)},
        .characters = rest.window(kTraceNameHeadBytes, length).expect("the test above keeps the name in rest"),
    };
    rest = rest.window(after, rest.size() - after).expect("the test above keeps the name in rest");
    return name;
}

// The report of a refusal of the name table, with the offset in the image of
// the start of the table.
[[gnu::cold, gnu::noinline]] inline void report_name_table_(TraceLoadError error, uint64_t table_at) noexcept {
    switch (error) {
        case TraceLoadError::NameTableTruncated:
            ::fixy::report(::fixy::Sink::Err,
                           "load_trace: the file ends inside the name table at the offset {}, so the file is cut\n",
                           table_at);
            return;
        case TraceLoadError::NameLengthPastLimit:
            ::fixy::report(::fixy::Sink::Err,
                           "load_trace: a name of the table at the offset {} has a length outside [{}, {}]\n", table_at,
                           SCHEMA_NAME_LEN_MIN, SCHEMA_NAME_LEN_MAX);
            return;
        case TraceLoadError::TrailingBytes:
            ::fixy::report(::fixy::Sink::Err, "load_trace: bytes follow the last name of the table at the offset {}\n",
                           table_at);
            return;
        case TraceLoadError::CountPastLimit:
            ::fixy::report(::fixy::Sink::Err, "load_trace: the name table at the offset {} counts more than {} names\n",
                           table_at, TRACE_NAMES_MAX);
            return;
        default:
            return;
    }
}

}  // namespace detail

// The format of a trace file: the format of today, or the first format (THE
// FIRST FORMAT).
enum class TraceFormat : uint8_t {
    current,
    first,
};

// A header that check() accepted (THE LOAD DOOR): the magic CRTR, a version
// that the loader reads, and counts inside their gates.  Only check() makes
// one.
class CheckedTraceHeader {
public:
    // Checks the bytes of a header: the magic, the version, and each count
    // against its gate.  It gives a Result of a CheckedTraceHeader or of the
    // TraceLoadError.  The return type is deduced, because a Result of the
    // class needs the complete class.  Complexity: constant.
    [[nodiscard]] static auto check(::fixy::View<std::byte const, TRACE_HEADER_BYTES> header) noexcept;

    [[nodiscard]] TraceFormat format() const noexcept { return format_; }
    [[nodiscard]] uint32_t num_ops() const noexcept { return num_ops_; }
    [[nodiscard]] uint32_t num_metas() const noexcept { return num_metas_; }

    // The bytes of one op record and of one metadata record of the format.
    [[nodiscard]] uint64_t op_record_bytes() const noexcept {
        return format_ == TraceFormat::first ? TRACE_FIRST_OP_BYTES : sizeof(TraceOpRecord);
    }
    [[nodiscard]] uint64_t meta_record_bytes() const noexcept {
        return format_ == TraceFormat::first ? TRACE_FIRST_META_BYTES : TRACE_META_BYTES;
    }

    // The offset of the name table: the header, then the records of the
    // counts.  The gates of the counts keep the sum below 2^33, so it does not
    // wrap.
    [[nodiscard]] uint64_t table_at() const noexcept {
        return TRACE_HEADER_BYTES + uint64_t{num_ops_} * op_record_bytes() + uint64_t{num_metas_} * meta_record_bytes();
    }

private:
    // The image holds a copy of its checked header.
    friend class CheckedTraceImage;

    CheckedTraceHeader() noexcept = default;

    TraceFormat format_ = TraceFormat::current;
    uint32_t num_ops_ = 0;
    uint32_t num_metas_ = 0;
};

inline auto CheckedTraceHeader::check(::fixy::View<std::byte const, TRACE_HEADER_BYTES> header) noexcept {
    using detail::read_record_field_;
    using Checked = ::fixy::Result<CheckedTraceHeader, TraceLoadError>;
    if (!::fixy::equal(detail::record_block_<0, TRACE_MAGIC.size()>(header), TRACE_MAGIC.view())) {
        ::fixy::report(::fixy::Sink::Err, "load_trace: the trace file does not start with the magic CRTR\n");
        return Checked{::fixy::err(TraceLoadError::WrongMagic)};
    }
    const auto version = read_record_field_<uint32_t, 4>(header);
    if (version != TRACE_FORMAT_VERSION && version != TRACE_FORMAT_FIRST_VERSION) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the trace file has the version {}, and the loader reads {} and {}\n", version,
                       TRACE_FORMAT_VERSION, TRACE_FORMAT_FIRST_VERSION);
        return Checked{::fixy::err(TraceLoadError::WrongVersion)};
    }
    const auto raw_num_ops = read_record_field_<uint32_t, 8>(header);
    const auto raw_num_metas = read_record_field_<uint32_t, 12>(header);
    if (!kTraceNumOpsBound(raw_num_ops) || !kTraceNumMetasBound(raw_num_metas)) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the header counts are past their limits (num_ops={} num_metas={})\n", raw_num_ops,
                       raw_num_metas);
        return Checked{::fixy::err(TraceLoadError::CountPastLimit)};
    }
    // The test above refuses an out-of-range count with a message.  The header
    // then takes each count only through the mint of its gate.
    CheckedTraceHeader checked{};
    checked.format_ = version == TRACE_FORMAT_FIRST_VERSION ? TraceFormat::first : TraceFormat::current;
    checked.num_ops_ = ::fixy::mint_refined<kTraceNumOpsBound>(raw_num_ops).value();
    checked.num_metas_ = ::fixy::mint_refined<kTraceNumMetasBound>(raw_num_metas).value();
    return Checked{checked};
}

// An image of a trace file that check() accepted (THE LOAD DOOR): a checked
// header, the records of its counts, and a name table that holds the names of
// its count and ends at the end of the image.  It borrows the bytes of the
// image, so it must not outlive them.  Only check() makes one.
class CheckedTraceImage {
public:
    // Checks an image whose first bytes are the checked header.  Records of
    // the counts that do not fit give CountPastFile.  A name count past
    // kTraceNumNamesBound gives CountPastLimit.  The errors of the names are
    // those of the table (TraceLoadError).  It gives a Result of a
    // CheckedTraceImage or of the TraceLoadError, with a deduced return type,
    // as CheckedTraceHeader::check.  Complexity: linear in the names.
    [[nodiscard]] static auto check(CheckedTraceHeader const& header, ::fixy::View<std::byte const> image) noexcept;

    // Checks a whole image: its header, then the rest.  An image shorter than
    // a header gives HeaderTruncated.
    [[nodiscard]] static auto check(::fixy::View<std::byte const> image) noexcept;

    [[nodiscard]] CheckedTraceHeader const& header() const noexcept { return header_; }

    // The op records and the metadata records, each of the length of its
    // format times its count.
    [[nodiscard]] ::fixy::View<std::byte const> op_records() const noexcept { return op_records_; }
    [[nodiscard]] ::fixy::View<std::byte const> meta_records() const noexcept { return meta_records_; }

    // The names of the table, after its count, and the count.  The names end
    // at the end of the image.
    [[nodiscard]] ::fixy::View<std::byte const> names() const noexcept { return names_; }
    [[nodiscard]] uint32_t num_names() const noexcept { return num_names_; }

private:
    CheckedTraceImage() noexcept = default;

    CheckedTraceHeader header_ = CheckedTraceHeader{};
    ::fixy::View<std::byte const> op_records_{};
    ::fixy::View<std::byte const> meta_records_{};
    ::fixy::View<std::byte const> names_{};
    uint32_t num_names_ = 0;
};

inline auto CheckedTraceImage::check(CheckedTraceHeader const& header, ::fixy::View<std::byte const> image) noexcept {
    using Checked = ::fixy::Result<CheckedTraceImage, TraceLoadError>;
    const uint64_t table_at = header.table_at();
    if (table_at > image.size()) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the header counts do not fit in the file "
                       "(num_ops={} num_metas={}, {} bytes in the file)\n",
                       header.num_ops(), header.num_metas(), image.size());
        return Checked{::fixy::err(TraceLoadError::CountPastFile)};
    }
    CheckedTraceImage checked{};
    checked.header_ = header;
    const std::size_t op_bytes = header.num_ops() * header.op_record_bytes();
    checked.op_records_ = image.window(TRACE_HEADER_BYTES, op_bytes).expect("the records fit in the image");
    checked.meta_records_ = image.window(TRACE_HEADER_BYTES + op_bytes, table_at - TRACE_HEADER_BYTES - op_bytes)
                                .expect("the records fit in the image");
    const ::fixy::View<std::byte const> table =
        image.window(table_at, image.size() - table_at).expect("the table starts inside the image");
    if (table.size() < detail::kTraceNameCountBytes) {
        detail::report_name_table_(TraceLoadError::NameTableTruncated, table_at);
        return Checked{::fixy::err(TraceLoadError::NameTableTruncated)};
    }
    const auto raw_num_names = detail::read_record_field_<uint32_t, 0>(
        table.template window<detail::kTraceNameCountBytes>(0).expect("the test above keeps the count in the table"));
    if (!kTraceNumNamesBound(raw_num_names)) {
        detail::report_name_table_(TraceLoadError::CountPastLimit, table_at);
        return Checked{::fixy::err(TraceLoadError::CountPastLimit)};
    }
    checked.num_names_ = ::fixy::mint_refined<kTraceNumNamesBound>(raw_num_names).value();
    checked.names_ = table.window(detail::kTraceNameCountBytes, table.size() - detail::kTraceNameCountBytes)
                         .expect("the count is the start of the table");
    ::fixy::View<std::byte const> rest = checked.names_;
    for (uint32_t index = 0; index < checked.num_names_; ++index) {
        const ::fixy::Result<detail::TraceName, TraceLoadError> taken = detail::take_trace_name_(rest);
        if (taken.is_err()) {
            const TraceLoadError error = taken.err().value_or(TraceLoadError::NameTableTruncated);
            detail::report_name_table_(error, table_at);
            return Checked{::fixy::err(error)};
        }
    }
    if (rest.size() != 0) {
        detail::report_name_table_(TraceLoadError::TrailingBytes, table_at);
        return Checked{::fixy::err(TraceLoadError::TrailingBytes)};
    }
    return Checked{checked};
}

inline auto CheckedTraceImage::check(::fixy::View<std::byte const> image) noexcept {
    using Checked = ::fixy::Result<CheckedTraceImage, TraceLoadError>;
    if (image.size() < TRACE_HEADER_BYTES) {
        ::fixy::report(::fixy::Sink::Err, "load_trace: the header of the trace file is truncated\n");
        return Checked{::fixy::err(TraceLoadError::HeaderTruncated)};
    }
    const ::fixy::Result<CheckedTraceHeader, TraceLoadError> header = CheckedTraceHeader::check(
        image.template window<TRACE_HEADER_BYTES>(0).expect("the test above keeps the header in the image"));
    return header.match(
        [image](CheckedTraceHeader const& checked_header) noexcept { return check(checked_header, image); },
        [](TraceLoadError error) noexcept { return Checked{::fixy::err(error)}; });
}

namespace detail {

// Decodes the op records and then the metadata records of a checked image of
// the format of OpBytes and MetaBytes into trace, whose arrays hold the counts
// of the image.  The first refusal of a record ends the decode.  Complexity:
// linear in the records.
template <std::size_t OpBytes, std::size_t MetaBytes>
[[nodiscard]] inline ::fixy::Result<::fixy::Unit, TraceLoadError> decode_trace_records_(CheckedTraceImage const& image,
                                                                                        LoadedTrace& trace) noexcept {
    uint32_t meta_cursor = 0;
    uint32_t index = 0;
    for (const ::fixy::View<std::byte const, OpBytes> op_record : image.op_records().template chunks<OpBytes>()) {
        CRUCIBLE_TRY(_, decode_trace_op_<OpBytes>(op_record, index, trace, meta_cursor));
        ++index;
    }
    // Each record goes through the checked door of TensorMeta, so no byte of
    // the file reaches a TensorMeta unchecked.
    index = 0;
    for (const ::fixy::View<std::byte const, MetaBytes> raw : image.meta_records().template chunks<MetaBytes>()) {
        const ::fixy::Option<TensorMeta> meta = decode_meta_record_<MetaBytes>(raw);
        if (meta.is_none()) [[unlikely]] {
            ::fixy::report(::fixy::Sink::Err,
                           "load_trace: the metadata record {} breaks a claim of its type, so the trace is corrupt\n",
                           index);
            return ::fixy::err(TraceLoadError::MetaCorrupt);
        }
        for (const TensorMeta& decoded : meta) {
            trace.metas.with_element(
                index, [&decoded](TensorMeta& slot) noexcept { slot = decoded; },
                [] noexcept { ::fixy::fatal("a metadata index is below the metadata count"); });
        }
        ++index;
    }
    return ::fixy::Unit{};
}

}  // namespace detail

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
    lanes.with_mut([&block](auto run) noexcept { ::fixy::copy(run.view(), block.lanes()); });
    return lanes;
}

}  // namespace detail

// The header of a trace of num_ops ops and num_metas metadata records.
[[nodiscard]] inline ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES>
encode_trace_header(uint32_t num_ops, uint32_t num_metas) noexcept {
    using detail::write_record_field_;
    ::fixy::FixedArray<std::byte, TRACE_HEADER_BYTES> header{};
    header.with_mut([num_ops, num_metas](auto run) noexcept {
        const ::fixy::View<std::byte, TRACE_HEADER_BYTES> bytes = run.view();
        write_record_field_<0>(bytes, TRACE_MAGIC);
        write_record_field_<4>(bytes, TRACE_FORMAT_VERSION);
        write_record_field_<8>(bytes, num_ops);
        write_record_field_<12>(bytes, num_metas);
    });
    return header;
}

// The bytes of one op record, at the offsets of TraceOpRecord.
[[nodiscard]] inline ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)>
encode_trace_op(TraceOpRecord const& record) noexcept {
    using detail::write_record_field_;
    ::fixy::FixedArray<std::byte, sizeof(TraceOpRecord)> image{};
    image.with_mut([&record](auto run) noexcept {
        const ::fixy::View<std::byte, sizeof(TraceOpRecord)> bytes = run.view();
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
        write_record_field_<offsetof(TraceOpRecord, data_slots)>(bytes, record.data_slots);
    });
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
    image.with_mut([&meta](auto run) noexcept {
        const ::fixy::View<std::byte, TRACE_META_BYTES> bytes = run.view();
        write_record_field_<offsetof(TensorMeta, sizes)>(bytes, record_lanes_(meta.sizes));
        write_record_field_<offsetof(TensorMeta, strides)>(bytes, record_lanes_(meta.strides));
        write_record_field_<offsetof(TensorMeta, ndim)>(bytes, meta.ndim);
        write_record_field_<offsetof(TensorMeta, dtype)>(bytes, std::to_underlying(meta.dtype));
        write_record_field_<offsetof(TensorMeta, device_type)>(bytes, std::to_underlying(meta.device_type));
        write_record_field_<offsetof(TensorMeta, device_idx)>(bytes, meta.device_idx);
        write_record_field_<offsetof(TensorMeta, layout)>(bytes, std::to_underlying(meta.layout));
        write_record_field_<offsetof(TensorMeta, requires_grad)>(bytes,
                                                                 static_cast<uint8_t>(meta.requires_grad ? 1 : 0));
        write_record_field_<offsetof(TensorMeta, flags)>(bytes, meta.flags);
        write_record_field_<offsetof(TensorMeta, output_nr)>(bytes, meta.output_nr);
        write_record_field_<offsetof(TensorMeta, storage_offset)>(bytes, meta.storage_offset);
        write_record_field_<offsetof(TensorMeta, version)>(bytes, meta.version);
        write_record_field_<offsetof(TensorMeta, storage_nbytes)>(bytes, meta.storage_nbytes);
        write_record_field_<offsetof(TensorMeta, grad_fn_hash)>(bytes, meta.grad_fn_hash.value());
    });
    return image;
}

// The use of the names of a trace: a load that leaves the global schema table
// alone, or a load that registers each name of the trace in it.
enum class TraceNameUse : uint8_t {
    skip,
    register_names,
};

// Loads the trace at path (THE LOAD DOOR).  With register_names, the schema
// names of the trace go into the global table through schema_table_view after
// the whole trace decodes.  An empty view means that the table is sealed, and
// the names then stay out.  Complexity: linear in the bytes of the file.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace_(Ctx const& io, ::fixy::Path<::fixy::tags::source::Sanitized> const& path,
            ::fixy::Option<SchemaTable::MutableView> const& schema_table_view, TraceNameUse name_use) {
    // A report names no path, because a report writes no raw text.  Each
    // caller knows the path that it gave.
    using Opened = ::fixy::Result<detail::TraceFile, TraceLoadError>;
    CRUCIBLE_TRY(
        trace_file,
        ::fixy::fs::mint_file<::fixy::fs::read_only>(io, path).match(
            [](::fixy::Linear<detail::TraceFile> opened) noexcept -> Opened { return std::move(opened).consume(); },
            [](::foundation::os::Errno error) noexcept -> Opened {
                ::fixy::report(::fixy::Sink::Err, "load_trace: cannot open the trace file (error {})\n",
                               ::foundation::os::error_number(error));
                return ::fixy::err(TraceLoadError::OpenFailed);
            }));
    const detail::TraceFileRef source = trace_file.ref();

    // The size of the file bounds every count in it, so it is read first.
    using Sized = ::fixy::Result<uint64_t, TraceLoadError>;
    CRUCIBLE_TRY(file_bytes, ::fixy::fs::file_size(io, source)
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
    CRUCIBLE_TRY(
        _, header.with_mut([&io, &source](auto run) noexcept { return detail::read_exact_(io, source, run.view()); }));
    CRUCIBLE_TRY(checked_header,
                 CheckedTraceHeader::check(::fixy::View<std::byte const, TRACE_HEADER_BYTES>{header.view()}));

    // A file longer than the records of its counts and the largest name table
    // holds bytes that no table reaches, so the load refuses it before it sizes
    // the buffer of the file.  A file shorter than its records gives
    // CountPastFile in the check of the image, after a read of that short
    // file, so a small file that claims millions of records allocates nothing.
    const uint64_t table_at = checked_header.table_at();
    if (file_bytes > table_at && file_bytes - table_at > detail::kTraceNameTableMaxBytes) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the file holds {} bytes after its records, and a name table holds at most {}\n",
                       file_bytes - table_at, detail::kTraceNameTableMaxBytes);
        return ::fixy::err(TraceLoadError::TrailingBytes);
    }

    // The image of the file in one buffer: the header, then one read of the
    // rest.  Each step after the check reads the checked image, and no step
    // reads the file again.
    ::fixy::HeapArray<std::byte> image = ::fixy::mint_heap_array(io.cap().alloc, file_bytes, std::byte{0});
    auto [read, filled] = ::fixy::with_mut(std::move(image), [&io, &source, &header, file_bytes](auto run) noexcept {
        ::fixy::copy(
            run.view().template window<TRACE_HEADER_BYTES>(0).expect("the size test keeps the header in the image"),
            header.view());
        return detail::read_exact_(io, source,
                                   run.view()
                                       .window(TRACE_HEADER_BYTES, file_bytes - TRACE_HEADER_BYTES)
                                       .expect("the header is the start of the image"));
    });
    image = std::move(filled);
    CRUCIBLE_TRY(_, std::move(read));
    CRUCIBLE_TRY(checked, CheckedTraceImage::check(checked_header, ::fixy::View<std::byte const>{image.view()}));

    LoadedTrace trace{};
    trace.num_ops = checked_header.num_ops();
    trace.num_metas = checked_header.num_metas();
    trace.entries = ::fixy::mint_heap_array(io.cap().alloc, trace.num_ops, TraceRing::Entry{});
    trace.meta_starts = ::fixy::mint_heap_array(io.cap().alloc, trace.num_ops, MetaIndex{});
    trace.scope_hashes = ::fixy::mint_heap_array(io.cap().alloc, trace.num_ops, ScopeHash{});
    trace.callsite_hashes = ::fixy::mint_heap_array(io.cap().alloc, trace.num_ops, CallsiteHash{});
    trace.depths = ::fixy::mint_heap_array(io.cap().alloc, trace.num_ops, OpDepth{});
    trace.metas = ::fixy::mint_heap_array(io.cap().alloc, trace.num_metas, TensorMeta{});

    if (checked_header.format() == TraceFormat::first) {
        CRUCIBLE_TRY(_, (detail::decode_trace_records_<TRACE_FIRST_OP_BYTES, TRACE_FIRST_META_BYTES>(checked, trace)));
    } else {
        CRUCIBLE_TRY(_, (detail::decode_trace_records_<sizeof(TraceOpRecord), TRACE_META_BYTES>(checked, trace)));
    }

    // The whole trace decodes before the first name goes into the table, so a
    // refused file registers no name.  Once the table is sealed the background
    // thread reads it with no lock, so a trace loaded after the seal keeps its
    // names out of the table.  The trace itself still loads.
    if (name_use == TraceNameUse::skip || checked.num_names() == 0) return trace;
    if (schema_table_view.is_none()) {
        ::fixy::report(::fixy::Sink::Err,
                       "load_trace: the schema table is sealed, so the {} names of the trace are not registered\n",
                       checked.num_names());
        return trace;
    }
    ::fixy::View<std::byte const> rest = checked.names();
    for (uint32_t index = 0; index < checked.num_names(); ++index) {
        const detail::TraceName entry =
            detail::take_trace_name_(rest).expect("the checked image holds each name of its count");
        ::fixy::FixedText<SCHEMA_NAME_LEN_MAX> name_text{};
        for (const std::byte name_byte : entry.characters) {
            name_text.append(static_cast<char>(name_byte)).expect("a name of a bounded length fits in its text");
        }
        // The check of the image bounds the length, which is what lets the
        // bytes cross from untrusted file content into the table.  The table
        // copies the characters before the first zero.  A seal that lands
        // during the load ends the names here too: the table refuses every
        // later write.
        auto name = ::fixy::mint_tagged<::fixy::tags::source::External>(name_text.view())
                        .retag<::fixy::tags::source::Sanitized>();
        // A name that the table refuses ends the names here.  A collision is a
        // name that the file gives a held hash in place of its first name.  A
        // full table holds SCHEMA_TABLE_CAP names and refuses each new hash.
        // The report names the name of each of the two.
        const SchemaRegistration registration = schema_table_view.match(
            [&](SchemaTable::MutableView const& view) {
                return register_schema_name(io, io.cap().alloc, view, entry.hash, name);
            },
            [] noexcept { return SchemaRegistration{::fixy::err(SchemaRefusal::sealed)}; });
        if (registration.is_err()) {
            if (is_collision(registration)) detail::report_name_collision_(entry.hash.raw(), name_text.view());
            if (is_full(registration)) detail::report_name_table_full_(entry.hash.raw(), name_text.view());
            break;
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
    return load_trace_(io, path, ::fixy::none, TraceNameUse::skip);
}

// Loads the trace at path, and puts its schema names into the global table.
// It takes two contexts, because no one context proves the two claims: io
// owns the file reads, and fg proves that the caller holds a Vigil's
// producer claim, which the table asks for before it takes a name.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace(Ctx const& io, VigilFgCtx const& fg, ::fixy::Path<::fixy::tags::source::Sanitized> const& path) {
    return load_trace_(io, path, global_schema_table().mint_mutable_view(fg), TraceNameUse::register_names);
}

}  // namespace crucible
