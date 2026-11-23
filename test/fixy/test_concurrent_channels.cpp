// What fixy/concurrent/PermissionedChannel.h,
// fixy/concurrent/PermissionedChannel.h and
// fixy/concurrent/WorkingSet.h claim, checked.
//
// The two channels put the same ring behind two different ownership
// arguments.  SPSC splits one whole permission into two linear halves,
// so single-producer-single-consumer is a fact about the token types.
// MPSC keeps the consumer linear and makes the producer side a
// refcounted pool, because the ring is structurally many-producer and
// its membership is dynamic.  The refcount is the half that has runtime
// behaviour, so it is the half with the most runtime checks here.

#include <fixy/Core.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/Linear.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace c = fixy::concurrent;
namespace perm = foundation::permissions;

namespace {

// The tag of a snapshot of a channel.  The test context holds Nondet.
constexpr ::foundation::effects::cap::Nondet kSnapshot = ::foundation::effects::testing::test().nondet;

struct TraceTag {};
struct MetaTag {};
struct OtherTag {};
// A tag of its own for the threaded channel.
struct BulkTag {};

// One root site for each channel.  The brand of a root names its site,
// and a channel type names that brand, so each channel below is the
// channel of one site.
auto trace_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, TraceTag>>();
}
auto meta_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, MetaTag>>();
}
auto other_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::wire, OtherTag>>();
}
auto bulk_root() noexcept {
    return perm::mint_permission_root<c::channel_tag::Whole<c::ChannelForm::merge, BulkTag>>();
}

using Spsc = c::channel_t<int, 8, decltype(trace_root())>;
using Mpsc = c::channel_t<int, 8, decltype(meta_root())>;
// The lend of a producer gives the Option of the base: the result of the
// body, or Unit for a body that gives void.
static_assert(
    std::is_same_v<decltype(c::with_producer(std::declval<Mpsc&>(), std::declval<::foundation::effects::cap::Nondet>(),
                                             [](auto) noexcept { return 0; })),
                   ::foundation::core::Option<int>>);
static_assert(
    std::is_same_v<decltype(c::with_producer(std::declval<Mpsc&>(), std::declval<::foundation::effects::cap::Nondet>(),
                                             [](auto) noexcept {})),
                   ::foundation::core::Option<::foundation::core::Unit>>);
using OtherSpsc = c::channel_t<int, 8, decltype(other_root())>;

// ── Ownership is in the types ────────────────────────────────────────

// The ends of the roles.  The handles of a frame read the same in
// run_spsc_handles.
static_assert(!std::is_copy_constructible_v<Spsc::ProducerEnd>);
static_assert(!std::is_copy_assignable_v<Spsc::ProducerEnd>);
static_assert(std::is_move_constructible_v<Spsc::ProducerEnd>);
// A handle binds to one channel for life.  Rebinding would orphan the
// original permission and let a second producer coexist with it.
static_assert(!std::is_move_assignable_v<Spsc::ProducerEnd>);
static_assert(!std::is_move_assignable_v<Spsc::ConsumerEnd>);
static_assert(!std::is_copy_constructible_v<Mpsc::ProducerEnd>);
static_assert(std::is_move_constructible_v<Mpsc::ProducerEnd>);
static_assert(!std::is_move_assignable_v<Mpsc::ConsumerEnd>);

// The channel's identity is its address: the ring's atomics depend on a
// stable one.
static_assert(!std::is_move_constructible_v<Spsc>);
static_assert(!std::is_move_constructible_v<Mpsc>);

// Each channel's tags are distinct types, so two channels cannot trade
// endpoints even when their value type and capacity agree.
static_assert(!std::is_same_v<Spsc::producer_tag, OtherSpsc::producer_tag>);
static_assert(!std::is_same_v<Spsc::producer_tag, Mpsc::producer_tag>);

// The split relation admits exactly the channel's own triple, in that
// order, and both the binary and the variadic form carry an authoring
// witness beside them so a forged half is caught.
static_assert(perm::can_split_into_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::has_split_authoring_witness_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::can_split_into_pack_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::has_split_pack_authoring_witness_v<Spsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);
static_assert(perm::can_split_into_v<Mpsc::whole_tag, Mpsc::producer_tag, Mpsc::consumer_tag>);

