#include <crucible/cntp/ConnectionPool.h>
#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

#include <atomic>
#include "test_assert.h"
#include <cstdio>
#include <string_view>
#include <thread>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace fe = ::foundation::effects;

namespace {

[[nodiscard]] cog::CogIdentity remote(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0xC0A5ull, lo};
    id.kind = cog::CogKind::NicPort;
    return id;
}

[[nodiscard]] cntp::LinearConnection<cntp::TransportClass::MtlsTcp> connection(int fd, cog::CogIdentity const& id,
                                                                               std::uint64_t connection_id) {
    cntp::SocketFd const socket = cntp::admit_socket_fd(fd).expect("the descriptor of the test is not negative");
    auto cid = cntp::admit_connection_id(connection_id);
    assert(cid.is_ok());
    auto conn = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(
        socket, id, std::move(cid).expect("the connection id is positive"));
    return std::move(conn).expect("the remote of the test is valid");
}

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, cntp::PoolError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.  A call that succeeds ends
// the process.
template <class Value>
cntp::PoolError error_of(::fixy::Result<Value, cntp::PoolError> result) {
    return std::move(result).match(
        [](Value) noexcept -> cntp::PoolError { ::fixy::fatal("the call succeeds, and the test expects an error"); },
        [](cntp::PoolError error) noexcept { return error; });
}

using MtlsConnection = cntp::LinearConnection<cntp::TransportClass::MtlsTcp>;

cntp::PoolError error_of(::fixy::Result<MtlsConnection, cntp::PoolError> result) {
    return std::move(result).match(
        [](MtlsConnection made) noexcept -> cntp::PoolError {
            drop(std::move(made));
            ::fixy::fatal("the mint succeeds, and the test expects an error");
        },
        [](cntp::PoolError error) noexcept { return error; });
}

[[nodiscard]] cntp::PoolConfig config(std::uint16_t per_remote, std::uint64_t idle_ns) {
    return cntp::PoolConfig{
        .max_per_remote = cntp::admit_pool_size(per_remote).expect("the pool size is positive"),
        .max_idle_ns = cntp::admit_idle_timeout_ns(idle_ns).expect("the idle timeout is positive"),
    };
}

void test_admission_and_names() {
    static_assert(cntp::transport_class_name(cntp::TransportClass::RdmaRcQp) == "RdmaRcQp");
    static_assert(cntp::pool_error_name(cntp::PoolError::PoolFull) == "PoolFull");
    static_assert(cntp::pool_event_kind_name(cntp::PoolEventKind::Returned) == "returned");
    static_assert(cntp::pool_event_kind_name(cntp::PoolEventKind::EvictedUnhealthy) == "evicted_unhealthy");
    assert(cntp::transport_class_name(static_cast<cntp::TransportClass>(255)) == "<unknown TransportClass>");

    auto size = cntp::admit_pool_size(4);
    auto zero_size = cntp::admit_pool_size(0);
    auto idle = cntp::admit_idle_timeout_ns(1000);
    auto zero_idle = cntp::admit_idle_timeout_ns(0);
    auto id = cntp::admit_connection_id(77);
    auto zero_id = cntp::admit_connection_id(0);

    assert(size.is_ok());
    assert(zero_size.is_err());
    assert(zero_size.err() == ::fixy::some(cntp::PoolError::InvalidPoolSize));
    assert(idle.is_ok());
    assert(zero_idle.is_err());
    assert(zero_idle.err() == ::fixy::some(cntp::PoolError::InvalidIdleTimeout));
    assert(id.is_ok());
    assert(zero_id.is_err());
    assert(zero_id.err() == ::fixy::some(cntp::PoolError::InvalidConnectionId));
    auto const admitted_id = std::move(id).expect("77 is positive");

    cog::CogIdentity empty{};
    auto socket = cntp::admit_socket_fd(100).expect("100 is not negative");
    auto bad = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(socket, empty, admitted_id);
    assert(error_of(std::move(bad)) == cntp::PoolError::InvalidRemoteCog);

    auto good = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(socket, remote(9), admitted_id);
    MtlsConnection made = take(std::move(good));
    assert(made.peek().socket().value() == 100);
    assert(made.peek().remote_uuid() == remote(9).uuid);
    assert(made.peek().connection_id().value() == 77);
    drop(std::move(made));

    crucible::test::pass("  test_admission_and_names:       PASSED\n");
}

