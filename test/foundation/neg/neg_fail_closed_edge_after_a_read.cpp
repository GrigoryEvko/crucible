// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A query reads one walk of a relation for each count of its members.
// The first read below walks the sealed relation and finds the count of
// its seal.  The late member changes the count, so the second read walks
// again and finds the member outside the seal.  A read that kept the
// first walk for the whole relation would answer over the old members.
//
// Expected diagnostic: the seal check in admits, the second read, calls
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

[[maybe_unused]] constexpr auto first_answer = ffc::edge_count<^^ingest>();

namespace ingest {
inline constexpr ffc::edge<Checked, Stored> checked_to_stored{};
}  // namespace ingest

[[maybe_unused]] constexpr auto second_answer = ffc::admits(^^ingest, ^^Checked, ^^Stored);

}  // namespace

int main() { return 0; }
