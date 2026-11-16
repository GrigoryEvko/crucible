// The compile-time checks of crucible/TraceLoader.h.

#include <crucible/TraceLoader.h>

namespace crucible {

// The strong hash types are the size of the integers they wrap.  Version 4
// of the format puts the depth at offset 80, the tags of the scalars at 82,
// and four bytes of pad after them, so a record of the file is 88 bytes.
static_assert(sizeof(TraceOpRecord) == 88, "TraceOpRecord must be 88 bytes");
static_assert(offsetof(TraceOpRecord, depth) == 80, "the depth follows the packed op flags");
static_assert(offsetof(TraceOpRecord, scalar_types) == 82, "the tags of the scalars follow the depth");
static_assert(std::is_trivially_copyable_v<TraceOpRecord>);
static_assert(std::is_standard_layout_v<TraceOpRecord>);

// The reader of TraceLoader.h reads a metadata record at the offsets of
// TensorMeta, which offsetof gives.
static_assert(std::is_standard_layout_v<TensorMeta>, "the record offsets below come from offsetof");
static_assert(offsetof(TensorMeta, output_nr) < 144, "the shortest record must carry every byte-sized field");

}  // namespace crucible
