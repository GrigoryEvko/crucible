#include <crucible/cntp/ConnectionPool.h>
#include <crucible/cntp/ConnectionPoolRuntime.h>
#include <fixy/Ctx.h>

#include <atomic>
#include <cassert>
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
    auto socket = cntp::admit_socket_fd(fd);
    auto cid = cntp::admit_connection_id(connection_id);
    assert(socket.has_value());
    assert(cid.has_value());
    auto conn = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(*socket, id, *cid);
    assert(conn.has_value());
    return std::move(*conn);
}

[[nodiscard]] cntp::PoolConfig config(std::uint16_t per_remote, std::uint64_t idle_ns) {
    return cntp::PoolConfig{
        .max_per_remote = cntp::admit_pool_size(per_remote).value(),
        .max_idle_ns = cntp::admit_idle_timeout_ns(idle_ns).value(),
    };
}

void test_admission_and_names() {
    static_assert(cntp::transport_class_name(cntp::TransportClass::RdmaRcQp) == std::string_view{"RdmaRcQp"});
    static_assert(cntp::pool_error_name(cntp::PoolError::PoolFull) == std::string_view{"PoolFull"});
    static_assert(cntp::pool_event_kind_name(cntp::PoolEventKind::Returned) == std::string_view{"returned"});
    static_assert(cntp::pool_event_kind_name(cntp::PoolEventKind::EvictedUnhealthy)
                  == std::string_view{"evicted_unhealthy"});
    assert(cntp::transport_class_name(static_cast<cntp::TransportClass>(255))
           == std::string_view{"<unknown TransportClass>"});

    auto size = cntp::admit_pool_size(4);
    auto zero_size = cntp::admit_pool_size(0);
    auto idle = cntp::admit_idle_timeout_ns(1000);
    auto zero_idle = cntp::admit_idle_timeout_ns(0);
    auto id = cntp::admit_connection_id(77);
    auto zero_id = cntp::admit_connection_id(0);

    assert(size.has_value());
    assert(!zero_size.has_value());
    assert(zero_size.error() == cntp::PoolError::InvalidPoolSize);
    assert(idle.has_value());
    assert(!zero_idle.has_value());
    assert(zero_idle.error() == cntp::PoolError::InvalidIdleTimeout);
    assert(id.has_value());
    assert(!zero_id.has_value());
    assert(zero_id.error() == cntp::PoolError::InvalidConnectionId);

    cog::CogIdentity empty{};
    auto socket = cntp::admit_socket_fd(100).value();
    auto bad = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(socket, empty, *id);
    assert(!bad.has_value());
    assert(bad.error() == cntp::PoolError::InvalidRemoteCog);

    auto good = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(socket, remote(9), *id);
    assert(good.has_value());
    assert(good->peek().socket().value() == 100);
    assert(good->peek().remote_uuid() == remote(9).uuid);
    assert(good->peek().connection_id().value() == 77);
    drop(std::move(*good));

    std::printf("  test_admission_and_names:       PASSED\n");
}

void test_lease_return_and_capacity() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(1);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init);

    assert(pool.add_connection(bg, connection(10, id, 1), 100).has_value());
    assert(pool.add_connection(bg, connection(11, id, 2), 100).has_value());
    auto full = pool.add_connection(bg, connection(12, id, 3), 100);
    assert(!full.has_value());
    assert(full.error() == cntp::PoolError::PoolFull);
    assert(pool.available_count(bg, id) == 2);

    {
        auto lease = pool.lease(bg, id, 200);
        assert(lease.has_value());
        assert(static_cast<bool>(*lease));
        assert((**lease).remote_uuid() == id.uuid);
        assert((*lease)->socket().value() == 10);
        static_assert(std::is_const_v<std::remove_reference_t<decltype(**lease)>>,
                      "a lease reads its connection and cannot rewrite it");
        assert(pool.available_count(bg, id) == 1);
    }
    assert(pool.available_count(bg, id) == 2);

    assert(pool.event_count(bg) >= 4);
    auto event0 = pool.event_at(bg, 0);
    auto event2 = pool.event_at(bg, 2);
    auto event3 = pool.event_at(bg, 3);
    assert(event0.has_value());
    assert(event2.has_value());
    assert(event3.has_value());
    assert(event0->value().kind == cntp::PoolEventKind::Added);
    assert(event2->value().kind == cntp::PoolEventKind::Leased);
    assert(event3->value().kind == cntp::PoolEventKind::Returned);

    // An explicit return records the same event as a scope exit.
    auto lease = pool.lease(bg, id, 300);
    assert(lease.has_value());
    assert(pool.available_count(bg, id) == 1);
    pool.return_lease(bg, std::move(*lease));
    assert(pool.available_count(bg, id) == 2);
    auto last = pool.event_at(bg, pool.event_count(bg) - 1);
    assert(last.has_value());
    assert(last->value().kind == cntp::PoolEventKind::Returned);

    // A move assignment gives the slot of the target back, then takes over
    // the slot of the source, so one slot is free after it.
    auto held = pool.lease(bg, id, 400);
    auto taken = pool.lease(bg, id, 401);
    assert(held.has_value());
    assert(taken.has_value());
    assert(pool.available_count(bg, id) == 0);
    *held = std::move(*taken);
    assert(static_cast<bool>(*held));
    assert(pool.available_count(bg, id) == 1);
    held->reset();
    assert(pool.available_count(bg, id) == 2);

    std::printf("  test_lease_return_and_capacity: PASSED\n");
}

