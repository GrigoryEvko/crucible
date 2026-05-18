#include <crucible/canopy/VectorClock.h>

#include <array>
#include <cassert>
#include <compare>
#include <cstdint>
#include <thread>
#include <type_traits>

namespace {
struct ReplayClockTag {};
}  // namespace

int main() {
    using Clock = crucible::canopy::VectorClock<4, ReplayClockTag>;
    using Snapshot = Clock::snapshot_type;
    using Node = Clock::node_index_type;

    static_assert(Clock::max_nodes == 4);
    static_assert(alignof(Clock) == 64);
    static_assert(!std::is_copy_constructible_v<Clock>);
    static_assert(!std::is_move_constructible_v<Clock>);
    static_assert(sizeof(Snapshot) == 4 * sizeof(std::uint64_t));

    constexpr Node n0{0};
    constexpr Node n1{1};
    constexpr Node n2{2};

    constexpr Snapshot origin{};
    static_assert(origin.at(n0) == 0);
    static_assert(!origin.positive_at(n0).has_value());

    constexpr Snapshot a{std::in_place, 1, 0, 0, 0};
    constexpr Snapshot b{std::in_place, 1, 1, 0, 0};
    constexpr Snapshot x{std::in_place, 2, 0, 0, 0};
    constexpr Snapshot y{std::in_place, 0, 1, 0, 0};

    static_assert(a.happens_before(b));
    static_assert(!b.happens_before(a));
    static_assert(x.concurrent_with(y));
    static_assert((x <=> y) == std::partial_ordering::unordered);
    static_assert(a.comparable_with(b));
    static_assert(a.positive_at(n0)->value() == 1);

    constexpr auto delta = b.sparse_delta();
    static_assert(delta.raw_count() == 2);
    static_assert(Snapshot::from_sparse_delta(delta) == b);

    auto duplicate_delta = Clock::delta_type{};
    assert(duplicate_delta.push(n0, 10));
    assert(duplicate_delta.push(n0, 1));
    assert(duplicate_delta.raw_count() == 1);
    assert(Snapshot::from_sparse_delta(duplicate_delta).at(n0) == 10);

    auto c0 = crucible::canopy::mint_vector_clock<4, ReplayClockTag>(
        crucible::effects::testing::init(),
        0);
    auto c1 = crucible::canopy::mint_vector_clock<4, ReplayClockTag>(
        crucible::effects::testing::init(),
        1);

    c0.on_local_event();
    c0.on_local_event();
    assert(c0.at(n0) == 2);
    assert(c0.positive_at(n0)->value() == 2);
    assert(!c0.positive_at(n1).has_value());

    const Snapshot sent_from_0 = c0.on_send();
    assert(sent_from_0.at(n0) == 3);
    c1.on_recv(sent_from_0);
    assert(c1.at(n0) == 3);
    assert(c1.at(n1) == 1);
    assert(sent_from_0.happens_before(c1.snapshot()));

    Clock carrier{n2};
    c1.on_send(carrier);
    assert(c1.snapshot() == carrier.snapshot());

    auto c2 = crucible::canopy::mint_vector_clock<4, ReplayClockTag>(
        crucible::effects::testing::init(),
        2);
    c2.apply_delta(c1.sparse_delta());
    assert(c2.at(n0) == c1.at(n0));
    assert(c2.at(n1) == c1.at(n1));
    assert(c2.at(n2) == 0);
    c2.on_recv(c1.sparse_delta());
    assert(c2.at(n2) == 1);

    auto left = crucible::canopy::mint_vector_clock<4, ReplayClockTag>(
        crucible::effects::testing::init(),
        0);
    auto right = crucible::canopy::mint_vector_clock<4, ReplayClockTag>(
        crucible::effects::testing::init(),
        1);
    left.on_local_event();
    right.on_local_event();
    assert(left.concurrent_with(right));
    assert(!left.comparable_with(right));

    auto shared = crucible::canopy::mint_vector_clock<4, ReplayClockTag>(
        crucible::effects::testing::init(),
        0);
    constexpr std::size_t per_thread = 256;
    std::jthread t0([&] {
        for (std::size_t i = 0; i < per_thread; ++i) {
            shared.on_local_event();
        }
    });
    std::jthread t1([&] {
        for (std::size_t i = 0; i < per_thread; ++i) {
            shared.on_local_event();
        }
    });
    t0.join();
    t1.join();
    assert(shared.at(n0) == per_thread * 2);

    return 0;
}
