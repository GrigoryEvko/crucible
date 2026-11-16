// The writer of the Vigil mode cell, lent to a session.
//
// The owner of the cell lends its ModeWriter to the mode session, and the
// move leaves the owner a writer with no right to write.  A write with that
// writer ends the process, so the cell has one writer at each time.  The
// write runs in a child through expect_dies of the Check family.  The close
// of the session gives the writer back, and the owner writes again.
//
// The test writes with the moved-from writer on purpose, so the rule
// use-after-move lists this file with its probe.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/session/VigilMode.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace s = ::fixy::session;
namespace vm = ::fixy::session::vigil_mode;
namespace eff = ::foundation::effects;

namespace {

using BgCtx = ::fixy::BgDrainCtx;

// While the writer is lent, the owner holds a moved-from writer, and a
// write with it ends the process.  So the cell has one writer at each time.
void the_owner_cannot_write_while_the_writer_is_lent(::fixy::TestRunnerCtx const& test) {
    vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    const BgCtx ctx{eff::testing::bg()};
    auto session = vm::mint_vigil_mode_bridge(ctx, cell, std::move(writer));
    ::fixy::check::expect_dies(
        test, [&cell, &writer] noexcept { cell.publish_compiled(writer); },
        ::fixy::TextView{"fixy::session::diagnostic [VigilMode_Writer_Not_Held]"});
    auto terminal = std::move(session).select<2>(s::no_label);
    const vm::ModeWriter returned = std::move(terminal).close().return_writer();
    cell.publish_compiled(returned);
    CRUCIBLE_TEST_ASSERT(test, s::atomic_machine_state(cell) == vm::Mode::COMPILED);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    the_owner_cannot_write_while_the_writer_is_lent(test);
    ::fixy::check::pass(test, "test_session_vigil_mode_writer_lent: PASSED\n");
    return 0;
}