// Nothing else splits.  Mixing two channels' tags, or reversing the
// halves, is not a relation edge.
static_assert(!perm::can_split_into_v<Spsc::whole_tag, Spsc::consumer_tag, Spsc::producer_tag>);
static_assert(!perm::can_split_into_v<Spsc::whole_tag, OtherSpsc::producer_tag, Spsc::consumer_tag>);
static_assert(!perm::can_split_into_v<Spsc::whole_tag, Mpsc::producer_tag, Mpsc::consumer_tag>);
static_assert(!perm::can_split_into_v<OtherSpsc::whole_tag, Spsc::producer_tag, Spsc::consumer_tag>);

// ── The declared footprint ───────────────────────────────────────────

static_assert(c::hot_path_cache_line_bytes == 64);
static_assert(c::conservative_l1d_per_core < c::conservative_l2_per_core);
static_assert(c::conservative_l2_per_core < c::conservative_l3_total);

static_assert(c::cell_line_footprint(0) == 0);
static_assert(c::cell_line_footprint(1) == 64);
static_assert(c::cell_line_footprint(64) == 64);
static_assert(c::cell_line_footprint(65) == 128);

// Two control lines for the SPSC ring, its head and the line of the copies
// of the producer, and one for the head of the MPSC ring, plus the cell,
// which holds the turn word of the MPSC ring.
static_assert(Spsc::ProducerEnd::per_call_working_set == 2 * 64 + 64);
static_assert(Mpsc::ProducerEnd::per_call_working_set == 1 * 64 + 64);

// Unknown is the maximum, so a type that declares nothing reads as too
// big to parallelize blindly rather than as free.
struct DeclaresNothing {};
static_assert(c::HasStaticPerCallWorkingSet<Spsc::ProducerEnd>);
static_assert(!c::HasStaticPerCallWorkingSet<DeclaresNothing>);
static_assert(c::per_call_working_set_of_v<DeclaresNothing> == c::unknown_per_call_working_set);
static_assert(c::per_call_working_set_of_v<Spsc::ProducerEnd> == Spsc::ProducerEnd::per_call_working_set);

// The saturating add is what keeps a composed footprint from wrapping
// into a small number and reading as cache-resident.
static_assert(c::saturating_ws_add(64, 64) == 128);
static_assert(c::saturating_ws_add(c::unknown_per_call_working_set, 64) == c::unknown_per_call_working_set);
static_assert(c::saturating_ws_add(c::unknown_per_call_working_set - 1, 2) == c::unknown_per_call_working_set);

// ── Runtime: the SPSC split ──────────────────────────────────────────

// The channel reads its head and then its tail.  A reader on a third
// thread can see the producer push and the consumer pop past that head
// between the two loads, so the tail that it loads can be newer than its
// head.  The size clamps, so the reader never sees more than the capacity.
template <class Channel, class Producer, class Consumer>
[[nodiscard]] int size_on_a_third_thread(Channel const& channel, Producer&& producer, Consumer&& consumer) {
    constexpr int kReads = 1 << 20;
    fixy::Atomic<int> is_done{0};
    std::size_t largest = 0;

    {
        std::jthread pushes([handle = std::forward<Producer>(producer), &is_done]() mutable {
            while (is_done.load_acquire() == 0)
                static_cast<void>(handle.try_push(1));
        });
        std::jthread pops([handle = std::forward<Consumer>(consumer), &is_done]() mutable {
            while (is_done.load_acquire() == 0)
                static_cast<void>(handle.try_pop());
        });
        for (int read = 0; read < kReads; ++read) {
            const std::size_t size = channel.size_approx(kSnapshot);
            largest = size > largest ? size : largest;
        }
        is_done.store_release(1);
    }

    if (largest > Channel::capacity()) {
        fixy::report(fixy::Sink::Err, "a third thread read the size {} of a channel of {} cells\n", largest,
                     Channel::capacity());
        return 1;
    }
    return 0;
}

