// A context that claims a session epoch takes the claim from the live epoch
// source.  The source is the only thing that advances the epoch and the
// generation, and at most one source lives in a process.  The one door of the
// wrapper checks the claim against the source in every build mode.
//
// The test proves four facts:
//   1. A wrapper claiming the coordinate the source holds is built, and a
//      protocol whose thresholds that coordinate meets admits it.
//   2. No other route builds a wrapper: not empty braces, not the context it
//      wraps, not the old one-argument door, and not bytes.
//   3. A claim the source does not hold ends the process, at the door and
//      again at the mint when the source advanced after the door.
//   4. A second live source ends the process.

#include <crucible/sessions/_SessionMint.h>
#include <foundation/Lifetime.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace session_epoch_source_test {

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

using Fg = eff::HotFgCtx;
using Claim = proto::EpochExecCtx<2, 1, Fg>;
using Payload = proto::DelegatedSession<proto::End, proto::EmptyPermSet>;

// ── 2. No route but the door builds a wrapper ──

template <class Ctx>
concept builds_without_a_source = requires(Ctx const& ctx) { proto::with_session_epoch<2, 1>(ctx); };

static_assert(!std::is_default_constructible_v<Claim>, "empty braces state an epoch");
static_assert(!std::is_constructible_v<Claim, Fg>, "the wrapped context alone states an epoch");
static_assert(!builds_without_a_source<Fg>, "the one-argument door states an epoch");
static_assert(!std::is_trivially_copyable_v<Claim>, "std::bit_cast builds a wrapper from bytes");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Claim>,
              "a lifetime start over bytes builds a wrapper");
static_assert(std::is_copy_constructible_v<Claim>, "a wrapper the door built still copies");
static_assert(sizeof(Claim) == sizeof(proto::SessionEpochSource const*), "the wrapper carries its source only");

// The source is the one authority: it cannot be copied or moved, and it is
// built only from the root permission of the authority.
static_assert(!std::is_copy_constructible_v<proto::SessionEpochSource>);
static_assert(!std::is_move_constructible_v<proto::SessionEpochSource>);
static_assert(!std::is_default_constructible_v<proto::SessionEpochSource>);

// ── 1. A true claim fits the protocol its coordinate meets ──

static_assert(proto::CtxFitsPermissionedProtocol<proto::EpochedAccept<Payload, proto::End, 2, 1>, Claim,
                                                 proto::EmptyPermSet>);
static_assert(!proto::CtxFitsPermissionedProtocol<proto::EpochedAccept<Payload, proto::End, 3, 1>, Claim,
                                                  proto::EmptyPermSet>);

