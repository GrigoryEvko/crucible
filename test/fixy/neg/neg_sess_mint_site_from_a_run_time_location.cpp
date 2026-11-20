// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The watch keeps the file of the site of each mint as a static text, so the
// site of a mint is a constant.  A location of the run time could name a text
// that the watch does not own, so the site of a mint refuses it.  The
// location here comes from a policy object of the run time.
//
// Expected diagnostic: the site of the mint is no constant expression.

#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>

#include <foundation/effects/Ctx.h>

#include <utility>

namespace mint_site_fixture {
namespace s = ::fixy::session;
struct Counter {
    static constexpr s::Network session_network = s::Network::Local;
    [[no_unique_address]] s::MoveOnlyResource move_only{};
};
using Once = s::Send<int, s::End>;
}  // namespace mint_site_fixture

int main() {
    using namespace mint_site_fixture;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    s::check::Enforced policy{};
    auto head = s::mint_session<Once>(ctx, Counter{}, policy.construction_loc());
    auto at_end = std::move(head).send(7, [](Counter&, int&) noexcept { return true; });
    static_cast<void>(std::move(at_end).close());
    return 0;
}
