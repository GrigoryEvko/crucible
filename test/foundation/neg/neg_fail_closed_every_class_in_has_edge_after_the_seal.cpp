// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A relation sealed at its header counts every member.  A member added
// after the header changes the count, and every read of the relation
// checks the count again.  every_class_in_has_edge reads the seal before
// it answers, so the late member stops the build there.  Without that
// read, every_class_in_has_edge answers over the late member.
//
// Expected diagnostic: the seal check in every_class_in_has_edge calls
// the function that names a member outside the seal.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};
struct Stored {};

namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
inline constexpr ffc::seal sealed{.members = 1};
}  // namespace ingest

namespace ingest {
inline constexpr ffc::edge<Checked, Stored> checked_to_stored{};
}  // namespace ingest

namespace vacant {}

[[maybe_unused]] constexpr auto answer = ffc::every_class_in_has_edge<^^ingest, ^^vacant, ffc::EdgeEnd::Either>();

}  // namespace

int main() { return 0; }
