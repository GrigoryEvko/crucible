// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A specialization of relation_state_at cannot build or copy a state, but
// it can view the cells of another walk through relation_state::of.  Here
// the state of ingest views the cells of another relation, which admits
// Checked to Raw.  The view carries the namespace of its cells, and the
// read compares it with its own, so the forged answer never reaches
// admits.
//
// Expected diagnostic: the read calls the function that names a state of
// another reading.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
}  // namespace ingest

namespace forged {
inline constexpr ffc::edge<Checked, Raw> checked_to_raw{};
}  // namespace forged

}  // namespace

template <>
inline constexpr ::foundation::fail_closed::detail::relation_state
    foundation::fail_closed::detail::relation_state_at<^^ingest, 1> =
        ::foundation::fail_closed::detail::relation_state::of(
            ::foundation::fail_closed::detail::relation_cells_at<^^forged, 1>);

namespace {

[[maybe_unused]] constexpr bool answer = ffc::admits(^^ingest, ^^Checked, ^^Raw);

}  // namespace

int main() { return 0; }