// The pushes and pops of the two handles of the SPSC frame, and then the
// read of the size on a third thread.
template <class Producer, class Consumer>
[[nodiscard]] int run_spsc_handles(Spsc const& channel, Producer&& producer, Consumer&& consumer) {
    // The handles of the frame keep the ownership of their ends.
    static_assert(!std::is_copy_constructible_v<Producer>);
    static_assert(!std::is_copy_assignable_v<Producer>);
    static_assert(std::is_move_constructible_v<Producer>);
    static_assert(!std::is_move_assignable_v<Producer>);
    static_assert(!std::is_move_assignable_v<Consumer>);
    static_assert(c::per_call_working_set_of_v<Producer> == Spsc::ProducerEnd::per_call_working_set);

    // An empty pop gives the value -1, which no push gave.
    if (consumer.try_pop().value_or(-1) != 100) {
        std::fprintf(stderr, "the body of with_recombined_access did not reach the ring\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        if (!producer.try_push(i)) {
            std::fprintf(stderr, "SPSC producer was refused below capacity\n");
            return 1;
        }
    }
    if (producer.try_push(8)) {
        std::fprintf(stderr, "SPSC producer pushed past capacity\n");
        return 1;
    }
    if (producer.size_approx(kSnapshot) != 8 || consumer.size_approx(kSnapshot) != 8
        || channel.size_approx(kSnapshot) != 8) {
        std::fprintf(stderr, "the three size views disagreed\n");
        return 1;
    }

    for (int i = 0; i < 8; ++i) {
        if (consumer.try_pop().value_or(-1) != i) {
            std::fprintf(stderr, "SPSC consumer got item %d out of order\n", i);
            return 1;
        }
    }
    if (consumer.try_pop().is_some()) {
        std::fprintf(stderr, "SPSC consumer popped from an empty channel\n");
        return 1;
    }
    return size_on_a_third_thread(channel, std::move(producer), std::move(consumer));
}

[[nodiscard]] int spsc_split_and_run() {
    Spsc channel{};

    // The split of the root makes the two endpoint tokens, and no other
    // path gives a token of this brand.
    auto whole = trace_root();

    // The whole permission is the proof that no handle is alive, so the
    // body reaches the ring itself.  Here it leaves one value behind.
    auto returned =
        channel.with_recombined_access(std::move(whole), [](auto& ring) noexcept { (void)ring.try_push(100); });
    auto [producer_perm, consumer_perm] =
        perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(returned));

    // Always false by construction: there is no refcount to drain, and
    // the accessor exists only so this channel matches the shape of the
    // pool-backed ones.  Surrendering the recombined whole permission
    // is what proves no endpoint handle is alive here.
    static_assert(!Spsc::is_exclusive_active());
    return c::with_ends(channel, std::move(producer_perm), std::move(consumer_perm),
                        [&channel](auto producer, auto consumer) noexcept {
                            return run_spsc_handles(channel, std::move(producer), std::move(consumer));
                        });
}

// ── Runtime: the MPSC pool ───────────────────────────────────────────

// True when the producer root came out of the pool of the channel.  The root
// goes back to the pool at once.
template <class Channel>
[[nodiscard]] bool can_withdraw_root(Channel& channel) noexcept {
    return channel.try_withdraw_producer_root(kSnapshot).match(
        [&channel](auto root) noexcept {
            channel.deposit_producer_root(std::move(root));
            return true;
        },
        []() noexcept { return false; });
}

// The one scoped-access verb of a merge channel.  The lease of the producer
// root out of the pool and the consumer token open the access, and the body
// reaches the ring.  The access gives the two back, the lease goes back to
// the pool, and the consumer pops what the body pushed.
[[nodiscard]] int mpsc_recombined_access() {
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(meta_root());
    Mpsc channel{std::move(producer_root)};
    auto root = channel.try_withdraw_producer_root(kSnapshot).expect("the root comes out with no producer out");
    auto returned = channel.with_recombined_access(
        Mpsc::RecombinedParts{.root = std::move(root), .consumer = std::move(consumer_perm)},
        [](Mpsc::RecombinedRing& ring) noexcept { static_cast<void>(ring.try_push(41)); });
    channel.deposit_producer_root(std::move(returned.root));
    int const popped = c::with_consumer(channel, std::move(returned.consumer), [](auto consumer) noexcept {
        const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
        int const item = consumer.try_pop(merge).value_or(-1);
        ::foundation::linear::drop(std::move(consumer));
        return item;
    });
    if (popped != 41) {
        std::fprintf(stderr, "the body of with_recombined_access did not reach the ring of a merge channel\n");
        return 1;
    }
    if (channel.is_exclusive_active() || c::with_producer(channel, kSnapshot, [](auto) noexcept {}).is_none()) {
        std::fprintf(stderr, "the pool of a merge channel did not lend after the deposit of its root\n");
        return 1;
    }
    return 0;
}

