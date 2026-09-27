// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A crash-aware endpoint sends the label word of a keyed message, and
// crashes before the value of its payload.  The label word and the value
// are one message, so the peer would take the label and wait for a value
// that never comes, in a position with no crash branch.  crash() refuses
// the value step.
//
// Expected diagnostic: Crash_Splits_A_Message.

#include <fixy/session/CrashTransport.h>
#include <fixy/session/Projection.h>

#include <cstddef>
#include <utility>

namespace s = fixy::session;

namespace neg_sess_crash_between_keyed_label_and_value_types {
struct Alice {};
struct Bob {};
struct Hello {};
struct Wire {};
}  // namespace neg_sess_crash_between_keyed_label_and_value_types

int main() {
    using namespace neg_sess_crash_between_keyed_label_and_value_types;
    using Proto = s::Send<s::Labelled<Hello, int>, s::End>;
    s::PeerCrashCell peer_cell;
    s::PeerCrashCell own_cell;
    const ::foundation::effects::detail::ctx_witnesses::BgWitness ctx{::foundation::effects::testing::bg()};
    auto handle = s::mint_crash_session<Proto, Alice, Bob>(ctx, Wire{}, peer_cell, s::mint_crash_writer(own_cell));
    auto [half_sent, lost] = std::move(handle).send([](Wire&, std::size_t) noexcept { return true; });
    static_cast<void>(lost);
    (void)std::move(half_sent).crash(s::CrashCause::Abort);
    return 0;
}
