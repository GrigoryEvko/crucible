// The compile-time checks of crucible/Transaction.h.

#include <crucible/Transaction.h>

namespace crucible {

// The reading is not trivially copyable, so no byte copy builds a
// transaction that claims a time.  The empty state of the reading adds
// eight bytes to the layout, and the place in the order of displacements
// adds eight more.
static_assert(sizeof(Transaction) == 64, "Transaction layout must be 64 bytes");
static_assert(std::is_same_v<decltype(std::declval<Transaction>().ts_ns), std::optional<Transaction::Timestamp>>,
              "a transaction timestamp must carry its monotonic-clock provenance");
static_assert(!std::is_trivially_copyable_v<Transaction>,
              "a transaction must not be built from bytes, because its timestamp claims a clock read");

// The ring composition adds nothing beyond its three members.
static_assert(sizeof(::fixy::CyclicBuffer<Transaction, 16>)
                  == sizeof(::fixy::FixedArray<Transaction, 16>) + sizeof(::fixy::Cyclic<std::size_t, 16>)
                         + sizeof(::fixy::BoundedMonotonic<std::size_t, 16>),
              "CyclicBuffer<Transaction, N> must stay a zero-overhead composition");

}  // namespace crucible
