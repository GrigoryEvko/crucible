// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A relation sealed at its header counts every member.  A member added
// after the header changes the count, and every read of the relation
// checks the count again.  every_edge_is_admitted reads the seal before
// it answers, so the late member stops the build there.  The relation
// here holds no edge, so admits is never asked and cannot read the seal
// in its place.  Without that read, every_edge_is_admitted answers over
// the late member.
//
// Expected diagnostic: the seal check in every_edge_is_admitted calls the
// function that names a member outside the seal.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

namespace ingest {
inline constexpr int width = 3;
inline constexpr ffc::seal sealed{.members = 1};
}  // namespace ingest

namespace ingest {
struct Late {};
}  // namespace ingest

[[maybe_unused]] constexpr auto answer = ffc::every_edge_is_admitted<^^ingest>();

}  // namespace

int main() { return 0; }
