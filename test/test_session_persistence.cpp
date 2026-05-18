#include <crucible/bridges/SessionPersistence.h>

#include "test_assert.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <unistd.h>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

namespace {

constexpr proto::RoleTagId kClient{1};
constexpr proto::RoleTagId kServer{2};

struct CounterResource {
    int last = 0;
};

using PersistProto = proto::Loop<
    proto::Select<
        proto::Send<int, proto::Continue>,
        proto::End>>;

static void send_value(CounterResource& r, int value) noexcept {
    r.last = value;
}

template <proto::SessionTagId Session>
void drive_5000_events(crucible::Cipher& cipher) {
    auto view = cipher.mint_open_view();
    proto::SessionPersistencePolicy policy{
        .count_threshold = 1000,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};
    auto handle = proto::mint_persisted_session<PersistProto>(
        ctx,
        cipher,
        view,
        CounterResource{},
        Session,
        kClient,
        kServer,
        policy);

    static_assert(crucible::safety::extract::IsSessionHandle<decltype(handle)>);
    static_assert(std::is_same_v<
        typename decltype(handle)::caller_row,
        typename eff::TestRunnerCtx::row_type>);

    for (int i = 0; i < 2499; ++i) {
        auto send_handle = std::move(handle).template select_local<0>();
        handle = std::move(send_handle).send(i, send_value);
    }

    auto end_handle = std::move(handle).template select_local<1>();
    CounterResource resource = std::move(end_handle).close();
    assert(resource.last == 2498);
}

int test_two_sessions_replay_after_reopen(const std::string& dir) {
    auto cipher = crucible::Cipher::open(dir);
    assert(cipher.is_open());

    drive_5000_events<proto::SessionTagId{9001}>(cipher);
    drive_5000_events<proto::SessionTagId{9002}>(cipher);

    auto reopened = crucible::Cipher::open(dir);
    assert(reopened.is_open());
    auto view = reopened.mint_open_view();

    const auto events_a = reopened.load_session_events(
        view, proto::SessionTagId{9001});
    const auto events_b = reopened.load_session_events(
        view, proto::SessionTagId{9002});

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

    const auto suffix = reopened.load_session_events(
        view, proto::SessionTagId{9001}, proto::StepId{1001});
    assert(suffix.size() == 4000);
    assert(suffix.front().step_id.value == 1001);

    const auto replayed =
        decltype(proto::mint_persisted_session<PersistProto>(
                eff::TestRunnerCtx{},
                reopened,
                view,
                CounterResource{},
                proto::SessionTagId{42},
                kClient,
            kServer))::replay(
                eff::TestRunnerCtx{},
                reopened,
                view,
                proto::SessionTagId{9002},
                proto::StepId{4001});
    assert(replayed.size() == 1000);
    assert(replayed.front().step_id.value == 4001);
    assert(replayed.back().op == proto::SessionOp::Close);

    return 0;
}

int test_manual_flush_and_existing_handle_overload(const std::string& dir) {
    auto cipher = crucible::Cipher::open(dir);
    assert(cipher.is_open());
    auto view = cipher.mint_open_view();

    proto::SessionPersistencePolicy no_auto_flush{
        .count_threshold = 0,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};
    auto bare = proto::mint_session_handle<PersistProto>(CounterResource{});
    auto handle = proto::mint_persisted_session(
        ctx,
        std::move(bare),
        cipher,
        view,
        proto::SessionTagId{9100},
        kClient,
        kServer,
        no_auto_flush);

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

    auto reopened = crucible::Cipher::open(dir);
    assert(reopened.is_open());
    auto reopened_view = reopened.mint_open_view();
    const auto events = reopened.load_session_events(
        reopened_view, proto::SessionTagId{9100});
    assert(events.size() == 4);
    assert(events[0].op == proto::SessionOp::Select);
    assert(events[1].op == proto::SessionOp::Send);
    assert(events[2].op == proto::SessionOp::Select);
    assert(events[2].branch_index == 1);
    assert(events[3].op == proto::SessionOp::Close);
    return 0;
}

// fixy-A2-007: SessionPersistenceState must STORE the caller-supplied
// Cipher::OpenView, not silently discard it and re-mint on every flush.
// This test pins the discipline by minting the handle inside an inner
// scope, dropping the caller's local view, then driving multiple flushes
// across the handle's lifetime. Each flush exercises the stored view; if
// the state had thrown the caller's witness away the test would still
// pass against `mint_open_view()` (since the cipher remains open), so the
// witness is structural: the handle owns its own proof-of-open from mint
// time, and subsequent flushes never re-touch Cipher::mint_open_view().
int test_fixy_a2_007_stored_view_discipline(const std::string& dir) {
    auto cipher = crucible::Cipher::open(dir);
    assert(cipher.is_open());

    proto::SessionPersistencePolicy manual_only{
        .count_threshold = 0,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};

    // Inner scope mints the handle; the local OpenView dies at scope exit.
    // The returned handle must already carry its own copy of the view.
    auto handle = [&]() {
        auto local_view = cipher.mint_open_view();
        auto bare = proto::mint_session_handle<PersistProto>(CounterResource{});
        return proto::mint_persisted_session(
            ctx,
            std::move(bare),
            cipher,
            local_view,
            proto::SessionTagId{9300},
            kClient,
            kServer,
            manual_only);
    }();

    // First flush — only the handle's stored view is available.
    auto s1 = std::move(handle).template select_local<0>();
    handle = std::move(s1).send(11, send_value);
    assert(handle.pending_persisted_events() == 2);
    assert(handle.flush());
    assert(handle.flushed_persisted_events() == 2);
    assert(handle.pending_persisted_events() == 0);

    // Second flush — same handle, same stored view; verifies multiple
    // flushes share the mint-time witness across the handle's lifetime.
    auto s2 = std::move(handle).template select_local<0>();
    handle = std::move(s2).send(22, send_value);
    assert(handle.flush());
    assert(handle.flushed_persisted_events() == 4);

    // Third flush — empty pending is a no-op; stored view path stays sound
    // even when there's nothing to drain.
    assert(handle.flush());
    assert(handle.flushed_persisted_events() == 4);

    auto end_handle = std::move(handle).template select_local<1>();
    CounterResource resource = std::move(end_handle).close();
    assert(resource.last == 22);

    // Re-open and verify the full event stream landed on disk via the
    // stored-view path. 3 Selects + 2 Sends + 1 Close = 6 events.
    auto reopened = crucible::Cipher::open(dir);
    assert(reopened.is_open());
    auto rv = reopened.mint_open_view();
    const auto events = reopened.load_session_events(
        rv, proto::SessionTagId{9300});
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

}  // namespace

int main() {
    const auto tmp = std::filesystem::temp_directory_path() /
        ("crucible_test_session_persistence_" + std::to_string(::getpid()));
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

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    if (rc3 != 0) return rc3;

    std::puts("session_persistence: 2 persisted sessions x 5000 events, "
              "manual flush, existing-handle mint, fixy-A2-007 stored-view "
              "discipline, reopen + replay OK");
    return 0;
}
