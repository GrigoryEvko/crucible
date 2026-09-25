// Each SessionHandle specialization below is instantiated so that its
// friend declaration for the minting factory is parsed.  Losing such a
// friend declaration is otherwise silent here and fails only at a
// faraway call site.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/_SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <type_traits>
#include <utility>

namespace pf = ::crucible::safety::proto;

namespace {

struct FakeRes {
    int sentinel = 42;
};

struct Msg {};
struct Reply {};

inline constexpr std::size_t kFixyFriendMintHandleCount = 11;

static_assert(kFixyFriendMintHandleCount == 11, "Friend `detail::make_session_handle` reach count drift — a "
                                                "SessionHandle specialization was added or removed without "
                                                "updating both the count and the per-protocol cell here.");

using H_End = pf::SessionHandle<pf::End, FakeRes, void>;
static_assert(std::is_same_v<typename H_End::protocol, pf::End>);
static_assert(std::is_same_v<typename H_End::resource_type, FakeRes>);

using H_Send = pf::SessionHandle<pf::Send<Msg, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Send::protocol, pf::Send<Msg, pf::End>>);
static_assert(std::is_same_v<typename H_Send::message_type, Msg>);

using H_Recv = pf::SessionHandle<pf::Recv<Msg, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Recv::protocol, pf::Recv<Msg, pf::End>>);
static_assert(std::is_same_v<typename H_Recv::message_type, Msg>);

using H_Select = pf::SessionHandle<pf::Select<pf::Send<Msg, pf::End>, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Select::protocol, pf::Select<pf::Send<Msg, pf::End>, pf::End>>);
static_assert(H_Select::branch_count == 2);

using H_Offer = pf::SessionHandle<pf::Offer<pf::Recv<Msg, pf::End>, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Offer::protocol, pf::Offer<pf::Recv<Msg, pf::End>, pf::End>>);
static_assert(H_Offer::branch_count == 2);

using H_Stop = pf::SessionHandle<pf::Stop, FakeRes, void>;
static_assert(std::is_same_v<typename H_Stop::protocol, pf::Stop>);
static_assert(H_Stop::crash_class == ::crucible::algebra::lattices::CrashClass::Abort);

using H_Checkpoint = pf::SessionHandle<pf::CheckpointedSession<pf::End, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Checkpoint::protocol, pf::CheckpointedSession<pf::End, pf::End>>);
static_assert(std::is_same_v<typename H_Checkpoint::base_protocol, pf::End>);
static_assert(std::is_same_v<typename H_Checkpoint::rollback_protocol, pf::End>);

using DelegatedProto = pf::Send<Msg, pf::End>;
using H_Delegate = pf::SessionHandle<pf::Delegate<DelegatedProto, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Delegate::protocol, pf::Delegate<DelegatedProto, pf::End>>);

using H_Accept = pf::SessionHandle<pf::Accept<DelegatedProto, pf::End>, FakeRes, void>;
static_assert(std::is_same_v<typename H_Accept::protocol, pf::Accept<DelegatedProto, pf::End>>);

// No LoopCtx restriction, unlike EpochedAccept below.
using H_EpDelegate = pf::SessionHandle<pf::EpochedDelegate<DelegatedProto, pf::End,
                                                           /*MinEpoch=*/1, /*MinGeneration=*/1>,
                                       FakeRes, void>;
static_assert(std::is_same_v<typename H_EpDelegate::protocol, pf::EpochedDelegate<DelegatedProto, pf::End, 1, 1>>);
static_assert(H_EpDelegate::min_epoch == 1);
static_assert(H_EpDelegate::min_generation == 1);

// The handle body requires LoopCtx to be an EpochCtx, so the factory
// default of void cannot reach it.  Passing an EpochCtx directly still
// instantiates the specialization and its friend declaration.
using EpochLoopCtx = pf::EpochCtx</*CurrentEpoch=*/2, /*CurrentGeneration=*/2>;
using H_EpAccept = pf::SessionHandle<pf::EpochedAccept<DelegatedProto, pf::End,
                                                       /*MinEpoch=*/1, /*MinGeneration=*/1>,
                                     FakeRes, EpochLoopCtx>;
static_assert(std::is_same_v<typename H_EpAccept::protocol, pf::EpochedAccept<DelegatedProto, pf::End, 1, 1>>);
static_assert(H_EpAccept::min_epoch == 1);
static_assert(H_EpAccept::min_generation == 1);

// Each cell wraps the call in a lambda and checks decltype of its
// return, so the factory is instantiated at compile time with no
// runtime driver.  EpochedAccept is absent for the reason above.

[[maybe_unused]] constexpr auto mint_check_end = []() noexcept { return pf::mint_session_handle<pf::End>(FakeRes{}); };
static_assert(std::is_same_v<decltype(mint_check_end()), H_End>);

[[maybe_unused]] constexpr auto mint_check_send = []() noexcept {
    return pf::mint_session_handle<pf::Send<Msg, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_send()), H_Send>);

[[maybe_unused]] constexpr auto mint_check_recv = []() noexcept {
    return pf::mint_session_handle<pf::Recv<Msg, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_recv()), H_Recv>);

[[maybe_unused]] constexpr auto mint_check_select = []() noexcept {
    return pf::mint_session_handle<pf::Select<pf::Send<Msg, pf::End>, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_select()), H_Select>);

[[maybe_unused]] constexpr auto mint_check_offer = []() noexcept {
    return pf::mint_session_handle<pf::Offer<pf::Recv<Msg, pf::End>, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_offer()), H_Offer>);

