// The obligation is the row the region tag carries, and the caller
// presents a session handle.  A handle discharges a protocol position
// and says nothing about what a caller is permitted to do, so the gate
// refuses it.  The two kinds are one gate and not one obligation.

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
    auto borrow = ::fixy::mint_borrowed<Cache>(storage);
    auto witnessed = ::fixy::mint_witnessed_under(borrow);
    auto at_send = s::mint_session_handle<Sending, Wire>(Wire{});
    [[maybe_unused]] auto read = deref(witnessed, at_send);
    (void)std::move(at_send).send(Ping{1}, [](Wire& w, Ping&& p) noexcept { w.last_sent = p.value; }).close();
    return 0;
}