void test_unhealthy_idle_and_quarantine_eviction() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(2);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init, config(2, 50));

    auto fd20 = cntp::admit_socket_fd(20).value();
    assert(pool.add_connection(bg, connection(20, id, 20), 10).has_value());
    assert(pool.add_connection(bg, connection(21, id, 21), 10).has_value());
    pool.mark_unhealthy(bg, id, fd20);
    pool.evict_unhealthy(bg, id);
    assert(pool.available_count(bg, id) == 1);
    pool.evict_idle(bg, id, 70);
    assert(pool.available_count(bg, id) == 0);

    assert(pool.add_connection(bg, connection(22, id, 22), 100).has_value());
    assert(pool.add_connection(bg, connection(23, id, 23), 100).has_value());
    auto lease = pool.lease(bg, id, 110);
    assert(lease.has_value());
    assert(pool.available_count(bg, id) == 1);
    pool.drain_quarantined(bg, id);
    assert(pool.available_count(bg, id) == 0);
    lease->reset();
    auto blocked = pool.lease(bg, id, 120);
    assert(!blocked.has_value());
    assert(blocked.error() == cntp::PoolError::PoolEmpty);

    std::printf("  test_unhealthy_idle_and_quarantine_eviction: PASSED\n");
}

void test_quarantined_remote_refuses_a_lease() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(4);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init);

    assert(pool.add_connection(bg, connection(90, id, 90), 0).has_value());
    assert(pool.add_connection(bg, connection(91, id, 91), 0).has_value());
    auto held = pool.lease(bg, id, 0);
    assert(held.has_value());
    pool.drain_quarantined(bg, id);

    // The leased slot stays, quarantined, so the remote is still counted and
    // a new lease for it is refused by name.
    assert(pool.distinct_remote_count(bg) == 1u);
    auto refused = pool.lease(bg, id, 1);
    assert(!refused.has_value());
    assert(refused.error() == cntp::PoolError::RemoteQuarantined);

    held->reset();
    assert(pool.distinct_remote_count(bg) == 0u);

    std::printf("  test_quarantined_remote_refuses_a_lease: PASSED\n");
}

