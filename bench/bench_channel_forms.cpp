// Microbench of the push and the pop of the three forms of
// fixy/concurrent/PermissionedChannel.h on one thread: the wire, the merge and
// the lanes.  The last row is the parked form of the lanes
// (fixy/concurrent/ParkedChannel.h).
//
// Each row gives the latency of a push and the pop of the same item through
// the handles of one channel, which one thread holds in its own cache, with
// the sink of bench::do_not_optimize in each sample.  The multi-thread rows
// of bench_permissioned_channel.cpp and the tests of each form run the same
// calls on several threads.
//
// Each call is a function of its own, aligned to 64 bytes and never inlined,
// so the code of one row does not move against the code of another row, and
// the instructions of each call stand apart in the binary.  The codegen rows
// inline a push and a pop in one loop of the caller.  Each line gives the
// percentiles of one row in picoseconds, and the coefficient of variation in
// thousandths.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include <cstdint>
#include <utility>

#include "bench_harness.h"

namespace {

namespace cc = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

using Item = std::uint64_t;
constexpr std::size_t kCapacity = 1024;
constexpr cc::LaneCount kLanes{2};
constexpr cc::LaneIndex<kLanes> kFirstLane{::fixy::index<0>()};
constexpr cc::LaneIndex<kLanes> kSecondLane{::fixy::index<1>()};

struct WireBenchTag {};
struct MergeBenchTag {};
struct LanesBenchTag {};
struct ParkedLanesBenchTag {};

// A time in nanoseconds, as the nearest count of picoseconds.
[[nodiscard]] std::uint64_t picoseconds(double nanoseconds) noexcept {
    return static_cast<std::uint64_t>(nanoseconds * 1000.0 + 0.5);
}

// Writes one line: the name of the row and the percentiles of its samples.
void print_row(::fixy::TextView name, bench::Report const& measured) noexcept {
    bench::print("{}  p50={} ps  p99={} ps  p99.9={} ps  max={} ps  cv={} permille\n", name,
                 picoseconds(measured.pct.p50), picoseconds(measured.pct.p99), picoseconds(measured.pct.p99_9),
                 picoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// The item of a pop, or zero for no item.
[[nodiscard]] Item item_or_zero(::fixy::Option<Item> popped) noexcept {
    return std::move(popped).match([](Item item) noexcept { return item; }, []() noexcept { return Item{0}; });
}

// The item of a receive, or zero for no item.
[[nodiscard]] Item item_or_zero(::fixy::Result<Item, cc::NoItem> received) noexcept {
    return std::move(received).match([](Item item) noexcept { return item; },
                                     [](cc::NoItem /*why*/) noexcept { return Item{0}; });
}

template <class Producer>
[[gnu::noinline, gnu::aligned(64)]] bool push_once(Producer& producer, Item item) noexcept {
    return producer.try_push(item);
}

template <class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item pop_wire_once(Consumer& consumer) noexcept {
    return item_or_zero(consumer.try_pop());
}

template <class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item pop_merge_once(Consumer& consumer,
                                                        ::foundation::effects::cap::Nondet merge) noexcept {
    return item_or_zero(consumer.try_pop(merge));
}

template <class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item receive_lanes_once(Consumer& consumer) noexcept {
    return item_or_zero(consumer.try_receive());
}

template <class Producer>
[[gnu::noinline, gnu::aligned(64)]] bool push_parked_once(Producer& producer, ::fixy::BgLoadCtx const& ctx,
                                                          Item item) noexcept {
    return producer.try_push(ctx, item).is_ok();
}

template <class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item receive_parked_once(Consumer& consumer,
                                                             ::fixy::BgLoadCtx const& ctx) noexcept {
    return item_or_zero(consumer.try_receive(ctx));
}

// The codegen rows: a push and a pop inlined in one loop of the caller.
template <class Producer, class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item run_wire_rounds(Producer& producer, Consumer& consumer, Item rounds) noexcept {
    Item sum = 0;
    for (Item round = 0; round < rounds; ++round) {
        sum += producer.try_push(round) ? Item{1} : Item{0};
        sum += item_or_zero(consumer.try_pop());
    }
    return sum;
}

template <class Producer, class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item run_merge_rounds(Producer& producer, Consumer& consumer,
                                                          ::foundation::effects::cap::Nondet merge,
                                                          Item rounds) noexcept {
    Item sum = 0;
    for (Item round = 0; round < rounds; ++round) {
        sum += producer.try_push(round) ? Item{1} : Item{0};
        sum += item_or_zero(consumer.try_pop(merge));
    }
    return sum;
}

template <class First, class Second, class Consumer>
[[gnu::noinline, gnu::aligned(64)]] Item run_lanes_rounds(First& first, Second& second, Consumer& consumer,
                                                          Item rounds) noexcept {
    Item sum = 0;
    for (Item round = 0; round < rounds; ++round) {
        sum += first.try_push(round) ? Item{1} : Item{0};
        sum += second.try_push(round) ? Item{1} : Item{0};
        sum += item_or_zero(consumer.try_receive());
        sum += item_or_zero(consumer.try_receive());
    }
    return sum;
}

void report_wire() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, WireBenchTag>>();
    using Wire = cc::channel_t<Item, kCapacity, decltype(root)>;
    Wire channel{};
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Wire::producer_tag, Wire::consumer_tag>(std::move(root));
    // The frame of the two ends opens before the row, and the row runs in its body.
    cc::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                  [](auto producer, auto consumer) noexcept {
                      bench::Report const row = bench::run("wire push then pop", [producer_ref = ::fixy::to(producer),
                                                                                  consumer_ref = ::fixy::to(consumer)] {
                          bool const is_pushed = push_once(*producer_ref, 7);
                          bench::do_not_optimize(is_pushed);
                          Item const item = pop_wire_once(*consumer_ref);
                          bench::do_not_optimize(item);
                      });
                      print_row("wire push then pop", row);
                      bench::do_not_optimize(run_wire_rounds(producer, consumer, 64));
                      ::foundation::linear::drop(std::move(producer));
                      ::foundation::linear::drop(std::move(consumer));
                  });
}

void report_merge() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::merge, MergeBenchTag>>();
    using Merge = cc::channel_t<Item, kCapacity, decltype(root)>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Merge::producer_tag, Merge::consumer_tag>(std::move(root));
    Merge channel{std::move(producer_root)};
    ::foundation::effects::cap::Nondet const merge = ::foundation::effects::testing::test().nondet;
    // The producer frame and the consumer frame open before the row, and the
    // row runs in the inner body.
    auto lent = cc::with_producer(
        channel, merge, [&channel, merge, consumer_perm = std::move(consumer_perm)](auto producer) mutable noexcept {
            cc::with_consumer(channel, std::move(consumer_perm), [&producer, merge](auto consumer) noexcept {
                bench::Report const row =
                    bench::run("merge push then pop",
                               [producer_ref = ::fixy::to(producer), consumer_ref = ::fixy::to(consumer), merge] {
                                   bool const is_pushed = push_once(*producer_ref, 7);
                                   bench::do_not_optimize(is_pushed);
                                   Item const item = pop_merge_once(*consumer_ref, merge);
                                   bench::do_not_optimize(item);
                               });
                print_row("merge push then pop", row);
                bench::do_not_optimize(run_merge_rounds(producer, consumer, merge, 64));
                ::foundation::linear::drop(std::move(consumer));
            });
        });
    static_cast<void>(std::move(lent).expect("the pool lends a producer while its root is in the pool"));
}