void test_lease_return_and_capacity() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(1);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init);

    assert(pool.add_connection(bg, connection(10, id, 1), 100).is_ok());
    assert(pool.add_connection(bg, connection(11, id, 2), 100).is_ok());
    assert(error_of(pool.add_connection(bg, connection(12, id, 3), 100)) == cntp::PoolError::PoolFull);
    assert(pool.available_count(bg, id) == 2);

    {
        auto lease = take(pool.lease(bg, id, 200));
        assert(static_cast<bool>(lease));
        assert((*lease).remote_uuid() == id.uuid);
        assert(lease->socket().value() == 10);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(*lease)>>,
                      "a lease reads its connection and cannot rewrite it");
        assert(pool.available_count(bg, id) == 1);
    }
    assert(pool.available_count(bg, id) == 2);

    assert(pool.event_count(bg) >= 4);
    assert(take(pool.event_at(bg, 0)).value().kind == cntp::PoolEventKind::Added);
    assert(take(pool.event_at(bg, 2)).value().kind == cntp::PoolEventKind::Leased);
    assert(take(pool.event_at(bg, 3)).value().kind == cntp::PoolEventKind::Returned);

    // An explicit return records the same event as a scope exit.
    auto lease = take(pool.lease(bg, id, 300));
    assert(pool.available_count(bg, id) == 1);
    pool.return_lease(bg, std::move(lease));
    assert(pool.available_count(bg, id) == 2);
    assert(take(pool.event_at(bg, pool.event_count(bg) - 1)).value().kind == cntp::PoolEventKind::Returned);

    // A move assignment gives the slot of the target back, then takes over
    // the slot of the source, so one slot is free after it.
    auto held = take(pool.lease(bg, id, 400));
    auto taken = take(pool.lease(bg, id, 401));
    assert(pool.available_count(bg, id) == 0);
    held = std::move(taken);
    assert(static_cast<bool>(held));
    assert(pool.available_count(bg, id) == 1);
    held.reset();
    assert(pool.available_count(bg, id) == 2);

    crucible::test::pass("  test_lease_return_and_capacity: PASSED\n");
}

void test_unhealthy_idle_and_quarantine_eviction() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(2);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init, config(2, 50));

    auto fd20 = cntp::admit_socket_fd(20).expect("20 is not negative");
    assert(pool.add_connection(bg, connection(20, id, 20), 10).is_ok());
    assert(pool.add_connection(bg, connection(21, id, 21), 10).is_ok());
    pool.mark_unhealthy(bg, id, fd20);
    pool.evict_unhealthy(bg, id);
    assert(pool.available_count(bg, id) == 1);
    pool.evict_idle(bg, id, 70);
    assert(pool.available_count(bg, id) == 0);

    assert(pool.add_connection(bg, connection(22, id, 22), 100).is_ok());
    assert(pool.add_connection(bg, connection(23, id, 23), 100).is_ok());
    auto lease = take(pool.lease(bg, id, 110));
    assert(pool.available_count(bg, id) == 1);
    pool.drain_quarantined(bg, id);
    assert(pool.available_count(bg, id) == 0);
    lease.reset();
    assert(error_of(pool.lease(bg, id, 120)) == cntp::PoolError::PoolEmpty);

    crucible::test::pass("  test_unhealthy_idle_and_quarantine_eviction: PASSED\n");
}

void test_quarantined_remote_refuses_a_lease() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(4);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init);

    assert(pool.add_connection(bg, connection(90, id, 90), 0).is_ok());
    assert(pool.add_connection(bg, connection(91, id, 91), 0).is_ok());
    auto held = take(pool.lease(bg, id, 0));
    pool.drain_quarantined(bg, id);

    // The leased slot stays, quarantined, so the remote is still counted and
    // a new lease for it is refused by name.
    assert(pool.distinct_remote_count(bg) == 1u);
    assert(error_of(pool.lease(bg, id, 1)) == cntp::PoolError::RemoteQuarantined);

    held.reset();
    assert(pool.distinct_remote_count(bg) == 0u);

    crucible::test::pass("  test_quarantined_remote_refuses_a_lease: PASSED\n");
}

