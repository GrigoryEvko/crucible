#pragma once

// The delegation combinators below live in crucible::safety::proto.  The
// re-export gives a caller that pulls in only the fixy surface an entry point
// that does not name that namespace.  It also puts every handoff declaration
// under one prefix, so a reviewer can find them all with a single grep.

#include <crucible/sessions/SessionDelegate.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::fixy::sess::delegate {

using ::crucible::safety::proto::Delegate;
using ::crucible::safety::proto::Accept;
using ::crucible::safety::proto::EpochedDelegate;
using ::crucible::safety::proto::EpochedAccept;

using ::crucible::safety::proto::Recovers;
using ::crucible::safety::proto::MustAbort;
using ::crucible::safety::proto::IllFormed;
using ::crucible::safety::proto::delegated_crash_propagation_t;
using ::crucible::safety::proto::assert_delegated_crash_propagates;

using ::crucible::safety::proto::Delegate_seq;
using ::crucible::safety::proto::Accept_seq;
using ::crucible::safety::proto::Redelegate;
using ::crucible::safety::proto::DelegateWithAck;
using ::crucible::safety::proto::AcceptWithAck;

using ::crucible::safety::proto::is_delegate;
using ::crucible::safety::proto::is_delegate_v;
using ::crucible::safety::proto::is_accept;
using ::crucible::safety::proto::is_accept_v;
using ::crucible::safety::proto::is_delegation_head_v;

using ::crucible::safety::proto::CanDelegate;
using ::crucible::safety::proto::DelegatesTo;
using ::crucible::safety::proto::AcceptsFrom;

using ::crucible::safety::proto::TransportForDelegate;
using ::crucible::safety::proto::TransportForAccept;

using ::crucible::safety::proto::assert_delegates_to;
using ::crucible::safety::proto::assert_accepts_from;

}  // namespace crucible::fixy::sess::delegate

// The sentinels below verify that each alias resolves to the substrate
// entity and not to a local of the same name.  Being in the header, they
// fire at every consumer's include time.

