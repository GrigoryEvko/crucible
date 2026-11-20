// The region is spilled to disk, so its tag carries Row<IO> and
// touching it costs IO.  The read is taken from a foreground context,
// whose row is empty.  The borrow runs the same Subrow test that a lend
// from the permission pool runs.

#include <fixy/session/Witnessed.h>

#include <fixy/Core.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = ::foundation::effects;
namespace s = ::fixy::session;

namespace {
struct Cache {
    using permission_row = eff::Row<>;
};
struct Spilled {
    using permission_row = eff::Row<eff::Effect::IO>;
};
struct Unrowed {};
struct Ping {
    int value = 0;
};
struct Wire {
    int last_sent = 0;
};
using Sending = s::Send<Ping, s::End>;
using FgCtx = eff::ExecCtx<eff::ctx_cap::Fg, eff::Row<>>;
::fixy::FixedArray<std::uint64_t, 3> storage{1, 2, 3};
}  // namespace

int main() {
    auto borrow = storage.view();
    auto witnessed = s::mint_witnessed_under<Spilled>(borrow);
    FgCtx ctx = eff::testing::foreground();
    [[maybe_unused]] auto read = deref(witnessed, ctx);
    return 0;
}