// A lend door that lends no end calls no body, and it ends the body at
// end_unrun_body of foundation/Linear.h.  The body holds the producer handle
// of a wire channel, and the end of that handle closes the wire.  So the
// consumer of the wire sees the close after the refused lend.
[[nodiscard]] int mpsc_refused_lend_ends_its_body() {
    auto merge_tokens = perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(meta_root());
    Mpsc merge{std::move(merge_tokens.at(::foundation::core::index<0>()))};
    auto root = merge.try_withdraw_producer_root(kSnapshot).expect("the root comes out with no producer out");
    auto wire_tokens = perm::mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(trace_root());
    Spsc wire{};
    bool const is_lent = c::with_producer(
        wire, std::move(wire_tokens.at(::foundation::core::index<0>())), [&merge](auto producer) noexcept {
            return c::with_producer(merge, kSnapshot,
                                    [held = std::move(producer)](auto) mutable noexcept { std::move(held).close(); })
                .is_some();
        });
    bool const is_closed =
        c::with_consumer(wire, std::move(wire_tokens.at(::foundation::core::index<1>())), [](auto consumer) noexcept {
            bool const closed = consumer.is_closed();
            ::foundation::linear::drop(std::move(consumer));
            return closed;
        });
    merge.deposit_producer_root(std::move(root));
    c::with_consumer(merge, std::move(merge_tokens.at(::foundation::core::index<1>())),
                     [](auto consumer) noexcept { ::foundation::linear::drop(std::move(consumer)); });
    if (is_lent || !is_closed) {
        fixy::report(fixy::Sink::Err,
                     "a refused lend called its body, or it did not end the producer that the body held\n");
        return 1;
    }
    return 0;
}

// The result of two nested producer frames when the pool lends no share to
// one of them.
constexpr int kNoProducerLent = -1;

// The checks of the pool while two producer handles of a merge channel live.
template <class First, class Second>
[[nodiscard]] int check_two_producers(Mpsc& channel, First& first, Second& second) {
    if (channel.outstanding_producers() != 2) {
        std::fprintf(stderr, "the pool counted %llu shares where two were out\n",
                     static_cast<unsigned long long>(channel.outstanding_producers()));
        return 1;
    }

    // The producer root comes out of the pool only when no share is out.
    if (can_withdraw_root(channel)) {
        std::fprintf(stderr, "the producer root came out while producers were still out\n");
        return 1;
    }

    if (!first.try_push(1) || !second.try_push(2)) {
        std::fprintf(stderr, "a producer handle could not push\n");
        return 1;
    }
    return 0;
}

// The checks of the pool of a merge channel while the consumer handle of
// the frame of mpsc_pool_accounting lives.
template <class Consumer>
[[nodiscard]] int check_pool_accounting(Mpsc& channel, Consumer&& consumer) {
    if (channel.outstanding_producers() != 0 || channel.is_exclusive_active()) {
        std::fprintf(stderr, "a fresh MPSC channel reported outstanding producers\n");
        return 1;
    }

    // The two producer frames nest, so the two handles hold two shares at
    // the same time.
    auto lent_twice = c::with_producer(channel, kSnapshot, [&channel](auto first) noexcept {
        return c::with_producer(channel, kSnapshot, [&channel, first = std::move(first)](auto second) mutable noexcept {
            return check_two_producers(channel, first, second);
        });
    });
    int const lent = std::move(lent_twice).value_or(::foundation::core::none).value_or(kNoProducerLent);
    if (lent == kNoProducerLent) {
        std::fprintf(stderr, "the pool refused a second producer — the fractional side is not fractional\n");
        return 1;
    }
    if (lent != 0) return lent;

    if (channel.outstanding_producers() != 0) {
        std::fprintf(stderr, "the pool did not reclaim its shares when the handles went out of scope\n");
        return 1;
    }

    // With no share out, the root comes out, and the pool lends no share
    // while it is out.
    auto root = channel.try_withdraw_producer_root(kSnapshot).expect("the root comes out with no producer out");
    bool const is_lent_while_out = c::with_producer(channel, kSnapshot, [](auto) noexcept {}).is_some();
    channel.deposit_producer_root(std::move(root));
    if (is_lent_while_out) {
        std::fprintf(stderr, "the pool lent a share while its root was out\n");
        return 1;
    }

    // The pool lends again once the root is back, which is what makes the
    // exclusive window scoped rather than terminal.
    if (c::with_producer(channel, kSnapshot, [](auto) noexcept {}).is_none()) {
        std::fprintf(stderr, "the pool stayed exclusive after the body returned\n");
        return 1;
    }

    // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
    const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
    auto first_item = consumer.try_pop(merge);
    auto second_item = consumer.try_pop(merge);
    if (first_item.is_none() || second_item.is_none()) {
        std::fprintf(stderr, "the consumer lost what the two producers pushed\n");
        return 1;
    }
    if (std::move(first_item).expect("the test above found an item")
            + std::move(second_item).expect("the test above found an item")
        != 3) {
        std::fprintf(stderr, "the consumer got the wrong payloads\n");
        return 1;
    }

    // The release door gives the consumer role and its token back, and the
    // released handle names no channel.  The token opens a new consumer
    // handle with no second claim.
    auto token = std::move(consumer).release();
    bool const is_released_bound = consumer.channel_identity().is_bound();
    bool is_second_bound = false;
    auto second_token = c::with_consumer(channel, std::move(token), [&is_second_bound](auto second_consumer) noexcept {
        is_second_bound = second_consumer.channel_identity().is_bound();
        return std::move(second_consumer).release();
    });
    ::foundation::linear::drop(std::move(second_token));
    if (is_released_bound || !is_second_bound) return 1;
    return 0;
}

