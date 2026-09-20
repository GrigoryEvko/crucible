// The region is spilled to disk, so its tag carries Row<IO> and
// touching it costs IO.  The read is taken from a foreground context,
// whose row is empty.  This is the same Subrow test a lend from the
// permission pool runs, moved onto the borrow where it was missing.

#include <fixy/Witnessed.h>

#include <fixy/Borrowed.h>
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
std::uint64_t storage[3] = {1, 2, 3};
}  // namespace

int main() {
    auto borrow = ::fixy::mint_borrowed<Spilled>(storage);
    auto witnessed = ::fixy::mint_witnessed_under(borrow);
    FgCtx ctx{};
    [[maybe_unused]] auto read = deref(witnessed, ctx);
    return 0;
}
