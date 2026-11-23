// The compile-time checks of crucible/TraceLoader.h.

#include <crucible/TraceLoader.h>

namespace crucible {

// The strong hash types are the size of the integers they wrap.  The format
// puts the depth at offset 80, the tags of the scalars at 82, the data slots
// at 84 and three bytes of pad after them, so a record of the file is 88
// bytes.
static_assert(sizeof(TraceOpRecord) == 88, "TraceOpRecord must be 88 bytes");
static_assert(offsetof(TraceOpRecord, depth) == 80, "the depth follows the packed op flags");
static_assert(offsetof(TraceOpRecord, scalar_types) == 82, "the tags of the scalars follow the depth");
static_assert(offsetof(TraceOpRecord, data_slots) == 84, "the data slots follow the tags of the scalars");
static_assert(std::is_trivially_copyable_v<TraceOpRecord>);
static_assert(std::is_standard_layout_v<TraceOpRecord>);

// The reader of TraceLoader.h reads a metadata record at the offsets of
// TensorMeta, which offsetof gives.
static_assert(std::is_standard_layout_v<TensorMeta>, "the record offsets below come from offsetof");
static_assert(offsetof(TensorMeta, output_nr) < 144, "the shortest record must carry every byte-sized field");

// An op record of the first format is the start of an op record of today, up
// to the depth, so the fields that the two formats share have one offset.  A
// metadata record of the first format holds eight lanes of sizes and eight of
// strides, as TensorMeta does.
static_assert(TRACE_FIRST_OP_BYTES == offsetof(TraceOpRecord, depth), "the first op record ends before the depth");
static_assert(kMaxTensorNDim * sizeof(int64_t) == detail::kFirstMetaStridesAt - detail::kFirstMetaSizesAt,
              "a first metadata record holds the lanes of the sizes of today");
static_assert(detail::kFirstMetaGradFnHashAt + sizeof(uint64_t) == TRACE_FIRST_META_BYTES,
              "the hash of the gradient function ends the first metadata record");

// The loader takes each name that the schema table holds, so the writer writes
// no name that a load refuses.  The writer writes the union of the global and
// the late tables.
static_assert(SCHEMA_NAME_LEN_MAX == SCHEMA_NAME_MAX, "a name of the file is as long as a name of the schema table");
static_assert(TRACE_NAMES_MAX == SCHEMA_TABLE_CAP + LATE_SCHEMA_TABLE_CAP,
              "a name table holds the names of the two schema tables");

}  // namespace crucible
