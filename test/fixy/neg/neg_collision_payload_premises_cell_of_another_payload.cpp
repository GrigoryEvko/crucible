// The verdict of a binding reads the premises of its payload from their
// sealed cell.  Only the build of the cell makes one, and the build reads the
// payload that its key names.  This file gives the premises cell of a Result
// with a plain error, which I002 refuses under the strict Security pole, the
// cell of int.  The verdict reads the cell with the key of its own payload, so
// the read refuses the cell of another payload, and the forgery admits
// nothing.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

namespace fixture {
struct plain_error final {};
using Fails = ::foundation::core::Result<int, plain_error>;
}  // namespace fixture

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives the premises cell of a payload the cell of another payload")
template <>
inline constexpr fixy::collision::detail::payload_premises_cell
    fixy::collision::detail::payload_premises_cell_v<fixy::collision::verdict_key<fixture::Fails>> =
        fixy::collision::detail::build_payload_premises_cell(^^fixy::collision::verdict_key<int>);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::ValidComposition<fixture::Fails>);

int main() { return 0; }
