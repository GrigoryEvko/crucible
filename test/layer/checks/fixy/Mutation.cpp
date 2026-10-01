// The compile-time checks of fixy/Mutation.h.

#include <fixy/Mutation.h>

namespace fixy {

static_assert(sizeof(AppendOnly<char*>) == sizeof(std::vector<char*>),
              "AppendOnly<char*> must collapse to sizeof(Storage<char*>). The grade "
              "(Length{size_t}) is computed from c.size() rather than stored "
              "separately, which holds only while SeqPrefixLattice supplies a "
              "grade_of trait method.");
static_assert(sizeof(AppendOnly<std::uint64_t>) == sizeof(std::vector<std::uint64_t>),
              "AppendOnly<uint64_t> must collapse to sizeof(Storage<T>). The grade is "
              "computed from c.size() rather than stored separately.");

static_assert(sizeof(OrderedAppendOnly<std::uint64_t>) == sizeof(AppendOnly<std::uint64_t>),
              "OrderedAppendOnly must collapse empty KeyFn/Cmp to zero layout cost");

static_assert(sizeof(Monotonic<uint32_t, std::less<uint32_t>>) == sizeof(uint32_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: value and grade have the "
              "same type and collapse to one storage cell.");
static_assert(sizeof(Monotonic<uint64_t, std::less<uint64_t>>) == sizeof(uint64_t),
              "Monotonic<T, EmptyCmp> must be zero-cost: value and grade have the "
              "same type and collapse to one storage cell.");

static_assert(sizeof(BoundedMonotonic<std::uint32_t, 1024U>) == sizeof(std::uint32_t),
              "BoundedMonotonic must collapse to underlying T");
static_assert(!std::is_trivially_copyable_v<BoundedMonotonic<std::uint32_t, 1024U>>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<BoundedMonotonic<std::uint32_t, 1024U>>,
              "std::bit_cast and std::start_lifetime_as must not build a counter above its bound");

static_assert(sizeof(WriteOnceNonNull<int*>) == sizeof(int*));
static_assert(sizeof(WriteOnceNonNull<void*>) == sizeof(void*));

static_assert(alignof(AtomicMonotonic<uint64_t>) >= 64, "AtomicMonotonic must be cache-line aligned: repeated "
                                                        "advance/bump/CAS traffic invalidates the consumer's "
                                                        "cached line every iteration, so the counter must not "
                                                        "share a line with unrelated embedder state.");
static_assert(alignof(AtomicMonotonic<uint32_t>) >= 64);
static_assert(sizeof(AtomicMonotonic<uint64_t>) >= 64, "AtomicMonotonic occupies a full cache line by "
                                                       "construction; embedders rely on the counter NOT "
                                                       "sharing a line with any field touched on the "
                                                       "producer/consumer hot path.");
static_assert(sizeof(AtomicMonotonic<uint32_t>) >= 64);

}  // namespace fixy
