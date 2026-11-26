// The compile-time checks of crucible/MetaLog.h.

#include <crucible/MetaLog.h>

#include <type_traits>

namespace crucible {

// The state of the producer lives in its end, on a cache line of its own.  An
// end holds the Ref of its log and then its state, so the state starts the
// second line of the end, and no line of the producer is the line of head_ or
// of tail_.  The consumer end keeps no state.
static_assert(alignof(MetaLog::producer_state) == 64 && sizeof(MetaLog::producer_state) == 64);
static_assert(sizeof(MetaLog::Producer) == 128 && alignof(MetaLog::Producer) == 64);
static_assert(std::is_empty_v<MetaLog::consumer_state>);

// The position past a run is the start of the run plus its count.
inline constexpr ValidMetaAppendCount kThreeRecords =
    ::fixy::mint_refined<ValidMetaAppendCount::predicate_type{}, ValidMetaAppendCount::value_type>(3u);
static_assert(advance_meta_index(MetaIndex{7}, kThreeRecords) == MetaIndex{10});
static_assert(advance_meta_index(MetaIndex{(uint64_t{1} << 32) - 1}, kThreeRecords)
                  == MetaIndex{(uint64_t{1} << 32) + 2},
              "the position passes 2^32 with no wrap");

}  // namespace crucible
