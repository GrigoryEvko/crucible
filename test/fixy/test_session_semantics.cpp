// The transition systems of fixy/session/Semantics.h, checked.
//
// The first half checks each rule of Figure 7 (global types) and of
// Figure 8 (configurations) of Barwell, Hou, Yoshida and Zhou,
// "Crash-Stop Failures in Asynchronous Multiparty Session Types" (LMCS
// 21:2, 2025), one small state at a time.  Remark 4.13 of the paper is
// checked as the paper writes it.
//
// The second half checks operational correspondence on a corpus of
// global types, with every role reliable.  The walk starts from G and its
// projected context, and it takes each label that G allows, to a bounded
// depth.  At each state the two sides must allow the same labels, and
// after each label the context must stay associated with G (Definition
// 21 of Pischke, Masters and Yoshida).  That is Theorems 4.20 and 4.21 of
// the crash-stop paper for the reliable case.  The corpus includes a
// sender that acts before its message arrives, where only the chosen
// branch of the en-route node reduces.  A context that is not associated
// must make the walk report a fault, and the self-attack at the foot
// checks that it does.
//
// The test is several source files of one executable, so that no
// translation unit holds every check:
//
//   session_semantics.h           the shared part and the walk
//   this file                     the self-attack and main
//   ..._global.cpp                the rules of Figure 7
//   ..._config.cpp                the rules of Figure 8
//   ..._corpus.cpp                the walks of the corpus
//   ..._sender.cpp                the walks of a sender that acts before
//                                 its message arrives
//   ..._sender_tirore3.cpp        the walk of the third type of that gap

#include "session_semantics.h"

#include <cstdio>

namespace test_session_semantics {

// ── Self-attack: the walk must see a context that is not associated ──

// Q expects another label, so G sends a message that Q never takes.
using WrongLabel = s::TypingContext<s::RoleState<P, s::OutQueue<>, SendM>,
                                    s::RoleState<Q, s::OutQueue<>, s::Recv<s::PeerMsg<P, M1, int>, s::End>>>;
inline constexpr Tally kWrongLabel = explore<WrongLabel, Once, 2>();
static_assert(kWrongLabel.enabled_mismatches > 0 && kWrongLabel.unassociated > 0);

// A message waits that G never sent, so Q can receive it at once.
using ExtraMessage =
    s::TypingContext<s::RoleState<P, s::OutQueue<s::Queued<Q, M, int>>, SendM>, s::RoleState<Q, s::OutQueue<>, RecvM>>;
inline constexpr Tally kExtraMessage = explore<ExtraMessage, Once, 2>();
static_assert(kExtraMessage.enabled_mismatches > 0);

}  // namespace test_session_semantics

namespace {

namespace t = test_session_semantics;

int check(char const* name, t::Walk walk, t::Tally expected, bool expect_clean) {
    const t::Tally got = walk();
    const bool agrees = got.states == expected.states && got.labels == expected.labels
                     && got.enabled_mismatches == expected.enabled_mismatches
                     && got.unassociated == expected.unassociated && t::is_clean(got) == expect_clean;
    if (!agrees) std::fprintf(stderr, "test_session_semantics: %s: the runtime walk disagrees\n", name);
    return agrees ? 0 : 1;
}

}  // namespace

int main() {
    volatile t::Walk ring = t::ring_walk();
    volatile t::Walk ping_pong = t::ping_pong_walk();
    volatile t::Walk sender_goes_on = t::sender_goes_on_walk();
    volatile t::Walk wrong = &t::explore<t::WrongLabel, t::Once, 2>;
    int failures = 0;
    failures += check("ring", ring, t::ring_tally(), true);
    failures += check("ping pong", ping_pong, t::ping_pong_tally(), true);
    failures += check("sender goes on", sender_goes_on, t::sender_goes_on_tally(), true);
    failures += check("wrong label", wrong, t::kWrongLabel, false);
    const t::Tally ring_tally = t::ring_tally();
    const t::Tally ping_pong_tally = t::ping_pong_tally();
    std::printf("test_session_semantics: ring %d states %d labels, ping pong %d states %d labels\n", ring_tally.states,
                ring_tally.labels, ping_pong_tally.states, ping_pong_tally.labels);
    return failures;
}
