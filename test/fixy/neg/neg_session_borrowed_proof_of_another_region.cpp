// A Borrowed built from the read loan of a different region.  The loan
// of Other says nothing about Region, so the loan does not convert to the
// loan that the constructor takes.

#include <fixy/session/Flow.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

// The loan comes from a singleton root, so each tag has a stable identity
// and external linkage.
namespace neg_session_borrowed_proof_of_another_region_types {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace neg_session_borrowed_proof_of_another_region_types

using namespace neg_session_borrowed_proof_of_another_region_types;

int main() {
    // The marker names the brand of the root of Other, so only the tags differ.
    auto root = fp::mint_permission_root<Other>();
    using Brand = ::foundation::brand::brand_of_t<decltype(root)>;
    auto [loan, lent] = fp::mint_read_loan(std::move(root));
    sess::Borrowed<int, Region, Brand> message{1, std::move(loan)};
    return message.peek();
}
