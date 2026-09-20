// The obligation is a protocol position, and the caller presents an
// execution context.  A context discharges a row and says nothing about
// where a session stands, so the gate refuses it.

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
    auto at_send = s::mint_session_handle<Sending, Wire>(Wire{});
    auto borrow = ::fixy::mint_borrowed<Cache>(storage);
    auto witnessed = ::fixy::mint_witnessed_at(at_send, borrow);
    FgCtx ctx{};
    [[maybe_unused]] auto read = deref(witnessed, ctx);
    (void)std::move(at_send).send(Ping{1}, [](Wire& w, Ping&& p) noexcept { w.last_sent = p.value; }).close();
    return 0;
}
