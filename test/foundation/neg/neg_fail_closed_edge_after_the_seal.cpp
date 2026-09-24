// An edge added to a sealed relation from outside its header.
//
// The relation `ingest` states one edge and seals that count.  The
// second block opens the namespace again and adds an edge, as a
// translation unit could after it includes the header of a shipped
// relation.  Every read of a sealed relation counts the edges again, so
// the admission check stops the build with the name of the fault rather
// than admitting the new pair.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};
struct Stored {};

namespace ingest {
inline constexpr ffc::edge<Checked, Stored> checked_to_stored{};
inline constexpr ffc::seal sealed{.members = 1};
}  // namespace ingest

namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
}  // namespace ingest

static_assert(ffc::admits<^^ingest, Raw, Checked>());

}  // namespace

int main() { return 0; }
