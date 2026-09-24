// The recorder outside a crash-aware send gets a write that returns an
// int.  The write is neither a trying write nor a declared write, so the
// recorder refuses it before the crash transport under it sees it.

#include <fixy/session/Recording.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_crash_recorded_send_transport_result_types {
struct Alice {};
struct Bob {};
struct Wire {};
using Proto = s::Select<s::Send<int, s::End>>;
}  // namespace neg_sess_crash_recorded_send_transport_result_types

using namespace neg_sess_crash_recorded_send_transport_result_types;

int main() {
    s::PeerCrashCell cell;
    s::SessionEventLog log;
    auto handle = s::mint_recorded_session(s::mint_crash_session<Proto, Alice, Bob>(Wire{}, cell), log,
                                           s::RoleTagId{1}, s::RoleTagId{2});
    auto chosen = std::move(handle).template select<0>([](Wire&, std::size_t) noexcept { return true; });
    auto sent = std::move(chosen).send(1, [](Wire&, int&) noexcept { return 1; });
    (void)std::move(sent.next).close();
    return 0;
}
