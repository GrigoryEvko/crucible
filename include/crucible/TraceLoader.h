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
//   Op records, 80 bytes each, at the offsets of TraceOpRecord:
//     uint64 schema hash, uint64 shape hash,
//     uint64 scope hash, uint64 callsite hash,
//     int64  scalar values, five of them,
//     uint16 input count, uint16 output count, uint16 scalar count,
//     uint8  gradient flag, uint8 packed operation flags
//
//   Metadata records, 176 bytes each, at the offsets of the tensor metadata
//   struct.  Each field is read through the checked doors of that struct.
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
#include <fixy/FixedArray.h>
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
};

struct LoadedTrace {
    // Parallel arrays, indexed by the same operation position.
    ::fixy::HeapArray<TraceRing::Entry> entries;
    ::fixy::HeapArray<MetaIndex> meta_starts;
    ::fixy::HeapArray<ScopeHash> scope_hashes;
    ::fixy::HeapArray<CallsiteHash> callsite_hashes;

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
// with a storage size of 64 bits.  The loader refuses each other version.
inline constexpr uint32_t TRACE_FORMAT_VERSION = 2;

// The length of each metadata record.  A count whose records would not fit
// in the bytes that remain is refused before any array is sized from it.
inline constexpr uint64_t TRACE_META_BYTES = sizeof(TensorMeta);

// The magic at the start of each trace file.
inline constexpr ::foundation::core::FixedArray<std::byte, 4> TRACE_MAGIC{std::byte{'C'}, std::byte{'R'},
                                                                          std::byte{'T'}, std::byte{'R'}};

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

// The bytes of the lanes of one dimension array of a metadata record.
inline constexpr std::size_t kTraceDimBlockBytes = sizeof(int64_t) * kMaxTensorNDim;

// The bytes of the inline scalar values of an op record.
inline constexpr std::size_t kTraceScalarBlockBytes =
    sizeof(int64_t) * decltype(TraceOpRecord::scalar_values)::capacity;

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
    ::foundation::core::FixedArray<std::byte, sizeof(Field)> image{};
    ::fixy::copy(image.view(), record_block_<Offset, sizeof(Field)>(record));
    return ::fixy::bit_cast<Field>(image);
}

// A lane inside the rank must fit the extent bound that the storage-span
// arithmetic relies on.  A lane past the rank must hold zero, because the
// wire image writes every lane and two equal descriptors must write equal
// bytes.
[[nodiscard]] inline bool read_trace_dims_(::fixy::View<std::byte const, kTraceDimBlockBytes> block, uint8_t ndim,
                                           TensorDimArray& out) noexcept {
    uint8_t lane_index = 0;
    for (::fixy::View<std::byte const, sizeof(int64_t)> lane_bytes : block.chunks<sizeof(int64_t)>()) {
        const auto lane = read_record_field_<int64_t, 0>(lane_bytes);
        const bool lane_is_valid = lane_index < ndim ? lane <= kMaxTensorDimExtent : lane == 0;
        if (!lane_is_valid) return false;
        out[lane_index] = tensor_dim(lane);
        ++lane_index;
    }
    return true;
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
    ::foundation::core::FixedArray<std::byte, RecordBytes * kTraceRecordsPerRead> batch{};
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
    uint16_t lane = 0;
    const auto scalar_block = record_block_<offsetof(TraceOpRecord, scalar_values), kTraceScalarBlockBytes>(op_record);
    for (::fixy::View<std::byte const, sizeof(int64_t)> scalar : scalar_block.chunks<sizeof(int64_t)>()) {
        if (lane == num_scalars) break;
        entry.scalar_values[lane] = read_record_field_<int64_t, 0>(scalar);
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
    return ::fixy::Unit{};
}

}  // namespace detail

