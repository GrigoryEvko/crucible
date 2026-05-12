#include <crucible/cntp/ConnectionPool.h>
#include <crucible/rt/ConnectionPool.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace rt = crucible::rt;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] cog::CogIdentity remote(std::uint64_t lo) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0xC0A5ull, lo};
    id.kind = cog::CogKind::NicPort;
    return id;
}

[[nodiscard]] cntp::LinearConnection<cntp::TransportClass::MtlsTcp>
connection(int fd, cog::CogIdentity const& id, std::uint64_t connection_id) {
    auto socket = cntp::admit_socket_fd(fd);
    auto cid = cntp::admit_connection_id(connection_id);
    assert(socket.has_value());
    assert(cid.has_value());
    auto conn = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(
        *socket, id, *cid);
    assert(conn.has_value());
    return std::move(*conn);
}

void test_admission_and_names() {
    assert(cntp::transport_class_name(cntp::TransportClass::RdmaRcQp) ==
           std::string_view{"RdmaRcQp"});
    assert(cntp::pool_error_name(cntp::PoolError::PoolFull) ==
           std::string_view{"PoolFull"});
    assert(cntp::pool_event_kind_name(cntp::PoolEventKind::Returned) ==
           std::string_view{"returned"});

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
    auto bad = cntp::mint_connection<cntp::TransportClass::MtlsTcp>(
        socket, empty, *id);
    assert(!bad.has_value());
    assert(bad.error() == cntp::PoolError::InvalidRemoteCog);

    std::printf("  test_admission_and_names:       PASSED\n");
}

void test_lease_return_and_capacity() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto id = remote(1);
    auto pool = rt::mint_connection_pool<
        cntp::TransportClass::MtlsTcp, 2, 2>(init);

    assert(pool.add_connection(bg, connection(10, id, 1), 100).has_value());
    assert(pool.add_connection(bg, connection(11, id, 2), 100).has_value());
    auto full = pool.add_connection(bg, connection(12, id, 3), 100);
    assert(!full.has_value());
    assert(full.error() == cntp::PoolError::PoolFull);
    assert(pool.available_count(id) == 2);

    {
        auto lease = pool.lease(bg, id, 200);
        assert(lease.has_value());
        assert(static_cast<bool>(*lease));
        assert((**lease).remote_uuid == id.uuid);
        assert(pool.available_count(id) == 1);
    }
    assert(pool.available_count(id) == 2);

    assert(pool.event_count() >= 4);
    auto event0 = pool.event_at(0);
    auto event2 = pool.event_at(2);
    auto event3 = pool.event_at(3);
    assert(event0.has_value());
    assert(event2.has_value());
    assert(event3.has_value());
    assert(event0->value().kind == cntp::PoolEventKind::Added);
    assert(event2->value().kind == cntp::PoolEventKind::Leased);
    assert(event3->value().kind == cntp::PoolEventKind::Returned);

    std::printf("  test_lease_return_and_capacity: PASSED\n");
}

void test_unhealthy_idle_and_quarantine_eviction() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto id = remote(2);
    auto pool = rt::mint_connection_pool<
        cntp::TransportClass::MtlsTcp, 2, 2>(init, cntp::PoolConfig{
            .max_per_remote = cntp::PositivePoolSize{
                std::uint16_t{2}, typename cntp::PositivePoolSize::Trusted{}},
            .max_idle_ns = cntp::PositiveIdleTimeoutNs{
                std::uint64_t{50}, typename cntp::PositiveIdleTimeoutNs::Trusted{}},
            .probe_health = true,
        });

    auto fd20 = cntp::admit_socket_fd(20).value();
    assert(pool.add_connection(bg, connection(20, id, 20), 10).has_value());
    assert(pool.add_connection(bg, connection(21, id, 21), 10).has_value());
    pool.mark_unhealthy(bg, id, fd20);
    pool.evict_unhealthy(bg, id);
    assert(pool.available_count(id) == 1);
    pool.evict_idle(bg, id, 70);
    assert(pool.available_count(id) == 0);

    assert(pool.add_connection(bg, connection(22, id, 22), 100).has_value());
    assert(pool.add_connection(bg, connection(23, id, 23), 100).has_value());
    auto lease = pool.lease(bg, id, 110);
    assert(lease.has_value());
    assert(pool.available_count(id) == 1);
    pool.drain_quarantined(bg, id);
    assert(pool.available_count(id) == 0);
    lease->reset();
    auto blocked = pool.lease(bg, id, 120);
    assert(!blocked.has_value());
    assert(blocked.error() == cntp::PoolError::PoolEmpty);

    std::printf("  test_unhealthy_idle_and_quarantine_eviction: PASSED\n");
}

void test_configured_per_remote_limit() {
    effects::ColdInitCtx init{};
    effects::BgDrainCtx bg{};
    auto id = remote(3);
    auto pool = rt::mint_connection_pool<
        cntp::TransportClass::MtlsTcp, 2, 2>(init, cntp::PoolConfig{
            .max_per_remote = cntp::PositivePoolSize{
                std::uint16_t{1}, typename cntp::PositivePoolSize::Trusted{}},
            .max_idle_ns = cntp::PositiveIdleTimeoutNs{
                std::uint64_t{1000},
                typename cntp::PositiveIdleTimeoutNs::Trusted{}},
            .probe_health = false,
        });

    assert(pool.add_connection(bg, connection(30, id, 30), 0).has_value());
    auto full = pool.add_connection(bg, connection(31, id, 31), 0);
    assert(!full.has_value());
    assert(full.error() == cntp::PoolError::PoolFull);

    std::printf("  test_configured_per_remote_limit: PASSED\n");
}

}  // namespace

int main() {
    static_assert(cntp::PoolTransportClass<cntp::TransportClass::MtlsTcp>);
    static_assert(!cntp::PoolTransportClass<
                  static_cast<cntp::TransportClass>(255)>);
    static_assert(sizeof(cntp::PositivePoolSize) == sizeof(std::uint16_t));
    static_assert(sizeof(cntp::PositiveIdleTimeoutNs) == sizeof(std::uint64_t));
    static_assert(sizeof(cntp::DeclaredPoolEvent) == sizeof(cntp::PoolEvent));
    static_assert(std::same_as<
                  cntp::DeclaredPoolEvent::tag_type,
                  saf::source::ConnectionPool>);
    static_assert(!std::copy_constructible<
                  cntp::LinearConnection<cntp::TransportClass::MtlsTcp>>);
    static_assert(rt::CtxFitsConnectionPoolMint<effects::ColdInitCtx>);
    static_assert(!rt::CtxFitsConnectionPoolMint<effects::BgDrainCtx>);
    static_assert(rt::CtxFitsConnectionPoolRuntime<effects::BgDrainCtx>);
    static_assert(!rt::CtxFitsConnectionPoolRuntime<effects::HotFgCtx>);

    std::printf("test_cntp_connection_pool:\n");
    test_admission_and_names();
    test_lease_return_and_capacity();
    test_unhealthy_idle_and_quarantine_eviction();
    test_configured_per_remote_limit();
    std::printf("test_cntp_connection_pool: all PASSED\n");
    return 0;
}
