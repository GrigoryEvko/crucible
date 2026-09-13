// The session-persistence header does not carry the Cipher definition.
// This file constructs a Cipher and calls its methods, so it needs the
// complete class.
#include <crucible/Cipher.h>
#include <crucible/bridges/SessionPersistence.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionMint.h>

#include "test_assert.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <utility>

using CipherRoot = crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>;

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

namespace {

constexpr proto::RoleTagId kClient{1};
constexpr proto::RoleTagId kServer{2};

struct CounterResource {
    int last = 0;
};

using PersistProto = proto::Loop<proto::Select<proto::Send<int, proto::Continue>, proto::End>>;

static void send_value(CounterResource& r, int value) noexcept { r.last = value; }

// mint_persisted_session allocates, yet is noexcept: under -fno-exceptions
// an allocation failure terminates rather than throws, so the declaration
// is truthful.  One assertion per overload form pins it against a
// signature edit that silently drops noexcept.
//
// A Loop protocol unrolls into its body at mint time, so no handle
// specialization names the unrolled type.  decltype of the constructor
// mint recovers it, which is well-formed inside a noexcept operand.
using BareSH = decltype(proto::mint_session_handle<PersistProto>(std::declval<CounterResource>()));
using BarePSH = decltype(proto::mint_permissioned_session<PersistProto>(std::declval<eff::TestRunnerCtx const&>(),
                                                                        std::declval<CounterResource>()));

static_assert(noexcept(proto::mint_persisted_session<PersistProto>(
                  std::declval<eff::TestRunnerCtx const&>(), std::declval<crucible::Cipher&>(),
                  std::declval<crucible::CipherOpenView const&>(), std::declval<CounterResource>(),
                  std::declval<proto::SessionTagId>(), std::declval<proto::RoleTagId>(),
                  std::declval<proto::RoleTagId>(), std::declval<proto::SessionPersistencePolicy>())),
              "mint_persisted_session<Proto>(Ctx, Cipher&, OpenView, Resource&&, "
              "...) must be noexcept.");

static_assert(noexcept(proto::mint_persisted_session(std::declval<eff::TestRunnerCtx const&>(), std::declval<BareSH>(),
                                                     std::declval<crucible::Cipher&>(),
                                                     std::declval<crucible::CipherOpenView const&>(),
                                                     std::declval<proto::SessionTagId>(),
                                                     std::declval<proto::RoleTagId>(), std::declval<proto::RoleTagId>(),
                                                     std::declval<proto::SessionPersistencePolicy>())),
              "mint_persisted_session(Ctx, SessionHandle, Cipher&, OpenView, ...) "
              "must be noexcept.");

static_assert(noexcept(proto::mint_persisted_session(std::declval<eff::TestRunnerCtx const&>(), std::declval<BarePSH>(),
                                                     std::declval<crucible::Cipher&>(),
                                                     std::declval<crucible::CipherOpenView const&>(),
                                                     std::declval<proto::SessionTagId>(),
                                                     std::declval<proto::RoleTagId>(), std::declval<proto::RoleTagId>(),
                                                     std::declval<proto::SessionPersistencePolicy>())),
              "mint_persisted_session(Ctx, PermissionedSessionHandle, Cipher&, "
              "OpenView, ...) must be noexcept.");

template <proto::SessionTagId Session>
void drive_5000_events(crucible::Cipher& cipher) {
    auto view = cipher.mint_open_view();
    proto::SessionPersistencePolicy policy{
        .count_threshold = 1000,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};
    auto handle = proto::mint_persisted_session<PersistProto>(ctx, cipher, view, CounterResource{}, Session, kClient,
                                                              kServer, policy);

    static_assert(crucible::safety::extract::IsSessionHandle<decltype(handle)>);
    static_assert(std::is_same_v<typename decltype(handle)::caller_row, typename eff::TestRunnerCtx::row_type>);

    for (int i = 0; i < 2499; ++i) {
        auto send_handle = std::move(handle).template select_local<0>();
        handle = std::move(send_handle).send(i, send_value);
    }

    auto end_handle = std::move(handle).template select_local<1>();
    CounterResource resource = std::move(end_handle).close();
    assert(resource.last == 2498);
}

int test_two_sessions_replay_after_reopen(const std::string& dir) {
    auto cipher = crucible::Cipher::open(CipherRoot{dir});
    assert(cipher.is_open());

    drive_5000_events<proto::SessionTagId{9001}>(cipher);
    drive_5000_events<proto::SessionTagId{9002}>(cipher);

    auto reopened = crucible::Cipher::open(CipherRoot{dir});
    assert(reopened.is_open());
    auto view = reopened.mint_open_view();

    const auto events_a = reopened.load_session_events(view, proto::SessionTagId{9001});
    const auto events_b = reopened.load_session_events(view, proto::SessionTagId{9002});

    assert(events_a.size() == 5000);
    assert(events_b.size() == 5000);

    assert(events_a.front().session == proto::SessionTagId{9001});
    assert(events_b.front().session == proto::SessionTagId{9002});
    assert(events_a.front().op == proto::SessionOp::Select);
    assert(events_a[1].op == proto::SessionOp::Send);
    assert(events_a[4998].op == proto::SessionOp::Select);
    assert(events_a[4998].branch_index == 1);
    assert(events_a[4999].op == proto::SessionOp::Close);

    for (std::size_t i = 1; i < events_a.size(); ++i) {
        assert(events_a[i - 1].step_id.value < events_a[i].step_id.value);
        assert(events_b[i - 1].step_id.value < events_b[i].step_id.value);
    }

    const auto suffix = reopened.load_session_events(view, proto::SessionTagId{9001}, proto::StepId{1001});
    assert(suffix.size() == 4000);
    assert(suffix.front().step_id.value == 1001);

    const auto replayed = decltype(proto::mint_persisted_session<PersistProto>(
        eff::TestRunnerCtx{}, reopened, view, CounterResource{}, proto::SessionTagId{42}, kClient,
        kServer))::replay(eff::TestRunnerCtx{}, reopened, view, proto::SessionTagId{9002}, proto::StepId{4001});
    assert(replayed.size() == 1000);
    assert(replayed.front().step_id.value == 4001);
    assert(replayed.back().op == proto::SessionOp::Close);

    return 0;
}

int test_manual_flush_and_existing_handle_overload(const std::string& dir) {
    auto cipher = crucible::Cipher::open(CipherRoot{dir});
    assert(cipher.is_open());
    auto view = cipher.mint_open_view();

    proto::SessionPersistencePolicy no_auto_flush{
        .count_threshold = 0,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};
    auto bare = proto::mint_session_handle<PersistProto>(CounterResource{});
    auto handle = proto::mint_persisted_session(ctx, std::move(bare), cipher, view, proto::SessionTagId{9100}, kClient,
                                                kServer, no_auto_flush);

    auto send_handle = std::move(handle).template select_local<0>();
    handle = std::move(send_handle).send(7, send_value);
    assert(handle.pending_persisted_events() == 2);
    assert(handle.flushed_persisted_events() == 0);
    assert(handle.flush());
    assert(handle.pending_persisted_events() == 0);
    assert(handle.flushed_persisted_events() == 2);

    auto end_handle = std::move(handle).template select_local<1>();
    CounterResource resource = std::move(end_handle).close();
    assert(resource.last == 7);

    auto reopened = crucible::Cipher::open(CipherRoot{dir});
    assert(reopened.is_open());
    auto reopened_view = reopened.mint_open_view();
    const auto events = reopened.load_session_events(reopened_view, proto::SessionTagId{9100});
    assert(events.size() == 4);
    assert(events[0].op == proto::SessionOp::Select);
    assert(events[1].op == proto::SessionOp::Send);
    assert(events[2].op == proto::SessionOp::Select);
    assert(events[2].branch_index == 1);
    assert(events[3].op == proto::SessionOp::Close);
    return 0;
}

// The persistence state must store the caller's open view rather than
// re-mint one on every flush.  The handle is minted in an inner scope so
// the caller's own view is already dead by the first flush.  A state that
// discarded the witness would still pass while the cipher stays open, so
// the claim is structural: the handle carries its own proof of open from
// mint time and later flushes never re-mint one.
int test_fixy_a2_007_stored_view_discipline(const std::string& dir) {
    auto cipher = crucible::Cipher::open(CipherRoot{dir});
    assert(cipher.is_open());

    proto::SessionPersistencePolicy manual_only{
        .count_threshold = 0,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};

    auto handle = [&]() {
        auto local_view = cipher.mint_open_view();
        auto bare = proto::mint_session_handle<PersistProto>(CounterResource{});
        return proto::mint_persisted_session(ctx, std::move(bare), cipher, local_view, proto::SessionTagId{9300},
                                             kClient, kServer, manual_only);
    }();

    auto s1 = std::move(handle).template select_local<0>();
    handle = std::move(s1).send(11, send_value);
    assert(handle.pending_persisted_events() == 2);
    assert(handle.flush());
    assert(handle.flushed_persisted_events() == 2);
    assert(handle.pending_persisted_events() == 0);

    auto s2 = std::move(handle).template select_local<0>();
    handle = std::move(s2).send(22, send_value);
    assert(handle.flush());
    assert(handle.flushed_persisted_events() == 4);

    // A flush with nothing pending is a no-op and still reports success.
    assert(handle.flush());
    assert(handle.flushed_persisted_events() == 4);

    auto end_handle = std::move(handle).template select_local<1>();
    CounterResource resource = std::move(end_handle).close();
    assert(resource.last == 22);

    // Three selects, two sends and one close make the six events.
    auto reopened = crucible::Cipher::open(CipherRoot{dir});
    assert(reopened.is_open());
    auto rv = reopened.mint_open_view();
    const auto events = reopened.load_session_events(rv, proto::SessionTagId{9300});
    assert(events.size() == 6);
    assert(events[0].op == proto::SessionOp::Select);
    assert(events[1].op == proto::SessionOp::Send);
    assert(events[2].op == proto::SessionOp::Select);
    assert(events[3].op == proto::SessionOp::Send);
    assert(events[4].op == proto::SessionOp::Select);
    assert(events[4].branch_index == 1);
    assert(events[5].op == proto::SessionOp::Close);
    return 0;
}

static_assert(!std::is_trivially_destructible_v<proto::SessionPersistenceState<eff::TestRunnerCtx::row_type>>,
              "~SessionPersistenceState must be non-trivial: it flushes pending "
              "events at destruction, or an abandoned handle loses its audit "
              "trail.");
static_assert(std::is_nothrow_destructible_v<proto::SessionPersistenceState<eff::TestRunnerCtx::row_type>>,
              "~SessionPersistenceState must be noexcept, so every flush gate "
              "shares one termination policy.");

// The state is driven directly rather than through a persisted handle.
// A handle deletes detach, so user code cannot abandon one cleanly, and a
// debug build fires the handle's abandonment check before the state's
// destructor runs.  Either route would hide whether the destructor itself
// drains the pending tail.
int test_fixy_a2_013_destructor_flushes_pending_events(const std::string& dir) {
    auto cipher = crucible::Cipher::open(CipherRoot{dir});
    assert(cipher.is_open());

    constexpr proto::SessionTagId kSession{0xA2013};
    constexpr int kEvents = 5;

    {
        auto view = cipher.mint_open_view();
        // A zero count threshold disables count gating and an hour-scale
        // time threshold outlives the test, so the destructor is the only
        // flush path left.
        proto::SessionPersistencePolicy manual_only{
            .count_threshold = 0,
            .time_threshold = std::chrono::hours{1},
        };
        proto::SessionPersistenceState<eff::TestRunnerCtx::row_type> state{cipher, view, kSession, manual_only};

        for (int i = 0; i < kEvents; ++i) {
            proto::SessionEvent ev{};
            ev.from_role = kClient;
            ev.to_role = kServer;
            ev.op = proto::SessionOp::Send;
            state.log().record_now(ev);
        }
        assert(state.pending_count() == kEvents);
        assert(state.flushed_count() == 0);
        // Scope exit runs the destructor, which must drain the pending
        // tail into the cold tier.
    }

    auto reopened = crucible::Cipher::open(CipherRoot{dir});
    assert(reopened.is_open());
    auto view = reopened.mint_open_view();
    const auto events = reopened.load_session_events(view, kSession);
    assert(events.size() == kEvents);
    for (std::size_t i = 0; i < events.size(); ++i) {
        assert(events[i].session == kSession);
        assert(events[i].op == proto::SessionOp::Send);
        if (i > 0) {
            assert(events[i - 1].step_id.value < events[i].step_id.value);
        }
    }
    return 0;
}

}  // namespace

int main() {
    const auto tmp =
        std::filesystem::temp_directory_path() / ("crucible_test_session_persistence_" + std::to_string(::getpid()));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    const int rc = test_two_sessions_replay_after_reopen(tmp.string());
    if (rc != 0) return rc;
    const int rc2 = test_manual_flush_and_existing_handle_overload(tmp.string());
    if (rc2 != 0) {
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
        return rc2;
    }
    const int rc3 = test_fixy_a2_007_stored_view_discipline(tmp.string());
    if (rc3 != 0) {
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
        return rc3;
    }
    const int rc4 = test_fixy_a2_013_destructor_flushes_pending_events(tmp.string());

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    if (rc4 != 0) return rc4;

    std::puts("session_persistence: 2 persisted sessions x 5000 events, "
              "manual flush, existing-handle mint, stored-view discipline, "
              "destructor-flush discipline, reopen + replay OK");
    return 0;
}
