// The compile-time checks of crucible/MetaLog.h.

#include <crucible/MetaLog.h>

#include <cstdint>
#include <type_traits>

namespace crucible {

// The view of a MetaRun states that it holds at most kMetaLogCapacity records.
// That rests on the doors: no default run, no public constructor and no
// conversion from a View, and no assignment, so no code writes a run after
// its door.  A run is a View and nothing more.
static_assert(!std::is_default_constructible_v<MetaRun>);
static_assert(!std::is_constructible_v<MetaRun, ::fixy::View<const TensorMeta>>);
static_assert(!std::is_convertible_v<::fixy::View<const TensorMeta>, MetaRun>);
static_assert(!std::is_copy_assignable_v<MetaRun> && !std::is_move_assignable_v<MetaRun>);
static_assert(!std::is_assignable_v<MetaRun&, ::fixy::View<const TensorMeta>>);
static_assert(std::is_trivially_copy_constructible_v<MetaRun>);
static_assert(sizeof(MetaRun) == sizeof(::fixy::View<const TensorMeta>));

// The count of the records of an op is the sum of two counts of 16 bits, so
// the run of an op fits the log (the door of an op, ValidatedOpDoor).
static_assert(uint32_t{::fixy::max_of<uint16_t>} + uint32_t{::fixy::max_of<uint16_t>} <= kMetaLogCapacity);
static_assert(MetaLog::CAPACITY == kMetaLogCapacity);

// The door of a fixed extent takes each extent up to the capacity, and the
// checked door takes only the dynamic extent.
static_assert(MetaRunFixedExtent<1> && MetaRunFixedExtent<kMetaLogCapacity>);
static_assert(!MetaRunFixedExtent<kMetaLogCapacity + 1> && !MetaRunFixedExtent<::fixy::dynamic_extent>);
static_assert(MetaRunDynamicExtent<::fixy::dynamic_extent> && !MetaRunDynamicExtent<4>);

// The state of each side lives in its end, on a cache line of its own.  An end
// holds the Ref of its log and then its state, so the state starts the second
// line of the end, and no line of a side is the line of head_ or of tail_.
static_assert(alignof(MetaLog::producer_state) == 64 && sizeof(MetaLog::producer_state) == 64);
static_assert(sizeof(MetaLog::Producer) == 128 && alignof(MetaLog::Producer) == 64);
static_assert(alignof(MetaLog::consumer_state) == 64 && sizeof(MetaLog::consumer_state) == 64);
static_assert(sizeof(MetaLog::Consumer) == 128 && alignof(MetaLog::Consumer) == 64);

// The position past a run is the start of the run plus its count.
inline constexpr ValidMetaAppendCount kThreeRecords =
    ::fixy::mint_refined<ValidMetaAppendCount::predicate_type{}, ValidMetaAppendCount::value_type>(3u);
static_assert(advance_meta_index(MetaIndex{7}, kThreeRecords) == MetaIndex{10});
static_assert(advance_meta_index(MetaIndex{(uint64_t{1} << 32) - 1}, kThreeRecords)
                  == MetaIndex{(uint64_t{1} << 32) + 2},
              "the position passes 2^32 with no wrap");

}  // namespace crucible
