// A read through a hold that stays named.  read consumes the hold for
// the call and hands it back, so a body that names the hold finds it
// spent.  A named hold stays with the caller, so read refuses it: the
// member takes an rvalue only.

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

}  // namespace

int main() {
    auto lender = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto [loan, lent] = std::move(lender).template lend<Region>(0);
    auto [value, borrowing] = sess::mint_permission_hold().accept_loan(std::move(loan));
    auto back = borrowing.template read<Region>([](auto const&) noexcept {});
    (void)back;
    (void)lent;
    return value;
}