[[nodiscard]] int mpsc_pool_accounting() {
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<Mpsc::producer_tag, Mpsc::consumer_tag>(meta_root());
    Mpsc channel{std::move(producer_root)};
    return c::with_consumer(channel, std::move(consumer_perm), [&channel](auto consumer) noexcept {
        return check_pool_accounting(channel, std::move(consumer));
    });
}

// Several producer threads, each holding its own pool share.  The
// consumer is linear and stays on this thread, which is the ring's
// requirement rather than the pool's.
[[nodiscard]] int mpsc_many_producer_threads() {
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 500;

    using BulkChannel = c::channel_t<int, 256, decltype(bulk_root())>;
    auto [producer_root, consumer_perm] =
        perm::mint_permission_split<BulkChannel::producer_tag, BulkChannel::consumer_tag>(bulk_root());
    BulkChannel channel{std::move(producer_root)};

    fixy::Atomic<int> failures{0};
    fixy::Atomic<int> finished{0};
    int received = 0;
    long long sum = 0;

    auto consumer_token = c::with_consumer(
        channel, std::move(consumer_perm), [&channel, &failures, &finished, &received, &sum](auto consumer) noexcept {
            {
                std::vector<std::jthread> producers;
                for (int p = 0; p < kProducers; ++p) {
                    producers.emplace_back([&channel, &failures, &finished] {
                        bool const is_lent = c::with_producer(channel, kSnapshot, [](auto handle) noexcept {
                                                 for (int i = 0; i < kPerProducer;) {
                                                     if (handle.try_push(1)) ++i;
                                                 }
                                             }).is_some();
                        if (!is_lent) static_cast<void>(failures.fetch_add_acq_rel(1));
                        static_cast<void>(finished.fetch_add_acq_rel(1));
                    });
                }

                const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
                while (received < kProducers * kPerProducer) {
                    auto item = consumer.try_pop(merge);
                    if (item.is_some()) {
                        sum += std::move(item).expect("the test above found an item");
                        ++received;
                    } else if (finished.load_acquire() == kProducers && consumer.empty_approx(kSnapshot)) {
                        break;
                    }
                }
            }
            return std::move(consumer).release();
        });
    ::foundation::linear::drop(std::move(consumer_token));

    if (failures.load_acquire() != 0) {
        std::fprintf(stderr, "the pool refused a producer thread a share\n");
        return 1;
    }
    if (sum != kProducers * kPerProducer) {
        std::fprintf(stderr, "the channel delivered %lld of %d\n", sum, kProducers * kPerProducer);
        return 1;
    }
    if (channel.outstanding_producers() != 0) {
        std::fprintf(stderr, "shares outlived their producer threads\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = spsc_split_and_run(); rc != 0) return rc;
    if (const int rc = mpsc_pool_accounting(); rc != 0) return rc;
    if (const int rc = mpsc_recombined_access(); rc != 0) return rc;
    if (const int rc = mpsc_refused_lend_ends_its_body(); rc != 0) return rc;
    if (const int rc = mpsc_many_producer_threads(); rc != 0) return rc;
    return 0;
}
