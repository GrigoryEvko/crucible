// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The door of the crash mint puts a plain handle under crash-stop
// semantics.  A call from a scope that is not the mint does not do the
// admission check of the mint.  The member of the door is private.  The
// compiler rejects the call, also for a protocol that the mint accepts.
//
// Expected diagnostic: the door member is private in this context.

#include <fixy/session/CrashTransport.h>

#include <source_location>

namespace neg_sess_crash_door_called_outside_types {
struct Alice {};
struct Bob {};
struct Wire {};
}  // namespace neg_sess_crash_door_called_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_crash_door_called_outside_types;
    using Guarded = s::Offer<s::Recv<int, s::End>, s::Recv<s::Crash<Bob>, s::End>>;
    s::PeerCrashCell cell;
    auto forged = s::CrashSessionDoor::open_<Guarded, Alice, Bob, s::ReliableSet<>, s::DefaultAbandonmentPolicy>(
        Wire{}, cell, std::source_location{});
    std::move(forged).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
