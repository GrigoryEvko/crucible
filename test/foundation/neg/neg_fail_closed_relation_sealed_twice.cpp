// A sealed relation that a second seal tries to widen.
//
// A translation unit that wants one more edge in a shipped relation
// could add the edge and a second seal that counts it.  A relation with
// two seals is a fault of its own, so the read stops the build.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
inline constexpr ffc::seal sealed{.members = 1};
inline constexpr ffc::seal widened{.members = 1};
}  // namespace ingest

static_assert(ffc::edge_count<^^ingest>() == 1);

}  // namespace

int main() { return 0; }
