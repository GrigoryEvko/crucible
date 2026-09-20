// A tag that declares no permission row states no obligation, so there
// is nothing for a context to be weighed against.  The mint refuses
// rather than minting a witness that every context discharges.

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
    auto borrow = ::fixy::mint_borrowed<Unrowed>(storage);
    [[maybe_unused]] auto witnessed = ::fixy::mint_witnessed_under(borrow);
    return 0;
}
