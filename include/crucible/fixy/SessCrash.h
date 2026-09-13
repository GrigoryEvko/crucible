#pragma once

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/Session.h>

#include <type_traits>

namespace crucible::fixy::sess::crash {

// Stop_g is the terminal combinator for a crashed endpoint. It carries a
// CrashClass grade, and Stop is the alias for Stop_g<CrashClass::Abort>.
using ::crucible::safety::proto::Stop_g;
using ::crucible::safety::proto::Stop;
using ::crucible::safety::proto::CrashClass;
using ::crucible::safety::proto::is_stop;
using ::crucible::safety::proto::is_stop_v;

// Crash<PeerTag> is the payload that appears in an Offer branch when the named
// peer crashes. A crash branch is any Recv<Crash<Peer>, K>.
using ::crucible::safety::proto::Crash;
using ::crucible::safety::proto::is_crash;
using ::crucible::safety::proto::is_crash_v;

// A queue into a crashed recipient becomes an UnavailableQueue. Every send
// that follows is dropped without notice.
using ::crucible::safety::proto::UnavailableQueue;

// The set names the roles assumed not to crash within the protocol's scope.
// Every participant outside it must appear in a Crash branch of each peer that
// receives from it.
using ::crucible::safety::proto::ReliableSet;
using ::crucible::safety::proto::UnreliableAll;
using ::crucible::safety::proto::is_reliable;
using ::crucible::safety::proto::is_reliable_v;

// This asks whether one Offer carries a crash branch for the peer. The
// whole-tree form is below.
using ::crucible::safety::proto::has_crash_branch_for_peer;
using ::crucible::safety::proto::has_crash_branch_for_peer_v;
using ::crucible::safety::proto::assert_has_crash_branch_for;

// The walker checks every Offer reachable from the protocol root. That is the
// property a crash-aware transport demands, because a single-Offer check is
// local and can hide a violation further down the tree.
using ::crucible::safety::proto::every_offer_has_crash_branch_for_peer_v;
using ::crucible::safety::proto::assert_every_offer_has_crash_branch_for;

// A protocol can be well formed and still have no branch to land in when the
// peer dies. The transport then abandons the handle or terminates. Binding
// well-formedness and per-tree crash coverage under one name makes that a
// compile error at the wiring site instead of a surprise at runtime.
template <typename Proto, typename PeerTag>
concept CrashAwareForTransport =
    ::crucible::safety::proto::is_well_formed_v<Proto> && every_offer_has_crash_branch_for_peer_v<Proto, PeerTag>;

}  // namespace crucible::fixy::sess::crash

// The substrate validates the semantics of these predicates. The cells here
// pin the re-export contract and the two-part discipline of the synthesis
// concept.

