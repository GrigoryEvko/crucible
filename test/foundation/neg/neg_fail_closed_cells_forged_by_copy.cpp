// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The view of a relation reads the cells that the walk keeps in the
// variable template relation_cells_at, and a translation unit can
// specialize a variable template.  Here the cells of ingest are a copy of
// the cells of another relation, which admits Checked to Raw.  The copy
// constructor of the cells is private, and the walk is its one friend.
// The access check applies to the initializer of an explicit
// specialization, so the copy does not compile.
//
// Expected diagnostic: the copy constructor of the cells is private.

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

template <>
inline constexpr ::foundation::fail_closed::detail::relation_cells<1>
    foundation::fail_closed::detail::relation_cells_at<^^ingest, 1> =
        ::foundation::fail_closed::detail::relation_cells_at<^^forged, 1>;

int main() { return 0; }