// Advances the source to epoch 2 generation 1, builds the wrapper, and mints
// an accept session with it.  Returns true when every step holds.
[[nodiscard]] inline bool true_claim_is_admitted() noexcept {
    proto::SessionEpochSource source{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
    if (source.holds(2, 1)) return false;
    source.advance_epoch();
    source.advance_epoch();
    source.advance_generation();
    if (!source.holds(2, 1) || source.holds(2, 0) || source.holds(3, 1)) return false;

    const Claim claim = proto::with_session_epoch<2, 1>(Fg{}, source);
    struct Channel {};
    auto handle = proto::mint_permissioned_session<proto::EpochedAccept<Payload, proto::End, 2, 1>>(claim, Channel{});
    using Handle = decltype(handle);
    std::move(handle).detach(proto::detach_reason::TestInstrumentation{});
    return std::is_same_v<typename Handle::loop_ctx, proto::EpochCtx<2, 1>>;
}

// Runs one attack in a child process and reports whether the child died of
// SIGABRT.  An attack the source refuses ends the process, so it cannot run
// in the parent.
template <class Attack>
[[nodiscard]] bool dies_of_abort(Attack attack) noexcept {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: a refused claim ends the process, so it runs in a
    // child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) {
        std::fprintf(stderr, "test_session_epoch_source: fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        attack();
        std::_Exit(0);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {  // SPAWN-PROCESS-OK: death test, see above
        std::fprintf(stderr, "test_session_epoch_source: waitpid failed\n");
        std::_Exit(2);
    }
    return WIFSIGNALED(status) != 0 && WTERMSIG(status) == SIGABRT;
}

}  // namespace session_epoch_source_test

int main() {
    namespace t = session_epoch_source_test;
    namespace eff = ::crucible::effects;
    namespace proto = ::crucible::safety::proto;

    if (!t::true_claim_is_admitted()) {
        std::fprintf(stderr, "test_session_epoch_source: a claim the source holds was refused\n");
        return 1;
    }

    // ── 3. A claim the source does not hold ends the process ──
    const bool false_claim_dies = t::dies_of_abort([] {
        proto::SessionEpochSource source{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
        source.advance_epoch();
        const auto forged = proto::with_session_epoch<999, 999>(eff::HotFgCtx{}, source);
        (void)forged;
    });
    if (!false_claim_dies) {
        std::fprintf(stderr, "test_session_epoch_source: a claim the source does not hold was admitted\n");
        return 1;
    }

    // An older coordinate is a false claim too: the wrapper states what the
    // source holds now, not what it held before.
    const bool stale_claim_dies = t::dies_of_abort([] {
        proto::SessionEpochSource source{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
        source.advance_epoch();
        source.advance_epoch();
        const auto stale = proto::with_session_epoch<1, 0>(eff::HotFgCtx{}, source);
        (void)stale;
    });
    if (!stale_claim_dies) {
        std::fprintf(stderr, "test_session_epoch_source: a claim of an older epoch was admitted\n");
        return 1;
    }

    // A wrapper built at the door and made stale by a later advance fails at
    // the mint that reads its claim.
    const bool stale_wrapper_dies_at_mint = t::dies_of_abort([] {
        proto::SessionEpochSource source{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
        source.advance_epoch();
        const auto claim = proto::with_session_epoch<1, 0>(eff::HotFgCtx{}, source);
        source.advance_epoch();
        struct Channel {};
        auto handle = proto::mint_permissioned_session<proto::EpochedAccept<t::Payload, proto::End, 1, 0>>(
            claim, Channel{});
        // A mint that admits the claim must not abort here for another reason.
        std::move(handle).detach(proto::detach_reason::TestInstrumentation{});
    });
    if (!stale_wrapper_dies_at_mint) {
        std::fprintf(stderr, "test_session_epoch_source: a wrapper made stale by an advance was admitted by a mint\n");
        return 1;
    }

    // A wrapper whose source ended fails at the mint too.  The mint compares
    // the address first, so it never reads the ended source.
    const bool orphan_wrapper_dies_at_mint = t::dies_of_abort([] {
        struct Channel {};
        auto build_orphan = [] {
            proto::SessionEpochSource first{
                ::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
            return proto::with_session_epoch<0, 0>(eff::HotFgCtx{}, first);
        };
        const auto orphan = build_orphan();
        auto handle = proto::mint_permissioned_session<proto::EpochedAccept<t::Payload, proto::End, 0, 0>>(
            orphan, Channel{});
        // A mint that admits the claim must not abort here for another reason.
        std::move(handle).detach(proto::detach_reason::TestInstrumentation{});
    });
    if (!orphan_wrapper_dies_at_mint) {
        std::fprintf(stderr, "test_session_epoch_source: a wrapper whose source ended was admitted by a mint\n");
        return 1;
    }

    // ── 4. A second live source ends the process ──
    const bool second_source_dies = t::dies_of_abort([] {
        proto::SessionEpochSource first{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
        proto::SessionEpochSource second{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
        second.advance_epoch();
        (void)first.holds(0, 0);
    });
    if (!second_source_dies) {
        std::fprintf(stderr, "test_session_epoch_source: a second live epoch source was built\n");
        return 1;
    }

    // A source built after the first one ends is a new authority, not a second
    // live one.
    {
        proto::SessionEpochSource first{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
        (void)first.holds(0, 0);
    }
    proto::SessionEpochSource next{::crucible::safety::mint_permission_root<proto::SessionEpochAuthority>()};
    if (!next.holds(0, 0)) {
        std::fprintf(stderr, "test_session_epoch_source: a new source did not start at epoch 0 generation 0\n");
        return 1;
    }

    std::fprintf(stderr, "test_session_epoch_source: true claims admitted, false and duplicate authorities refused\n");
    return 0;
}
