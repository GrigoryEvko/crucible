// A sentinel is an edge only when its type is
// foundation::fail_closed::edge.  A struct of the same name declared in
// the relation's own namespace is a different type, so a variable of
// that type admits nothing and the constrained call is rejected.

#include <foundation/diag/FailClosed.h>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};

namespace ingest {
template <class From, class To>
struct edge {};
inline constexpr edge<Raw, Checked> raw_to_checked{};
}  // namespace ingest

template <class From, class To>
    requires ffc::Admitted<^^ingest, From, To>
constexpr int transition(From const&, To const&) noexcept {
    return 0;
}

}  // namespace

int main() { return transition(Raw{}, Checked{}); }