void report_lanes() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::lanes, LanesBenchTag>>();
    using Lanes = cc::channel_t<Item, kCapacity, decltype(root), kLanes>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    // The lend of a lane reads the schedule, so it takes the tag of Nondet.
    ::foundation::effects::cap::Nondet const lend = ::foundation::effects::testing::test().nondet;
    // The frame of each lane and the consumer frame open before the row, and
    // the row runs in the innermost body.
    auto first_lent = cc::with_producer(
        channel, lend, kFirstLane,
        [&channel, lend, consumer_perm = std::move(consumer_perm)](auto first) mutable noexcept {
            auto second_lent = cc::with_producer(
                channel, lend, kSecondLane,
                [&channel, &first, consumer_perm = std::move(consumer_perm)](auto second) mutable noexcept {
                    cc::with_consumer(channel, std::move(consumer_perm), [&first, &second](auto consumer) noexcept {
                        bench::Report const row =
                            bench::run("lanes push into two lanes then two receives",
                                       [first_ref = ::fixy::to(first), second_ref = ::fixy::to(second),
                                        consumer_ref = ::fixy::to(consumer)] {
                                           bool const is_first_pushed = push_once(*first_ref, 7);
                                           bench::do_not_optimize(is_first_pushed);
                                           bool const is_second_pushed = push_once(*second_ref, 8);
                                           bench::do_not_optimize(is_second_pushed);
                                           Item const first_item = receive_lanes_once(*consumer_ref);
                                           bench::do_not_optimize(first_item);
                                           Item const second_item = receive_lanes_once(*consumer_ref);
                                           bench::do_not_optimize(second_item);
                                       });
                        print_row("lanes push into two lanes then two receives", row);
                        bench::do_not_optimize(run_lanes_rounds(first, second, consumer, 64));
                        ::foundation::linear::drop(std::move(consumer));
                    });
                });
            static_cast<void>(std::move(second_lent).expect("the pool lends the producer of the second lane"));
        });
    static_cast<void>(std::move(first_lent).expect("the pool lends the producer of the first lane"));
}

