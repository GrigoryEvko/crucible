// An edge declared outside the relation's namespace is inert.  The
// only Raw -> Checked edge here lives in `elsewhere`, where it is a
// real edge, and the transition is constrained on `ingest`, so the
// call fails its constraint.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};
struct Stored {};

namespace ingest {
inline constexpr ffc::edge<Checked, Stored> checked_to_stored{};
}  // namespace ingest

namespace elsewhere {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
}  // namespace elsewhere

static_assert(ffc::Admitted<^^elsewhere, Raw, Checked>);

template <class From, class To>
    requires ffc::Admitted<^^ingest, From, To>
constexpr int transition(From const&, To const&) noexcept {
    return 0;
}

}  // namespace

int main() { return transition(Raw{}, Checked{}); }