void test_configured_per_remote_limit() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(3);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init, config(1, 1000));

    assert(pool.add_connection(bg, connection(30, id, 30), 0).is_ok());
    assert(error_of(pool.add_connection(bg, connection(31, id, 31), 0)) == cntp::PoolError::PoolFull);

    crucible::test::pass("  test_configured_per_remote_limit: PASSED\n");
}

void test_event_ring_wrap_chronological_order() {
    // Indexing is chronological, not by physical slot.  Only a ring
    // that has wrapped can tell the two apart: until then the oldest
    // event happens to sit at slot zero.  Once it wraps, the oldest
    // survivor sits at the slot about to be overwritten and slot zero
    // holds a much newer event.
    //
    // Two remotes with two connections each give room for eight
    // events.  Two additions plus four lease-and-return cycles produce
    // ten, so the first two fall off and the survivors are numbered
    // three through ten.
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(10);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init);

    assert(pool.add_connection(bg, connection(40, id, 1), 100).is_ok());
    assert(pool.add_connection(bg, connection(41, id, 2), 100).is_ok());
    for (int i = 0; i < 4; ++i) {
        auto lease = take(pool.lease(bg, id, std::uint64_t{200} + std::uint64_t(i)));
        assert(static_cast<bool>(lease));
        // The lease returns itself at the end of this scope, and that
        // is what records the return event.
    }

    assert(pool.event_count(bg) == 8u);

    assert(take(pool.event_at(bg, 0)).value().sequence == 3u);
    assert(take(pool.event_at(bg, 7)).value().sequence == 10u);

    std::uint64_t prev = 0;
    for (std::size_t k = 0; k < pool.event_count(bg); ++k) {
        std::uint64_t const sequence = take(pool.event_at(bg, k)).value().sequence;
        assert(sequence > prev);
        prev = sequence;
    }
    assert(prev == 10u);

    assert(error_of(pool.event_at(bg, 8)) == cntp::PoolError::InvalidConnectionId);

    crucible::test::pass("  test_event_ring_wrap_chronological_order: PASSED\n");
}

void test_distinct_remote_counter_parity() {
    // The distinct-remote count is a cached counter rather than a
    // scan, so every path that can change it has to maintain it.  This
    // walks all of those paths and checks the count after each one.
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto first = remote(20);
    auto second = remote(21);
    auto third = remote(22);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 3, 2>(init, config(2, 50));

    assert(pool.distinct_remote_count(bg) == 0u);

    // Only the first connection to a remote raises the count.
    assert(pool.add_connection(bg, connection(50, first, 1), 0).is_ok());
    assert(pool.distinct_remote_count(bg) == 1u);

    assert(pool.add_connection(bg, connection(51, first, 2), 0).is_ok());
    assert(pool.distinct_remote_count(bg) == 1u);

    assert(pool.add_connection(bg, connection(60, second, 3), 0).is_ok());
    assert(pool.distinct_remote_count(bg) == 2u);

    assert(pool.add_connection(bg, connection(70, third, 4), 0).is_ok());
    assert(pool.distinct_remote_count(bg) == 3u);

    // The admission gate reads that same counter, so a fourth remote
    // is refused once three are held.
    auto fourth = remote(23);
    assert(error_of(pool.add_connection(bg, connection(80, fourth, 5), 0)) == cntp::PoolError::PoolFull);
    assert(pool.distinct_remote_count(bg) == 3u);

    // Evicting one unhealthy connection leaves the remote with its
    // other one, so the count does not move.
    auto fd50 = cntp::admit_socket_fd(50).expect("50 is not negative");
    pool.mark_unhealthy(bg, first, fd50);
    pool.evict_unhealthy(bg, first);
    assert(pool.distinct_remote_count(bg) == 3u);

    // Evicting the last connection to a remote does move it.
    pool.evict_idle(bg, first, /*now_ns=*/100);
    assert(pool.distinct_remote_count(bg) == 2u);

    // Draining a remote that holds no lease drops it outright.
    pool.drain_quarantined(bg, second);
    assert(pool.distinct_remote_count(bg) == 1u);

    assert(pool.add_connection(bg, connection(80, fourth, 5), 0).is_ok());
    assert(pool.distinct_remote_count(bg) == 2u);

    // A connection marked unhealthy while leased is drained when the
    // lease comes back, and it was the remote's only one.
    auto lease = take(pool.lease(bg, third, 0));
    auto fd70 = cntp::admit_socket_fd(70).expect("70 is not negative");
    pool.mark_unhealthy(bg, third, fd70);
    lease.reset();
    assert(pool.distinct_remote_count(bg) == 1u);

    crucible::test::pass("  test_distinct_remote_counter_parity: PASSED\n");
}

