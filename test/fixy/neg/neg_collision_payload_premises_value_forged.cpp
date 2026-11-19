// The cache of the premises of a payload holds a sealed cell.  The
// constructors of the cell are private, and the build of the cell is their
// one friend, so a specialization of the cache cannot hold premises that no
// pass gave.  This file tries to give a Result with a plain error, which I002
// refuses under the strict Security pole, the premises of no failure.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

namespace fixture {
struct plain_error final {};
using Fails = ::foundation::core::Result<int, plain_error>;
}  // namespace fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges the premises of a payload that I002 refuses")
template <>
inline constexpr fixy::collision::detail::payload_premises_cell
    fixy::collision::detail::payload_premises_cell_v<fixy::collision::verdict_key<fixture::Fails>> = {};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
