// A crash-aware send with a write that returns an int.  A trying write
// returns bool: true when it took the value, and false when it kept the
// value with the caller.  An int says nothing that the decorator can read
// about the write, so send() refuses it.

#include <fixy/session/CrashTransport.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_send_transport_result_types {
struct Alice {};
struct Bob {};
struct Wire {};
using Proto = s::Select<s::Send<int, s::End>>;
}  // namespace neg_sess_crash_send_transport_result_types

using namespace neg_sess_crash_send_transport_result_types;

int main() {
    s::PeerCrashCell cell;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, cell);
    auto chosen = std::move(handle).template select<0>([](Wire&, std::size_t) noexcept { return true; });
    auto sent = std::move(chosen).send(1, [](Wire&, int&) noexcept { return 1; });
    (void)std::move(sent.next).close();
    return 0;
}