// The parked lanes beside the plain lanes: each push notifies the count of
// items, and a receive notifies the count of room of its lane after it
// published the tail of the lane.  No thread waits, so each notify is one fence
// and one load.
void report_parked_lanes() noexcept {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::lanes, ParkedLanesBenchTag>>();
    using Lanes = cc::ParkedChannel<cc::channel_t<Item, kCapacity, decltype(root), kLanes>>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Lanes::producer_tag, Lanes::consumer_tag>(std::move(root));
    Lanes channel{std::move(producer_root)};
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    ::foundation::effects::cap::Nondet const lend = ::foundation::effects::testing::test().nondet;
    auto measure = [&ctx](auto first, auto second, auto consumer) noexcept {
        bench::Report const row =
            bench::run("parked lanes push into two lanes then two receives", [&ctx, first, second, consumer] {
                bool const is_first_pushed = push_parked_once(*first, ctx, 7);
                bench::do_not_optimize(is_first_pushed);
                bool const is_second_pushed = push_parked_once(*second, ctx, 8);
                bench::do_not_optimize(is_second_pushed);
                Item const first_item = receive_parked_once(*consumer, ctx);
                bench::do_not_optimize(first_item);
                Item const second_item = receive_parked_once(*consumer, ctx);
                bench::do_not_optimize(second_item);
            });
        print_row("parked lanes push into two lanes then two receives", row);
    };
    auto first_lent = cc::with_producer(
        channel, lend, kFirstLane,
        [&channel, &ctx, &measure, lend, token = std::move(consumer_perm)](auto first) mutable noexcept {
            auto second_lent =
                cc::with_producer(channel, lend, kSecondLane,
                                  [&channel, &ctx, &measure, first_ref = ::fixy::to(first),
                                   token = std::move(token)](auto second) mutable noexcept {
                                      cc::with_consumer(channel, std::move(token),
                                                        [&ctx, &measure, first_ref,
                                                         second_ref = ::fixy::to(second)](auto consumer) noexcept {
                                                            measure(first_ref, second_ref, ::fixy::to(consumer));
                                                            std::move(consumer).close(ctx);
                                                        });
                                      std::move(second).close(ctx);
                                  });
            static_cast<void>(std::move(second_lent).expect("the pool lends the producer of the second lane"));
            std::move(first).close(ctx);
        });
    static_cast<void>(std::move(first_lent).expect("the pool lends the producer of the first lane"));
}

}  // namespace

int main() {
    report_wire();
    report_merge();
    report_lanes();
    report_parked_lanes();
    return 0;
}
