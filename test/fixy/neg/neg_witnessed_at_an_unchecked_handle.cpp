// A protocol witness checks the time of the presented handle at the read,
// from the record of the session in the watch and the count of steps.  A
// handle under check::Off keeps neither, so two handles of its type could
// not be told apart at the read.  The mint refuses such a handle.

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
using Sending = s::Send<Ping, s::End>;
using CacheBorrow = ::fixy::View<std::uint64_t>;
using Unchecked = s::SessionHandle<Sending, Wire, void, s::check::Off>;

[[maybe_unused]] void witness_an_unchecked_handle(Unchecked const& unchecked, CacheBorrow borrow) {
    [[maybe_unused]] auto witnessed = s::mint_witnessed_at(unchecked, borrow);
}
}  // namespace

int main() { return 0; }
