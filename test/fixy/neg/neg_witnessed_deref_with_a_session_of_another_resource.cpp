// The borrow was minted at a handle of a session over one Resource, and
// the read is taken against a handle of another session at the same
// protocol position, over another Resource.  The witness names the full
// type of the handle of the mint, so a handle of another session type does
// not discharge it, although the two stand at the same position.

#include <fixy/session/Witnessed.h>

#include <fixy/Core.h>
#include <fixy/session/Handle.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace eff = ::foundation::effects;
namespace s = ::fixy::session;

namespace {
struct Cache {
    using permission_row = eff::Row<>;
};
struct Ping {
    int value = 0;
};
struct Wire {
    int last_sent = 0;
};
struct OtherWire {
    int last_sent = 0;
};
using Sending = s::Send<Ping, s::End>;
using CacheBorrow = ::fixy::View<std::uint64_t>;
using MintedAt = s::SessionHandle<Sending, Wire>;
using Other = s::SessionHandle<Sending, OtherWire>;
using WitnessedAtSend = s::Witnessed<CacheBorrow, s::witness::AtProtocol<MintedAt>>;

[[maybe_unused]] CacheBorrow read_against_another_session(WitnessedAtSend const& witnessed, Other const& other) {
    return deref(witnessed, other);
}
}  // namespace

int main() { return 0; }