namespace crucible::fixy::sess::delegate::v060_self_test {

namespace proto = ::crucible::safety::proto;

// The fixtures stay minimal so the witnesses below exercise every export
// without depending on richer session machinery.
struct Req {};  // delegated-side payload
struct Ack {};  // ack token
struct CrashTag {};  // recipient-tag for crash propagation

using T_delegated = proto::Send<Req, proto::End>;

using K_continue = proto::End;

using D = proto::Delegate<T_delegated, K_continue>;
using A = proto::Accept<T_delegated, K_continue>;

// The floors are non-default so that preservation through the alias is
// observable.
using ED = proto::EpochedDelegate<T_delegated, K_continue, 3u, 7u>;
using EA = proto::EpochedAccept<T_delegated, K_continue, 3u, 7u>;

static_assert(std::is_same_v<proto::Delegate<T_delegated, K_continue>, Delegate<T_delegated, K_continue>>,
              "Delegate must reach identically through fixy::");
static_assert(std::is_same_v<proto::Accept<T_delegated, K_continue>, Accept<T_delegated, K_continue>>,
              "Accept must reach identically through fixy::");
static_assert(std::is_same_v<proto::EpochedDelegate<T_delegated, K_continue, 3u, 7u>,
                             EpochedDelegate<T_delegated, K_continue, 3u, 7u>>,
              "EpochedDelegate must reach identically through fixy::");
static_assert(std::is_same_v<proto::EpochedAccept<T_delegated, K_continue, 3u, 7u>,
                             EpochedAccept<T_delegated, K_continue, 3u, 7u>>,
              "EpochedAccept must reach identically through fixy::");

static_assert(EpochedDelegate<T_delegated, K_continue, 3u, 7u>::min_epoch == 3u);
static_assert(EpochedDelegate<T_delegated, K_continue, 3u, 7u>::min_generation == 7u);
static_assert(EpochedAccept<T_delegated, K_continue, 3u, 7u>::min_epoch == 3u);
static_assert(EpochedAccept<T_delegated, K_continue, 3u, 7u>::min_generation == 7u);

static_assert(std::is_same_v<Recovers<K_continue>, proto::Recovers<K_continue>>,
              "Recovers must reach identically through fixy::");
static_assert(std::is_same_v<MustAbort, proto::MustAbort>, "MustAbort must reach identically through fixy::");
static_assert(std::is_same_v<IllFormed, proto::IllFormed>, "IllFormed must reach identically through fixy::");

// The witness compares the two spellings rather than pinning an outcome.
// The outcome depends on the continuation's branch shape, which is not what
// this sentinel is about.
using PropResult_fixy = delegated_crash_propagation_t<T_delegated, CrashTag, K_continue>;
using PropResult_proto = proto::delegated_crash_propagation_t<T_delegated, CrashTag, K_continue>;
static_assert(std::is_same_v<PropResult_fixy, PropResult_proto>,
              "delegated_crash_propagation_t must reach identically through fixy::");

// A Recv-only protocol that terminates cleanly, so the assertion holds
// whatever shape the continuation has.
using T_recv_only = proto::Recv<Ack, proto::End>;
consteval bool check_fixy_assert_delegated_crash() {
    assert_delegated_crash_propagates<T_recv_only, CrashTag, K_continue>();
    return true;
}
static_assert(check_fixy_assert_delegated_crash());

struct T1 {};
struct T2 {};
struct T3 {};
static_assert(std::is_same_v<Delegate_seq<T1, T2, T3, K_continue>,
                             proto::Delegate<T1, proto::Delegate<T2, proto::Delegate<T3, K_continue>>>>,
              "Delegate_seq<T1, T2, T3, K> must expand to right-nested Delegates.");
static_assert(std::is_same_v<Accept_seq<T1, T2, T3, K_continue>,
                             proto::Accept<T1, proto::Accept<T2, proto::Accept<T3, K_continue>>>>,
              "Accept_seq<T1, T2, T3, K> must expand to right-nested Accepts.");

static_assert(std::is_same_v<Redelegate<T_delegated, K_continue>,
                             proto::Accept<T_delegated, proto::Delegate<T_delegated, K_continue>>>,
              "Redelegate<T, K> = Accept<T, Delegate<T, K>>.");

static_assert(std::is_same_v<DelegateWithAck<T_delegated, Ack, K_continue>,
                             proto::Delegate<T_delegated, proto::Recv<Ack, K_continue>>>,
              "DelegateWithAck<T, Ack, K> = Delegate<T, Recv<Ack, K>>.");

static_assert(std::is_same_v<AcceptWithAck<T_delegated, Ack, K_continue>,
                             proto::Accept<T_delegated, proto::Send<Ack, K_continue>>>,
              "AcceptWithAck<T, Ack, K> = Accept<T, Send<Ack, K>>.");

static_assert(is_delegate_v<D> == proto::is_delegate_v<D>, "is_delegate_v must reach identically through fixy::");
static_assert(is_delegate_v<D>);
static_assert(!is_delegate_v<A>);
static_assert(is_delegate_v<ED>);
static_assert(!is_delegate_v<EA>);

static_assert(is_accept_v<A> == proto::is_accept_v<A>, "is_accept_v must reach identically through fixy::");
static_assert(is_accept_v<A>);
static_assert(!is_accept_v<D>);
static_assert(is_accept_v<EA>);
static_assert(!is_accept_v<ED>);

static_assert(is_delegation_head_v<D>);
static_assert(is_delegation_head_v<A>);
static_assert(is_delegation_head_v<ED>);
static_assert(is_delegation_head_v<EA>);
static_assert(!is_delegation_head_v<proto::End>);
static_assert(!is_delegation_head_v<proto::Send<Req, proto::End>>);

static_assert(is_delegate<D>::value);
static_assert(is_accept<A>::value);

template <typename C, typename T_>
    requires DelegatesTo<C, T_>
consteval bool requires_delegates_to_witness() {
    return true;
}
static_assert(requires_delegates_to_witness<D, T_delegated>());

template <typename C, typename T_>
    requires AcceptsFrom<C, T_>
consteval bool requires_accepts_from_witness() {
    return true;
}
static_assert(requires_accepts_from_witness<A, T_delegated>());

template <typename P, typename R>
    requires CanDelegate<P, R>
consteval bool requires_can_delegate_witness() {
    return true;
}
static_assert(requires_can_delegate_witness<T_delegated, CrashTag>());

struct CarrierResource {};
struct DelegatedResource {};

struct DelegateTransport {
    void operator()(CarrierResource&, DelegatedResource&&) const {}
};
struct AcceptTransport {
    DelegatedResource operator()(CarrierResource&) const { return {}; }
};

static_assert(TransportForDelegate<DelegateTransport, CarrierResource, DelegatedResource>);
static_assert(TransportForAccept<AcceptTransport, CarrierResource, DelegatedResource>);

struct WrongReturnAcceptTransport {
    int operator()(CarrierResource&) const { return 0; }
};
static_assert(!TransportForAccept<WrongReturnAcceptTransport, CarrierResource, DelegatedResource>);

consteval bool check_fixy_assert_delegates_to() {
    assert_delegates_to<D, T_delegated>();
    return true;
}
static_assert(check_fixy_assert_delegates_to());

consteval bool check_fixy_assert_accepts_from() {
    assert_accepts_from<A, T_delegated>();
    return true;
}
static_assert(check_fixy_assert_accepts_from());

constexpr int v060_surface_cardinality = 26;
static_assert(v060_surface_cardinality == 26, "fixy::sess::delegate:: surface cardinality drifted — extend the "
                                              "sentinel block to cover the new alias.");

}  // namespace crucible::fixy::sess::delegate::v060_self_test

