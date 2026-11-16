// The compile-time checks of crucible/Transaction.h.

#include <crucible/Transaction.h>

namespace crucible {

// A transaction fills one cache line: five words, the place of its last
// event, the status and fifteen bytes of padding.
static_assert(sizeof(Transaction) == 64, "Transaction layout must be 64 bytes");
static_assert(std::is_same_v<decltype(std::declval<Transaction>().event_order), ::fixy::Monotonic<std::uint64_t>>,
              "the stamp of a transaction is a place in the order of the events of its log, which only grows");

// The ring composition adds nothing beyond its three members.
static_assert(sizeof(::fixy::CyclicBuffer<Transaction, 16>)
                  == sizeof(::fixy::FixedArray<Transaction, 16>) + sizeof(::fixy::Cyclic<std::size_t, 16>)
                         + sizeof(::fixy::BoundedMonotonic<std::size_t, 16>),
              "CyclicBuffer<Transaction, N> must stay a zero-overhead composition");

}  // namespace crucible
