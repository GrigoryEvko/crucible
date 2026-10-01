// The compile-time checks of crucible/ledger/Ledger.h.

#include <crucible/ledger/Ledger.h>

namespace crucible::ledger {

namespace ledger_detail::self_test {

// An empty view answers every question with the caller's conservative value
// and never with a zero it invented.
inline const VerdictLookup s_from_empty =
    LedgerView::empty(LedgerError::StoreReadFailed).lookup(VerdictId::TimerFloorNanos);

static_assert(std::is_same_v<decltype(LedgerView{}.lookup(VerdictId::TimerFloorNanos)), VerdictLookup>,
              "the only way out of a LedgerView is a VerdictLookup");

// VerdictLookup must not grow a bare accessor. If one is ever added, the
// fail-closed contract is gone: a caller could read a value without naming
// a fallback, and a miss would hand back a default-constructed zero that
// looks exactly like a measured zero.
template <class T>
concept HasBareValueAccessor = requires(T const& lookup) { lookup.value(); };
static_assert(!HasBareValueAccessor<VerdictLookup>,
              "VerdictLookup must never expose value(); use value_or_conservative()");

template <class T>
concept HasDereference = requires(T const& lookup) { *lookup; };
static_assert(!HasDereference<VerdictLookup>, "VerdictLookup must never expose operator*");

}  // namespace ledger_detail::self_test

}  // namespace crucible::ledger