// Two background threads take and return leases on one pool at once.  The
// gate serializes them, so every lease finds a free slot or an honest
// refusal, and the count of free connections is whole at the end.
void test_concurrent_leases_keep_the_count() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(30);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 1, 4>(init);
    for (int fd = 0; fd < 4; ++fd) {
        assert(pool.add_connection(bg, connection(200 + fd, id, static_cast<std::uint64_t>(fd + 1)), 0).is_ok());
    }

    std::atomic<int> granted{0};
    auto worker = [&]() noexcept {
        for (int round = 0; round < 2000; ++round) {
            auto lease = pool.lease(bg, id, static_cast<std::uint64_t>(round));
            if (lease.is_ok()) {
                granted.fetch_add(1, std::memory_order_relaxed);
            } else {
                assert(error_of(std::move(lease)) == cntp::PoolError::PoolEmpty);
            }
        }
    };
    {
        std::jthread left{worker};
        std::jthread right{worker};
    }
    assert(granted.load() > 0);
    assert(pool.available_count(bg, id) == 4);
    assert(pool.distinct_remote_count(bg) == 1u);

    crucible::test::pass("  test_concurrent_leases_keep_the_count: PASSED\n");
}

void test_gate_cache_line_isolation() {
    // The gate and the counters beside it must occupy separate cache
    // lines, so that a waiter on the gate does not share a line with
    // writes to the counters.  Alignment on the type is what buys that
    // separation.
    using PoolT = cntp::ConnectionPool<cntp::TransportClass::MtlsTcp, 2, 2>;
    static_assert(alignof(PoolT) >= 64u, "The pool must be aligned to a cache line so that the "
                                         "gate does not share one with the counters.");
    crucible::test::pass("  test_gate_cache_line_isolation: PASSED\n");
}

}  // namespace

int main() {
    static_assert(cntp::PoolTransportClass<cntp::TransportClass::MtlsTcp>);
    static_assert(!cntp::PoolTransportClass<static_cast<cntp::TransportClass>(255)>);
    // No transport class has the value 3, so the pool refuses it.
    static_assert(!cntp::PoolTransportClass<static_cast<cntp::TransportClass>(3)>);
    static_assert(sizeof(cntp::PositivePoolSize) == sizeof(std::uint16_t));
    static_assert(sizeof(cntp::PositiveIdleTimeoutNs) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredPoolEvent) == sizeof(cntp::PoolEvent));
    static_assert(std::same_as<cntp::DeclaredPoolEvent::tag_type, ::fixy::tags::source::ConnectionPool>);
    static_assert(!std::copy_constructible<cntp::LinearConnection<cntp::TransportClass::MtlsTcp>>);
    static_assert(!std::is_default_constructible_v<cntp::Connection<cntp::TransportClass::MtlsTcp>>);
    static_assert(!std::is_default_constructible_v<cntp::ConnectionPool<cntp::TransportClass::MtlsTcp, 2, 2>>);
    static_assert(cntp::CtxFitsConnectionPoolMint<::fixy::ColdInitCtx>);
    static_assert(!cntp::CtxFitsConnectionPoolMint<::fixy::BgDrainCtx>);
    static_assert(cntp::CtxFitsConnectionPoolRuntime<::fixy::BgLoadCtx>);
    static_assert(cntp::CtxFitsConnectionPoolRuntime<::fixy::TestRunnerCtx>);
    static_assert(!cntp::CtxFitsConnectionPoolRuntime<::fixy::BgDrainCtx>);
    static_assert(!cntp::CtxFitsConnectionPoolRuntime<::fixy::HotFgCtx>);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_connection_pool:\n");
    test_admission_and_names();
    test_lease_return_and_capacity();
    test_unhealthy_idle_and_quarantine_eviction();
    test_quarantined_remote_refuses_a_lease();
    test_configured_per_remote_limit();
    test_event_ring_wrap_chronological_order();
    test_distinct_remote_counter_parity();
    test_concurrent_leases_keep_the_count();
    test_gate_cache_line_isolation();
    crucible::test::pass("test_cntp_connection_pool: all PASSED\n");
    return 0;
}
