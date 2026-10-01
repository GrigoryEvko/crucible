// The compile-time checks of crucible/TraceLoader.h.

#include <crucible/TraceLoader.h>

namespace crucible {

// The strong hash types are the size of the integers they wrap, so the record
// stays byte-compatible with a file written before they were introduced.
static_assert(sizeof(TraceOpRecord) == 80, "TraceOpRecord must be 80 bytes");
static_assert(std::is_trivially_copyable_v<TraceOpRecord>);
static_assert(std::is_standard_layout_v<TraceOpRecord>);

// The reader of TraceLoader.h reads a metadata record at the offsets of
// TensorMeta, which offsetof gives.
static_assert(std::is_standard_layout_v<TensorMeta>, "the record offsets below come from offsetof");
static_assert(offsetof(TensorMeta, output_nr) < 144, "the shortest record must carry every byte-sized field");

}  // namespace crucible
