// fixy-A4-022 integration test for PermissionedSnapshot +
// SnapshotSession.
//
// Tier A: header-level static_asserts prove pointer-sized PSH and
// matching protocol shapes (Loop<Send<T,Continue>> /
// Loop<Recv<T,Continue>>, EmptyPermSet).
// Tier B: direct permissioned endpoint use — Writer publishes,
// Reader loads, refcount accounts for outstanding shares.
// Tier C: typed-session send/recv smoke — the writer's Send and the
// reader's Recv both detach cleanly through PSH.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <thread>
#include <utility>

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SnapshotSession.h>

namespace {

struct TestSnapTag {};
using PermissionedSnap = ::crucible::concurrent::PermissionedSnapshot<int, TestSnapTag>;

int total_passed = 0;
int total_failed = 0;

#define CRUCIBLE_TEST_REQUIRE(cond)                                  \
    do {                                                              \
        if (!(cond)) {                                                \
            std::fprintf(stderr,                                      \
                "  REQUIRE FAILED: %s @ %s:%d\n",                     \
                #cond, __FILE__, __LINE__);                           \
            ++total_failed;                                           \
            return;                                                   \
        }                                                             \
    } while (0)

template <typename Body>
void run_test(const char* name, Body body) {
    std::fprintf(stderr, "  %s ... ", name);
    int before = total_failed;
    body();
    if (total_failed == before) {
        ++total_passed;
        std::fprintf(stderr, "OK\n");
    } else {
        std::fprintf(stderr, "FAILED\n");
    }
}

[[nodiscard]] auto mint_writer(PermissionedSnap& snap) {
    using ::crucible::safety::mint_permission_root;
    auto perm = mint_permission_root<typename PermissionedSnap::writer_tag>();
    return snap.writer(std::move(perm));
}

void test_permissioned_publish_load() {
    PermissionedSnap snap{42};
    auto writer = mint_writer(snap);

    auto reader_opt = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader_opt.has_value());
    auto& reader = *reader_opt;

    CRUCIBLE_TEST_REQUIRE(reader.load() == 42);
    writer.publish(100);
    CRUCIBLE_TEST_REQUIRE(reader.load() == 100);

    CRUCIBLE_TEST_REQUIRE(snap.outstanding_readers() == 1);
}

void test_typed_session_writer_reader_round_trip() {
    namespace ses = ::crucible::safety::proto::snapshot_session;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    PermissionedSnap snap{0};
    auto writer = mint_writer(snap);
    auto reader_opt = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader_opt.has_value());
    auto& reader = *reader_opt;

    constexpr int kCount = 64;
    std::atomic<bool> writer_done{false};

    std::jthread writer_thread{
        [&writer, &writer_done](auto) mutable {
            auto psh = ses::mint_snapshot_writer_session<PermissionedSnap>(
                ::crucible::effects::HotFgCtx{}, writer);
            for (int i = 1; i <= kCount; ++i) {
                auto next = std::move(psh).send(i, ses::blocking_publish);
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
            writer_done.store(true, std::memory_order_release);
        }
    };

    std::jthread reader_thread{
        [&reader](auto) mutable {
            auto psh = ses::mint_snapshot_reader_session<PermissionedSnap>(
                ::crucible::effects::HotFgCtx{}, reader);
            for (int i = 0; i < kCount; ++i) {
                auto [value, next] = std::move(psh).recv(ses::blocking_load);
                (void)value;
                psh = std::move(next);
            }
            std::move(psh).detach(TestInstrumentation{});
        }
    };

    writer_thread.join();
    reader_thread.join();

    CRUCIBLE_TEST_REQUIRE(writer_done.load(std::memory_order_acquire));
    // After writer finished, snapshot must hold the final published value.
    CRUCIBLE_TEST_REQUIRE(reader.load() == kCount);
}

void test_typed_session_immediate_detach() {
    namespace ses = ::crucible::safety::proto::snapshot_session;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    PermissionedSnap snap{};
    auto writer = mint_writer(snap);
    auto reader_opt = snap.reader();
    CRUCIBLE_TEST_REQUIRE(reader_opt.has_value());

    auto writer_psh =
        ses::mint_snapshot_writer_session<PermissionedSnap>(
            ::crucible::effects::HotFgCtx{}, writer);
    auto reader_psh =
        ses::mint_snapshot_reader_session<PermissionedSnap>(
            ::crucible::effects::HotFgCtx{}, *reader_opt);

    std::move(writer_psh).detach(TestInstrumentation{});
    std::move(reader_psh).detach(TestInstrumentation{});

    CRUCIBLE_TEST_REQUIRE(true);
}

namespace witness {
namespace proto = ::crucible::safety::proto;
namespace ses = proto::snapshot_session;

static_assert(ses::SnapshotSessionSurface<PermissionedSnap>);
static_assert(std::is_same_v<ses::WriterProto<int>,
                             proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(std::is_same_v<ses::ReaderProto<int>,
                             proto::Loop<proto::Recv<int, proto::Continue>>>);

using WriterHandle = PermissionedSnap::WriterHandle;
using ReaderHandle = PermissionedSnap::ReaderHandle;

using PSH_End_Write = proto::PermissionedSessionHandle<
    proto::End, proto::EmptyPermSet, WriterHandle*>;
using SH_End_Write = proto::SessionHandle<proto::End, WriterHandle*>;
static_assert(sizeof(PSH_End_Write) == sizeof(SH_End_Write));

using PSH_End_Read = proto::PermissionedSessionHandle<
    proto::End, proto::EmptyPermSet, ReaderHandle*>;
using SH_End_Read = proto::SessionHandle<proto::End, ReaderHandle*>;
static_assert(sizeof(PSH_End_Read) == sizeof(SH_End_Read));
}  // namespace witness

}  // namespace

int main() {
    std::fprintf(stderr, "[test_snapshot_session]\n");
    run_test("permissioned_publish_load", test_permissioned_publish_load);
    run_test("typed_session_writer_reader_round_trip",
             test_typed_session_writer_reader_round_trip);
    run_test("typed_session_immediate_detach",
             test_typed_session_immediate_detach);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    return total_failed == 0 ? 0 : 1;
}
