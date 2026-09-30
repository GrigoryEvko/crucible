// A Borrowed built from the read loan of a different region.  The loan
// of Other says nothing about Region, so the loan does not convert to the
// loan that the constructor takes.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct Other {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    auto loans = fp::mint_read_loan(fp::permission_erase_brand(fp::mint_permission_root<Other>()));
    sess::Borrowed<int, Region> message{1, std::move(loans.first)};
    return message.value;
}