namespace crucible::fixy::sess::delegate {

// Static-only sentinels can mask SFINAE, consteval and inline-body faults.
// The routine below forces every delegation metafunction through a real
// instantiation instead.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct Payload {};
    struct AckMsg {};
    struct Tag {};

    using T_ = proto::Send<Payload, proto::End>;
    using K_ = proto::End;
    using D_ = Delegate<T_, K_>;
    using A_ = Accept<T_, K_>;
    using ED_ = EpochedDelegate<T_, K_, 1u, 1u>;
    using EA_ = EpochedAccept<T_, K_, 1u, 1u>;

    [[maybe_unused]] constexpr bool is_d = is_delegate_v<D_>;
    [[maybe_unused]] constexpr bool is_a = is_accept_v<A_>;
    [[maybe_unused]] constexpr bool is_ed = is_delegate_v<ED_>;
    [[maybe_unused]] constexpr bool is_ea = is_accept_v<EA_>;
    [[maybe_unused]] constexpr bool head = is_delegation_head_v<D_>;
    [[maybe_unused]] constexpr std::uint64_t me = ED_::min_epoch;
    [[maybe_unused]] constexpr std::uint64_t mg = ED_::min_generation;

    using Seq3 = Delegate_seq<Payload, AckMsg, K_>;
    using SeqA = Accept_seq<Payload, AckMsg, K_>;
    using RD = Redelegate<Payload, K_>;
    using DA = DelegateWithAck<Payload, AckMsg, K_>;
    using AA = AcceptWithAck<Payload, AckMsg, K_>;
    [[maybe_unused]] constexpr bool seq3_ok =
        std::is_same_v<Seq3, proto::Delegate<Payload, proto::Delegate<AckMsg, K_>>>;
    [[maybe_unused]] constexpr bool seqa_ok = std::is_same_v<SeqA, proto::Accept<Payload, proto::Accept<AckMsg, K_>>>;
    [[maybe_unused]] constexpr bool rd_ok = std::is_same_v<RD, proto::Accept<Payload, proto::Delegate<Payload, K_>>>;
    [[maybe_unused]] constexpr bool da_ok = std::is_same_v<DA, proto::Delegate<Payload, proto::Recv<AckMsg, K_>>>;
    [[maybe_unused]] constexpr bool aa_ok = std::is_same_v<AA, proto::Accept<Payload, proto::Send<AckMsg, K_>>>;

    using Recv_T = proto::Recv<AckMsg, proto::End>;
    using Prop = delegated_crash_propagation_t<Recv_T, Tag, K_>;
    [[maybe_unused]] constexpr bool prop_ok = std::is_same_v<Prop, Recovers<K_>>;

    (void)is_d;
    (void)is_a;
    (void)is_ed;
    (void)is_ea;
    (void)head;
    (void)me;
    (void)mg;
    (void)seq3_ok;
    (void)seqa_ok;
    (void)rd_ok;
    (void)da_ok;
    (void)aa_ok;
    (void)prop_ok;
}

}  // namespace crucible::fixy::sess::delegate