namespace crucible::fixy::sess::crash::v064_self_test {

namespace proto = ::crucible::safety::proto;

struct Alice {};
struct Bob {};
struct Msg {};
struct Ack {};

using SendInt = proto::Send<int, proto::End>;
using RecvInt = proto::Recv<int, proto::End>;
using EndProto = proto::End;
using StopProto = Stop;

static_assert(std::is_same_v<Stop, proto::Stop>);
static_assert(std::is_same_v<Stop_g<CrashClass::Abort>, proto::Stop_g<proto::CrashClass::Abort>>);
static_assert(std::is_same_v<Stop_g<CrashClass::NoThrow>, proto::Stop_g<proto::CrashClass::NoThrow>>);
static_assert(std::is_same_v<CrashClass, proto::CrashClass>,
              "fixy::sess::crash::CrashClass must alias substrate exactly.");

static_assert(is_stop_v<Stop>);
static_assert(is_stop_v<Stop_g<CrashClass::Throw>>);
static_assert(!is_stop_v<EndProto>);
static_assert(!is_stop_v<SendInt>);
static_assert(is_stop_v<Stop> == proto::is_stop_v<Stop>, "is_stop_v must reach identically through fixy::");

static_assert(std::is_same_v<Crash<Alice>, proto::Crash<Alice>>);
static_assert(is_crash_v<Crash<Alice>>);
static_assert(is_crash_v<Crash<Bob>>);
static_assert(!is_crash_v<Msg>);
static_assert(!is_crash_v<Stop>);
static_assert(std::is_same_v<typename Crash<Alice>::peer, Alice>);

using NoneReliable = ReliableSet<>;
using AliceReliable = ReliableSet<Alice>;
using AliceBob = ReliableSet<Alice, Bob>;

static_assert(NoneReliable::size == 0);
static_assert(AliceReliable::size == 1);
static_assert(AliceBob::size == 2);
static_assert(std::is_same_v<UnreliableAll, NoneReliable>);
static_assert(!is_reliable_v<NoneReliable, Alice>);
static_assert(is_reliable_v<AliceReliable, Alice>);
static_assert(!is_reliable_v<AliceReliable, Bob>);
static_assert(is_reliable_v<AliceBob, Alice>);
static_assert(is_reliable_v<AliceBob, Bob>);

using NormalOffer = proto::Offer<proto::Recv<Msg, EndProto>, proto::Recv<Ack, EndProto>>;
using AliceCrashOffer = proto::Offer<proto::Recv<Msg, EndProto>, proto::Recv<Crash<Alice>, EndProto>>;

static_assert(!has_crash_branch_for_peer_v<NormalOffer, Alice>);
static_assert(has_crash_branch_for_peer_v<AliceCrashOffer, Alice>);
static_assert(!has_crash_branch_for_peer_v<AliceCrashOffer, Bob>);
static_assert(has_crash_branch_for_peer_v<AliceCrashOffer, Alice>
                  == proto::has_crash_branch_for_peer_v<AliceCrashOffer, Alice>,
              "has_crash_branch_for_peer_v must reach identically through fixy::");

using CrashAwareClient = proto::Send<Msg, AliceCrashOffer>;
using CrashOblivClient = proto::Send<Msg, NormalOffer>;

static_assert(every_offer_has_crash_branch_for_peer_v<CrashAwareClient, Alice>);
static_assert(!every_offer_has_crash_branch_for_peer_v<CrashOblivClient, Alice>);
static_assert(every_offer_has_crash_branch_for_peer_v<EndProto, Alice>);
static_assert(every_offer_has_crash_branch_for_peer_v<StopProto, Alice>);

static_assert(CrashAwareForTransport<CrashAwareClient, Alice>, "Well-formed crash-aware client must satisfy "
                                                               "CrashAwareForTransport<Proto, Alice>.");
static_assert(!CrashAwareForTransport<CrashOblivClient, Alice>,
              "Well-formed crash-OBLIVIOUS client must REJECT "
              "CrashAwareForTransport<Proto, Alice> — the per-tree crash-branch "
              "walker fires even when well-formedness passes.");
static_assert(CrashAwareForTransport<EndProto, Alice>, "End has no Offer<> — vacuously crash-aware for every peer.");
static_assert(CrashAwareForTransport<StopProto, Alice>, "Stop has no Offer<> — vacuously crash-aware for every peer.");

inline constexpr std::size_t v064_reexport_cardinality = 19;
static_assert(v064_reexport_cardinality == 19, "fixy::sess::crash:: holds exactly 18 substrate re-exports plus 1 "
                                               "synthesis concept.  Update the using-decls AND this cardinality "
                                               "witness in lockstep.");

}  // namespace crucible::fixy::sess::crash::v064_self_test

namespace crucible::fixy::sess::crash {

// A static assertion alone can mask a consteval-only fault. Every predicate
// here is type-level, so the body forces concept evaluation from a function
// context rather than exercising a runtime value.
inline void runtime_smoke_test() noexcept {
    constexpr bool aware_ok = CrashAwareForTransport<v064_self_test::CrashAwareClient, v064_self_test::Alice>;
    constexpr bool obliv_rejects = !CrashAwareForTransport<v064_self_test::CrashOblivClient, v064_self_test::Alice>;
    static_assert(aware_ok && obliv_rejects, "runtime_smoke_test: CrashAwareForTransport synthesis concept "
                                             "must accept crash-aware clients and reject crash-oblivious "
                                             "clients at the fixy:: re-export boundary.");
}

}  // namespace crucible::fixy::sess::crash