// Builds one metadata record from its bytes through the checked doors of
// TensorMeta.  A record whose bytes break a claim of its type is refused.
// The bool is read as a byte, because a bool that holds any other byte is
// undefined behaviour to read.  The stored address is not read: it has no
// meaning in this process, so the field keeps its null value and a later
// use of it fails loudly.
[[nodiscard]] inline ::fixy::Option<TensorMeta>
decode_trace_meta(::fixy::View<std::byte const, sizeof(TensorMeta)> raw) noexcept {
    using detail::read_record_field_;
    TensorMeta meta{};

    const auto ndim = read_record_field_<uint8_t, offsetof(TensorMeta, ndim)>(raw);
    if (ndim > kMaxTensorNDim) return ::fixy::none;
    meta.ndim = make_ndim(::fixy::mint_refined<::fixy::bounded_above<kMaxTensorNDim>>(ndim));
    if (!detail::read_trace_dims_(detail::record_block_<offsetof(TensorMeta, sizes), detail::kTraceDimBlockBytes>(raw),
                                  ndim, meta.sizes))
        return ::fixy::none;
    if (!detail::read_trace_dims_(
            detail::record_block_<offsetof(TensorMeta, strides), detail::kTraceDimBlockBytes>(raw), ndim, meta.strides))
        return ::fixy::none;

    const auto dtype = read_record_field_<int8_t, offsetof(TensorMeta, dtype)>(raw);
    const auto device_type = read_record_field_<int8_t, offsetof(TensorMeta, device_type)>(raw);
    const auto layout = read_record_field_<int8_t, offsetof(TensorMeta, layout)>(raw);
    if (!valid_scalar_type(dtype) || !valid_device_type(device_type) || !valid_layout(layout)) return ::fixy::none;
    meta.dtype = make_scalar_type(::fixy::mint_refined<valid_scalar_type>(dtype));
    meta.device_type = make_device_type(::fixy::mint_refined<valid_device_type>(device_type));
    meta.layout = make_layout(::fixy::mint_refined<valid_layout>(layout));

    // The host is -1, and a device index is never negative otherwise.  A
    // flag bit that no meta_flags constant names is reserved and written as
    // zero.
    constexpr uint8_t known_flags = meta_flags::IS_LEAF | meta_flags::IS_CONTIGUOUS | meta_flags::HAS_GRAD_FN
                                  | meta_flags::IS_VIEW | meta_flags::IS_NEG | meta_flags::IS_CONJ;
    const auto device_idx = read_record_field_<int8_t, offsetof(TensorMeta, device_idx)>(raw);
    const auto requires_grad = read_record_field_<uint8_t, offsetof(TensorMeta, requires_grad)>(raw);
    const auto flags = read_record_field_<uint8_t, offsetof(TensorMeta, flags)>(raw);
    if (device_idx < -1 || requires_grad > 1 || (flags & ~known_flags) != 0) return ::fixy::none;
    meta.device_idx = device_idx;
    meta.requires_grad = requires_grad == 1;
    meta.flags = flags;

    meta.output_nr = read_record_field_<uint8_t, offsetof(TensorMeta, output_nr)>(raw);
    meta.storage_offset = read_record_field_<int64_t, offsetof(TensorMeta, storage_offset)>(raw);
    meta.version = read_record_field_<uint32_t, offsetof(TensorMeta, version)>(raw);
    meta.storage_nbytes = read_record_field_<uint64_t, offsetof(TensorMeta, storage_nbytes)>(raw);
    meta.grad_fn_hash = grad_fn_hash(read_record_field_<uint64_t, offsetof(TensorMeta, grad_fn_hash)>(raw));
    return ::fixy::Option<TensorMeta>::some(meta);
}

// Loads the trace at path.  When should_register_names is true, the schema
// names of the trace go into the global table through schema_table_view.  An
// empty view means that the table is sealed, and the names then stay out.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace_(Ctx const& io, ::fixy::Path<::fixy::tags::source::Sanitized> path,
            ::fixy::Option<SchemaTable::MutableView> const& schema_table_view, bool should_register_names) {
    using detail::read_record_field_;

    // A report names no path, because a report writes no raw text.  Each
    // caller knows the path that it gave.
    using Opened = ::fixy::Result<::fixy::fs::OwnedFd, TraceLoadError>;
    CRUCIBLE_TRY(trace_file, ::fixy::fs::mint_file<::fixy::fs::read_only>(io, std::move(path))
                                 .match([](::fixy::Linear<::fixy::fs::OwnedFd> opened) noexcept
                                            -> Opened { return std::move(opened).consume(); },
                                        [](::foundation::os::Errno error) noexcept -> Opened {
                                            ::fixy::report(::fixy::Sink::Err,
                                                           "load_trace: cannot open the trace file (error {})\n",
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

    ::foundation::core::FixedArray<std::byte, TRACE_HEADER_BYTES> header{};
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
    ::foundation::core::FixedArray<std::byte, sizeof(uint32_t)> count_bytes{};
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
            ::foundation::core::FixedArray<std::byte, sizeof(uint64_t) + sizeof(uint16_t)> entry_head{};
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
            ::foundation::core::FixedArray<std::byte, SCHEMA_NAME_LEN_MAX> name_bytes{};
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
            const bool was_registered = schema_table_view.match(
                [&](SchemaTable::MutableView const& view) {
                    return register_schema_name(view, SchemaHash{schema_hash_raw}, name);
                },
                [] noexcept { return false; });
            if (!was_registered) break;
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
load_trace(Ctx const& io, ::fixy::Path<::fixy::tags::source::Sanitized> path) {
    return load_trace_(io, std::move(path), ::fixy::none, false);
}

// Loads the trace at path, and puts its schema names into the global table.
// It takes two contexts, because no one context proves the two claims: io
// owns the file reads, and fg proves that the caller holds a Vigil's
// producer claim, which the table asks for before it takes a name.
template <CtxFitsTraceLoad Ctx>
[[nodiscard]] inline ::fixy::Result<LoadedTrace, TraceLoadError>
load_trace(Ctx const& io, VigilFgCtx const& fg, ::fixy::Path<::fixy::tags::source::Sanitized> path) {
    return load_trace_(io, std::move(path), global_schema_table().mint_mutable_view(fg), true);
}

}  // namespace crucible
