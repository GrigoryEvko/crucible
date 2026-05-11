#include <crucible/canopy/Hlc.h>
#include <crucible/permissions/Permission.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
struct HlcStreamTag {};
}  // namespace

int main() {
    using crucible::canopy::Hlc;
    using crucible::canopy::HlcClockTimestamp;
    using crucible::canopy::HlcCounterDelta;
    using crucible::canopy::HlcTimestamp;

    static_assert(sizeof(HlcTimestamp) == 16);
    static_assert(sizeof(HlcClockTimestamp) == sizeof(HlcTimestamp));
    static_assert(sizeof(HlcCounterDelta) == sizeof(std::uint32_t));
    static_assert(alignof(Hlc) == 64);
    static_assert(!std::is_copy_constructible_v<Hlc>);
    static_assert(!std::is_move_constructible_v<Hlc>);

    constexpr HlcTimestamp zero{};
    static_assert(Hlc::local_event(zero, 10) ==
                  HlcTimestamp{.physical_ns = 10, .counter = 0});
    static_assert(Hlc::local_event({.physical_ns = 10, .counter = 0}, 9) ==
                  HlcTimestamp{.physical_ns = 10, .counter = 1});
    static_assert(Hlc::recv_event(
                      {.physical_ns = 10, .counter = 3},
                      {.physical_ns = 10, .counter = 7},
                      9) == HlcTimestamp{.physical_ns = 10, .counter = 8});
    static_assert(Hlc::recv_event(
                      {.physical_ns = 10, .counter = 3},
                      {.physical_ns = 20, .counter = 7},
                      9) == HlcTimestamp{.physical_ns = 20, .counter = 8});
    static_assert(Hlc::recv_event(
                      {.physical_ns = 10, .counter = 3},
                      {.physical_ns = 20, .counter = 7},
                      30) == HlcTimestamp{.physical_ns = 30, .counter = 0});
    static_assert(Hlc::local_event(
                      {.physical_ns = 10,
                       .counter = UINT32_MAX},
                      9) == HlcTimestamp{.physical_ns = 11, .counter = 0});

    auto clock = crucible::canopy::mint_hlc(crucible::effects::Init{});
    const HlcTimestamp a = clock.now();
    const HlcTimestamp b = clock.on_send();
    const HlcTimestamp c = clock.now();
    assert(a < b);
    assert(b < c);
    assert(clock.peek() == c);

    clock.on_recv(HlcTimestamp{.physical_ns = c.physical_ns + 1, .counter = 9});
    const HlcTimestamp after_recv = clock.peek();
    const HlcTimestamp peer_floor{.physical_ns = c.physical_ns + 1,
                                  .counter = 9};
    assert(after_recv > c);
    assert(after_recv >= peer_floor);

    const HlcClockTimestamp tagged = clock.tagged_on_send();
    clock.on_recv(tagged);
    assert(clock.peek() > tagged.value());

    constexpr std::size_t per_thread = 256;
    std::array<HlcTimestamp, per_thread * 2> seen{};
    Hlc concurrent_clock;
    std::jthread t0([&] {
        for (std::size_t i = 0; i < per_thread; ++i) {
            seen[i] = concurrent_clock.on_send();
        }
    });
    std::jthread t1([&] {
        for (std::size_t i = 0; i < per_thread; ++i) {
            seen[per_thread + i] = concurrent_clock.on_send();
        }
    });
    t0.join();
    t1.join();
    std::vector<HlcTimestamp> sorted{seen.begin(), seen.end()};
    std::ranges::sort(sorted);
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        assert(sorted[i - 1] < sorted[i]);
    }

    using Channel = crucible::canopy::HlcTimestampChannel<16, HlcStreamTag>;
    Channel channel;
    auto whole =
        crucible::safety::mint_permission_root<typename Channel::whole_tag>();
    auto [producer_perm, consumer_perm] =
        crucible::safety::mint_permission_split<
            typename Channel::producer_tag,
            typename Channel::consumer_tag>(std::move(whole));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    assert(crucible::canopy::try_push_hlc_timestamp(producer, tagged));
    auto popped = crucible::canopy::try_pop_hlc_timestamp(consumer);
    assert(popped.has_value());
    assert(popped->value() == tagged.value());

    return 0;
}
