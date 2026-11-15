// The writer of the Vigil mode cell, lent to a session.
//
// The owner of the cell lends its ModeWriter to the mode session, and the
// move leaves the owner a writer with no right to write.  A write with that
// writer ends the process, so the cell has one writer at each time.  The
// close of the session gives the writer back, and the owner writes again.
//
// The test writes with the moved-from writer on purpose, so the rule
// use-after-move lists this file with its probe.

#include <fixy/Ctx.h>
#include <fixy/session/VigilMode.h>

#include "../test_abort_probe.h"

#include <foundation/effects/Ctx.h>

#include <utility>

namespace s = ::fixy::session;
namespace vm = ::fixy::session::vigil_mode;
namespace eff = ::foundation::effects;

namespace {

using BgCtx = ::fixy::BgDrainCtx;

// While the writer is lent, the owner holds a moved-from writer, and a
// write with it ends the process.  So the cell has one writer at each time.
[[nodiscard]] int the_owner_cannot_write_while_the_writer_is_lent() {
    vm::ModeWriter writer = vm::mint_mode_writer();
    vm::ModeCell cell{writer};
    const BgCtx ctx{eff::testing::bg()};
    auto session = vm::mint_vigil_mode_bridge(ctx, cell, std::move(writer));
    const bool is_ended = crucible::test::aborts([&cell, &writer] { cell.publish_compiled(writer); });
    auto terminal = std::move(session).select<2>(s::no_label);
    const vm::ModeWriter returned = std::move(terminal).close().return_writer();
    cell.publish_compiled(returned);
    if (!is_ended || s::atomic_machine_state(cell) != vm::Mode::COMPILED) {
        ::fixy::report(::fixy::Sink::Err, "the owner wrote the cell while the session held the writer\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() { return the_owner_cannot_write_while_the_writer_is_lent(); }