void test_configured_per_remote_limit() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg{fe::testing::bg()};
    auto id = remote(3);
    auto pool = cntp::mint_connection_pool<cntp::TransportClass::MtlsTcp, 2, 2>(init, config(1, 1000));

    assert(pool.add_connection(bg, connection(30, id, 30), 0).has_value());
    auto full = pool.add_connection(bg, connection(31, id, 31), 0);
    assert(!full.has_value());
    assert(full.error() == cntp::PoolError::PoolFull);

    std::printf("  test_configured_per_remote_limit: PASSED\n");
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

    assert(pool.add_connection(bg, connection(40, id, 1), 100).has_value());
    assert(pool.add_connection(bg, connection(41, id, 2), 100).has_value());
    for (int i = 0; i < 4; ++i) {
        auto lease = pool.lease(bg, id, std::uint64_t{200} + std::uint64_t(i));
        assert(lease.has_value());
        // The lease returns itself at the end of this scope, and that
        // is what records the return event.
    }

    assert(pool.event_count(bg) == 8u);

    auto oldest = pool.event_at(bg, 0);
    auto newest = pool.event_at(bg, 7);
    assert(oldest.has_value());
    assert(newest.has_value());
    assert(oldest->value().sequence == 3u);
    assert(newest->value().sequence == 10u);

    std::uint64_t prev = 0;
    for (std::size_t k = 0; k < pool.event_count(bg); ++k) {
        auto event = pool.event_at(bg, k);
        assert(event.has_value());
        assert(event->value().sequence > prev);
        prev = event->value().sequence;
    }
    assert(prev == 10u);

    auto out_of_range = pool.event_at(bg, 8);
    assert(!out_of_range.has_value());
    assert(out_of_range.error() == cntp::PoolError::InvalidConnectionId);

    std::printf("  test_event_ring_wrap_chronological_order: PASSED\n");
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
    assert(pool.add_connection(bg, connection(50, first, 1), 0).has_value());
    assert(pool.distinct_remote_count(bg) == 1u);

    assert(pool.add_connection(bg, connection(51, first, 2), 0).has_value());
    assert(pool.distinct_remote_count(bg) == 1u);

    assert(pool.add_connection(bg, connection(60, second, 3), 0).has_value());
    assert(pool.distinct_remote_count(bg) == 2u);

    assert(pool.add_connection(bg, connection(70, third, 4), 0).has_value());
    assert(pool.distinct_remote_count(bg) == 3u);

    // The admission gate reads that same counter, so a fourth remote
    // is refused once three are held.
    auto fourth = remote(23);
    auto overflow = pool.add_connection(bg, connection(80, fourth, 5), 0);
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::PoolError::PoolFull);
    assert(pool.distinct_remote_count(bg) == 3u);

    // Evicting one unhealthy connection leaves the remote with its
    // other one, so the count does not move.
    auto fd50 = cntp::admit_socket_fd(50).value();
    pool.mark_unhealthy(bg, first, fd50);
    pool.evict_unhealthy(bg, first);
    assert(pool.distinct_remote_count(bg) == 3u);

    // Evicting the last connection to a remote does move it.
    pool.evict_idle(bg, first, /*now_ns=*/100);
    assert(pool.distinct_remote_count(bg) == 2u);

    // Draining a remote that holds no lease drops it outright.
    pool.drain_quarantined(bg, second);
    assert(pool.distinct_remote_count(bg) == 1u);

    assert(pool.add_connection(bg, connection(80, fourth, 5), 0).has_value());
    assert(pool.distinct_remote_count(bg) == 2u);

    // A connection marked unhealthy while leased is drained when the
    // lease comes back, and it was the remote's only one.
    auto lease = pool.lease(bg, third, 0);
    assert(lease.has_value());
    auto fd70 = cntp::admit_socket_fd(70).value();
    pool.mark_unhealthy(bg, third, fd70);
    lease->reset();
    assert(pool.distinct_remote_count(bg) == 1u);

    std::printf("  test_distinct_remote_counter_parity: PASSED\n");
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
        assert(pool.add_connection(bg, connection(200 + fd, id, static_cast<std::uint64_t>(fd + 1)), 0).has_value());
    }

    std::atomic<int> granted{0};
    auto worker = [&]() noexcept {
        for (int round = 0; round < 2000; ++round) {
            auto lease = pool.lease(bg, id, static_cast<std::uint64_t>(round));
            if (lease.has_value()) {
                granted.fetch_add(1, std::memory_order_relaxed);
            } else {
                assert(lease.error() == cntp::PoolError::PoolEmpty);
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

    std::printf("  test_concurrent_leases_keep_the_count: PASSED\n");
}

void test_gate_cache_line_isolation() {
    // The gate and the counters beside it must occupy separate cache
    // lines, so that a waiter on the gate does not share a line with
    // writes to the counters.  Alignment on the type is what buys that
    // separation.
    using PoolT = cntp::ConnectionPool<cntp::TransportClass::MtlsTcp, 2, 2>;
    static_assert(alignof(PoolT) >= 64u, "The pool must be aligned to a cache line so that the "
                                         "gate does not share one with the counters.");
    std::printf("  test_gate_cache_line_isolation: PASSED\n");
}

}  // namespace

int main() {
    static_assert(cntp::PoolTransportClass<cntp::TransportClass::MtlsTcp>);
    static_assert(!cntp::PoolTransportClass<static_cast<cntp::TransportClass>(255)>);
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

    std::printf("test_cntp_connection_pool:\n");
    test_admission_and_names();
    test_lease_return_and_capacity();
    test_unhealthy_idle_and_quarantine_eviction();
    test_quarantined_remote_refuses_a_lease();
    test_configured_per_remote_limit();
    test_event_ring_wrap_chronological_order();
    test_distinct_remote_counter_parity();
    test_concurrent_leases_keep_the_count();
    test_gate_cache_line_isolation();
    std::printf("test_cntp_connection_pool: all PASSED\n");
    return 0;
}
