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
    proto::SessionPersistencePolicy policy{
        .count_threshold = 1000,
        .time_threshold = std::chrono::steady_clock::duration::zero(),
    };

    eff::TestRunnerCtx ctx{};
    auto handle = proto::mint_persisted_session<PersistProto>(
        ctx,
        cipher,
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
            CounterResource{},
            proto::SessionTagId{42},
            kClient,
            kServer))::replay(
                eff::TestRunnerCtx{},
                reopened,
                proto::SessionTagId{9002},
                proto::StepId{4001});
    assert(replayed.size() == 1000);
    assert(replayed.front().step_id.value == 4001);
    assert(replayed.back().op == proto::SessionOp::Close);

    return 0;
}

}  // namespace

int main() {
    const auto tmp = std::filesystem::temp_directory_path() /
        ("crucible_test_session_persistence_" + std::to_string(::getpid()));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    const int rc = test_two_sessions_replay_after_reopen(tmp.string());

    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    if (rc != 0) return rc;

    std::puts("session_persistence: 2 persisted sessions x 5000 events, "
              "Cipher federation-format batches, reopen + replay OK");
    return 0;
}