[[maybe_unused]] constexpr auto mint_check_stop = []() noexcept {
    return pf::mint_session_handle<pf::Stop>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_stop()), H_Stop>);

[[maybe_unused]] constexpr auto mint_check_checkpoint = []() noexcept {
    return pf::mint_session_handle<pf::CheckpointedSession<pf::End, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_checkpoint()), H_Checkpoint>);

[[maybe_unused]] constexpr auto mint_check_delegate = []() noexcept {
    return pf::mint_session_handle<pf::Delegate<DelegatedProto, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_delegate()), H_Delegate>);

[[maybe_unused]] constexpr auto mint_check_accept = []() noexcept {
    return pf::mint_session_handle<pf::Accept<DelegatedProto, pf::End>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_accept()), H_Accept>);

[[maybe_unused]] constexpr auto mint_check_ep_delegate = []() noexcept {
    return pf::mint_session_handle<pf::EpochedDelegate<DelegatedProto, pf::End, 1, 1>>(FakeRes{});
};
static_assert(std::is_same_v<decltype(mint_check_ep_delegate()), H_EpDelegate>);

// Every cell builds its handle from a volatile-seeded value, so the
// factory runs under runtime semantics rather than consteval folding.

[[noreturn]] void fail_smoke(const char* msg) {
    std::fprintf(stderr, "test_fixy_sess_mint_handle_reach: %s\n", msg);
    std::abort();
}

void smoke_check(bool cond, const char* msg) {
    if (!cond) fail_smoke(msg);
}

template <typename Expected, typename Actual>
void assert_same_handle_type(const char* tag) {
    smoke_check(std::is_same_v<Expected, Actual>, tag);
}

// A non-terminal handle is linear: dropping it without advancing past
// it aborts in the base destructor.  This file exercises only the
// factory, so every non-terminal handle is retired with the
// TestInstrumentation detach tag the substrate provides for that case.
namespace detach_reason = ::crucible::safety::proto::detach_reason;

void smoke_runtime_mints() {
    volatile int seed = 17;

    {
        FakeRes r{seed};
        auto h = pf::mint_session_handle<pf::End>(std::move(r));
        assert_same_handle_type<H_End, decltype(h)>("End handle type");
        FakeRes recovered = std::move(h).close();
        smoke_check(recovered.sentinel == seed, "End resource recover");
    }
    {
        FakeRes r{seed + 1};
        auto h = pf::mint_session_handle<pf::Send<Msg, pf::End>>(std::move(r));
        assert_same_handle_type<H_Send, decltype(h)>("Send handle type");
        smoke_check(h.resource().sentinel == seed + 1, "Send resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 2};
        auto h = pf::mint_session_handle<pf::Recv<Msg, pf::End>>(std::move(r));
        assert_same_handle_type<H_Recv, decltype(h)>("Recv handle type");
        smoke_check(h.resource().sentinel == seed + 2, "Recv resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 3};
        auto h = pf::mint_session_handle<pf::Select<pf::Send<Msg, pf::End>, pf::End>>(std::move(r));
        assert_same_handle_type<H_Select, decltype(h)>("Select handle type");
        smoke_check(h.resource().sentinel == seed + 3, "Select resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 4};
        auto h = pf::mint_session_handle<pf::Offer<pf::Recv<Msg, pf::End>, pf::End>>(std::move(r));
        assert_same_handle_type<H_Offer, decltype(h)>("Offer handle type");
        smoke_check(h.resource().sentinel == seed + 4, "Offer resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 5};
        auto h = pf::mint_session_handle<pf::Stop>(std::move(r));
        assert_same_handle_type<H_Stop, decltype(h)>("Stop handle type");
        FakeRes recovered = std::move(h).close();
        smoke_check(recovered.sentinel == seed + 5, "Stop resource recover");
    }
    {
        FakeRes r{seed + 6};
        auto h = pf::mint_session_handle<pf::CheckpointedSession<pf::End, pf::End>>(std::move(r));
        assert_same_handle_type<H_Checkpoint, decltype(h)>("Checkpoint handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 7};
        auto h = pf::mint_session_handle<pf::Delegate<DelegatedProto, pf::End>>(std::move(r));
        assert_same_handle_type<H_Delegate, decltype(h)>("Delegate handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 8};
        auto h = pf::mint_session_handle<pf::Accept<DelegatedProto, pf::End>>(std::move(r));
        assert_same_handle_type<H_Accept, decltype(h)>("Accept handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 9};
        auto h = pf::mint_session_handle<pf::EpochedDelegate<DelegatedProto, pf::End, 1, 1>>(std::move(r));
        assert_same_handle_type<H_EpDelegate, decltype(h)>("EpochedDelegate handle type");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
    {
        FakeRes r{seed + 10};
        // The value constructor is private.  make_session_handle is the
        // one factory that exposes the LoopCtx parameter, which this
        // protocol needs to satisfy its fresh-epoch assertion.
        auto h =
            pf::detail::make_session_handle<pf::EpochedAccept<DelegatedProto, pf::End, 1, 1>, FakeRes, EpochLoopCtx>(
                std::move(r));
        static_assert(std::is_same_v<H_EpAccept, decltype(h)>);
        smoke_check(h.resource().sentinel == seed + 10, "EpochedAccept resource carry");
        std::move(h).detach(detach_reason::TestInstrumentation{});
    }
}

}  // namespace

int main() {
    smoke_runtime_mints();
    return 0;
}
