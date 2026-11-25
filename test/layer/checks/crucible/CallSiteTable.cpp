// The compile-time checks of crucible/CallSiteTable.h.

#include <crucible/CallSiteTable.h>

namespace crucible {

static_assert(sizeof(CallSiteTable) >= CallSiteTable::SET_CAP * sizeof(CallsiteHash),
              "CallSiteTable footprint should be dominated by seen[]");
static_assert(sizeof(CallSiteTable)
                  <= CallSiteTable::SET_CAP * sizeof(CallsiteHash) + sizeof(::fixy::AppendOnly<CallSiteTable::Entry>),
              "CallSiteTable grew beyond its hash array and its log of the entries");

}  // namespace crucible
