// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A query reads the walk of a relation through the variable template
// relation_state_at, and a translation unit can specialize a variable
// template.  Here the state of ingest is a copy of the state of another
// relation, which admits Checked to Raw.  The copy constructor of a state
// is private, and the access check applies to the initializer of an
// explicit specialization, so the copy does not compile.
//
// Expected diagnostic: the copy constructor of the state is private.

#include <foundation/Quarantine.h>
#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

namespace ingest {
[[maybe_unused]] inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
}  // namespace ingest

namespace forged {
[[maybe_unused]] inline constexpr ffc::edge<Checked, Raw> checked_to_raw{};
}  // namespace forged

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges relation_state_at of ingest with a copy of another state")
template <>
inline constexpr ::foundation::fail_closed::detail::relation_state
    foundation::fail_closed::detail::relation_state_at<^^ingest, 1> =
        ::foundation::fail_closed::detail::relation_state_at<^^forged, 1>;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
