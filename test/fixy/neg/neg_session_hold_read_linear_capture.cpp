// The body of a read of a hold captures a linear end by move.  The read calls
// the body, and a call is no consume, so the end would end with the body.
// The read refuses a body that holds a linear value.

#include <fixy/session/Flow.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace fp = ::foundation::permissions;
namespace sess = ::fixy::session;

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] End {
    int handle = 0;
    constexpr explicit End(int value) noexcept : handle{value} {}
    constexpr End(End&&) noexcept = default;
    constexpr End& operator=(End&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    End(End const&) = delete("a linear end of the fixture does not copy");
    End& operator=(End const&) = delete("a linear end of the fixture does not copy");
};

}  // namespace

int main() {
    auto lender = sess::mint_permission_hold(fp::mint_permission_root<Region>());
    auto [loan, lent] = std::move(lender).template lend<Region>(0);
    auto [value, borrowing] = sess::mint_permission_hold().accept_loan(std::move(loan));
    End end{7};
    auto back = std::move(borrowing).template read<Region>(
        [held = std::move(end)](auto const&) noexcept { static_cast<void>(held.handle); });
    (void)back;
    (void)lent;
    return value;
}
